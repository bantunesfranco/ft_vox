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
		glm::mat4 clip = proj * view;

		// Extract planes from the combined view-projection matrix
		planes[0] = glm::vec4(clip[0][3] - clip[0][0], clip[1][3] - clip[1][0], clip[2][3] - clip[2][0], clip[3][3] - clip[3][0]); // Right
		planes[1] = glm::vec4(clip[0][3] + clip[0][0], clip[1][3] + clip[1][0], clip[2][3] + clip[2][0], clip[3][3] + clip[3][0]); // Left
		planes[2] = glm::vec4(clip[0][3] + clip[0][1], clip[1][3] + clip[1][1], clip[2][3] + clip[2][1], clip[3][3] + clip[3][1]); // Bottom
		planes[3] = glm::vec4(clip[0][3] - clip[0][1], clip[1][3] - clip[1][1], clip[2][3] - clip[2][1], clip[3][3] - clip[3][1]); // Top
		planes[4] = glm::vec4(clip[0][3] - clip[0][2], clip[1][3] - clip[1][2], clip[2][3] - clip[2][2], clip[3][3] - clip[3][2]); // Far
		planes[5] = glm::vec4(clip[0][3] + clip[0][2], clip[1][3] + clip[1][2], clip[2][3] + clip[2][2], clip[3][3] + clip[3][2]); // Near

		// Normalize the planes
		for (auto& plane : planes) {
			plane /= glm::length(glm::vec3(plane));
		}
	}

	[[nodiscard]] bool isBoxInFrustum(const glm::vec3& min, const glm::vec3& max) const {
		for (const auto& plane : planes) {
			auto normal = glm::vec3(plane);
			const float distance = plane.w;

			// Compute the positive vertex (vertex most likely outside the plane)
			glm::vec3 positiveVertex = min;
			if (normal.x >= 0) positiveVertex.x = max.x;
			if (normal.y >= 0) positiveVertex.y = max.y;
			if (normal.z >= 0) positiveVertex.z = max.z;

			// If the positive vertex is outside, the box is fully outside
			if (glm::dot(normal, positiveVertex) + distance < 0)
				return false;
		}
		return true; // Inside or intersecting
	}
};


class World {
public:
	static constexpr int CHUNK_RADIUS = 12; // 8 ~140 fps , 16 ~30 fps
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

	void buildRenderStates();
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
	std::unordered_map<ChunkCoord, std::uint16_t> meshedSections;
	std::unordered_set<ChunkCoord> generatingChunks;
	std::vector<ChunkRenderState> renderStates;
	std::vector<GpuMesh> pendingRetiredMeshes;

	mutable std::mutex chunk_mutex;

	BlockRegistry blockRegistry{};
};

#endif
