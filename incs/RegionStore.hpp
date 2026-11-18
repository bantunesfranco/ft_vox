#pragma once
#ifndef REGION_STORE_HPP
# define REGION_STORE_HPP

// ============================================================================
//  RegionStore : whole columns on disk, 32x32 columns per region file
//
//  <dir>/r.<rx>.<rz>.bin, rx = floor(cx / 32):
//    "FVRG", u32 version (1), 1024 x {u32 offset, u32 length}   (entry index
//    (cx - rx*32) + (cz - rz*32) * 32; length 0 = absent), then blobs.
//  A blob is {i32 cx, i32 cz} + the ChunkCodec encoding of the column.
//
//  Saving writes the blob into space no table entry uses (a gap left by an
//  older blob when it fits, else the end of the file), flushes, and only
//  then points the table entry at it: if the process dies, the file holds
//  either the old or the new version, never a broken one. Writes are
//  flushed, not fsynced, so a power loss or OS crash gives no such promise.
//  Anything unreadable (damaged file, bad range, failed decode) loads as
//  absent, so the column is simply generated again. A damaged file is kept
//  as r.<rx>.<rz>.bin.bad before a save starts it again; a file from a newer
//  version of the game is never written.
//
//  At most MAX_OPEN_FILES region files are open at once (least recently
//  used ones are closed), so a long session never runs out of handles.
//
//  Background saves: stage() records the newest snapshot of a column, which
//  load() returns until it is on disk. One writeStaged() per column writes
//  it (and any snapshot staged meanwhile), so saves of one column never
//  overlap or land out of order, and a reload never sees an older version.
//
//  Thread-safe: one mutex per region file.
// ============================================================================

#include <filesystem>
#include <fstream>
#include <list>
#include <vector>
#include <memory>
#include <mutex>
#include <unordered_map>

#include "Chunk.hpp"

class RegionStore {
public:
	explicit RegionStore(std::filesystem::path regionDir);   // created on first save

	// The saved column (or the staged snapshot), or nullptr when absent or unreadable.
	[[nodiscard]] std::unique_ptr<ChunkColumn> load(const ChunkCoord& coord);

	// False when the file could not be written (the previous version stays).
	bool save(const ChunkColumn& column);

	// Records `column` as the newest version of its coordinates, to be written
	// by writeStaged(). True: no write of it is pending, so the caller must
	// run writeStaged(coord) once (on any thread). False: the pending one
	// writes this snapshot too. The snapshot must not change afterwards.
	bool stage(std::shared_ptr<const ChunkColumn> column);
	// Writes the staged snapshot of `coord` until none newer is staged, then
	// forgets it. False when a write failed.
	bool writeStaged(const ChunkCoord& coord);

	static constexpr int REGION_SIZE = 32;
	static constexpr std::size_t MAX_OPEN_FILES = 64;

private:
	// A region file and its table. Entries live as long as the store; the
	// file is open only while the region is among the most recently used.
	struct Region {
		std::mutex        mutex;
		std::fstream      file;
		std::vector<char> header;     // the table as on disk (while valid)
		bool              valid   = false;   // file open and table read
		bool              newer   = false;   // a newer version's file: never touched
		bool              listed  = false;   // in _open
		std::list<Region*>::iterator lru;    // its place in _open (while listed)
	};
	// Opens the file (and reads the table) when it is not open; `create`
	// starts a missing or damaged file. Region mutex held.
	bool open(Region& region, const ChunkCoord& regionCoord, bool create);
	// `region` was just used: most recent in _open, closing the least recently
	// used files beyond MAX_OPEN_FILES. Region mutex held.
	void touch(Region& region);

	Region& regionFor(const ChunkCoord& regionCoord);
	[[nodiscard]] std::filesystem::path fileFor(const ChunkCoord& regionCoord) const;

	std::filesystem::path _dir;
	std::mutex _regionsMutex;     // _regions and _open
	std::unordered_map<ChunkCoord, std::unique_ptr<Region>> _regions;
	std::list<Region*> _open;     // regions with an open file, most recent first

	std::mutex _stagedMutex;
	std::unordered_map<ChunkCoord, std::shared_ptr<const ChunkColumn>> _staged;
};

#endif /* REGION_STORE_HPP */
