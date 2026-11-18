#ifndef PLAYER_STORE_HPP
#define PLAYER_STORE_HPP

// ============================================================================
//  PlayerStore : one player's saved position, look and inventory in a small
//  binary file. A missing or damaged file just means a new player (a damaged
//  one is kept aside as <file>.bad).
// ============================================================================

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include <glm/glm.hpp>

#include "Inventory.hpp"

struct PlayerRecord {
	glm::vec3 eye{};
	float yaw = 0.0f;
	float pitch = 0.0f;
	Inventory inventory;
};

// A multiplayer player's file: worldFolder/players/<lowercase hex of the name's bytes>.bin
[[nodiscard]] std::filesystem::path playerFile(const std::filesystem::path& worldFolder, std::string_view name);
// The singleplayer file: worldFolder/player.bin
[[nodiscard]] std::filesystem::path singleplayerFile(const std::filesystem::path& worldFolder);

// Puts the cursor stack back into the slots, then writes a temp file and renames it over `file`.
// Safe to call concurrently for different files.
bool savePlayer(const std::filesystem::path& file, PlayerRecord record, std::string& error);

// nullopt for a missing file (silently) or a damaged one: that one is renamed to <file>.bad
// (replacing an older .bad), so a later save cannot destroy it, and the reason logged to stderr.
// Damaged includes a position outside PlayerCollision::plausibleEye. The pitch is clamped to [-90, 90].
[[nodiscard]] std::optional<PlayerRecord> loadPlayer(const std::filesystem::path& file);

#endif
