// ============================================================================
//  Transport.cpp : Transport facade + Transport::Impl (pimpl)
//
//  Portable: no OS headers, no #ifdef. Sockets, polling and error codes come
//  from Platform.hpp, and ticks come from std::chrono::steady_clock plus the
//  poll timeout. (WSAPoll can only wait on sockets, so there is no timerfd /
//  eventfd / waitable-timer handle to wait on.)
//
//  The rules this file is built around:
//
//   R1  The `conns` map is mutated in exactly two places: onAcceptable()
//       (insert) and reap() (erase). Nothing else adds or removes entries,
//       so it is always safe to iterate `conns` or to hold a Connection&
//       while calling back into game code.
//   R2  A closed connection is only marked dead (its id goes into `dead` and
//       it is left out of the next poll). The object is destroyed at the end
//       of the loop pass, in reap(). Poll results already collected for it in
//       this pass are detected and skipped.
//   R3  Everything is addressed by ConnId, never by socket handle or by a
//       pointer stored across loop passes.
//   R4  Game callbacks may call send() / disconnect() on any id, including
//       the one being processed. After every callback, re-check c.closing.
//   R5  Only post() and stop() may be called from other threads.
// ============================================================================

#include "Net.hpp"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <mutex>
#include <ranges>
#include <thread>
#include <unordered_set>

namespace net {

using detail::Connection;
using detail::ConnState;
using detail::EventTag;
using detail::FlushResult;
using detail::ParseResult;
using detail::SourceKind;

static_assert(std::atomic<bool>::is_always_lock_free,
              "stop() is called from signal handlers and must be lock-free");

// ============================================================================
//  Impl
// ============================================================================

struct Transport::Impl {
	Impl(const Config& c, ITransportHandler& h) : cfg(c), handler(h) {}

	// ---- state -------------------------------------------------------------

	// MUST stay the first member: it is destroyed last, so WSACleanup() runs
	// only after every socket below has been closed.
	platform::NetworkRuntime runtime;

	Config              cfg;
	ITransportHandler&  handler;
	detail::Listener    listener;

	std::unordered_map<ConnId, std::unique_ptr<Connection>> conns;   // R1
	std::unordered_set<ConnId>                              dead;    // R2
	ConnId              nextId    = 1;           // 0 is INVALID_CONN
	uint64_t            tickCount = 0;

	std::vector<PollItem>   items;               // rebuilt every loop pass
	std::vector<PollResult> results;

	// tick schedule (steady clock)
	Clock::duration     tickPeriod{};
	TimePoint           nextTick{};

	// accept back-off after EMFILE (poll is level-triggered: a readable
	// listener we cannot accept from would spin the loop)
	bool                acceptPaused   = false;
	TimePoint           acceptResumeAt{};

	// cross-thread (R5)
	std::atomic<bool>   stopRequested{false};
	std::mutex          postMutex;
	std::vector<std::function<void()>> postQueue;

	// ========================================================================
	//  Lifecycle
	// ========================================================================

	bool start(std::string& error) {
		if (!runtime.ok()) { error = runtime.error(); return false; }
		if (cfg.tickRateHz == 0) { error = "tickRateHz must be greater than zero"; return false; }
		if (!listener.open(cfg.port, cfg.backlog, error)) return false;

		tickPeriod = std::chrono::duration_cast<Clock::duration>(
			std::chrono::duration<double>(1.0 / cfg.tickRateHz));
		nextTick = Clock::now() + tickPeriod;
		return true;
	}

	void run() {
		while (!stopRequested.load(std::memory_order_acquire)) {
			if (acceptPaused && Clock::now() >= acceptResumeAt) acceptPaused = false;

			buildPollItems();
			// The timeout never exceeds the time to the next tick, so ticks stay on
			// schedule and stop() / post() are noticed within one tick period.
			const int n = pollSockets(items, results, msUntil(nextTick));
			if (n < 0) {
				std::fprintf(stderr, "poll failed: %s\n",
				             platform::errorString(platform::lastError()).c_str());
				break;
			}

			for (const PollResult& r : results) {
				const EventTag tag = EventTag::unpack(r.tag);
				switch (tag.kind) {
					case SourceKind::Listener: onAcceptable();        break;
					case SourceKind::Client:   onClientEvent(tag.id, r); break;
				}
			}

			drainPosted();
			runDueTicks();
			reap();                                     // R2: the only place objects die
		}
		shutdown();
	}

