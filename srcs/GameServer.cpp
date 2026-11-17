#include "GameServer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <exception>
#include <ranges>
#include <utility>

#include "ChunkCodec.hpp"
#include "PlayerCollision.hpp"
#include "ThreadPool.hpp"

namespace {

int floorDiv(const int value, const int divisor) noexcept {
	const int q = value / divisor;
	const int r = value % divisor;
	return (r != 0 && ((r < 0) != (divisor < 0))) ? q - 1 : q;
}

ChunkCoord chunkOf(const glm::vec3& pos) noexcept {
	return {floorDiv(static_cast<int>(std::floor(pos.x)), ChunkSection::SIZE),
	        floorDiv(static_cast<int>(std::floor(pos.z)), ChunkSection::SIZE)};
}

int chebyshev(const ChunkCoord& a, const ChunkCoord& b) noexcept {
	return std::max(std::abs(a.x - b.x), std::abs(a.z - b.z));
}

bool finite(const glm::vec3& v) noexcept {
	return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

bool validName(const std::string& name) {
	return !name.empty() && name.size() <= proto::MAX_NAME_BYTES
	    && std::ranges::all_of(name, [](const char c) { return c >= 0x20 && c < 0x7F; });
}

constexpr BlockId BEDROCK_BLOCK = toBlockId(BlockType::Bedrock);

} // namespace

// ============================================================================
//  Lifecycle
// ============================================================================

GameServer::GameServer(ServerConfig config) : _config(std::move(config)) {
	_config.viewRadius    = std::clamp(_config.viewRadius, 2, 32);
	_config.workerThreads = std::max(1u, _config.workerThreads);
	_netConfig.port           = _config.port;
	_netConfig.maxConnections = _config.maxPlayers + 4;   // room to refuse politely
}

GameServer::~GameServer() {
	_pool.reset();          // join workers first: they post to the transport
	_transport.reset();
	_lock.reset();          // the world is free again only after the last save
}

bool GameServer::start(std::string& error) {
	const auto world = openOrCreateWorld(_config.worldFolder, _config.seed.value_or(randomSeed()), error);
	if (!world)
		return false;
	_lock = WorldLock::acquire(world->folder, error);
	if (!_lock) {
		error = "world " + world->folder.string() + " is locked: " + error;
		return false;
	}
	_seed  = world->seed;
	_world = *world;
	_timeOfDay = world->timeOfDay % TimeOfDay::TICKS_PER_DAY;
	_store = std::make_shared<RegionStore>(world->folder / "region");
	_generator = std::make_shared<worldgen::TerrainGenerator>(_seed);
	_spawnXZ = _generator->findSpawn();

	_transport = std::make_unique<net::Transport>(_netConfig, *this);
	if (!_transport->start(error))
		return false;
	_pool = std::make_unique<ThreadPool>(_config.workerThreads);
	log("world '%s' (%s) seed %lld", world->name.c_str(), world->folder.string().c_str(),
	    static_cast<long long>(_seed));
	log("listening on port %u (view radius %d)", static_cast<unsigned>(_config.port), _config.viewRadius);
	return true;
}

void GameServer::run() {
	if (!_transport)
		return;
	_transport->run();
	saveAllAndWait();
	_world.timeOfDay = _timeOfDay;
	std::string error;
	if (!markPlayed(_world, error))
		log("could not save the time of day: %s", error.c_str());
	log("saved, stopped");
}

void GameServer::saveColumn(ColumnPtr column) {
	{
		std::lock_guard lock(_saveMutex);
		++_savesInFlight;
	}
	_pool->enqueueFireAndForget([this, store = _store, column = std::move(column)] {
		if (!store->save(*column))
			std::fprintf(stderr, "[server] failed to save chunk %d,%d\n", column->coord().x, column->coord().z);
		std::lock_guard lock(_saveMutex);
		--_savesInFlight;
		_saveDone.notify_all();
	});
}

void GameServer::saveAllAndWait() {
	for (const ChunkCoord& coord : _dirty)
		if (const auto it = _columns.find(coord); it != _columns.end())
			saveColumn(it->second);
	_dirty.clear();
	std::unique_lock lock(_saveMutex);
	_saveDone.wait(lock, [this] { return _savesInFlight == 0; });
}
void GameServer::stop() { if (_transport) _transport->stop(); }

// ============================================================================
//  Connections
// ============================================================================

bool GameServer::onConnect(const net::ConnId id, const net::PeerInfo& peer) {
	if (_players.size() >= _config.maxPlayers) {
		log("refused %s:%u: server full", peer.address.c_str(), static_cast<unsigned>(peer.port));
		return false;
	}
	_players[id].conn = id;   // not logged in yet: no name, not spawned
	return true;
}

bool GameServer::onLogin(const net::ConnId id, const std::string& name) {
	const auto it = _players.find(id);
	if (it == _players.end() || !validName(name))
		return false;
	it->second.name = name;
	log("%s joined (player %u)", name.c_str(), static_cast<unsigned>(id));
	return true;               // spawned (LoginOk) once the spawn column exists
}

void GameServer::onDisconnect(const net::ConnId id, const net::DisconnectReason why) {
	const auto it = _players.find(id);
	if (it == _players.end())
		return;
	const bool wasSpawned = it->second.spawned;
	if (!it->second.name.empty())
		log("%s left (%s)", it->second.name.c_str(), net::toString(why));
	_players.erase(it);

	if (wasSpawned && _transport) {
		net::ByteWriter w(net::PacketId::PlayerLeave, 4);
		w.writeU32(id);
		_transport->broadcast(spawnedConns(), w.finish());
	}
}

void GameServer::onPacket(const net::ConnId id, const net::PacketId type, net::ByteReader& body) {
	const auto it = _players.find(id);
	if (it == _players.end())
		return;
	Player& p = it->second;

	switch (type) {
		case net::PacketId::PlayerMove: onPlayerMove(p, body); break;
		case net::PacketId::BreakBlock: onBreakBlock(p, body); break;
		case net::PacketId::PlaceBlock: onPlaceBlock(p, body); break;
		case net::PacketId::Disconnect:
			_transport->disconnect(id, net::DisconnectReason::PeerClosed);
			break;
		default:
			break;             // packetAllowedIn() already filtered everything else
	}
}

void GameServer::violation(const Player& p, const char* what) {
	log("kicking %s: %s", p.name.c_str(), what);
	_transport->disconnect(p.conn, net::DisconnectReason::ProtocolViolation);
}

// ============================================================================
//  Packets
// ============================================================================

void GameServer::onPlayerMove(Player& p, net::ByteReader& body) {
	const glm::vec3 pos{body.readF32(), body.readF32(), body.readF32()};
	const float yaw   = body.readF32();
	const float pitch = body.readF32();
	if (!body.ok())
		return;                                        // the transport kicks
	if (!body.atEnd() || !finite(pos) || !std::isfinite(yaw) || !std::isfinite(pitch)
	    || std::abs(pos.x) > 3.0e7f || std::abs(pos.z) > 3.0e7f
	    || pos.y < static_cast<float>(ChunkColumn::MIN_WORLD_Y - 512)
	    || pos.y > static_cast<float>(ChunkColumn::MAX_WORLD_Y + 512)) {
		violation(p, "bad PlayerMove");
		return;
	}
	if (!p.spawned)
		return;                                        // it does not know where it is yet
	p.pos   = pos;
	p.yaw   = yaw;
	p.pitch = std::clamp(pitch, -90.0f, 90.0f);
}

void GameServer::onBreakBlock(Player& p, net::ByteReader& body) {
	const glm::ivec3 pos{body.readI32(), body.readI32(), body.readI32()};
	if (!body.ok())
		return;
	if (!body.atEnd()) { violation(p, "bad BreakBlock"); return; }

	BlockId current = AIR_BLOCK;
	const bool allowed = p.spawned && inReach(p, pos) && blockAt(pos, current)
	                  && current != AIR_BLOCK && current != BEDROCK_BLOCK;
	if (allowed)
		setBlock(pos, AIR_BLOCK);
	else
		sendBlockTo(p, pos);                           // tell it what is really there
}

void GameServer::onPlaceBlock(Player& p, net::ByteReader& body) {
	const glm::ivec3 pos{body.readI32(), body.readI32(), body.readI32()};
	const BlockId block = body.readU16();
	if (!body.ok())
		return;
	if (!body.atEnd() || block >= BLOCK_TYPE_COUNT) { violation(p, "bad PlaceBlock"); return; }

	BlockId current = AIR_BLOCK;
	bool allowed = p.spawned && block != AIR_BLOCK && block != BEDROCK_BLOCK
	            && inReach(p, pos) && blockAt(pos, current)
	            && _blocks.get(current).replaceable;
	BlockId below = AIR_BLOCK;
	if (allowed && (!blockAt(pos - glm::ivec3(0, 1, 0), below) || !placementAllowed(_blocks, block, below)))
		allowed = false;                               // a torch needs solid ground
	if (allowed) {
		for (const auto& other : _players | std::views::values) {
			if (other.spawned && PlayerCollision::overlapsBlock(other.pos, pos)) {
				allowed = false;                       // would trap a player
				break;
			}
		}
	}
	if (allowed)
		setBlock(pos, block);
	else
		sendBlockTo(p, pos);
}

// ============================================================================
//  Tick
// ============================================================================

void GameServer::sendTimeTo(const net::ConnId conn) {
	net::ByteWriter time(net::PacketId::TimeUpdate, 4);
	time.writeU32(_timeOfDay);
	_transport->send(conn, time.finish());
}

void GameServer::onTick(const std::uint64_t tick) {
	_timeOfDay = (_timeOfDay + 1) % TimeOfDay::TICKS_PER_DAY;
	spawnWaitingPlayers();
	for (auto& p : _players | std::views::values) {
		if (!p.spawned)
			continue;
		updateView(p);
		streamChunks(p);
	}
	requestGeneration();
	broadcastPositions();

	if (tick % kKeepAliveTicks == 0) {
		std::vector<net::ConnId> all;
		for (const auto& [id, p] : _players)
			if (!p.name.empty())
				all.push_back(id);
		_transport->broadcast(all, net::ByteWriter(net::PacketId::KeepAlive, 0).finish());
	}
	if (tick % kTimeUpdateTicks == 0)
		for (const auto& [id, p] : _players)
			if (p.spawned)
				sendTimeTo(id);
	if (tick % kEvictEveryTicks == 0)
		evictColumns();
}

void GameServer::spawnWaitingPlayers() {
	bool anyWaiting = false;
	for (const auto& p : _players | std::views::values)
		anyWaiting |= !p.spawned && !p.name.empty();
	if (!anyWaiting)
		return;

	if (!_spawnKnown) {
		const ChunkCoord spawnChunk = chunkOf(glm::vec3(_spawnXZ.x, 0, _spawnXZ.y));
		const auto it = _columns.find(spawnChunk);
		if (it == _columns.end()) {
			if (!_generating.contains(spawnChunk))
				requestColumn(spawnChunk);
			return;
		}
		// Stand on the highest opaque block at the middle of the spawn chunk.
		const int lx = _spawnXZ.x - spawnChunk.x * ChunkSection::SIZE;
		const int lz = _spawnXZ.y - spawnChunk.z * ChunkSection::SIZE;
		int surface = 100;
		for (int sy = ChunkColumn::HEIGHT - 1; sy >= 0; --sy) {
			if (_blocks.get(it->second->getUnchecked(lx, sy, lz)).solid) {
				surface = ChunkColumn::toWorldY(sy);
				break;
			}
		}
		_spawnPoint = {static_cast<float>(_spawnXZ.x) + 0.5f,
		               static_cast<float>(surface + 1) + PlayerCollision::EYE_HEIGHT,
		               static_cast<float>(_spawnXZ.y) + 0.5f};
		_spawnKnown = true;
	}

	for (auto& p : _players | std::views::values)
		if (!p.spawned && !p.name.empty())
			spawn(p);
}

void GameServer::spawn(Player& p) {
	p.spawned = true;
	p.pos     = _spawnPoint;

	net::ByteWriter ok(net::PacketId::LoginOk, 16);
	ok.writeU32(p.conn).writeF32(p.pos.x).writeF32(p.pos.y).writeF32(p.pos.z);
	_transport->send(p.conn, ok.finish());
	sendTimeTo(p.conn);                    // night shows at once, not after the first update

	// Everyone already here, to the newcomer...
	for (const auto& other : _players | std::views::values) {
		if (!other.spawned || other.conn == p.conn)
			continue;
		net::ByteWriter join(net::PacketId::PlayerJoin, 8 + other.name.size());
		join.writeU32(other.conn).writeString(other.name);
		_transport->send(p.conn, join.finish());
	}
	// ...and the newcomer to everyone else.
	net::ByteWriter join(net::PacketId::PlayerJoin, 8 + p.name.size());
	join.writeU32(p.conn).writeString(p.name);
	_transport->broadcast(spawnedConns(p.conn), join.finish());
}

void GameServer::updateView(Player& p) {
	const ChunkCoord center = chunkOf(p.pos);
	if (center == p.center)
		return;
	p.center = center;

	const int r = _config.viewRadius;
	p.wanted.clear();
	p.wanted.reserve(static_cast<std::size_t>((2 * r + 1) * (2 * r + 1)));
	for (int dz = -r; dz <= r; ++dz)
		for (int dx = -r; dx <= r; ++dx)
			p.wanted.push_back({center.x + dx, center.z + dz});
	std::ranges::sort(p.wanted, [&center](const ChunkCoord& a, const ChunkCoord& b) {
		const int da = (a.x - center.x) * (a.x - center.x) + (a.z - center.z) * (a.z - center.z);
		const int db = (b.x - center.x) * (b.x - center.x) + (b.z - center.z) * (b.z - center.z);
		return da < db;
	});

	// One column of hysteresis so walking along a border does not thrash.
	for (auto it = p.sent.begin(); it != p.sent.end();) {
		if (chebyshev(*it, center) <= r + 1) {
			++it;
			continue;
		}
		net::ByteWriter w(net::PacketId::UnloadChunk, 8);
		w.writeI32(it->x).writeI32(it->z);
		_transport->send(p.conn, w.finish());
		it = p.sent.erase(it);
	}
}

void GameServer::streamChunks(Player& p) {
	int budget = kChunksPerTick;
	for (const ChunkCoord& coord : p.wanted) {
		if (budget <= 0 || _transport->pendingBytes(p.conn) >= _netConfig.lowWaterMark)
			return;
		if (p.sent.contains(coord))
			continue;
		net::SharedBuffer frame = encodedColumn(coord);
		if (!frame)
			continue;                                  // not generated yet
		_transport->send(p.conn, std::move(frame));
		p.sent.insert(coord);
		--budget;
	}
}

void GameServer::requestGeneration() {
	const std::size_t maxInFlight = static_cast<std::size_t>(_config.workerThreads) * 3;

	// Each round gives every player its nearest missing column, so one player
	// far from the others does not starve them. Each round requests at least
	// one column or ends the loop, so it runs at most maxInFlight times.
	bool requested = true;
	while (requested && _generating.size() < maxInFlight) {
		requested = false;
		for (const auto& p : _players | std::views::values) {
			if (!p.spawned)
				continue;
			const auto missing = std::ranges::find_if(p.wanted, [this](const ChunkCoord& c) {
				return !_columns.contains(c) && !_generating.contains(c);
			});
			if (missing == p.wanted.end())
				continue;
			requestColumn(*missing);
			requested = true;
			if (_generating.size() >= maxInFlight)
				return;
		}
	}
}

void GameServer::broadcastPositions() {
	std::vector<net::ConnId> to = spawnedConns();
	if (to.size() < 2)
		return;                                        // nobody to tell about anybody

	net::ByteWriter w(net::PacketId::EntityMove, 2 + to.size() * 24);
	w.writeU16(static_cast<std::uint16_t>(to.size()));
	for (const net::ConnId id : to) {
		const Player& p = _players.at(id);
		w.writeU32(id).writeF32(p.pos.x).writeF32(p.pos.y).writeF32(p.pos.z)
		 .writeF32(p.yaw).writeF32(p.pitch);
	}
	_transport->broadcast(to, w.finish());
}

void GameServer::evictColumns() {
	const int keep = _config.viewRadius + 2;
	for (auto it = _columns.begin(); it != _columns.end();) {
		bool needed = false;
		for (const auto& p : _players | std::views::values) {
			const ChunkCoord center = chunkOf(p.spawned ? p.pos : glm::vec3(_spawnXZ.x, 0, _spawnXZ.y));
			if (chebyshev(it->first, center) <= keep) {
				needed = true;
				break;
			}
		}
		if (needed) {
			++it;
			continue;
		}
		_encoded.erase(it->first);
		if (_dirty.erase(it->first) != 0)
			saveColumn(it->second);                    // a snapshot: the map entry goes now
		it = _columns.erase(it);
	}
}

// ============================================================================
//  World
// ============================================================================

void GameServer::requestColumn(const ChunkCoord& coord) {
	_generating.insert(coord);
	net::Transport* transport = _transport.get();
	_pool->enqueueFireAndForget([this, transport, generator = _generator, store = _store, coord] {
		ColumnPtr column;
		bool generated = false;
		try {
			if (auto saved = store->load(coord)) {
				column = std::move(saved);             // a saved column is used as is
			} else {
				column = std::make_shared<ChunkColumn>(coord);
				generator->generate(*column);
				generated = true;
			}
		} catch (const std::exception& e) {
			std::fprintf(stderr, "[server] failed to generate chunk %d,%d: %s\n", coord.x, coord.z, e.what());
			column.reset();
		}
		transport->post([this, coord, column, generated] { onColumnGenerated(coord, column, generated); });
	});
}

void GameServer::onColumnGenerated(const ChunkCoord& coord, ColumnPtr column, const bool generated) {
	_generating.erase(coord);
	if (!column)
		return;                                        // failures are retried by requestGeneration()
	_columns[coord] = std::move(column);
	if (generated)
		_dirty.insert(coord);
}

ChunkColumn* GameServer::columnAt(const glm::ivec3& pos, glm::ivec3& local) {
	if (pos.y < ChunkColumn::MIN_WORLD_Y || pos.y >= ChunkColumn::MAX_WORLD_Y)
		return nullptr;
	const ChunkCoord coord{floorDiv(pos.x, ChunkSection::SIZE), floorDiv(pos.z, ChunkSection::SIZE)};
	const auto it = _columns.find(coord);
	if (it == _columns.end())
		return nullptr;
	local = {pos.x - coord.x * ChunkSection::SIZE, ChunkColumn::toStorageY(pos.y), pos.z - coord.z * ChunkSection::SIZE};
	return it->second.get();
}

bool GameServer::blockAt(const glm::ivec3& pos, BlockId& out) {
	glm::ivec3 local{};
	const ChunkColumn* column = columnAt(pos, local);
	if (!column)
		return false;
	out = column->getUnchecked(local.x, local.y, local.z);
	return true;
}

void GameServer::setBlock(const glm::ivec3& pos, const BlockId block) {
	glm::ivec3 local{};
	if (!columnAt(pos, local))
		return;
	const ChunkCoord coord{floorDiv(pos.x, ChunkSection::SIZE), floorDiv(pos.z, ChunkSection::SIZE)};
	ColumnPtr& column = _columns.at(coord);
	if (column.use_count() > 1)                        // a save job still reads it: copy on write
		column = std::make_shared<ChunkColumn>(*column);
	column->setUnchecked(local.x, local.y, local.z, block);

	_dirty.insert(coord);
	_encoded.erase(coord);

	net::ByteWriter w(net::PacketId::BlockUpdate, 14);
	w.writeI32(pos.x).writeI32(pos.y).writeI32(pos.z).writeU16(block);
	const net::SharedBuffer frame = w.finish();
	for (const auto& p : _players | std::views::values)
		if (p.spawned && p.sent.contains(coord))
			_transport->send(p.conn, frame);
}

void GameServer::sendBlockTo(const Player& p, const glm::ivec3& pos) {
	BlockId current = AIR_BLOCK;
	if (!blockAt(pos, current))
		return;                                        // nothing to resync: its pending edit expires
	net::ByteWriter w(net::PacketId::BlockUpdate, 14);
	w.writeI32(pos.x).writeI32(pos.y).writeI32(pos.z).writeU16(current);
	_transport->send(p.conn, w.finish());
}

net::SharedBuffer GameServer::encodedColumn(const ChunkCoord& coord) {
	if (const auto it = _encoded.find(coord); it != _encoded.end())
		return it->second;
	const auto col = _columns.find(coord);
	if (col == _columns.end())
		return nullptr;

	net::ByteWriter w(net::PacketId::ChunkData, 16u * 1024);
	w.writeI32(coord.x).writeI32(coord.z);
	ChunkCodec::encode(*col->second, w);
	net::SharedBuffer frame = w.finish();
	_encoded[coord] = frame;                           // shared by every player that needs it
	return frame;
}

bool GameServer::inReach(const Player& p, const glm::ivec3& pos) const {
	const glm::vec3 center = glm::vec3(pos) + glm::vec3(0.5f);
	const glm::vec3 d = center - p.pos;
	constexpr float limit = proto::EDIT_REACH + 0.87f;   // + half a block diagonal
	return glm::dot(d, d) <= limit * limit;
}

// ============================================================================
//  Helpers
// ============================================================================

std::vector<net::ConnId> GameServer::spawnedConns(const net::ConnId except) const {
	std::vector<net::ConnId> out;
	out.reserve(_players.size());
	for (const auto& [id, p] : _players)
		if (p.spawned && id != except)
			out.push_back(id);
	return out;
}

void GameServer::log(const char* fmt, ...) const {
	if (!_config.log)
		return;
	std::va_list args;
	va_start(args, fmt);
	std::fputs("[server] ", stdout);
	std::vfprintf(stdout, fmt, args);
	std::fputc('\n', stdout);
	std::fflush(stdout);
	va_end(args);
}

// ============================================================================
//  HostedServer
// ============================================================================

HostedServer::~HostedServer() {
	stop();
}

bool HostedServer::start(const ServerConfig& config, std::string& error) {
	stop();
	auto server = std::make_unique<GameServer>(config);
	if (!server->start(error))
		return false;
	_server = std::move(server);
	_thread = std::thread([server = _server.get()] { server->run(); });
	return true;
}

void HostedServer::stop() {
	if (_server)
		_server->stop();
	if (_thread.joinable())
		_thread.join();
	_server.reset();
}
