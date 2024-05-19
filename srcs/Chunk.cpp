#include <unordered_map>
#include <vector>
#include <iostream>
#include "Chunk.hpp"
#include "Noise.hpp"
#include "voxel.hpp"
#include <cstring>

Chunk& Chunk::operator=(const Chunk& other) {
	if (this != &other) {
		memcpy(voxels, other.voxels, sizeof(voxels));
	}
	return *this;
}

uint32_t&	Chunk::at(int x, int y, int z)
{
	return voxels[x + y * CHUNK_WIDTH + z * CHUNK_WIDTH * CHUNK_DEPTH];
}

void Chunk::initialize(const PerlinNoise& noise) {
	for (int x = 0; x < CHUNK_WIDTH; ++x) {
		for (int y = 0; y < CHUNK_DEPTH; ++y) {
			for (int z = 0; z < CHUNK_HEIGHT; ++z) {
				double noiseValue = noise.noise(x * 0.1, y * 0.1, z * 0.1);
				uint8_t isActive = noiseValue > 0.3 ? 1 : 0;
				uint8_t r = static_cast<uint8_t>(noiseValue * 255);
				uint8_t g = static_cast<uint8_t>(noiseValue * 255);
				uint8_t b = static_cast<uint8_t>(noiseValue * 255);
				uint8_t blockType;

				// Determine block type based on noise value
				if (noiseValue > 0.6) {
					blockType = GRASS;
				} else if (noiseValue > 0.5) {
					blockType = DIRT;
				} else if (noiseValue > 0.4) {
					blockType = STONE;
				} else if (noiseValue > 0.3) {
					blockType = WATER;
				} else {
					blockType = AIR;
				}

				at(x, y, z) = packVoxelData(isActive, r, g, b, blockType);
			}
		}
	}
}

ChunkManager::ChunkManager(uint32_t seed) { noise = PerlinNoise(seed); }

Chunk& ChunkManager::loadChunk(const ChunkCoords& coords) {
	Chunk& chunk = chunks[coords];
	chunk.initialize(noise);
	return chunk;
}

void ChunkManager::unloadChunk(const ChunkCoords& coords) {
	chunks.erase(coords);
}

void ChunkManager::updateChunks(const float* playerPos) {
	int chunkX = playerPos[0] / CHUNK_WIDTH;
	int chunkY = playerPos[1] / CHUNK_DEPTH;
	int chunkZ = playerPos[2] / CHUNK_HEIGHT;

	// Unload chunks that are out of range
	for (auto it = chunks.begin(); it != chunks.end(); ) {
		const ChunkCoords& coords = it->first;
		if (std::abs(coords.x - chunkX) > renderDistance ||
			std::abs(coords.y - chunkY) > renderDistance ||
			std::abs(coords.z - chunkZ) > renderDistance) {
			it = chunks.erase(it);
		} else {
			++it;
		}
	}

	// Load chunks within range
	for (int x = chunkX - renderDistance; x <= chunkX + renderDistance; ++x) {
		for (int y = chunkY - renderDistance; y <= chunkY + renderDistance; ++y) {
			for (int z = chunkZ - renderDistance; z <= chunkZ + renderDistance; ++z) {
				ChunkCoords coords = { x, y, z };
				if (chunks.find(coords) == chunks.end()) {
					loadChunk(coords);
				}
			}
		}
	}
}
