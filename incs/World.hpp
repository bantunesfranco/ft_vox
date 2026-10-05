#ifndef WORLD_HPP
#define WORLD_HPP

#include <unordered_set>

#include "Chunk.hpp"
#include "ChunkMesher.hpp"
#include "ThreadPool.hpp"

class Frustum {
public:
	std::array<glm::vec4, 6> planes;

	void updateFrustum(const glm::mat4& proj, const glm::mat4& view) {
		// Compute clip matrix (Transpose it to safely extract rows using GLM indexing)
		glm::mat4 clip = glm::transpose(proj * view);

		// Now clip[0] is Row 1, clip[1] is Row 2, etc.
		planes[0] = clip[3] - clip[0]; // Right
		planes[1] = clip[3] + clip[0]; // Left
		planes[2] = clip[3] + clip[1]; // Bottom
		planes[3] = clip[3] - clip[1]; // Top
		planes[4] = clip[3] - clip[2]; // Far
		planes[5] = clip[3] + clip[2]; // Near

		// Normalize the planes
		for (auto& plane : planes) {
			plane /= glm::length(glm::vec3(plane));
		}
	}

	[[nodiscard]] bool isBoxInFrustum(const glm::vec3& chunkMin, const glm::vec3& chunkMax) const {
		const glm::vec3 center((chunkMin + chunkMax) * 0.5f);
		const glm::vec3 extents(chunkMax - center); // Constant vector for all uniform chunks!

		std::ranges::all_of(planes, [&](const glm::vec4& plane) {
			const glm::vec3 normal(plane);

			// Project center onto the plane normal
			const float projectionCenter = glm::dot(normal, center) + plane.w;

			// Project extents box radius along the plane normal
			const float projectionRadius = glm::dot(glm::abs(normal), extents);

			// If the chunk center is further away than its radius, it is completely behind the plane
			return projectionCenter >= -projectionRadius;
		});

		return true;
	}
};


class World {
public:
	static constexpr int CHUNK_RADIUS = 16; // 8 ~140 fps , 16 ~30 fps
	static constexpr int CHUNK_DIAMETER = CHUNK_RADIUS * 2 + 1;
	static constexpr int CHUNK_LOAD_RADIUS = CHUNK_RADIUS + 1;
	static constexpr int CHUNK_LOAD_DIAMETER = CHUNK_LOAD_RADIUS * 2 + 1;

	World() = default;

	void updateChunks(
		const glm::vec3& playerPos,
		ThreadPool& threadPool
	);

	void generateTerrain(
		ChunkColumn &column
	);

	void buildRenderStates(const glm::vec3& playerPosition, const glm::vec3& playerForward);
	std::vector<GpuMesh> pruneRenderStates();
	[[nodiscard]] bool getBlockAt(const glm::ivec3& worldPos, BlockId& block) const;
	bool setBlockAt(const glm::ivec3& worldPos, BlockId block);
	[[nodiscard]] bool getSurfaceHeight(int worldX, int worldZ, int& height) const;

	const BlockRegistry& getBlockRegistry() const noexcept;

	[[nodiscard]]
	const std::vector<ChunkRenderState>& getRenderStates() const noexcept;

	[[nodiscard]]
	std::vector<ChunkRenderState>& getRenderStates() noexcept;

	const std::unordered_map<ChunkCoord, std::unique_ptr<ChunkColumn>>&
	getChunks() const noexcept;

	void saveChunk(const ChunkCoord& coord, const glm::ivec3& localPos, BlockId block) const;
	std::unordered_map<glm::ivec3, BlockId> loadChunk(const ChunkCoord& coord) const;

	Frustum frustum{};

private:
	glm::ivec2 playerChunk{
		std::numeric_limits<int>::max(),
		std::numeric_limits<int>::max()
	};

	std::unordered_map<ChunkCoord, std::unique_ptr<ChunkColumn>> chunks;
	std::unordered_map<ChunkCoord, std::uint32_t> meshedSections;
	std::unordered_map<ChunkCoord, std::uint32_t> meshWaitFrames;
	std::unordered_set<ChunkCoord> generatingChunks;
	std::vector<ChunkRenderState> renderStates;
	std::vector<GpuMesh> pendingRetiredMeshes;

	mutable std::mutex chunk_mutex;

	BlockRegistry blockRegistry{};
};

#endif
