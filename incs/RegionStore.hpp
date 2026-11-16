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
//  Saving appends the blob, flushes, and only then points the table entry
//  at it: a crash leaves either the old or the new version, never a broken
//  file. Re-saving leaves the old blob as dead space (no compaction).
//  Anything unreadable (damaged file, bad range, failed decode) loads as
//  absent, so the column is simply generated again.
//
//  Thread-safe: one mutex per region file.
// ============================================================================

#include <filesystem>
#include <fstream>
#include <vector>
#include <memory>
#include <mutex>
#include <unordered_map>

#include "Chunk.hpp"

class RegionStore {
public:
	explicit RegionStore(std::filesystem::path regionDir);   // created on first save

	// The saved column, or nullptr when absent or unreadable.
	[[nodiscard]] std::unique_ptr<ChunkColumn> load(const ChunkCoord& coord);

	// False when the file could not be written (the previous version stays).
	bool save(const ChunkColumn& column);

	static constexpr int REGION_SIZE = 32;

private:
	// An open region file and its table, kept for the life of the store.
	struct Region {
		std::mutex        mutex;
		std::fstream      file;
		std::vector<char> header;     // the table as on disk
		bool              opened = false;
		bool              valid  = false;   // file open and table read
	};
	// Opens the file (and reads the table) once; `create` starts a missing
	// or damaged file. Region mutex held.
	bool open(Region& region, const ChunkCoord& regionCoord, bool create);

	Region& regionFor(const ChunkCoord& regionCoord);
	[[nodiscard]] std::filesystem::path fileFor(const ChunkCoord& regionCoord) const;

	std::filesystem::path _dir;
	std::mutex _regionsMutex;
	std::unordered_map<ChunkCoord, std::unique_ptr<Region>> _regions;
};

#endif /* REGION_STORE_HPP */
