#include "Terrain.hpp"
#include "stb_perlin.h"
#include <glm/glm.hpp>
#include <algorithm>
#include <mutex>

TerrainGenerator::TerrainGenerator(const int _seed)  { seed = _seed; }

// ============================================================================
// Noise Sampling Functions
// ============================================================================

float TerrainGenerator::sampleTerrainNoise(float x, float z)
{
    // Base terrain shape with multiple octaves
    float large = stb_perlin_fbm_noise3(x * 0.005f, 0, z * 0.005f, 2.0f, 0.5f, 6);
    float detail = stb_perlin_fbm_noise3(x * 0.05f, 0, z * 0.05f, 2.0f, 0.5f, 2);

    float largNorm = (large + 1.0f) * 0.5f;
    float detailNorm = (detail + 1.0f) * 0.5f;

    return largNorm * 0.8f + detailNorm * 0.2f;
}

float TerrainGenerator::sampleContinentalNoise(float x, float z)
{
    // Low-frequency noise for large-scale variations (mountains vs plains)
    const float val = stb_perlin_fbm_noise3(x * 0.003f, 0, z * 0.003f, 2.0f, 0.5f, 4);
    return (val + 1.0f) * 0.5f;
}

float TerrainGenerator::sampleErosionNoise(float x, float z)
{
    // Higher-frequency noise for detail and erosion effects
    const float val = stb_perlin_fbm_noise3(x * 0.02f, 0, z * 0.02f, 2.0f, 0.5f, 2);
    return (val + 1.0f) * 0.5f;
}

float TerrainGenerator::sampleCaveNoise(float x, float y, float z)
{
    // 3D Perlin noise for cave generation
    // Use multiple octaves for winding caves
    const float cave = stb_perlin_fbm_noise3(x * 0.03f, y * 0.03f, z * 0.03f, 2.0f, 0.6f, 2);
    return cave;
}

float TerrainGenerator::sampleCaveEntranceNoise(float x, float z)
{
    const float val = stb_perlin_fbm_noise3(x * 0.008f, 0, z * 0.008f,2.0f, 0.5f, 3);
    return (val + 1.0f) * 0.5f;
}

float TerrainGenerator::sampleTemperatureNoise(float x, float z) {
    // Temperature biome noise
    const float val = stb_perlin_noise3_seed(x * 0.008f, 0, z * 0.008f, 0, 0, 0, seed + 1);
    return (val + 1.0f) * 0.5f;
}

float TerrainGenerator::sampleHumidityNoise(float x, float z) {
    // Humidity biome noise
    const float val = stb_perlin_noise3_seed(x * 0.005f, 0, z * 0.005f, 0, 0, 0, seed + 2);
    return (val + 1.0f) * 0.5f;
}

// ============================================================================
// Cave Entrance Calculation
// ============================================================================
float TerrainGenerator::calculateCaveEntranceWeight(float continental, float temp, float humid)
{
    float biomeEntranceWeight = CAVE_ENTRACE_WEIGHT; // plains default

    // Mountains → lots of entrances
    biomeEntranceWeight += continental * 0.6f;

    // Cold biomes → fewer entrances
    if (temp < TEMPERATURE_SNOW)
        biomeEntranceWeight -= 0.1f;

    // Deserts → slightly fewer
    if (temp > TEMPERATURE_DESERT && humid < HUMIDITY_DRY)
        biomeEntranceWeight -= 0.05f;

    biomeEntranceWeight = glm::clamp(biomeEntranceWeight, 0.05f, 0.9f);
    return biomeEntranceWeight;
}

// ============================================================================
// Height Calculation
// ============================================================================

int TerrainGenerator::calculateHeight(float terrainNoise, float continentalNoise, float erosionNoise)
{
    // Combine multiple noise layers
    const float heightBoost = terrainNoise * 16.0f;
    const float mountainBoost = continentalNoise * continentalNoise * 150.0f; // Quadratic for peaks
    const float erosionDetail = erosionNoise * 4.0f;

    const int finalHeight = BASE_HEIGHT +
                      static_cast<int>(heightBoost) +
                      static_cast<int>(mountainBoost) +
                      static_cast<int>(erosionDetail);

    return std::clamp(finalHeight, MIN_Y, MAX_Y);
}

// ============================================================================
// Biome Determination
// ============================================================================

BlockType TerrainGenerator::determineBiome(float temperature, float humidity, float continentalNoise)
{
    // Mountain biome takes priority for high continentalness
    if (continentalNoise > MOUNTAIN_THRESHOLD) {
        return BlockType::Stone;
    }

    // Cold biomes
    if (temperature < TEMPERATURE_SNOW) {
        return BlockType::Snow;
    }

    // Hot, dry biomes
    if (temperature > TEMPERATURE_DESERT && humidity < HUMIDITY_DRY) {
        return BlockType::Sand;
    }

    // Default plains biome
    return BlockType::Grass;
}

