#pragma once
#ifndef REMOTE_SESSION_HPP
# define REMOTE_SESSION_HPP

// ============================================================================
//  RemoteSession : multiplayer
//
//  Keeps App's World in sync with a GameServer. Same GameSession API as
//  LocalSession, so App barely changes. It owns the NetClient; sockets are
//  never touched unless the player chose Multiplayer.
//
//  The server is authoritative:
//    - the client SENDS INTENT (move, break this block, place that block)
//    - the server validates, applies, and BROADCASTS THE RESULT (BlockUpdate)
//    - this class applies that result to the local World and reports it as a
//      SessionEvent. So BlockBroken / BlockPlaced come when the server CONFIRMS
//      the edit (an edit the server rejects, or does not answer within
//      kEditTimeout, produces no event).
//
//  Chunks are decoded on the main thread, in packet order: decoding a column
//  takes microseconds, and in-order application guarantees a BlockUpdate is
//  never applied before the ChunkData it modifies.
//
//  The wire contract is in Protocol.hpp.
//
//  While it exists, the World holds only what the server sent and is never
//  saved: the constructor and destructor both open() it without a store, so
//  no terrain leaks between singleplayer and a server.
// ============================================================================

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "GameSession.hpp"
#include "NetClient.hpp"

class World;
class BlockSystem;

// Where to connect and who you are.
struct RemoteConfig {
	std::string   host = "127.0.0.1";
	std::uint16_t port = 25565;
	std::string   playerName = "Player";      // 1..32 bytes
};

class RemoteSession final : public GameSession {
public:
	// Starts connecting; does not wait for the server. Both references must
	// outlive the session. If connecting fails at once, the first update()
	// reports it as a Disconnected event.
	RemoteSession(World& world, const BlockSystem& blocks, RemoteConfig config);
	~RemoteSession() override;

	// --- GameSession ---------------------------------------------------------

	void update(const glm::vec3& playerPos, float dt) override;
	[[nodiscard]] std::optional<glm::vec3> pollSpawn() override;     // from LoginOk, once
	[[nodiscard]] std::optional<std::uint32_t> pollTimeOfDay() override;

	void requestBreakBlock(const glm::ivec3& pos) override;          // sends BreakBlock
	void requestPlaceBlock(const glm::vec3& origin,
	                       const glm::vec3& direction,
	                       BlockId block) override;                   // raycasts locally, sends PlaceBlock

	void onLocalPlayerMoved(const glm::vec3& eyePos, float yaw, float pitch) override;   // rate limited

	[[nodiscard]] std::vector<SessionEvent> drainEvents() override;
	[[nodiscard]] bool isRemote() const noexcept override { return true; }

	[[nodiscard]] const std::vector<RemotePlayerView>& remotePlayers() const override { return _remoteView; }
	[[nodiscard]] std::string statusText() const override;

	enum class Phase : std::uint8_t {
		Connecting,     // TCP connect
		LoggingIn,      // connected, waiting for LoginOk
		Playing,        // LoginOk received
		Failed,         // a Disconnected event was queued
	};
	[[nodiscard]] Phase phase() const noexcept { return _phase; }

private:
	static constexpr int                       kMoveSendHz      = 20;
	static constexpr std::chrono::milliseconds kEditTimeout{5000};
	static constexpr float                     kRemoteSmoothing = 15.0f;   // 1/s, exponential approach

	// ---- inbound packets ------------------------------------------------------
	void handlePacket(net::PacketId id, net::ByteReader& body);
	void onLoginOk(net::ByteReader& body);
	void onChunkData(net::ByteReader& body);
	void onUnloadChunk(net::ByteReader& body);
	void onBlockUpdate(net::ByteReader& body);
	void onEntityMove(net::ByteReader& body);
	void onPlayerJoin(net::ByteReader& body);
	void onPlayerLeave(net::ByteReader& body);

	// ---- per-frame helpers ------------------------------------------------------
	void updateRemotePlayers(float dt);
	void expirePendingEdits();
	void fail(std::string reason);                // Phase::Failed + one Disconnected event

	// ---- state ------------------------------------------------------------------
	World&             _world;
	const BlockSystem& _blocks;
	RemoteConfig       _config;

	net::NetClient     _net;
	Phase              _phase    = Phase::Connecting;
	std::uint32_t      _playerId = 0;
	glm::vec3          _lastPlayerPos{};          // for the "Loading terrain" status

	std::optional<glm::vec3>  _spawn;

	std::optional<std::uint32_t> _timeOfDay;    // last TimeUpdate, until polled
	std::vector<SessionEvent> _events;

	// An edit we sent that the server has not answered yet. Settled by the
	// next BlockUpdate for the same cell: a break succeeded if it became air,
	// a place if it became `block`; anything else is a rejection.
	struct PendingEdit {
		enum class Kind : std::uint8_t { Break, Place };
		Kind           kind{};
		glm::ivec3     pos{};
		BlockId        block{};                   // Break: what was there. Place: what we placed.
		net::TimePoint sentAt{};
	};
	std::vector<PendingEdit> _pendingEdits;

	struct RemoteState {
		RemotePlayerView view;
		glm::vec3        targetPos{};
		float            targetYaw   = 0.0f;
		float            targetPitch = 0.0f;
		bool             placed      = false;     // view.eyePos is valid (snap on first update)
	};
	std::unordered_map<std::uint32_t, RemoteState> _remote;
	std::vector<RemotePlayerView>                  _remoteView;

	net::TimePoint _lastMoveSent{};
	glm::vec3      _lastSentPos{};
	float          _lastSentYaw   = 0.0f;
	float          _lastSentPitch = 0.0f;
	bool           _sentAnyMove   = false;
};

#endif /* REMOTE_SESSION_HPP */
