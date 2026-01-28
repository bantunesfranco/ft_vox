#ifndef TERRAIN_HPP
#define TERRAIN_HPP

#include "Chunk.hpp"
#include "glm/glm.hpp"

#include <vector>
#include <unordered_map>

#include "World.hpp"

using ChunkCoord = glm::ivec2;

typedef enum class BlockType {
	Air,
	Grass,
	Dirt ,
	Stone,
	Sand,
	Water,
	IronOre,
	Snow,
	Amethyst,
} BlockType;

struct NoiseCache {
	std::vector<float> terrain;
	std::vector<float> temperature;
	std::vector<float> humidity;
	std::vector<float> mountain;
};

inline uint64_t hashCoord(const ChunkCoord coord) {
	return (static_cast<uint64_t>(coord.x) << 32) | static_cast<uint32_t>(coord.y);
}

class TerrainGenerator {
public:
	TerrainGenerator(int seed = 1337);
	~TerrainGenerator() = default;
	TerrainGenerator(const TerrainGenerator&) = delete;
	TerrainGenerator& operator=(const TerrainGenerator&) = delete;

	void generateChunk(Chunk& chunk, const ChunkCoord& coord);
	static Voxel sampleVoxel(int wx, int y, int wz);

private:
	// Noise sampling methods
	static float sampleTerrainNoise(float x, float z);
	static float sampleContinentalNoise(float x, float z);
	static float sampleErosionNoise(float x, float z);
	static float sampleCaveNoise(float x, float y, float z);
	static float sampleCaveEntranceNoise(float x, float z);
	static float sampleTemperatureNoise(float x, float z);
	static float sampleHumidityNoise(float x, float z);

	// Cave generation
	static float calculateCaveEntranceWeight(float continental, float temp, float humid);

	// Height calculation
	static int calculateHeight(float terrainNoise, float continentalNoise, float erosionNoise);

	// Biome determination
	static BlockType determineBiome(float temperature, float humidity, float continentalNoise);

	std::unordered_map<uint64_t, NoiseCache> noiseCache;
	std::mutex noiseCacheMutex;

	static inline int seed = 1337;

	// Terrain parameters
	static constexpr int MIN_Y = 1;
	static constexpr int MAX_Y = Chunk::HEIGHT - 1;
	static constexpr int BASE_HEIGHT = Chunk::HEIGHT / 8;
	static constexpr int SEA_LEVEL = Chunk::HEIGHT / 4;
	static constexpr int SNOW_HEIGHT = Chunk::HEIGHT / 2;

	// Noise thresholds
	static constexpr float CAVE_THRESHOLD = 0.4f;
	static constexpr float MOUNTAIN_THRESHOLD = 0.65f;
	static constexpr float TEMPERATURE_SNOW = 0.2f;
	static constexpr float TEMPERATURE_DESERT = 0.65f;
	static constexpr float HUMIDITY_DRY = 0.5f;
	static constexpr float HUMIDITY_WET = 0.6f;
	static constexpr float CAVE_ENTRACE_WEIGHT = 0.2f;
};

#endif