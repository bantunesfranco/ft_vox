#ifndef BLOCKSYSTEM_HPP
#define BLOCKSYSTEM_HPP

#include <glm/glm.hpp>

#include "Block.hpp"
#include "World.hpp"

struct BlockRaycastHit {
	glm::ivec3 blockPos{};
	glm::ivec3 adjacentPos{};
	float distance = 0.0f;
	bool valid = false;
};

class BlockSystem {
public:
	explicit BlockSystem(float reach = 6.0f) noexcept;

	[[nodiscard]] BlockRaycastHit raycast(
		const glm::vec3& origin,
		const glm::vec3& direction,
		const World& world
	) const;

	bool removeBlock(
		const glm::vec3& origin,
		const glm::vec3& direction,
		World& world,
		BlockId* removedBlock = nullptr
	) const;
	bool removeBlock(const glm::ivec3& blockPosition, World& world, BlockId* removedBlock = nullptr) const;

	bool placeBlock(
		const glm::vec3& origin,
		const glm::vec3& direction,
		BlockId block,
		World& world
	) const;

private:
	float reach;
};

#endif
