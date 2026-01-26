#ifndef WORLD_HPP
#define WORLD_HPP

#include "defines.hpp"
#include "ThreadPool.hpp"
#include "Camera.hpp"
#include "Terrain.hpp"

#include <vector>
#include <unordered_map>
#include <functional>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

using ChunkCoord = glm::ivec2;

inline bool operator<(const ChunkCoord& a, const ChunkCoord& b) noexcept {
	if (a.x != b.x) return a.x < b.x;
	return a.y < b.y;
}

template <>
struct std::hash<ChunkCoord>{
	std::size_t operator()(const ChunkCoord& coord) const noexcept
	{
		return hash<int>()(coord.x) ^ (hash<int>()(coord.y) << 1);
	}
};

typedef enum class Direction {
    Front, Back, Left, Right, Top, Bottom
} Direction;

inline int floorDiv(const int x, const int d) {
	int q = x / d;
	if (const int r = x % d; r && ((r < 0) != (d < 0)))
		--q;
	return q;
}

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

constexpr int MAX_FACES = Chunk::WIDTH * Chunk::HEIGHT * Chunk::DEPTH * 6;

enum class ChunkState : uint8_t {
	Unloaded,
	Loading,
	Loaded,
	Meshing,
	Unloading
};

struct WorldUBO {
	glm::mat4 MVP;
	glm::vec4 light;       // xyz = pos, w = radius
	glm::vec4 ambientData; // x = ambient, yzw = padding
};

// Define the world as a collection of chunks
class World {
	public:
		constexpr static int CHUNK_RADIUS = 16;
		constexpr static int CHUNK_DIAMETER = CHUNK_RADIUS * 2 + 1;

		Frustum frustum{};
		WorldUBO worldUBO{};
		GLuint ubo;

		std::mutex chunk_mutex;
		std::mutex state_mutex;

		World(std::array<uint32_t, 256>& indices);
		~World() = default;
		World(const World&) = delete;
		World& operator=(const World&) = delete;

		void updateChunks(const glm::vec3& playerPos, ThreadPool& threadPool);
		// void generateChunkMesh(Chunk& chunk, const ChunkCoord& coord) const;
		void generateChunkGreedyMesh(Chunk& chunk, const ChunkCoord& coord);
		bool isBlockActiveWorld(int wx, int wy, int wz) const;
		bool isBoxInFrustum(const glm::vec3& min, const glm::vec3& max) const;
		void updateFrustum(const glm::mat4& proj_mat, const glm::mat4& view_mat);
		std::unordered_map<ChunkCoord, Chunk>& getChunks() { return chunks; }
		const std::unordered_map<ChunkCoord, Chunk>& getChunks() const { return chunks; }

		static void generateTerrain(Chunk& chunk, const ChunkCoord& coord);

	private:
		ChunkCoord playerChunk = {std::numeric_limits<int>::max(),std::numeric_limits<int>::max()};
		std::array<uint32_t, 256>& textureIndices;
		std::unordered_map<ChunkCoord, Chunk> chunks;
		std::unordered_map<ChunkCoord, std::atomic<ChunkState>> chunkStates;

};

template<typename F>
void forEachChunkSpiral(const ChunkCoord& center, const int radius, F&& fn)
{
	for (int r = 0; r <= radius; ++r) {
		for (int dx = -r; dx <= r; ++dx) {
			for (int dz = -r; dz <= r; ++dz) {
				if (std::abs(dx) != r && std::abs(dz) != r)
					continue;
				if (glm::length(glm::vec2(dx, dz)) > radius + 1)
					continue;
				fn(center + ChunkCoord(dx, dz));
			}
		}
	}
}


#endif