// ============================================================================
// Chunk Generation
// ============================================================================

void TerrainGenerator::generateChunk(Chunk& chunk, const glm::ivec2& coord) {
    const int baseWX = coord.x * Chunk::WIDTH;
    const int baseWZ = coord.y * Chunk::DEPTH;
    const uint64_t coordHash = hashCoord(coord);

    // Lock cache access
    std::lock_guard lock(noiseCacheMutex);

    // Check if noise is already cached
    if (!noiseCache.contains(coordHash)) {
        // Generate and cache noise maps
        std::vector<float> terrainNoise(Chunk::WIDTH * Chunk::DEPTH);
        std::vector<float> temperatureNoise(Chunk::WIDTH * Chunk::DEPTH);
        std::vector<float> humidityNoise(Chunk::WIDTH * Chunk::DEPTH);
        std::vector<float> mountain(Chunk::WIDTH * Chunk::DEPTH);

        // Precompute all 2D noise maps for this chunk
        for (int x = 0; x < Chunk::WIDTH; ++x) {
            for (int z = 0; z < Chunk::DEPTH; ++z) {
                int wx = baseWX + x;
                int wz = baseWZ + z;
                int idx = x * Chunk::DEPTH + z;

                // Sample all noise types
                float terrain = sampleTerrainNoise(wx, wz);
                float continental = sampleContinentalNoise(wx, wz);
                float erosion = sampleErosionNoise(wx, wz);
                float temp = sampleTemperatureNoise(wx, wz);
                float humid = sampleHumidityNoise(wx, wz);

                // Store computed values
                terrainNoise[idx] = calculateHeight(terrain, continental, erosion);
                temperatureNoise[idx] = temp;
                humidityNoise[idx] = humid;
                mountain[idx] = continental;
            }
        }

        noiseCache[coordHash] = {std::move(terrainNoise), std::move(temperatureNoise),
                                 std::move(humidityNoise), std::move(mountain)};
    }

    // Retrieve cached noise
    const NoiseCache& cached = noiseCache[coordHash];
    const auto& terrain = cached.terrain;
    const auto& temperature = cached.temperature;
    const auto& humidity = cached.humidity;
    const auto& mountain = cached.mountain;

    // Pre-pack common voxel types
    static const uint32_t STONE = packVoxelData(1, 255, 255, 255, (uint8_t)BlockType::Stone);
    static const uint32_t DIRT = packVoxelData(1, 255, 255, 255, (uint8_t)BlockType::Dirt);
    static const uint32_t GRASS = packVoxelData(1, 255, 255, 255, (uint8_t)BlockType::Grass);
    static const uint32_t SNOW = packVoxelData(1, 255, 255, 255, (uint8_t)BlockType::Snow);
    static const uint32_t SAND = packVoxelData(1, 255, 255, 255, (uint8_t)BlockType::Sand);
    static const uint32_t WATER = packVoxelData(1, 255, 255, 255, (uint8_t)BlockType::Water);

    // Fill voxels column by column
    for (int x = 0; x < Chunk::WIDTH; ++x) {
        for (int z = 0; z < Chunk::DEPTH; ++z) {
            const int idx = x * Chunk::DEPTH + z;
            const int surfaceY = static_cast<int>(terrain[idx]);
            const float continental = mountain[idx];
            const float temp = temperature[idx];
            const float humid = humidity[idx];

            // Determine biome for this column
            const BlockType surfaceBiome = determineBiome(temp, humid, continental);

            // Determine surface and subsurface blocks
            uint32_t surfaceBlock;
            uint32_t subsurfaceBlock;

            switch (surfaceBiome) {
                case BlockType::Stone: // Mountain
                    surfaceBlock = STONE;
                    subsurfaceBlock = STONE;
                    break;
                case BlockType::Snow: // Cold/snowy
                    surfaceBlock = SNOW;
                    subsurfaceBlock = DIRT;
                    break;
                case BlockType::Sand: // Desert
                    surfaceBlock = SAND;
                    subsurfaceBlock = SAND;
                    break;
                case BlockType::Grass: // Plains (default)
                default:
                    surfaceBlock = GRASS;
                    subsurfaceBlock = DIRT;
                    break;
            }

            const float entranceNoise = sampleCaveEntranceNoise(baseWX + x, baseWZ + z);
            const float entranceWeight = calculateCaveEntranceWeight(continental, temp, humid);

            // Fill the column from bottom to surface
            for (int y = MIN_Y; y <= surfaceY && y < Chunk::HEIGHT; ++y) {

                // Cave carving
                const float cave = sampleCaveNoise(baseWX + x, y, baseWZ + z);

                const bool isSurface      = (y == surfaceY);
                const bool isBelowSurface = (y < surfaceY);
                const bool allowEntrance = isSurface && entranceNoise < entranceWeight;

                const float depth = static_cast<float>(surfaceY - y);
                const float caveFade = glm::clamp(depth / 20.0f, 0.0f, 1.0f);

                const float threshold = CAVE_THRESHOLD + (1.0f - caveFade) * 0.4f;

                //      underground caves                       surface entrances
                if (y != MIN_Y && ((isBelowSurface && cave > threshold) || (allowEntrance && cave > 0.6f)))
                    continue;

                // Determine block type based on depth
                uint32_t voxel;
                if (y < surfaceY - 4) {
                    voxel = STONE;
                } else if (y < surfaceY) {
                    voxel = subsurfaceBlock;
                } else {
                    voxel = surfaceBlock;
                }

                chunk.setVoxelSilent(x, y, z, voxel);
            }

            // Fill water above surface up to sea level
            if (surfaceY < SEA_LEVEL) {
                for (int y = surfaceY + 1; y <= SEA_LEVEL && y < Chunk::HEIGHT; ++y) {
                    chunk.setVoxelSilent(x, y, z, WATER);
                }
            }

            // Snow cap for high mountains
            if (surfaceBiome != BlockType::Snow && surfaceY > SNOW_HEIGHT) {
                if (surfaceY < Chunk::HEIGHT) {
                    chunk.setVoxelSilent(x, surfaceY, z, SNOW);
                }
            }
        }
    }

    chunk.markMeshDirty();
}

