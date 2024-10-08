#ifndef WORLD_HPP
#define WORLD_HPP

#include <cstdint>
#include <vector>
#include <glm/glm.hpp>
#include <unordered_map>
#include "defines.hpp"
#include "ThreadPool.hpp"
#include "Camera.hpp"
#include <functional>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

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
        int visibility;
        bool isVisible;
        bool isMeshDirty;
        GLuint queryID;

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
		void generateWorldMesh(Renderer *renderer, Camera *camera, std::vector<Vertex>& vertices, std::vector<uint32_t>& indices);
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
            planes[i] = glm::normalize(planes[i]);
    }

    bool isBoxInFrustum(const glm::vec3& min, const glm::vec3& max) const {
        // Define the 8 corners of the box
        glm::vec3 corners[8] = {
            glm::vec3(min.x, min.y, min.z),
            glm::vec3(max.x, min.y, min.z),
            glm::vec3(min.x, max.y, min.z),
            glm::vec3(max.x, max.y, min.z),
            glm::vec3(min.x, min.y, max.z),
            glm::vec3(max.x, min.y, max.z),
            glm::vec3(min.x, max.y, max.z),
            glm::vec3(max.x, max.y, max.z)
        };

        // Check each frustum plane
        for (int i = 0; i < 6; i++) {
            const glm::vec3& normal = glm::vec3(planes[i]);
            float distance = planes[i].w;

            bool allPointsOutside = true;

            // Check each corner against the plane
            for (const auto& corner : corners) {
                if (glm::dot(normal, corner) + distance >= 0) {
                    allPointsOutside = false;
                    break; // If any corner is inside, the box is not outside
                }
            }

            // If all points are outside the plane, the box is outside the frustum
            if (allPointsOutside) {
                return false;
            }
        }

        // If none of the planes determined the box was outside, it must be inside or intersecting
        return true;
    }


};


#endif