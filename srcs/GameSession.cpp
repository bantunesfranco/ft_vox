#include "GameSession.hpp"

#include <cmath>

#include "BlockSystem.hpp"
#include "PlayerCollision.hpp"
#include "ThreadPool.hpp"
#include "World.hpp"

// All three references must outlive the session.
// `startPos` is the camera position when the game starts; its x and z decide
// where the player spawns (the height comes from the terrain).
LocalSession::LocalSession(World& world, ThreadPool& threadPool,
                           const BlockSystem& blocks, const glm::vec3& startPos)
	: _world(world),                       // reference members MUST be set in the init list
	  _threadPool(threadPool),
	  _blocks(blocks),
	  _spawnXZ(startPos.x, startPos.z) {}

void LocalSession::update(const glm::vec3& playerPos, float /*dt*/) {
	// Loads, generates and unloads chunks around the player. Without this call no
	// terrain ever appears, so pollSpawn() could never succeed.
	_world.updateChunks(playerPos, _threadPool);
}

std::optional<glm::vec3> LocalSession::pollSpawn() {
	if (_spawnDone)
		return std::nullopt;                                   // already handed out

	// floor, not a cast: a cast truncates toward zero, which picks the wrong block for x < 0
	const int x = static_cast<int>(std::floor(_spawnXZ.x));
	const int z = static_cast<int>(std::floor(_spawnXZ.y));

	int surface = 0;
	if (!_world.getSurfaceHeight(x, z, surface))
		return std::nullopt;                                   // the terrain there has not loaded yet

	_spawnDone = true;
	// Stand ON the surface block: its top is at surface + 1, and the camera is an eye height above that.
	return glm::vec3(_spawnXZ.x,
	                 static_cast<float>(surface + 1) + PlayerCollision::EYE_HEIGHT,
	                 _spawnXZ.y);
}

void LocalSession::requestBreakBlock(const glm::ivec3& pos) {
	BlockId removed = AIR_BLOCK;
	if (!_blocks.removeBlock(pos, _world, &removed))           // actually remove it
		return;                                                // nothing there: report nothing
	_events.push_back({
		.type  = SessionEvent::Type::BlockBroken,
		.pos   = pos,
		.block = removed,
		.reason = {},
	});
}

void LocalSession::requestPlaceBlock(const glm::vec3& origin, const glm::vec3& direction, BlockId block) {
	const BlockRaycastHit hit = _blocks.raycast(origin, direction, _world);
	if (!hit.valid)
		return;                                                // nothing in reach

	if (!_blocks.placeBlock(origin, direction, block, _world)) // actually place it
		return;                                                // refused: do not consume the item

	_events.push_back({
		.type  = SessionEvent::Type::BlockPlaced,
		.pos   = hit.adjacentPos,                              // where the new block went, not the block clicked
		.block = block,
		.reason = {},
	});
}

void LocalSession::onLocalPlayerMoved(const glm::vec3& , float , float ) {
	// Nothing to tell anyone in singleplayer.
}

std::vector<SessionEvent> LocalSession::drainEvents() {
	std::vector<SessionEvent> out;
	out.swap(_events);                                         // everything, oldest first, and clears the queue
	return out;
}