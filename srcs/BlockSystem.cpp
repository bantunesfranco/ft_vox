#include "BlockSystem.hpp"

#include "PlayerCollision.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

BlockSystem::BlockSystem(const float reachDistance) noexcept : reach(reachDistance) {}

BlockRaycastHit BlockSystem::raycast(
	const glm::vec3& origin,
	const glm::vec3& direction,
	const World& world
) const {
	BlockRaycastHit hit{};
	if (glm::dot(direction, direction) < 1e-8f || reach <= 0.0f)
		return hit;

	const glm::vec3 ray = glm::normalize(direction);
	glm::ivec3 cell{
		static_cast<int>(std::floor(origin.x)),
		static_cast<int>(std::floor(origin.y)),
		static_cast<int>(std::floor(origin.z))
	};
	glm::ivec3 previous = cell;

	const glm::ivec3 step{
		ray.x > 0.0f ? 1 : (ray.x < 0.0f ? -1 : 0),
		ray.y > 0.0f ? 1 : (ray.y < 0.0f ? -1 : 0),
		ray.z > 0.0f ? 1 : (ray.z < 0.0f ? -1 : 0)
	};
	const auto deltaFor = [](const float component) {
		return component == 0.0f
			? std::numeric_limits<float>::infinity()
			: std::abs(1.0f / component);
	};
	const glm::vec3 delta{deltaFor(ray.x), deltaFor(ray.y), deltaFor(ray.z)};
	const auto firstBoundary = [](const float position, const int block, const float component) {
		if (component == 0.0f)
			return std::numeric_limits<float>::infinity();
		const float boundary = static_cast<float>(block + (component > 0.0f ? 1 : 0));
		return (boundary - position) / component;
	};
	glm::vec3 next{
		firstBoundary(origin.x, cell.x, ray.x),
		firstBoundary(origin.y, cell.y, ray.y),
		firstBoundary(origin.z, cell.z, ray.z)
	};

	float distance = 0.0f;
	while (distance <= reach) {
		BlockId block = AIR_BLOCK;
		if (!world.getBlockAt(cell, block))
			return hit;
		if (block != AIR_BLOCK && block != WATER_BLOCK) {
			hit.blockPos = cell;
			hit.adjacentPos = previous;
			hit.distance = distance;
			hit.valid = true;
			return hit;
		}

		previous = cell;
		if (next.x <= next.y && next.x <= next.z) {
			distance = next.x;
			next.x += delta.x;
			cell.x += step.x;
		} else if (next.y <= next.z) {
			distance = next.y;
			next.y += delta.y;
			cell.y += step.y;
		} else {
			distance = next.z;
			next.z += delta.z;
			cell.z += step.z;
		}
	}

	return hit;
}

bool BlockSystem::removeBlock(
	const glm::vec3& origin,
	const glm::vec3& direction,
	World& world
) const {
	const BlockRaycastHit hit = raycast(origin, direction, world);
	return hit.valid && world.setBlockAt(hit.blockPos, AIR_BLOCK);
}

bool BlockSystem::placeBlock(
	const glm::vec3& origin,
	const glm::vec3& direction,
	const BlockId block,
	World& world
) const {
	if (block == AIR_BLOCK || block >= BLOCK_TYPE_COUNT)
		return false;

	const BlockRaycastHit hit = raycast(origin, direction, world);
	if (!hit.valid)
		return false;
	const glm::ivec3 cameraCell{
		static_cast<int>(std::floor(origin.x)),
		static_cast<int>(std::floor(origin.y)),
		static_cast<int>(std::floor(origin.z))
	};
	if (hit.adjacentPos == cameraCell)
		return false;

	BlockId existing = AIR_BLOCK;
	if (!world.getBlockAt(hit.adjacentPos, existing) || (existing != AIR_BLOCK && existing != WATER_BLOCK))
		return false;
	if (PlayerCollision::overlapsBlock(origin, hit.adjacentPos))
		return false;

	return world.setBlockAt(hit.adjacentPos, block);
}
