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


// Define a chunk as a 1D vector of voxels
class Chunk {
	public:
		Chunk();
		~Chunk();
		Chunk(const Chunk&) = default;
		Chunk& operator=(const Chunk&) = default;

		void		setVoxel(int x, int y, int z, Voxel voxel);
		[[nodiscard]] Voxel		getVoxel(int x, int y, int z) const;
		[[nodiscard]] bool		isBlockActive(int x, int y, int z) const;

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

constexpr int MAX_FACES = Chunk::WIDTH * Chunk::HEIGHT * Chunk::DEPTH * 6;

// Define the world as a collection of chunks
class World {
	public:
		World(const std::unordered_map<BlockType, GLuint>& textures);
		~World() = default;
		World(const World&) = delete;
		World& operator=(const World&) = delete;

		constexpr static int CHUNK_RADIUS = 3;
		constexpr static int CHUNK_DIAMETER = CHUNK_RADIUS * 2 + 1;

		void updateChunks(const glm::vec3& playerPos, ThreadPool& threadPool);
		void generateWorldMesh(const std::unique_ptr<Renderer>& renderer, const std::unique_ptr<Camera>& camera, std::vector<Vertex>& vertices, std::vector<uint32_t>& indices);
		bool isFaceVisible(const Chunk& chunk, const glm::ivec2& coord, int x, int y, int z, Direction direction) const;
		bool isBlockActiveWorld(int wx, int wy, int wz) const;
		void markChunkAndNeighborsDirty(const glm::ivec2& coord);

		static void createFace(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices, const glm::vec3& blockPos, Direction direction, GLuint textureID);

	private:
		glm::ivec2 playerChunk;
		std::unordered_map<BlockType, GLuint> textures;
		std::unordered_map<glm::ivec2, Chunk> chunks;
        std::mutex chunk_mutex;

		void generateChunkMesh(const Chunk& chunk, std::vector<Vertex>& vertices, std::vector<uint32_t>& indices, const glm::ivec2& coord);
		static void generateTerrain(Chunk& chunk, const glm::ivec2& coord);
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
        for (auto& plane : planes)
            plane = glm::normalize(plane);
    }

    [[nodiscard]] bool isBoxInFrustum(const glm::vec3& min, const glm::vec3& max) const {
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
        for (auto& plane : planes) {
            const auto& normal = glm::vec3(plane);
            const float distance = plane.w;

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