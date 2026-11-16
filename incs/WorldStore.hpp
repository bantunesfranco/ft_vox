#pragma once
#ifndef WORLD_STORE_HPP
# define WORLD_STORE_HPP

// ============================================================================
//  WorldStore : worlds on disk
//
//  saves/<folder>/level.bin    name, seed, last played (see below)
//  saves/<folder>/region/      RegionStore files
//  saves/<folder>/session.lock held open by the process playing the world
//
//  level.bin (little-endian): "FVLV", u32 version (1), u64 seed,
//  i64 lastPlayed (unix milliseconds), string name (u16 length + UTF-8).
// ============================================================================

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

struct WorldInfo {
	std::filesystem::path folder;           // saves/<folder>
	std::string           name;
	std::uint64_t         seed = 0;
	std::int64_t          lastPlayed = 0;   // unix milliseconds
	bool                  readable = true;  // false: level.bin missing or damaged
};

// Seed from what the player typed: whitespace trimmed; empty -> `randomSeed`;
// a decimal integer that fits in int64 -> that value; anything else -> a
// stable 64-bit hash of the text.
[[nodiscard]] std::uint64_t parseSeed(std::string_view text, std::uint64_t randomSeed);
[[nodiscard]] std::uint64_t randomSeed();

// Every world folder under `savesDir`, most recently played first.
[[nodiscard]] std::vector<WorldInfo> listWorlds(const std::filesystem::path& savesDir);

// New world in a new folder derived from `name` (sanitised, made unique).
std::optional<WorldInfo> createWorld(const std::filesystem::path& savesDir, std::string_view name,
                                     std::string_view seedText, std::string& error);

// The world in `folder`, created with `seedIfNew` when it does not exist (dedicated server).
std::optional<WorldInfo> openOrCreateWorld(const std::filesystem::path& folder, std::uint64_t seedIfNew,
                                           std::string& error);

// Sets lastPlayed to now and rewrites level.bin.
bool markPlayed(WorldInfo& world, std::string& error);

// Removes the world's folder (and nothing else).
bool deleteWorld(const WorldInfo& world, std::string& error);

// Exclusive use of a world folder by one process; released when destroyed.
class WorldLock {
public:
	static std::unique_ptr<WorldLock> acquire(const std::filesystem::path& folder, std::string& error);
	~WorldLock();
	WorldLock(const WorldLock&)            = delete;
	WorldLock& operator=(const WorldLock&) = delete;

private:
	WorldLock() = default;
#if defined(_WIN32)
	void* _handle = nullptr;
#else
	int   _fd = -1;
#endif
};

#endif /* WORLD_STORE_HPP */
