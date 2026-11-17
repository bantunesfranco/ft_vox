#ifndef COLUMN_JOB_TRACKER_HPP
#define COLUMN_JOB_TRACKER_HPP

#include <cstddef>
#include <cstdint>
#include <unordered_map>

#include "Chunk.hpp"

// Bookkeeping for background jobs that produce one result per column (mesh,
// light): which columns have an up-to-date result, which have a job running,
// and how many jobs run in all. Not thread-safe: the owner calls it from one
// thread (World: the main thread, under chunk_mutex). Versions come from one
// counter and are never reused, so a result computed for an older version is
// recognised and dropped.
class ColumnJobTracker {
public:
	struct Status {
		std::uint64_t version  = 0;      // changes whenever the column's inputs change
		std::uint64_t done     = 0;      // version the stored result was computed for
		bool          inFlight = false;  // a job is running
		std::uint32_t waited   = 0;      // frames spent waiting, for fair priority
		[[nodiscard]] bool current() const { return done == version; }
	};

	// The coord's status; a coord seen for the first time gets a fresh version,
	// so it is not current.
	Status& track(const ChunkCoord& coord) {
		const auto [it, inserted] = statuses.try_emplace(coord);
		if (inserted)
			it->second.version = ++versionCounter;
		return it->second;
	}

	[[nodiscard]] const Status* find(const ChunkCoord& coord) const {
		const auto it = statuses.find(coord);
		return it == statuses.end() ? nullptr : &it->second;
	}

	// The coord's inputs changed: a job in flight is now stale. Untracked: nothing to redo.
	void invalidate(const ChunkCoord& coord) {
		const auto it = statuses.find(coord);
		if (it != statuses.end())
			it->second.version = ++versionCounter;
	}

	[[nodiscard]] std::size_t inFlight() const { return running; }
	[[nodiscard]] bool hasRoom(const std::size_t maxInFlight) const { return running < maxInFlight; }
	[[nodiscard]] std::size_t room(const std::size_t maxInFlight) const {
		return running < maxInFlight ? maxInFlight - running : 0;
	}

	// A job starts for the coord: counted, and the version it computes is returned.
	std::uint64_t start(const ChunkCoord& coord) {
		Status& status = track(coord);
		status.inFlight = true;
		++running;
		return status.version;
	}

	// A job ends. It is uncounted even when the coord was erased meanwhile.
	// True (and the coord is now current) only when the coord is still
	// tracked, the job succeeded and the coord did not change since it started.
	bool finish(const ChunkCoord& coord, const std::uint64_t version, const bool succeeded) {
		if (running > 0)
			--running;
		const auto it = statuses.find(coord);
		if (it == statuses.end())
			return false;
		it->second.inFlight = false;
		if (!succeeded || it->second.version != version)
			return false;
		it->second.done = version;
		return true;
	}

	// Forgets the coord; a job still running for it stays counted until finish().
	void erase(const ChunkCoord& coord) { statuses.erase(coord); }

	// Forgets every coord for which `drop(coord, status)` is true.
	template<typename Predicate>
	void eraseIf(Predicate drop) {
		std::erase_if(statuses, [&](const auto& entry) { return drop(entry.first, entry.second); });
	}

	// Forgets everything and counts no job: results of jobs still running must
	// be recognised as stale by the owner (World: by epoch) and never finish().
	void reset() {
		statuses.clear();
		running = 0;
	}

private:
	std::unordered_map<ChunkCoord, Status> statuses;
	std::uint64_t versionCounter = 0;
	std::size_t   running = 0;
};

#endif