	void shutdown() {
		for (auto &c: conns | std::views::values)
			closeConnection(*c, DisconnectReason::ServerShutdown);   // best-effort goodbye
		conns.clear();                                  // closes every socket
		dead.clear();
	}

	// Milliseconds until `deadline`, rounded UP so we never wake just before it
	// and spin. 0 if it is already due.
	static int msUntil(const TimePoint deadline) {
		const auto left = deadline - Clock::now();
		if (left <= Clock::duration::zero()) return 0;
		const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(left);
		return static_cast<int>(ms.count()) + 1;
	}

	void buildPollItems() {
		items.clear();
		if (!acceptPaused)
			items.push_back({listener.handle(), EventTag{SourceKind::Listener, 0}.pack(), true, false});

		for (const auto& [id, c] : conns) {
			if (dead.contains(id)) continue;
			// A closing connection only lingers to flush its goodbye: no reads.
			items.push_back({c->handle(), EventTag{SourceKind::Client, id}.pack(),
			                 !c->closing, c->wantWrite});
		}
	}

	// ========================================================================
	//  Operations called by the facade (main thread)
	// ========================================================================

	Connection* find(const ConnId id) const {
		const auto it = conns.find(id);
		return it == conns.end() ? nullptr : it->second.get();
	}

	bool alive(const Connection& c) const { return !c.closing && !dead.contains(c.id()); }

	void send(const ConnId id, SharedBuffer frame) {
		if (Connection* c = find(id)) queueFrame(*c, std::move(frame));
	}

	void broadcast(const std::vector<ConnId>& ids, const SharedBuffer& frame) {
		for (const ConnId id : ids) send(id, frame);    // shares the buffer, no copy
	}

	void disconnect(const ConnId id, const DisconnectReason why) {
		if (Connection* c = find(id)) closeConnection(*c, why);
	}

	bool   isConnected(const ConnId id) const  { const Connection* c = find(id); return c && alive(*c); }
	size_t pendingBytes(const ConnId id) const { const Connection* c = find(id); return c ? c->out.pending() : 0; }
	size_t connectionCount() const             { return conns.size() - dead.size(); }
	bool   peerInfo(const ConnId id, PeerInfo& out) const {
		const Connection* c = find(id);
		if (!c) return false;
		out = c->peer();
		return true;
	}

	// ---- thread-safe (R5) --------------------------------------------------

	void post(std::function<void()> fn) {
		std::lock_guard lock(postMutex);
		postQueue.push_back(std::move(fn));             // picked up by drainPosted()
	}

	void stop() {                                       // async-signal-safe: one atomic store
		stopRequested.store(true, std::memory_order_release);
	}

	void drainPosted() {
		std::vector<std::function<void()>> batch;
		{
			std::lock_guard lock(postMutex);
			batch.swap(postQueue);
		}
		for (auto& fn : batch) fn();                    // run OUTSIDE the lock: fn may call post()
	}

	// ========================================================================
	//  Event handlers
	// ========================================================================

	void onAcceptable() {
		for (;;) {                                      // drain the accept queue
			Socket   sock;
			PeerInfo peer;
			switch (listener.acceptOne(sock, peer)) {
				case AcceptStatus::Accepted:   break;
				case AcceptStatus::Retry:      continue;
				case AcceptStatus::WouldBlock: return;
				case AcceptStatus::OutOfDescriptors:
					std::fprintf(stderr, "accept: out of descriptors, pausing accepts\n");
					acceptPaused   = true;              // do not spin on a readable listener
					acceptResumeAt = Clock::now() + std::chrono::milliseconds(100);
					return;
				case AcceptStatus::Error:
					std::fprintf(stderr, "accept failed: %s\n",
					             platform::errorString(platform::lastError()).c_str());
					return;
			}

			if (conns.size() >= cfg.maxConnections) continue;   // `sock` closes; the game's own limit (onConnect) sends ServerFull

			ConnId id = nextId++;
			if (nextId == INVALID_CONN) nextId = 1;     // wrap, skip 0
			auto conn = std::make_unique<Connection>(id, std::move(sock), peer, cfg, Clock::now());
			Connection& c = *conn;
			conns.emplace(id, std::move(conn));         // R1: insert point

			// CONTRACT: onDisconnect() fires for every connection that got
			// onConnect(), including one rejected here.
			if (!handler.onConnect(id, peer))
				closeConnection(c, DisconnectReason::ServerFull);
		}
	}

