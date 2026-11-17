#include "SelfTest.hpp"
#include "ColumnJobTracker.hpp"

namespace {
constexpr ChunkCoord A{1, 2};
constexpr ChunkCoord B{3, 4};
constexpr std::size_t MAX = 4;
}

SELFTEST(ColumnJobTracker, fresh_is_not_current) {
	ColumnJobTracker tracker;
	const ColumnJobTracker::Status& status = tracker.track(A);
	CHECK(!status.current());
	CHECK(!status.inFlight);
	CHECK(tracker.track(B).version != status.version);   // versions are never shared
	CHECK(&tracker.track(A) == &status);                  // tracking again returns the same status
}

SELFTEST(ColumnJobTracker, finish_matching_version_is_current) {
	ColumnJobTracker tracker;
	const std::uint64_t version = tracker.start(A);
	CHECK(tracker.track(A).inFlight);
	CHECK_EQ(tracker.room(MAX), MAX - 1);
	CHECK(tracker.finish(A, version, true));
	CHECK(tracker.track(A).current());
	CHECK(!tracker.track(A).inFlight);
	CHECK_EQ(tracker.room(MAX), MAX);
}

SELFTEST(ColumnJobTracker, invalidate_during_flight_drops_result) {
	ColumnJobTracker tracker;
	const std::uint64_t old = tracker.start(A);
	tracker.invalidate(A);
	CHECK(!tracker.finish(A, old, true));
	CHECK(!tracker.track(A).current());
	CHECK(!tracker.track(A).inFlight);
	CHECK_EQ(tracker.room(MAX), MAX);
	// The column is scheduled again and the new job can land.
	CHECK(tracker.finish(A, tracker.start(A), true));
	CHECK(tracker.track(A).current());
}

SELFTEST(ColumnJobTracker, invalidate_makes_current_stale) {
	ColumnJobTracker tracker;
	CHECK(tracker.finish(A, tracker.start(A), true));
	tracker.invalidate(A);
	CHECK(!tracker.track(A).current());
	tracker.invalidate(B);                                // untracked: stays untracked
	CHECK(tracker.find(B) == nullptr);
}

SELFTEST(ColumnJobTracker, failed_job_not_current) {
	ColumnJobTracker tracker;
	const std::uint64_t version = tracker.start(A);
	CHECK(!tracker.finish(A, version, false));
	CHECK(!tracker.track(A).current());
	CHECK(!tracker.track(A).inFlight);
	CHECK_EQ(tracker.room(MAX), MAX);
}

SELFTEST(ColumnJobTracker, erased_coord_finish_false_but_uncounted) {
	ColumnJobTracker tracker;
	const std::uint64_t version = tracker.start(A);
	tracker.erase(A);
	CHECK_EQ(tracker.room(MAX), MAX - 1);                 // still counted until it ends
	CHECK(!tracker.finish(A, version, true));
	CHECK_EQ(tracker.room(MAX), MAX);
	CHECK(tracker.find(A) == nullptr);
}

SELFTEST(ColumnJobTracker, reset_zeroes_in_flight) {
	ColumnJobTracker tracker;
	tracker.start(A);
	tracker.start(B);
	CHECK(!tracker.hasRoom(2));
	tracker.reset();
	CHECK_EQ(tracker.inFlight(), std::size_t{0});
	CHECK(tracker.hasRoom(1));
	CHECK(tracker.find(A) == nullptr);
}

SELFTEST(ColumnJobTracker, versions_not_reused_after_reset) {
	ColumnJobTracker tracker;
	const std::uint64_t before = tracker.track(A).version;
	tracker.reset();
	CHECK(tracker.track(A).version != before);
}

SELFTEST(ColumnJobTracker, erase_if_drops_matching) {
	ColumnJobTracker tracker;
	tracker.track(A);
	tracker.track(B);
	tracker.eraseIf([](const ChunkCoord& coord, const ColumnJobTracker::Status&) { return coord == A; });
	CHECK(tracker.find(A) == nullptr);
	CHECK(tracker.find(B) != nullptr);
}
