#include "GameSession.hpp"

#include <algorithm>
#include <cmath>
#include <exception>
#include <iostream>
#include <utility>

#include "BlockSystem.hpp"
#include "PlayerCollision.hpp"
#include "PlayerPhysics.hpp"
#include "PlayerStore.hpp"
#include "ThreadPool.hpp"
#include "World.hpp"

// All three references must outlive the session.
// `startPos`: its x and z decide where a new player spawns (the height comes
// from the terrain). A record in `playerFile` replaces that spawn and the
// starting items.
LocalSession::LocalSession(World& world, ThreadPool& threadPool, const BlockSystem& blocks,
                           const glm::vec3& startPos, std::optional<std::filesystem::path> playerFile)
	: _world(world),                       // reference members MUST be set in the init list
	  _threadPool(threadPool),
	  _blocks(blocks),
	  _spawnXZ(startPos.x, startPos.z),
	  _playerFile(std::move(playerFile)) {
	if (!_playerFile)
		return;
	if (std::optional<PlayerRecord> record = loadPlayer(*_playerFile)) {
		_inventory  = record->inventory;
		_savedSpawn = SpawnPoint{record->eye, record->yaw, record->pitch};
	}
}

LocalSession::~LocalSession() {
	try {
		savePlayerFile();
	} catch (const std::exception& e) {
		std::cerr << "Could not save the player: " << e.what() << '\n';
	} catch (...) {
		std::cerr << "Could not save the player\n";
	}
}

glm::vec3 LocalSession::spawnTarget() const {
	if (_savedSpawn)
		return _savedSpawn->eye;
	return {_spawnXZ.x, 0.0f, _spawnXZ.y};
}

void LocalSession::update(const glm::vec3& playerPos, const float dt) {
	// Loads, generates and unloads chunks around the player. Without this call no
	// terrain ever appears, so pollSpawn() could never succeed. Until the spawn,
	// the camera is not there yet: load around where it will be.
	_world.updateChunks(_spawnDone ? playerPos : spawnTarget(), _threadPool);

	if (!_spawnDone || !_playerFile)
		return;
	_sinceSave += std::max(dt, 0.0f);
	if (_sinceSave >= SAVE_INTERVAL) {
		_sinceSave = 0.0f;
		savePlayerFile();
	}
}

std::optional<SpawnPoint> LocalSession::pollSpawn() {
	if (_spawnDone)
		return std::nullopt;                                   // already handed out

	if (_savedSpawn) {
		const glm::vec3 eye = _savedSpawn->eye;
		// Every column the player's box touches, so it is not wedged at a chunk edge.
		const auto [lo, hi] = PlayerCollision::columnsUnderBox(eye);
		for (int cz = lo.z; cz <= hi.z; ++cz) {
			for (int cx = lo.x; cx <= hi.x; ++cx) {
				BlockId block = AIR_BLOCK;
				const glm::ivec3 corner{cx * ChunkSection::SIZE, ChunkColumn::MIN_WORLD_Y, cz * ChunkSection::SIZE};
				if (!_world.getBlockAt(corner, block))
					return std::nullopt;                       // that column has not loaded yet
			}
		}

		// Blocks may have been built where the player stood since: rise out of them.
		// Only real blocks count (an unloaded neighbour is not in the way).
		const BlockRegistry& registry = _world.getBlockRegistry();
		const auto solid = [this, &registry](const glm::ivec3& p) {
			if (p.y < ChunkColumn::MIN_WORLD_Y)
				return true;
			BlockId at = AIR_BLOCK;
			return _world.getBlockAt(p, at) && registry.get(at).solid;
		};
		_player = {PlayerPhysics::liftOutOfBlocks(eye, solid), _savedSpawn->yaw, _savedSpawn->pitch};
		_spawnDone = true;
		return _player;
	}

	const int x = static_cast<int>(std::floor(_spawnXZ.x));
	const int z = static_cast<int>(std::floor(_spawnXZ.y));

	int surface = 0;
	if (!_world.getSurfaceHeight(x, z, surface))
		return std::nullopt;                                   // the terrain there has not loaded yet

	_spawnDone = true;
	// Stand ON the surface block: its top is at surface + 1, and the camera is an eye height above that.
	_player = {glm::vec3(_spawnXZ.x,
	                     static_cast<float>(surface + 1) + PlayerCollision::EYE_HEIGHT,
	                     _spawnXZ.y),
	           0.0f, 0.0f};
	return _player;
}

void LocalSession::requestBreakBlock(const glm::ivec3& pos) {
	BlockId current = AIR_BLOCK;
	if (!_world.getBlockAt(pos, current) || !_inventory.canTakeDropOf(current))
		return;                                                // its item would not fit

	BlockId removed = AIR_BLOCK;
	if (!_blocks.removeBlock(pos, _world, &removed))           // actually remove it
		return;                                                // nothing there: report nothing
	(void)_inventory.add(ItemRegistry::fromBlock(removed));    // fits: canTakeDropOf said so (no item: nothing)
	_events.push_back({
		.type  = SessionEvent::Type::BlockBroken,
		.pos   = pos,
		.block = removed,
		.reason = {},
	});
}

void LocalSession::requestPlaceBlock(const glm::vec3& origin, const glm::vec3& direction) {
	const ItemStack& selected = _inventory.selectedStack();
	if (selected.empty())
		return;
	const BlockId block = ItemRegistry::get(selected.item).placedBlock;
	if (block == AIR_BLOCK)
		return;                                                // an item that is not a block

	const BlockRaycastHit hit = _blocks.raycast(origin, direction, _world);
	if (!hit.valid)
		return;                                                // nothing in reach

	if (!_blocks.placeBlock(origin, direction, block, _world)) // actually place it
		return;                                                // refused: do not consume the item

	(void)_inventory.consumeSelected();
	_events.push_back({
		.type  = SessionEvent::Type::BlockPlaced,
		.pos   = hit.adjacentPos,                              // where the new block went, not the block clicked
		.block = block,
		.reason = {},
	});
}

void LocalSession::inventoryAction(const InventoryAction& action) {
	(void)applyAction(_inventory, action);
}

void LocalSession::onLocalPlayerMoved(const glm::vec3& eyePos, const float yaw, const float pitch) {
	if (_spawnDone)                                            // before it, the camera is not the player yet
		_player = {eyePos, yaw, pitch};
}

std::vector<SessionEvent> LocalSession::drainEvents() {
	std::vector<SessionEvent> out;
	out.swap(_events);                                         // everything, oldest first, and clears the queue
	return out;
}

void LocalSession::savePlayerFile() const {
	if (!_playerFile || !_spawnDone)
		return;                                                // nothing to save, or no real position yet
	std::string error;
	if (!savePlayer(*_playerFile, PlayerRecord{_player.eye, _player.yaw, _player.pitch, _inventory}, error))
		std::cerr << "Could not save the player to " << _playerFile->string() << ": " << error << '\n';
}
