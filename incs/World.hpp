#ifndef WORLD_HPP
#define WORLD_HPP

#include <cstdint>
#include <vector>
#include <glm/glm.hpp>
#include <unordered_map>
#include "defines.hpp"
#include "ThreadPool.hpp"
#include <functional>

namespace std {
	template <>
	struct hash<glm::ivec2>{
		std::size_t operator()(const glm::ivec2& coord) const {
			return hash<int>()(coord.x) ^ (hash<int>()(coord.y) << 1);
		}
	};
}

inline Voxel packVoxelData(uint8_t isActive, uint8_t r, uint8_t g, uint8_t b, uint8_t blockType) {
    Voxel data = 0;
    data |= (isActive & 0x1) << 31;        // Store isActive in the highest bit
    data |= (blockType & 0x7) << 24;       // Store blockType in the next 3 bits
    data |= (r & 0xFF) << 16;              // Store red in the next 8 bits
    data |= (g & 0xFF) << 8;               // Store green in the next 8 bits
    data |= (b & 0xFF);                    // Store blue in the lowest 8 bits
    return data;
}

inline void unpackVoxelData(Voxel data, uint8_t& isActive, uint8_t& r, uint8_t& g, uint8_t& b, uint8_t& blockType) {
    isActive = (data >> 31) & 0x1;
    blockType = (data >> 24) & 0x7;
    r = (data >> 16) & 0xFF;
    g = (data >> 8) & 0xFF;
    b = data & 0xFF;
}
typedef enum class Direction {
    Front, Back, Left, Right, Top, Bottom
} Direction;

typedef enum class BlockType {
	Grass, Dirt , Stone, Sand, Water, // Air,
} BlockType;

typedef struct Face {
	Vertex vertices[4]; // Four vertices for a face
	uint32_t indices[6]; // Two triangles per face
} Face;

// Define a chunk as a 1D vector of voxels
class Chunk {
	public:
		Chunk();
		~Chunk();
		Chunk(const Chunk&) = default;
		Chunk& operator=(const Chunk&) = default;

		Voxel		getVoxel(int x, int y, int z) const;
		void		setVoxel(int x, int y, int z, Voxel voxel);
		bool		isBlockActive(int x, int y, int z) const;

		// Define the dimensions of a chunk
		static constexpr uint8_t WIDTH = 16;
		static constexpr uint16_t HEIGHT = 32;
		static constexpr uint8_t DEPTH = 16;
		static constexpr uint32_t SIZE = WIDTH * HEIGHT * DEPTH;

        std::vector<Vertex> cachedVertices;
        std::vector<uint32_t> cachedIndices;
        bool isMeshDirty;

	private:
		std::vector<Voxel> voxels;

};

// Define the world as a collection of chunks
class World {
	public:
		World(std::unordered_map<BlockType, GLint> textures);
		~World() = default;
		World(const World&) = delete;
		World& operator=(const World&) = delete;

		constexpr static int CHUNK_RADIUS = 3;
		constexpr static int CHUNK_DIAMETER = CHUNK_RADIUS * 2 + 1;

		void updateChunks(const glm::vec3& playerPos, ThreadPool& threadPool);
		void generateWorldMesh(const glm::mat4& proj, const glm::mat4& view, std::vector<Vertex>& vertices, std::vector<uint32_t>& indices);
		void generateBlockFaces(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices, const Chunk& chunk, int x, int y, int z, const glm::ivec2& coord);
		bool isFaceVisible(const Chunk& chunk, int x, int y, int z, Direction direction);
		void createFace(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices, const glm::vec3& blockPos, const Direction direction, const GLuint textureID);
	private:
		glm::ivec2 playerChunk;
		std::unordered_map<BlockType, GLint> textures;
		std::unordered_map<glm::ivec2, Chunk> chunks;
        std::mutex chunk_mutex;

		void generateChunkMesh(Chunk& chunk, std::vector<Vertex>& vertices, std::vector<uint32_t>& indices, const glm::ivec2& coord);
		void generateTerrain(Chunk& chunk, const glm::ivec2& coord);
};

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

class Frustum {
public:
    glm::vec4 planes[6];

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
        for (int i = 0; i < 6; i++)
            glm::normalize(planes[i]);
    }

bool isBoxInFrustum(const glm::vec3& min, const glm::vec3& max) const {
    for (int i = 0; i < 6; i++) {
        glm::vec3 normal = glm::vec3(planes[i]);
        float distance = planes[i].w;

        // Test the positive and negative vertices (closest and farthest from the plane)
        glm::vec3 positive = glm::vec3(
            normal.x > 0 ? max.x : min.x,
            normal.y > 0 ? max.y : min.y,
            normal.z > 0 ? max.z : min.z
        );

        glm::vec3 negative = glm::vec3(
            normal.x > 0 ? min.x : max.x,
            normal.y > 0 ? min.y : max.y,
            normal.z > 0 ? min.z : max.z
        );

        // If the negative vertex is outside the plane, the whole box is outside
        if (glm::dot(normal, negative) + distance < 0) {
            return false;  // Box is fully outside
        }

        // If the positive vertex is inside, the box is fully inside
        if (glm::dot(normal, positive) + distance >= 0) {
            return true;  // Box is fully inside
        }
    }
    return true;  // Box is inside or intersects the frustum
}

};


#endif