	void onClientEvent(const ConnId id, const PollResult& ev) {
		Connection* c = find(id);
		if (!c || dead.contains(id)) return;               // R2: stale result from this pass

		if (c->closing) {                               // lingering to flush the goodbye
			if (ev.error)         markDead(*c);
			else if (ev.writable) flushConnection(*c);
			return;
		}

		// On error/hangup read first: recv() reports the real cause (data, EOF or error).
		if (ev.readable || ev.error) onReadable(*c);
		if (ev.writable && !dead.contains(id)) flushConnection(*c);
	}

	void onReadable(Connection& c) {
		size_t budget = cfg.readBudgetPerEvent;         // fairness: bound work per event
		while (budget > 0 && !c.closing) {
			const size_t want = std::min<size_t>(budget, 16 * 1024);
			uint8_t* dst = c.in.prepare(want);
			if (!dst) { closeConnection(c, DisconnectReason::ProtocolViolation); return; }   // buffer cap hit

			const std::ptrdiff_t n = platform::recvSome(c.handle(), dst, want);
			if (n > 0) {
				c.in.commit(static_cast<size_t>(n));
				c.lastActivity = Clock::now();
				budget -= static_cast<size_t>(n);
				continue;
			}
			if (n == 0) { closeConnection(c, DisconnectReason::PeerClosed); return; }

			const int err = platform::lastError();      // portable: errno does not hold socket errors on Windows
			if (platform::isInterrupted(err)) continue;
			if (platform::isWouldBlock(err))  break;
			closeConnection(c, DisconnectReason::IoError);
			return;
		}
		parseFrames(c);
	}

	void parseFrames(Connection& c) {
		detail::PacketView pv;
		while (!c.closing) {                            // R4: callbacks may close c
			const ParseResult r = detail::tryParse(c.in, cfg.maxPacketSizeIn, pv);
			if (r == ParseResult::NeedMore) break;
			if (r == ParseResult::Error) { closeConnection(c, DisconnectReason::ProtocolViolation); return; }

			if (!c.limiter.allow(pv.frameSize, Clock::now())) { closeConnection(c, DisconnectReason::RateLimited); return; }
			if (!detail::packetAllowedIn(c.state, pv.id))     { closeConnection(c, DisconnectReason::ProtocolViolation); return; }

			handlePacket(c, pv);
			c.in.consume(pv.frameSize);                 // payload pointer is valid until here
		}
	}

	void handlePacket(Connection& c, const detail::PacketView& pv) {
		ByteReader body(pv.payload, pv.payloadSize);

		switch (c.state) {
			case ConnState::Handshake: {
				const auto version = body.read<uint32_t>();
				if (!body.ok() || !body.atEnd() || version != PROTOCOL_VERSION) {
					closeConnection(c, DisconnectReason::ProtocolViolation);
					return;
				}
				setState(c, ConnState::Login);
				return;
			}
			case ConnState::Login: {
				const std::string name = body.readString(32);
				if (!body.ok() || !body.atEnd() || name.empty()) {
					closeConnection(c, DisconnectReason::ProtocolViolation);
					return;
				}
				if (!handler.onLogin(c.id(), name)) {
					closeConnection(c, DisconnectReason::ProtocolViolation);
					return;
				}
				if (!c.closing) setState(c, ConnState::Play);   // R4: onLogin may have kicked
				return;
			}
			case ConnState::Play: {
				if (pv.id == PacketId::KeepAlive) return;       // activity already recorded
				handler.onPacket(c.id(), pv.id, body);
				if (!c.closing && !body.ok())
					closeConnection(c, DisconnectReason::ProtocolViolation);
				return;
			}
			case ConnState::Closing:
				return;
		}
	}

