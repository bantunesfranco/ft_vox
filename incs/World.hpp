#ifndef WORLD_HPP
#define WORLD_HPP

#include <cstdint>
#include <vector>
#include <glm/glm.hpp>
#include <unordered_map>
#include "defines.hpp"
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

typedef struct Face {
	Vertex vertices[4]; // Four vertices for a face
	uint32_t indices[6]; // Two triangles per face
} Face;

// Define a chunk as a 1D vector of voxels
class Chunk {
	public:
		Chunk();
		~Chunk() = default;
		Chunk(const Chunk&) = default;
		Chunk& operator=(const Chunk&) = default;

		Voxel		getVoxel(int x, int y, int z) const;
		void		setVoxel(int x, int y, int z, Voxel voxel);
		bool		isBlockActive(int x, int y, int z) const;

		// Define the dimensions of a chunk
		static constexpr uint8_t WIDTH = 5;
		// static constexpr uint16_t HEIGHT = 256;
		static constexpr uint16_t HEIGHT = 5;
		static constexpr uint8_t DEPTH = 5;
		static constexpr uint32_t SIZE = WIDTH * HEIGHT * DEPTH;

	private:
		std::vector<Voxel> voxels; 

};

// Define the world as a collection of chunks
class World {
	public:
		World();
		~World() = default;
		World(const World&) = delete;
		World& operator=(const World&) = delete;

		constexpr static int CHUNK_RADIUS = 0;
		constexpr static int CHUNK_DIAMETER = CHUNK_RADIUS * 2 + 1;

		void updateChunks(const glm::vec3& playerPos);
		void generateWorldMesh(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices);
		void generateBlockFaces(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices, const Chunk& chunk, int x, int y, int z);
		bool isFaceVisible(const Chunk& chunk, int x, int y, int z, Direction direction);
		void createFace(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices, const glm::vec3& blockPos, const glm::vec3& normal, const glm::vec3& color);

	private:
		std::unordered_map<glm::ivec2, Chunk> chunks;
		glm::ivec2 playerChunk;

		void generateChunkMesh(Chunk& chunk, std::vector<Vertex>& vertices, std::vector<uint32_t>& indices);
		void generateTerrain(Chunk& chunk);
};

#endif