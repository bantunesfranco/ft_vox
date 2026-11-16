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
//          camera->pos = *spawn;
//      ... input / movement ...
//      session->onLocalPlayerMoved(eyePos, yaw, pitch);  // after moving the player
//      session->requestBreakBlock(hit.blockPos);         // from destroyBlock()
//      session->requestPlaceBlock(origin, dir, block);   // from placeBlock()
//      for (const SessionEvent& e : session->drainEvents()) { ... }
//
//  Requests may complete LATER. In singleplayer an edit is applied before the
//  request returns; in multiplayer it is applied when the server answers. So
//  never assume the world changed right after a request: react to the events
//  (add the item on BlockBroken, consume it on BlockPlaced).
// ============================================================================

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "Block.hpp"        // BlockId

// Another player, as the game should draw it this frame (already smoothed).
struct RemotePlayerView {
	std::uint32_t id = 0;
	std::string   name;
	glm::vec3     eyePos{};
	float         yaw   = 0.0f;
	float         pitch = 0.0f;
};

// Something that happened that the game must react to.
struct SessionEvent {
	enum class Type : std::uint8_t {
		BlockBroken,        // `block` was removed at `pos`        -> give the player the item
		BlockPlaced,        // `block` was placed at `pos`         -> consume it from the hotbar
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

	// The eye position to start at, returned EXACTLY ONCE, as soon as it is known
	// (singleplayer: the terrain under the start position has loaded;
	// multiplayer: the server said where to spawn). Empty until then and after.
	[[nodiscard]] virtual std::optional<glm::vec3> pollSpawn() = 0;

	// Ask to remove the block at `pos` (a BlockRaycastHit::blockPos).
	// Result arrives as SessionEvent::BlockBroken.
	virtual void requestBreakBlock(const glm::ivec3& pos) = 0;

	// Ask to place `block` where the ray (origin, direction) hits the world.
	// Result arrives as SessionEvent::BlockPlaced.
	virtual void requestPlaceBlock(const glm::vec3& origin,
	                               const glm::vec3& direction,
	                               BlockId block) = 0;

	// Tell the session where the local player is. Called after movement each
	// frame. No-op in singleplayer; multiplayer sends it to the server (rate
	// limited internally).
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
//  immediately. It borrows everything, owns nothing, and never touches the
//  network layer.
//
//  Inventory handling stays in App: it reacts to the BlockBroken / BlockPlaced
//  events (the inventory.canAdd() check happens in App::destroyBlock, before
//  the request is made).
// ============================================================================

class World;
class ThreadPool;
class BlockSystem;

class LocalSession final : public GameSession {
public:
	// All three references must outlive the session.
	// `startPos` is the camera position when the game starts; its x and z decide
	// where the player spawns (the height comes from the terrain).
	LocalSession(World& world,
	             ThreadPool& threadPool,
	             const BlockSystem& blocks,
	             const glm::vec3& startPos);

	void update(const glm::vec3& playerPos, float dt) override;
	[[nodiscard]] std::optional<glm::vec3> pollSpawn() override;

	void requestBreakBlock(const glm::ivec3& pos) override;
	void requestPlaceBlock(const glm::vec3& origin,
	                       const glm::vec3& direction,
	                       BlockId block) override;

	void onLocalPlayerMoved(const glm::vec3& eyePos, float yaw, float pitch) override;

	[[nodiscard]] std::vector<SessionEvent> drainEvents() override;
	[[nodiscard]] bool isRemote() const noexcept override { return false; }

private:
	World&             _world;
	ThreadPool&        _threadPool;
	const BlockSystem& _blocks;

	glm::vec2          _spawnXZ;
	bool               _spawnDone = false;
	std::vector<SessionEvent> _events;
};

// RemoteSession (multiplayer) lives in RemoteSession.hpp.

#endif /* GAME_SESSION_HPP */