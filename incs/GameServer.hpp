#pragma once
#ifndef GAME_SERVER_HPP
# define GAME_SERVER_HPP

// ============================================================================
//  GameServer : the authoritative multiplayer world
//
//  Game logic on top of net::Transport. Everything runs on the thread that
//  calls run() (the Transport loop), except terrain generation, which runs on
//  its own ThreadPool and comes back through Transport::post().
//
//    - terrain   : World::generateTerrain (same generator and same saves/ as
//                  singleplayer), kept in `_columns` while some player is near
//    - streaming : every player gets the columns within viewRadius, nearest
//                  first, a few per tick, only while its send queue is below
//                  Config::lowWaterMark; far ones are unloaded
//    - edits     : Break/Place are validated (reach, target, collisions) and
//                  applied, saved, then broadcast as BlockUpdate; a rejected
//                  edit is answered with the real block so the client resyncs
//    - players   : positions are relayed 20 times a second (EntityMove), joins
//                  and leaves are announced
//
//  Two ways to run it:
//    dedicated  `ft_vox --server [port]`   (main.cpp, no window)
//    hosted     HostedServer, from the "Host" button (background thread)
// ============================================================================

#include <climits>
#include <condition_variable>
#include <filesystem>
#include <mutex>
#include <optional>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <glm/glm.hpp>

#include "Net.hpp"
#include "Protocol.hpp"
#include "World.hpp"
#include "RegionStore.hpp"
#include "WorldStore.hpp"

class ThreadPool;

struct ServerConfig {
	std::uint16_t port          = proto::DEFAULT_PORT;
	int           viewRadius    = proto::VIEW_RADIUS;
	unsigned      workerThreads = 2;
	std::size_t   maxPlayers    = 16;
	bool          log           = true;
	// The world played: created there (with `seed`, or a random one) when
	// missing. An existing world keeps its own seed.
	std::filesystem::path         worldFolder = "saves/server";
	std::optional<std::uint64_t>  seed;
};

class GameServer final : public net::ITransportHandler {
public:
	explicit GameServer(ServerConfig config);
	~GameServer() override;
	GameServer(const GameServer&)            = delete;
	GameServer& operator=(const GameServer&) = delete;

	// Opens the listener. False (and `error`) if the port is taken, etc.
	bool start(std::string& error);
	// The event loop. Returns after stop(), once every changed column is saved.
	void run();
	// Thread-safe and async-signal-safe.
	void stop();

	// --- ITransportHandler ------------------------------------------------------
	bool onConnect(net::ConnId id, const net::PeerInfo& peer) override;
	bool onLogin(net::ConnId id, const std::string& name) override;
	void onPacket(net::ConnId id, net::PacketId type, net::ByteReader& body) override;
	void onDisconnect(net::ConnId id, net::DisconnectReason why) override;
	void onTick(std::uint64_t tick) override;

private:
	static constexpr int           kChunksPerTick   = 12;     // per player
	static constexpr std::uint64_t kKeepAliveTicks  = 100;    // 5 s at 20 Hz
	static constexpr std::uint64_t kEvictEveryTicks = 40;

	using ColumnPtr = std::shared_ptr<ChunkColumn>;

	struct Player {
		net::ConnId  conn = net::INVALID_CONN;
		std::string  name;
		glm::vec3    pos{};
		float        yaw   = 0.0f;
		float        pitch = 0.0f;
		bool         spawned = false;     // LoginOk sent
		ChunkCoord   center{INT_MAX, INT_MAX};
		std::vector<ChunkCoord>        wanted;   // within viewRadius, nearest first
		std::unordered_set<ChunkCoord> sent;     // columns this client holds
	};

	// ---- packets ------------------------------------------------------------------
	void onPlayerMove(Player& p, net::ByteReader& body);
	void onBreakBlock(Player& p, net::ByteReader& body);
	void onPlaceBlock(Player& p, net::ByteReader& body);

	// ---- tick steps ---------------------------------------------------------------
	void spawnWaitingPlayers();
	void spawn(Player& p);
	void updateView(Player& p);            // wanted list + unloads when the center moved
	void streamChunks(Player& p);
	void requestGeneration();
	void broadcastPositions();
	void evictColumns();

	// ---- world --------------------------------------------------------------------
	void requestColumn(const ChunkCoord& coord);
	void onColumnGenerated(const ChunkCoord& coord, ColumnPtr column, bool generated);
	void saveColumn(ColumnPtr column);             // on the pool; counted in _savesInFlight
	void saveAllAndWait();
	ChunkColumn* columnAt(const glm::ivec3& pos, glm::ivec3& local);
	bool blockAt(const glm::ivec3& pos, BlockId& out);
	void setBlock(const glm::ivec3& pos, BlockId block);
	void sendBlockTo(const Player& p, const glm::ivec3& pos);
	net::SharedBuffer encodedColumn(const ChunkCoord& coord);
	[[nodiscard]] bool inReach(const Player& p, const glm::ivec3& pos) const;

	// ---- helpers ------------------------------------------------------------------
	std::vector<net::ConnId> spawnedConns(net::ConnId except = net::INVALID_CONN) const;
	void violation(const Player& p, const char* what);
	void log(const char* fmt, ...) const;

	ServerConfig _config;
	net::Config  _netConfig;

	World        _terrain;                 // generator only; its own chunk map is unused
	std::uint64_t _seed = 0;
	std::shared_ptr<RegionStore>                      _store;
	std::unique_ptr<WorldLock>                        _lock;
	std::unordered_map<ChunkCoord, ColumnPtr>         _columns;
	std::unordered_set<ChunkCoord>                    _dirty;      // generated or edited since saved
	std::mutex              _saveMutex;
	std::condition_variable _saveDone;
	std::size_t             _savesInFlight = 0;
	std::unordered_map<ChunkCoord, net::SharedBuffer> _encoded;   // ChunkData frames, until edited
	std::unordered_set<ChunkCoord>                    _generating;

	std::unordered_map<net::ConnId, Player> _players;
	bool         _spawnKnown = false;
	glm::vec3    _spawnPoint{};

	// Destroyed in this order: pool (joins workers, which post to the
	// transport), then transport. Both after everything the workers touch.
	std::unique_ptr<net::Transport> _transport;
	std::unique_ptr<ThreadPool>     _pool;
};

// ============================================================================
//  HostedServer : a GameServer on a background thread (the "Host" button)
// ============================================================================

class HostedServer {
public:
	HostedServer() = default;
	~HostedServer();
	HostedServer(const HostedServer&)            = delete;
	HostedServer& operator=(const HostedServer&) = delete;

	// Binds synchronously (so a taken port is reported here), then runs the
	// loop on a new thread.
	bool start(const ServerConfig& config, std::string& error);
	// Stops the loop and joins the thread. Idempotent.
	void stop();

private:
	std::unique_ptr<GameServer> _server;
	std::thread                 _thread;
};

#endif /* GAME_SERVER_HPP */
