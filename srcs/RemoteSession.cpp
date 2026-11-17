#include "RemoteSession.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <utility>

#include "TimeOfDay.hpp"

#include "BlockSystem.hpp"
#include "ChunkCodec.hpp"
#include "PlayerCollision.hpp"
#include "Protocol.hpp"
#include "World.hpp"

namespace {

bool finite(const glm::vec3& v) noexcept {
	return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

// Shortest signed difference between two angles in degrees, in [-180, 180).
float angleDelta(const float from, const float to) noexcept {
	float d = std::fmod(to - from + 180.0f, 360.0f);
	if (d < 0.0f) d += 360.0f;
	return d - 180.0f;
}

} // namespace

RemoteSession::RemoteSession(World& world, const BlockSystem& blocks, RemoteConfig config)
	: _world(world), _blocks(blocks), _config(std::move(config)) {
	_world.open(0, nullptr);              // only the server's terrain from now on, never saved

	if (!_net.connect(_config.host, _config.port, _config.playerName))
		fail(_net.error());
}

RemoteSession::~RemoteSession() {
	_net.close();
	_world.open(0, nullptr);              // the next session starts from an empty world
}

// ============================================================================
//  Per frame
// ============================================================================

void RemoteSession::update(const glm::vec3& playerPos, const float dt) {
	_lastPlayerPos = playerPos;
	if (_phase == Phase::Failed)
		return;

	_net.poll();

	// Packets that arrived before a close are still applied, in order.
	net::NetClient::Packet packet;
	while (_phase != Phase::Failed && _net.nextPacket(packet)) {
		net::ByteReader body(packet.payload.data(), packet.payload.size());
		handlePacket(packet.id, body);
	}
	if (_phase == Phase::Failed)
		return;

	if (_net.state() == net::NetClient::State::Closed) {
		fail(_net.error());
		return;
	}
	if (_phase == Phase::Connecting && _net.state() == net::NetClient::State::Connected)
		_phase = Phase::LoggingIn;

	updateRemotePlayers(dt);
	expirePendingEdits();
}

std::optional<std::uint32_t> RemoteSession::pollTimeOfDay() {
	return std::exchange(_timeOfDay, std::nullopt);
}

std::optional<SpawnPoint> RemoteSession::pollSpawn() {
	return std::exchange(_spawn, std::nullopt);
}

std::vector<SessionEvent> RemoteSession::drainEvents() {
	std::vector<SessionEvent> out;
	out.swap(_events);
	return out;
}

std::string RemoteSession::statusText() const {
	const std::string where = _config.host + ":" + std::to_string(_config.port);
	switch (_phase) {
		case Phase::Connecting: return "Connecting to " + where + "...";
		case Phase::LoggingIn:  return "Logging in to " + where + "...";
		case Phase::Failed:     return {};
		case Phase::Playing: {
			const int x = static_cast<int>(std::floor(_lastPlayerPos.x));
			const int y = static_cast<int>(std::floor(_lastPlayerPos.y));
			const int z = static_cast<int>(std::floor(_lastPlayerPos.z));
			BlockId block = AIR_BLOCK;
			const int clampedY = std::clamp(y, ChunkColumn::MIN_WORLD_Y, ChunkColumn::MAX_WORLD_Y - 1);
			return _world.getBlockAt({x, clampedY, z}, block) ? std::string{} : "Loading terrain...";
		}
	}
	return {};
}

// ============================================================================
//  Requests (intent only: nothing changes locally until the server answers)
// ============================================================================

void RemoteSession::requestBreakBlock(const glm::ivec3& pos) {
	if (_phase != Phase::Playing)
		return;
	for (const PendingEdit& e : _pendingEdits)
		if (e.pos == pos)
			return;                          // already waiting on this cell

	BlockId current = AIR_BLOCK;
	if (!_world.getBlockAt(pos, current) || current == AIR_BLOCK)
		return;
	if (!_inventory.view().canTakeDropOf(current))
		return;                              // its item would not fit

	net::ByteWriter w(net::PacketId::BreakBlock, 12);
	w.writeI32(pos.x).writeI32(pos.y).writeI32(pos.z);
	_net.send(w.finish());
	_pendingEdits.push_back({PendingEdit::Kind::Break, pos, current, net::Clock::now()});
}

void RemoteSession::requestPlaceBlock(const glm::vec3& origin, const glm::vec3& direction) {
	const ItemStack& selected = _inventory.view().selectedStack();
	if (_phase != Phase::Playing || selected.empty())
		return;
	const BlockId block = ItemRegistry::get(selected.item).placedBlock;
	if (block == AIR_BLOCK || block >= BLOCK_TYPE_COUNT)
		return;

	// Same local checks as BlockSystem::placeBlock, so obviously invalid
	// placements are never sent. The server re-checks everything.
	const BlockRaycastHit hit = _blocks.raycast(origin, direction, _world);
	if (!hit.valid)
		return;
	const glm::ivec3 target = hit.adjacentPos;
	const glm::ivec3 eyeCell{
		static_cast<int>(std::floor(origin.x)),
		static_cast<int>(std::floor(origin.y)),
		static_cast<int>(std::floor(origin.z))
	};
	if (target == eyeCell || PlayerCollision::overlapsBlock(origin, target))
		return;
	BlockId existing = AIR_BLOCK;
	if (!_world.getBlockAt(target, existing) || !_world.getBlockRegistry().get(existing).replaceable)
		return;
	BlockId below = AIR_BLOCK;
	if (!_world.getBlockAt(target - glm::ivec3(0, 1, 0), below) || !placementAllowed(_world.getBlockRegistry(), block, below))
		return;                              // a torch needs solid ground (the server checks too)
	for (const PendingEdit& e : _pendingEdits)
		if (e.pos == target)
			return;

	net::ByteWriter w(net::PacketId::PlaceBlock, 14);
	w.writeI32(target.x).writeI32(target.y).writeI32(target.z).writeU16(block);
	_net.send(w.finish());
	_pendingEdits.push_back({PendingEdit::Kind::Place, target, block, net::Clock::now()});
}

void RemoteSession::inventoryAction(const InventoryAction& action) {
	(void)_inventory.apply(action);          // shown at once; not sent to the server yet
}

void RemoteSession::onLocalPlayerMoved(const glm::vec3& eyePos, const float yaw, const float pitch) {
	if (_phase != Phase::Playing || !finite(eyePos))
		return;

	const net::TimePoint now = net::Clock::now();
	if (_sentAnyMove) {
		if (now - _lastMoveSent < std::chrono::milliseconds(1000 / kMoveSendHz))
			return;
		const glm::vec3 d = eyePos - _lastSentPos;
		if (glm::dot(d, d) < 1e-6f
		    && std::abs(yaw - _lastSentYaw) < 0.01f && std::abs(pitch - _lastSentPitch) < 0.01f)
			return;                          // nothing changed: the NetClient keep-alive covers idleness
	}

	net::ByteWriter w(net::PacketId::PlayerMove, 20);
	w.writeF32(eyePos.x).writeF32(eyePos.y).writeF32(eyePos.z).writeF32(yaw).writeF32(pitch);
	_net.send(w.finish());

	_lastMoveSent  = now;
	_lastSentPos   = eyePos;
	_lastSentYaw   = yaw;
	_lastSentPitch = pitch;
	_sentAnyMove   = true;
}

// ============================================================================
//  Inbound packets
// ============================================================================

void RemoteSession::handlePacket(const net::PacketId id, net::ByteReader& body) {
	using net::PacketId;
	switch (id) {
		case PacketId::LoginOk:     onLoginOk(body);     break;
		case PacketId::ChunkData:   onChunkData(body);   break;
		case PacketId::UnloadChunk: onUnloadChunk(body); break;
		case PacketId::BlockUpdate: onBlockUpdate(body); break;
		case PacketId::EntityMove:  onEntityMove(body);  break;
		case PacketId::PlayerJoin:  onPlayerJoin(body);  break;
		case PacketId::PlayerLeave: onPlayerLeave(body); break;
		case PacketId::TimeUpdate:  _timeOfDay = body.readU32() % TimeOfDay::TICKS_PER_DAY; break;
		default:
			return;                          // unknown / unused (e.g. ChunkDelta): ignore, stay compatible
	}
	if (_phase != Phase::Failed && (!body.ok() || !body.atEnd())) {
		char text[96];
		std::snprintf(text, sizeof(text), "malformed packet 0x%04X from server",
		              static_cast<unsigned>(id));
		fail(text);
	}
}

void RemoteSession::onLoginOk(net::ByteReader& body) {
	const std::uint32_t id = body.readU32();
	const glm::vec3 spawn{body.readF32(), body.readF32(), body.readF32()};
	if (!body.ok() || !finite(spawn))
		return;                              // handlePacket reports it
	_playerId = id;
	_spawn    = SpawnPoint{spawn, 0.0f, 0.0f};   // LoginOk carries no look yet
	_phase    = Phase::Playing;
	_remote.erase(id);                       // in case a join for ourselves slipped in
}

void RemoteSession::onChunkData(net::ByteReader& body) {
	const ChunkCoord coord{body.readI32(), body.readI32()};
	if (!body.ok())
		return;
	auto column = std::make_unique<ChunkColumn>(coord);
	if (!ChunkCodec::decode(body, *column)) {
		fail("invalid chunk data from server");
		return;
	}
	_world.adoptColumn(std::move(column));
}

void RemoteSession::onUnloadChunk(net::ByteReader& body) {
	const ChunkCoord coord{body.readI32(), body.readI32()};
	if (body.ok())
		_world.dropColumn(coord);
}

void RemoteSession::onBlockUpdate(net::ByteReader& body) {
	const glm::ivec3 pos{body.readI32(), body.readI32(), body.readI32()};
	const BlockId block = body.readU16();
	if (!body.ok())
		return;
	if (block >= BLOCK_TYPE_COUNT) {
		fail("invalid block id from server");
		return;
	}

	(void)_world.setBlockAt(pos, block);     // false when it already is `block`: fine

	// Settle the oldest pending edit of this cell.
	for (auto it = _pendingEdits.begin(); it != _pendingEdits.end(); ++it) {
		if (it->pos != pos)
			continue;
		if (it->kind == PendingEdit::Kind::Break && block == AIR_BLOCK)
			_events.push_back({SessionEvent::Type::BlockBroken, pos, it->block, {}});
		else if (it->kind == PendingEdit::Kind::Place && block == it->block)
			_events.push_back({SessionEvent::Type::BlockPlaced, pos, it->block, {}});
		// else: rejected; the BlockUpdate already restored the real block
		_pendingEdits.erase(it);
		break;
	}
}

void RemoteSession::onEntityMove(net::ByteReader& body) {
	const std::uint16_t count = body.readU16();
	for (std::uint16_t i = 0; i < count && body.ok(); ++i) {
		const std::uint32_t id = body.readU32();
		const glm::vec3 pos{body.readF32(), body.readF32(), body.readF32()};
		const float yaw   = body.readF32();
		const float pitch = body.readF32();
		if (!body.ok() || id == _playerId || !finite(pos) || !std::isfinite(yaw) || !std::isfinite(pitch))
			continue;

		RemoteState& state = _remote[id];    // creates an unnamed one if the join was missed
		state.view.id      = id;
		state.targetPos    = pos;
		state.targetYaw    = yaw;
		state.targetPitch  = pitch;
		if (!state.placed) {
			state.view.eyePos = pos;
			state.view.yaw    = yaw;
			state.view.pitch  = pitch;
			state.placed      = true;
		}
	}
}

void RemoteSession::onPlayerJoin(net::ByteReader& body) {
	const std::uint32_t id = body.readU32();
	std::string name = body.readString(proto::MAX_NAME_BYTES);
	if (!body.ok() || id == _playerId)
		return;
	RemoteState& state = _remote[id];
	state.view.id   = id;
	state.view.name = std::move(name);
}

void RemoteSession::onPlayerLeave(net::ByteReader& body) {
	const std::uint32_t id = body.readU32();
	if (body.ok())
		_remote.erase(id);
}

// ============================================================================
//  Helpers
// ============================================================================

void RemoteSession::updateRemotePlayers(const float dt) {
	const float alpha = 1.0f - std::exp(-kRemoteSmoothing * std::max(dt, 0.0f));

	_remoteView.clear();
	_remoteView.reserve(_remote.size());
	for (auto& [id, state] : _remote) {
		if (!state.placed)
			continue;                        // joined, but no position yet
		RemotePlayerView& v = state.view;
		const glm::vec3 delta = state.targetPos - v.eyePos;
		if (glm::dot(delta, delta) > 16.0f * 16.0f)
			v.eyePos = state.targetPos;      // teleport: do not glide across the map
		else
			v.eyePos += delta * alpha;
		v.yaw   += angleDelta(v.yaw, state.targetYaw) * alpha;
		v.pitch += (state.targetPitch - v.pitch) * alpha;
		_remoteView.push_back(v);
	}
}

void RemoteSession::expirePendingEdits() {
	const net::TimePoint now = net::Clock::now();
	std::erase_if(_pendingEdits, [now](const PendingEdit& e) { return now - e.sentAt > kEditTimeout; });
}

void RemoteSession::fail(std::string reason) {
	if (_phase == Phase::Failed)
		return;
	_phase = Phase::Failed;
	_pendingEdits.clear();
	_remote.clear();
	_remoteView.clear();
	_events.push_back({SessionEvent::Type::Disconnected, {}, {}, std::move(reason)});
	_net.close();
}
