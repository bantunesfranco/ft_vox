#pragma once
#ifndef GAME_SESSION_HPP
# define GAME_SESSION_HPP

// ============================================================================
//  GameSession : what App talks to, whether the world is local or remote
//
//    LocalSession   singleplayer. Runs the game directly on App's World.
//                   No sockets, no threads of its own, no Winsock.
//    RemoteSession  multiplayer. Owns a NetClient and keeps App's World in
//                   sync with a server.
//
//  App creates one when the player picks Singleplayer or Multiplayer and
//  destroys it when they return to the title screen. Everything runs on the
//  main thread.
//
//  How App uses it (per frame, in this order):
//
//      session->update(camera->pos, dt);
//      if (auto spawn = session->pollSpawn())
//          camera->pos = spawn->eye; ... spawn->yaw, spawn->pitch ...
//      ... input / movement ...
//      session->onLocalPlayerMoved(eyePos, yaw, pitch);  // after moving the player
//      session->requestBreakBlock(hit.blockPos);         // from destroyBlock()
//      session->requestPlaceBlock(origin, dir);          // from placeBlock()
//      session->inventoryAction(action);                 // hotbar / inventory screen input
//      for (const SessionEvent& e : session->drainEvents()) { ... }
//
//  Requests may complete LATER. In singleplayer an edit is applied before the
//  request returns; in multiplayer it is applied when the server answers. So
//  never assume the world changed right after a request: react to the events.
//
//  The session owns the player's inventory: the item of a broken block is
//  added and a placed block's item consumed inside the session. App only
//  reads inventory() and sends actions.
// ============================================================================

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "Block.hpp"        // BlockId
#include "Inventory.hpp"

// Another player, as the game should draw it this frame (already smoothed).
struct RemotePlayerView {
	std::uint32_t id = 0;
	std::string   name;
	glm::vec3     eyePos{};
	float         yaw   = 0.0f;
	float         pitch = 0.0f;
};

// Where the local player starts, and where it looks (degrees, as Camera stores them).
struct SpawnPoint {
	glm::vec3 eye{};
	float     yaw   = 0.0f;
	float     pitch = 0.0f;
};

// Something that happened that the game must react to.
struct SessionEvent {
	enum class Type : std::uint8_t {
		BlockBroken,        // `block` was removed at `pos` (its item is already in the inventory)
		BlockPlaced,        // `block` was placed at `pos`  (its item is already consumed)
		Disconnected,       // the session ended; `reason` says why -> back to the title screen
	};

	Type        type{};
	glm::ivec3  pos{};      // BlockBroken / BlockPlaced
	BlockId     block{};    // BlockBroken / BlockPlaced
	std::string reason;     // Disconnected ("" is never used: there is always a reason)
};

class GameSession {
public:
	virtual ~GameSession() = default;

	GameSession(const GameSession&)            = delete;
	GameSession& operator=(const GameSession&) = delete;

	// Once per frame while a game is running (also while paused / in menus).
	// `playerPos` is the camera (eye) position. Singleplayer: loads and
	// generates chunks around it. Multiplayer: processes network traffic.
	virtual void update(const glm::vec3& playerPos, float dt) = 0;

	// Where to start, returned EXACTLY ONCE, as soon as it is known
	// (singleplayer: the terrain under the start position has loaded;
	// multiplayer: the server said where to spawn). Empty until then and after.
	[[nodiscard]] virtual std::optional<SpawnPoint> pollSpawn() = 0;

	// Multiplayer: the server's time of day (ticks), once per update received.
	[[nodiscard]] virtual std::optional<std::uint32_t> pollTimeOfDay() { return std::nullopt; }

	// Ask to remove the block at `pos` (a BlockRaycastHit::blockPos). Nothing is
	// requested when the inventory could not take the block's item.
	// Result arrives as SessionEvent::BlockBroken.
	virtual void requestBreakBlock(const glm::ivec3& pos) = 0;

	// Ask to place the selected slot's block where the ray (origin, direction)
	// hits the world. Result arrives as SessionEvent::BlockPlaced.
	virtual void requestPlaceBlock(const glm::vec3& origin, const glm::vec3& direction) = 0;