// ============================================================================
// Single Voxel Sampling (for player-placed blocks, etc.)
// ============================================================================

Voxel TerrainGenerator::sampleVoxel(const int wx, const int y, const int wz) {
    if (y < MIN_Y || y > MAX_Y)
        return 0;

    // Sample all noise at world position
    const float terrainVal = sampleTerrainNoise(wx, wz);
    const float continentalVal = sampleContinentalNoise(wx, wz);
    const float erosionVal = sampleErosionNoise(wx, wz);
    const float tempVal = sampleTemperatureNoise(wx, wz);
    const float humidVal = sampleHumidityNoise(wx, wz);

    // Pre-pack voxels
    static const Voxel STONE = packVoxelData(1, 255, 255, 255, (uint8_t)BlockType::Stone);
    static const Voxel DIRT = packVoxelData(1, 255, 255, 255, (uint8_t)BlockType::Dirt);
    static const Voxel GRASS = packVoxelData(1, 255, 255, 255, (uint8_t)BlockType::Grass);
    static const Voxel SNOW = packVoxelData(1, 255, 255, 255, (uint8_t)BlockType::Snow);
    static const Voxel SAND = packVoxelData(1, 255, 255, 255, (uint8_t)BlockType::Sand);
    static const Voxel WATER = packVoxelData(1, 255, 255, 255, (uint8_t)BlockType::Water);

    // Calculate surface height
    const int surfaceY = calculateHeight(terrainVal, continentalVal, erosionVal);

    // Below sea level and above surface = water
    if (y > surfaceY && y <= SEA_LEVEL)
        return WATER;

    // Above surface = air
    if (y > surfaceY)
        return 0;

    // Determine biome
    const BlockType biome = determineBiome(tempVal, humidVal, continentalVal);

    // Cave carving
    const float cave = sampleCaveNoise(wx, y, wz);

    const bool isSurface      = (y == surfaceY);
    const bool isBelowSurface = (y < surfaceY);

    const float entranceWeight = calculateCaveEntranceWeight(continentalVal, tempVal, humidVal);
    const float entranceNoise = sampleCaveEntranceNoise(wx, wz);
    const bool allowEntrance = isSurface && entranceNoise < entranceWeight;

    const float depth = static_cast<float>(surfaceY - y);
    const float caveFade = glm::clamp(depth / 20.0f, 0.0f, 1.0f);

    const float threshold = CAVE_THRESHOLD + (1.0f - caveFade) * 0.4f;

    //      underground caves                       surface entrances
    if ((isBelowSurface && cave > threshold) || (allowEntrance && cave > 0.6f))
        return 0;

    // Deep underground = stone
    if (y < surfaceY - 4)
        return STONE;

    // Subsurface = biome-dependent dirt/sand
    if (y < surfaceY) {
        return (biome == BlockType::Stone) ? STONE :
               (biome == BlockType::Sand) ? SAND : DIRT;
    }

    // Surface block
    if (biome == BlockType::Stone) return STONE;
    if (biome == BlockType::Sand)  return SAND;
    if (biome == BlockType::Snow)  return SNOW;

    // Snow cap for high mountains
    if (surfaceY > SNOW_HEIGHT && continentalVal > MOUNTAIN_THRESHOLD)
        return SNOW;

    return GRASS; // Default
}