	// Runs every tick that is due (at most maxCatchUpTicks), then flushes and
	// checks timeouts once.
	void runDueTicks() {
		TimePoint now = Clock::now();
		unsigned  ran = 0;

		while (now >= nextTick) {
			if (ran >= cfg.maxCatchUpTicks) {           // far behind: drop ticks, avoid the spiral of death
				std::fprintf(stderr, "server behind: skipping ticks\n");
				nextTick = now + tickPeriod;
				break;
			}
			handler.onTick(++tickCount);
			nextTick += tickPeriod;
			++ran;
			now = Clock::now();                         // the handler may have been slow
		}
		if (ran == 0) return;

		// R1 makes these iterations safe even though callbacks close connections.
		for (auto& [id, c] : conns)
			if (!dead.contains(id) && !c->out.empty()) flushConnection(*c);

		for (auto& [id, c] : conns) {
			if (dead.contains(id) || now < c->deadline(cfg)) continue;
			if (c->closing) markDead(*c);               // linger expired: give up
			else            closeConnection(*c, DisconnectReason::Timeout);
		}
	}

	// ========================================================================
	//  Connection helpers
	// ========================================================================

	void setState(Connection& c, const ConnState s) {
		c.state      = s;
		c.stateSince = Clock::now();
	}

	void queueFrame(Connection& c, SharedBuffer frame) {
		if (!alive(c) || !frame) return;
		// Backpressure: judge the backlog that is ALREADY queued. Counting the new
		// frame too would reject any single frame larger than highWaterMark
		// (a big chunk packet) even on an idle connection.
		if (c.out.pending() >= cfg.highWaterMark) {
			closeConnection(c, DisconnectReason::TooSlow);
			return;
		}
		c.out.push(std::move(frame));
		// Not flushed here: the tick flushes every connection once per tick.
	}

	void flushConnection(Connection& c) {
		switch (c.out.flush(c.handle())) {
			case FlushResult::Drained:
				c.wantWrite = false;                    // stop asking poll for writability
				if (c.closing) markDead(c);             // goodbye packet delivered
				break;
			case FlushResult::WouldBlock:
				c.wantWrite = true;                     // finish when poll says writable
				break;
			case FlushResult::Error:
				if (c.closing) markDead(c);
				else           closeConnection(c, DisconnectReason::IoError);
				break;
		}
	}

	// The ONLY way a connection starts dying. Idempotent.
	void closeConnection(Connection& c, const DisconnectReason why) {
		if (c.closing || dead.contains(c.id())) return;

		c.closing     = true;
		c.closeReason = why;
		setState(c, ConnState::Closing);

		handler.onDisconnect(c.id(), why);              // exactly once; game may still call send(): it is a no-op now

		if (why == DisconnectReason::PeerClosed || why == DisconnectReason::IoError) { markDead(c); return; }

		// Best-effort goodbye, then linger until flushed or closingTimeout.
		ByteWriter w(PacketId::Disconnect);
		w.write(why);
		c.out.push(w.finish());                         // bypasses queueFrame's closing check on purpose
		flushConnection(c);
	}

	// Leaves the connection out of future polls and schedules destruction. The
	// object itself lives until reap(), so pointers held by callers up the
	// stack stay valid.
	void markDead(const Connection& c) {
		dead.insert(c.id());
	}

	void reap() {
		for (const ConnId id : dead) conns.erase(id);   // ~Connection closes the socket (RAII)
		dead.clear();
	}
};

// ============================================================================
//  Facade: one-line forwarders
// ============================================================================

Transport::Transport(const Config& cfg, ITransportHandler& handler): _impl(std::make_unique<Impl>(cfg, handler)) {}

// Must be defined here, where Impl is a complete type.
Transport::~Transport() = default;

bool   Transport::start(std::string& error)                   { return _impl->start(error); }
void   Transport::run()                                       { _impl->run(); }
void   Transport::stop()                                      { _impl->stop(); }

void   Transport::send(ConnId id, SharedBuffer frame)         { _impl->send(id, std::move(frame)); }
void   Transport::broadcast(const std::vector<ConnId>& ids,
                            const SharedBuffer& frame)        { _impl->broadcast(ids, frame); }
void   Transport::disconnect(ConnId id, DisconnectReason why) { _impl->disconnect(id, why); }

bool   Transport::isConnected(ConnId id) const                { return _impl->isConnected(id); }
size_t Transport::pendingBytes(ConnId id) const               { return _impl->pendingBytes(id); }
size_t Transport::connectionCount() const                     { return _impl->connectionCount(); }
bool   Transport::peerInfo(ConnId id, PeerInfo& out) const    { return _impl->peerInfo(id, out); }

void   Transport::post(std::function<void()> fn)              { _impl->post(std::move(fn)); }

} // namespace net