#ifndef WORLD_HPP
#define WORLD_HPP

#include <cstdint>
#include <vector>
#include <glm/glm.hpp>
#include <unordered_map>
#include "defines.hpp"

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
		static constexpr uint8_t WIDTH = 16;
		static constexpr uint16_t HEIGHT = 256;
		static constexpr uint8_t DEPTH = 16;
		static constexpr uint8_t SIZE = WIDTH * HEIGHT * DEPTH;

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

		constexpr static int CHUNK_RADIUS = 3;
		constexpr static int CHUNK_DIAMETER = CHUNK_RADIUS * 2 + 1;

		void updateChunks(const glm::vec3& playerPos);
		void generateWorldMesh(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices);

	private:
		std::unordered_map<glm::ivec2, Chunk> chunks;
		glm::ivec2 playerChunk;

		void generateChunkMesh(Chunk& chunk, std::vector<Vertex>& vertices, std::vector<uint32_t>& indices);
		void generateTerrain(Chunk& chunk);
};

enum class Direction {
    Front, Back, Left, Right, Top, Bottom
};


#endif