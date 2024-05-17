#ifndef CHUNK_HPP
#define CHUNK_HPP

#include <cstdint>
#include <unordered_map>
#include "vertex.hpp"
#include "Noise.hpp"
#include <vector>

#define CHUNK_WIDTH 16
#define CHUNK_DEPTH 16
#define CHUNK_HEIGHT 256
#define CHUNK_SIZE (CHUNK_WIDTH * CHUNK_DEPTH * CHUNK_HEIGHT)

struct ChunkCoords {
    int x, y, z;

    bool operator==(const ChunkCoords& other) const {
        return x == other.x && y == other.y && z == other.z;
    }
};

template <>
struct std::hash<ChunkCoords> {
    std::size_t operator()(const ChunkCoords& coords) const {
        std::size_t h1 = std::hash<int>{}(coords.x);
        std::size_t h2 = std::hash<int>{}(coords.y);
        std::size_t h3 = std::hash<int>{}(coords.z);
        return h1 ^ (h2 << 1) ^ (h3 << 2); 
    }
};

class Chunk {
	public:
		uint32_t voxels[CHUNK_SIZE];
	
		Chunk() = default;
		~Chunk() = default;
		Chunk(const Chunk& other) = delete;
		Chunk& operator=(const Chunk& other);

		uint32_t	&at(int x, int y, int z);
		void		initialize(const PerlinNoise& noise);
};

class ChunkManager {
	private:
		ChunkManager(const ChunkManager& other) = delete;
		ChunkManager&	operator=(const ChunkManager& other) = delete;

	public:
		std::unordered_map<ChunkCoords, Chunk, std::hash<ChunkCoords>> chunks;
		int	renderDistance = 2;
		PerlinNoise	noise;

		ChunkManager();
		ChunkManager(uint32_t seed);
		~ChunkManager() = default;

		Chunk&	loadChunk(const ChunkCoords& coords);
		void	unloadChunk(const ChunkCoords& coords);
		void	updateChunks(const float* playerPos);
};

std::vector<Vertex> generateMesh(Chunk& chunk);

#endif
