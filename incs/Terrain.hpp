#ifndef TERRAIN_HPP
#define TERRAIN_HPP

#include "Chunk.hpp"
#include "glm/glm.hpp"

#include <vector>
#include <unordered_map>

typedef enum class BlockType {
	Air,
	Grass,
	Dirt ,
	Stone,
	Sand,
	Water,
	Snow,
	IronOre,
	Amethyst,
} BlockType;

struct NoiseCache {
	std::vector<float> terrain;
	std::vector<float> temperature;
	std::vector<float> humidity;
	std::vector<float> mountain;
};

inline uint64_t hashCoord(const glm::ivec2 coord) {
	return (static_cast<uint64_t>(coord.x) << 32) | static_cast<uint32_t>(coord.y);
}

class TerrainGenerator {
	public:
		TerrainGenerator(int seed = 1337);
		~TerrainGenerator() = default;
		TerrainGenerator(const TerrainGenerator&) = delete;
		TerrainGenerator& operator=(const TerrainGenerator&) = delete;

		void generateChunk(Chunk& chunk, const glm::ivec2& coord);

	private:
		int seed;
		std::unordered_map<uint64_t, NoiseCache> noiseCache; // key = chunk coord hash
		std::mutex noiseCacheMutex;
};

#endif