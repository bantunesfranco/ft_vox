#ifndef WORLD_HPP
#define WORLD_HPP

#include "defines.hpp"
#include "Voxel.hpp"
#include "ThreadPool.hpp"
#include "Camera.hpp"

#include <vector>
#include <unordered_map>
#include <functional>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

template <>
struct std::hash<glm::ivec2>{
	std::size_t operator()(const glm::ivec2& coord) const noexcept
	{
		return hash<int>()(coord.x) ^ (hash<int>()(coord.y) << 1);
	}
};

typedef enum class Direction {
    Front, Back, Left, Right, Top, Bottom
} Direction;

typedef enum class BlockType {
	Air, Grass, Dirt , Stone, Sand, Water,
} BlockType;

typedef struct Face {
	Vertex vertices[4]; // Four vertices for a face
	uint32_t indices[6]; // Two triangles per face
} Face;

inline int floorDiv(const int x, const int d) {
	int q = x / d;
	if (const int r = x % d; r && ((r < 0) != (d < 0)))
		--q;
	return q;
}

// Define a chunk as a 1D vector of voxels
class Chunk {
	public:
		struct ChunkRenderData {
			GLuint vao = 0;
			GLuint vbo = 0;
			GLuint ibo = 0;
			uint32_t indexCount = 0;
		} renderData;


		Chunk();
		~Chunk() = default;
		Chunk(const Chunk&) = default;
		Chunk& operator=(const Chunk&) = default;

		void		setVoxel(int x, int y, int z, Voxel voxel);
		[[nodiscard]] Voxel		getVoxel(int x, int y, int z) const;
		[[nodiscard]] auto 		getVoxels() const { return voxels; }
		[[nodiscard]] bool		isBlockActive(int x, int y, int z) const;

		// Define the dimensions of a chunk
		static constexpr uint8_t WIDTH = 16;
		static constexpr uint16_t HEIGHT = 128;
		static constexpr uint8_t DEPTH = 16;
		static constexpr uint32_t SIZE = WIDTH * HEIGHT * DEPTH;

        std::vector<Vertex> cachedVertices;
        std::vector<uint32_t> cachedIndices;
        bool isMeshDirty = true;
		glm::vec3 worldMax;
		glm::vec3 worldMin;
		glm::vec3 worldCenter;

	private:
		std::vector<Voxel> voxels;

};

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
		std::mutex chunk_mutex;
		WorldUBO worldUBO;
		GLuint ubo;

		World(const std::unordered_map<BlockType, uint32_t>& indices);
		~World() = default;
		World(const World&) = delete;
		World& operator=(const World&) = delete;

		void updateChunks(const glm::vec3& playerPos, ThreadPool& threadPool);
		void generateChunkMeshGreedy(Chunk& chunk, const glm::ivec2& coord) const;
		bool isBlockActiveWorld(int wx, int wy, int wz) const;
		std::unordered_map<glm::ivec2, Chunk>& getChunks() { return chunks; }

		static void generateTerrain(Chunk& chunk, const glm::ivec2& coord);

	private:
		glm::ivec2 playerChunk = {std::numeric_limits<int>::max(),std::numeric_limits<int>::max()};
		std::unordered_map<BlockType, GLuint> textureIndices;
		std::unordered_map<glm::ivec2, Chunk> chunks;

};

#endif