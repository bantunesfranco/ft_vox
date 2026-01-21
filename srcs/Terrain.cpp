#include "Terrain.hpp"
#include "stb_perlin.h"
#include <glm/glm.hpp>
#include <algorithm>
#include <iostream>
#include <mutex>

TerrainGenerator::TerrainGenerator(const int seed): seed(seed) {}

void TerrainGenerator::generateChunk(Chunk& chunk, const glm::ivec2& coord) {
    constexpr int MIN_Y = 1;
    constexpr int MAX_Y = Chunk::HEIGHT - 1;
    constexpr int BASE_HEIGHT = Chunk::HEIGHT / 8;
    constexpr int SNOW_HEIGHT = Chunk::HEIGHT / 2;

    const int baseWX = coord.x * Chunk::WIDTH;
    const int baseWZ = coord.y * Chunk::DEPTH;

    // Compute cache key
    const uint64_t coordHash = ((uint64_t)coord.x << 32) | (uint32_t)coord.y;

    // Lock cache access
    std::lock_guard<std::mutex> lock(noiseCacheMutex);

    // Check if noise is already cached
    if (!noiseCache.contains(coordHash)){
        // Generate and cache noise maps
        std::vector<float> terrainNoise(Chunk::WIDTH * Chunk::DEPTH);
        std::vector<float> temperatureNoise(Chunk::WIDTH * Chunk::DEPTH);
        std::vector<float> humidityNoise(Chunk::WIDTH * Chunk::DEPTH);
        std::vector<float> mountainNoise(Chunk::WIDTH * Chunk::DEPTH);

        // --- Step 1: Precompute all noise maps ---
        for (int x = 0; x < Chunk::WIDTH; ++x) {
            for (int z = 0; z < Chunk::DEPTH; ++z) {
                int wx = baseWX + x;
                int wz = baseWZ + z;
                int idx = x * Chunk::DEPTH + z;

                // Mountain noise (controls where mountains are)
                float mountainVal = stb_perlin_fbm_noise3(wx*0.003f, 0, wz*0.003f, 2.0f, 0.5f, 4);
                mountainVal = (mountainVal + 1.0f) * 0.5f; // normalize to 0..1
                mountainNoise[idx] = mountainVal;

                // Base terrain
                float large = stb_perlin_fbm_noise3(wx*0.005f, 0, wz*0.005f, 2.0f, 0.5f, 6);
                float detail = stb_perlin_fbm_noise3(wx*0.05f, 0, wz*0.05f, 2.0f, 0.5f, 2);

                float heightNorm = (large + 1.0f) * 0.5f;
                float detailNorm = (detail + 1.0f) * 0.5f;

                // Apply mountain influence: boost height where mountainVal is high
                int height = BASE_HEIGHT
                           + static_cast<int>(heightNorm * 16 + detailNorm * 4)
                           + static_cast<int>(mountainVal * mountainVal * 150);
                terrainNoise[idx] = std::clamp(height, MIN_Y, MAX_Y);

                // Temperature (0=cold, 1=hot)
                temperatureNoise[idx] = (stb_perlin_noise3_seed(wx*0.008f, 0, wz*0.008f, 0, 0, 0, seed+1) + 1.0f) * 0.5f;

                // Humidity (0=dry, 1=wet)
                humidityNoise[idx] = (stb_perlin_noise3_seed(wx*0.005f, 0, wz*0.005f, 0, 0, 0, seed+2) + 1.0f) * 0.5f;
            }
        }

        // Cache the noise maps
        noiseCache[coordHash] = {std::move(terrainNoise), std::move(temperatureNoise), std::move(humidityNoise), std::move(mountainNoise)};
    }

    // --- Step 2: Voxel type lookup ---
    static const uint32_t STONE = packVoxelData(1, 255, 255, 255, (uint8_t)BlockType::Stone);
    static const uint32_t DIRT  = packVoxelData(1, 255, 255, 255, (uint8_t)BlockType::Dirt);
    static const uint32_t GRASS = packVoxelData(1, 255, 255, 255, (uint8_t)BlockType::Grass);
    static const uint32_t SNOW  = packVoxelData(1, 255, 255, 255, (uint8_t)BlockType::Snow);
    static const uint32_t SAND  = packVoxelData(1, 255, 255, 255, (uint8_t)BlockType::Sand);

    const NoiseCache& cached = noiseCache[coordHash];
    const auto& terrain = cached.terrain;
    const auto& temperature = cached.temperature;
    const auto& humidity = cached.humidity;
    const auto& mountain = cached.mountain;

    // --- Step 3: Fill voxels (optimized) ---
    for (int x = 0; x < Chunk::WIDTH; ++x) {
        for (int z = 0; z < Chunk::DEPTH; ++z) {
            const int idx = x*Chunk::DEPTH + z;
            const int surfaceY = terrain[idx];
            const float mount = mountain[idx];

            uint32_t surfaceBlock;
            uint32_t subsurfaceBlock;

            // Determine biome once
            const bool isMountain = mount > 0.65f;
            if (isMountain) {
                surfaceBlock = STONE;
                subsurfaceBlock = STONE;
            } else {
                const float temp = temperature[idx];
                const float humid = humidity[idx];

                if (temp < 0.2f) {
                    surfaceBlock = SNOW;
                    subsurfaceBlock = DIRT;
                } else if (temp > 0.65f && humid < 0.4f) {
                    surfaceBlock = SAND;
                    subsurfaceBlock = SAND;
                } else {
                    surfaceBlock = GRASS;
                    subsurfaceBlock = DIRT;
                }
            }

            // Fill stone/dirt layers
            for (int y = MIN_Y; y < surfaceY; ++y) {
                const uint32_t voxel = (y < surfaceY - 4) ? STONE : subsurfaceBlock;
                chunk.setVoxelSilent(x, y, z, voxel);
            }

            // Place top block
            if (surfaceY < Chunk::HEIGHT) {
                if (isMountain && surfaceY > SNOW_HEIGHT + 20) {
                    chunk.setVoxelSilent(x, surfaceY, z, SNOW);
                } else {
                    chunk.setVoxelSilent(x, surfaceY, z, surfaceBlock);
                }
            }
        }
    }

    chunk.markMeshDirty();
}