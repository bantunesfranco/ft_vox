#pragma once
#ifndef FRAME_PROFILER_HPP
# define FRAME_PROFILER_HPP

// ============================================================================
//  FrameProfiler : frame phase timings for `ft_vox --bench`
//
//  mark(phase) charges the time since the previous mark to `phase`, so the
//  phases of a frame add up to the whole frame. GL calls are asynchronous:
//  "draw" is the cost of SUBMITTING the draws; GPU time shows up in "swap"
//  (SwapBuffers waits for the GPU when it is behind).
//
//  Besides the text report it writes a Chrome trace (JSON) that Perfetto UI
//  (ui.perfetto.dev) or chrome://tracing open directly. Other threads can add
//  spans with Trace::Scope (thread-safe), e.g. chunk generation on the pool.
// ============================================================================

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// ---- Trace : a global, thread-safe span recorder (off unless enabled) ---------------

class Trace {
public:
	using Clock = std::chrono::steady_clock;

	struct Event {
		const char*  name;        // string literal
		std::int64_t startUs;
		std::int64_t durUs;
		std::uint64_t tid;
	};

	static Trace& instance() {
		static Trace trace;
		return trace;
	}

	void enable() noexcept { _enabled = true; _origin = Clock::now(); }
	[[nodiscard]] bool enabled() const noexcept { return _enabled; }

	void add(const char* name, const Clock::time_point start, const Clock::time_point end) {
		if (!_enabled)
			return;
		const Event e{name, us(start), us(end) - us(start), threadId()};
		std::lock_guard lock(_mutex);
		_events.push_back(e);
	}

	// RAII span: `Trace::Scope s("generate");`
	class Scope {
	public:
		explicit Scope(const char* name) noexcept : _name(name), _start(Clock::now()) {}
		~Scope() { Trace::instance().add(_name, _start, Clock::now()); }
		Scope(const Scope&)            = delete;
		Scope& operator=(const Scope&) = delete;
	private:
		const char*       _name;
		Clock::time_point _start;
	};

	// Chrome trace event format: complete ("X") events, microseconds.
	bool write(const char* path) {
		std::lock_guard lock(_mutex);
		std::FILE* f = std::fopen(path, "wb");
		if (!f)
			return false;
		std::fputs("{\"displayTimeUnit\":\"ms\",\"traceEvents\":[\n", f);
		const std::uint64_t mainTid = _mainTid;
		std::fprintf(f, "{\"name\":\"thread_name\",\"ph\":\"M\",\"pid\":1,\"tid\":%llu,\"args\":{\"name\":\"main\"}}",
		             static_cast<unsigned long long>(mainTid));
		for (const Event& e : _events)
			std::fprintf(f, ",\n{\"name\":\"%s\",\"ph\":\"X\",\"pid\":1,\"tid\":%llu,\"ts\":%lld,\"dur\":%lld}",
			             e.name, static_cast<unsigned long long>(e.tid),
			             static_cast<long long>(e.startUs), static_cast<long long>(e.durUs));
		std::fputs("\n]}\n", f);
		std::fclose(f);
		return true;
	}

	void setMainThread() noexcept { _mainTid = threadId(); }

private:
	Trace() = default;

	[[nodiscard]] std::int64_t us(const Clock::time_point t) const noexcept {
		return std::chrono::duration_cast<std::chrono::microseconds>(t - _origin).count();
	}
	static std::uint64_t threadId() noexcept {
		return static_cast<std::uint64_t>(std::hash<std::thread::id>{}(std::this_thread::get_id()) & 0xFFFFFFFFu);
	}

	bool               _enabled = false;
	Clock::time_point  _origin{};
	std::uint64_t      _mainTid = 0;
	std::mutex         _mutex;
	std::vector<Event> _events;
};

// ---- FrameProfiler : per-phase main thread timings ------------------------------------

class FrameProfiler {
public:
	enum Phase { Begin, Session, Prune, Mesh, Upload, Draw, Ui, Swap, PhaseCount };

	void beginFrame() noexcept {
		_frameStart = _last = Clock::now();
		_current.fill(0.0);
	}

	void mark(const Phase phase) {
		const auto now = Clock::now();
		_current[phase] += std::chrono::duration<double, std::milli>(now - _last).count();
		Trace::instance().add(kNames[phase], _last, now);
		_last = now;
	}

	// `record` false: warm-up frames, traced but not counted in the report.
	void endFrame(const bool record, const std::size_t drawCalls) {
		const auto now = Clock::now();
		Trace::instance().add("frame", _frameStart, now);
		if (!record)
			return;
		_frames.push_back(std::chrono::duration<double, std::milli>(now - _frameStart).count());
		for (int i = 0; i < PhaseCount; ++i) {
			_totals[i] += _current[i];
			_phaseFrames[i].push_back(_current[i]);
		}
		_drawCalls += drawCalls;
	}

	void report() const {
		if (_frames.empty()) {
			std::printf("bench: no frames recorded\n");
			return;
		}
		const double n = static_cast<double>(_frames.size());
		double sum = 0.0;
		for (const double f : _frames)
			sum += f;

		std::printf("\n==== bench: %zu frames ====\n", _frames.size());
		std::printf("frame ms   avg %.2f   p50 %.2f   p95 %.2f   p99 %.2f   p99.9 %.2f   max %.2f   (avg %.0f fps)\n",
		            sum / n, percentile(_frames, 0.50), percentile(_frames, 0.95),
		            percentile(_frames, 0.99), percentile(_frames, 0.999), percentile(_frames, 1.0),
		            1000.0 * n / sum);
		const auto slow = [this](const double ms) {
			return std::ranges::count_if(_frames, [ms](const double f) { return f > ms; });
		};
		std::printf("hitches: %lld frames > 4 ms, %lld > 8 ms, %lld > 16 ms\n",
		            static_cast<long long>(slow(4.0)), static_cast<long long>(slow(8.0)),
		            static_cast<long long>(slow(16.0)));
		std::printf("avg ms per phase:");
		for (int i = 0; i < PhaseCount; ++i)
			std::printf("  %s %.2f", kNames[i], _totals[i] / n);
		std::printf("\np99 ms per phase:");
		for (int i = 0; i < PhaseCount; ++i)
			std::printf("  %s %.2f", kNames[i], percentile(_phaseFrames[i], 0.99));
		std::printf("\nmeshes drawn per frame: %.0f\n", static_cast<double>(_drawCalls) / n);
		std::fflush(stdout);
	}

private:
	using Clock = std::chrono::steady_clock;
	static constexpr std::array<const char*, PhaseCount> kNames{
		"begin", "session", "prune", "mesh", "upload", "draw", "ui", "swap"};

	static double percentile(std::vector<double> values, const double p) {
		std::ranges::sort(values);
		const auto i = static_cast<std::size_t>(p * static_cast<double>(values.size() - 1));
		return values[i];
	}

	Clock::time_point                           _frameStart{};
	Clock::time_point                           _last{};
	std::array<double, PhaseCount>              _current{};
	std::array<double, PhaseCount>              _totals{};
	std::vector<double>                         _frames;
	std::array<std::vector<double>, PhaseCount> _phaseFrames{};
	std::size_t                                 _drawCalls = 0;
};

#endif /* FRAME_PROFILER_HPP */