	// The local player's inventory, as the UI should show it.
	[[nodiscard]] virtual const Inventory& inventory() const = 0;

	// A hotbar selection, a click in the inventory screen, or closing it.
	virtual void inventoryAction(const InventoryAction& action) = 0;

	// Tell the session where the local player is. Called after movement each
	// frame. Singleplayer keeps it for the player save; multiplayer sends it to
	// the server (rate limited internally).
	virtual void onLocalPlayerMoved(const glm::vec3& eyePos, float yaw, float pitch) = 0;

	// Returns and clears everything that happened since the last call, oldest first.
	[[nodiscard]] virtual std::vector<SessionEvent> drainEvents() = 0;

	// True for multiplayer. Lets the UI hide things that only make sense offline
	// (pause stops the game offline, but cannot online).
	[[nodiscard]] virtual bool isRemote() const noexcept = 0;

	// Other players to draw. Always empty in singleplayer.
	[[nodiscard]] virtual const std::vector<RemotePlayerView>& remotePlayers() const {
		static const std::vector<RemotePlayerView> none;
		return none;
	}

	// One line for the HUD while the session is not playable yet
	// ("Connecting to ..."), empty once it is.
	[[nodiscard]] virtual std::string statusText() const { return {}; }

protected:
	GameSession() = default;
};

// ============================================================================
//  LocalSession : singleplayer
//
//  Drives the World App already owns: generates chunks around the player on
//  the ThreadPool, edits blocks through BlockSystem, and answers every request
//  immediately. It borrows the world and never touches the network layer.
//
//  It owns the player: its inventory and, with a player file, its saved
//  position, look and inventory. The player is saved every SAVE_INTERVAL
//  seconds of update() time and when the session is destroyed, but only once
//  it has spawned (a world left while loading keeps its previous save).
// ============================================================================

class World;
class ThreadPool;
class BlockSystem;

class LocalSession final : public GameSession {
public:
	static constexpr float SAVE_INTERVAL = 60.0f;   // seconds of update() time

	// All three references must outlive the session.
	// `startPos`: its x and z decide where a new player spawns (the height comes
	// from the terrain). `playerFile`: where the player is loaded from and saved
	// to; when it holds a record, the player resumes from it instead (missing
	// or damaged: a new player). nullopt never saves (the benchmark).
	LocalSession(World& world,
	             ThreadPool& threadPool,
	             const BlockSystem& blocks,
	             const glm::vec3& startPos,
	             std::optional<std::filesystem::path> playerFile);
	// Saves the player (see above). Never throws.
	~LocalSession() override;

	void update(const glm::vec3& playerPos, float dt) override;
	[[nodiscard]] std::optional<SpawnPoint> pollSpawn() override;

	void requestBreakBlock(const glm::ivec3& pos) override;
	void requestPlaceBlock(const glm::vec3& origin, const glm::vec3& direction) override;

	[[nodiscard]] const Inventory& inventory() const override { return _inventory; }
	void inventoryAction(const InventoryAction& action) override;

	void onLocalPlayerMoved(const glm::vec3& eyePos, float yaw, float pitch) override;

	[[nodiscard]] std::vector<SessionEvent> drainEvents() override;
	[[nodiscard]] bool isRemote() const noexcept override { return false; }

private:
	[[nodiscard]] glm::vec3 spawnTarget() const;   // where the spawn will be, roughly
	void savePlayerFile() const;                   // logs a failure to stderr

	World&             _world;
	ThreadPool&        _threadPool;
	const BlockSystem& _blocks;

	glm::vec2          _spawnXZ;
	bool               _spawnDone = false;
	std::vector<SessionEvent> _events;

	Inventory                            _inventory;
	std::optional<std::filesystem::path> _playerFile;
	std::optional<SpawnPoint>            _savedSpawn;   // from the player file
	SpawnPoint                           _player;       // the latest position and look
	float                                _sinceSave = 0.0f;
};

// RemoteSession (multiplayer) lives in RemoteSession.hpp.

#endif /* GAME_SESSION_HPP */
