#include "World.hpp"

Chunk::Chunk() : voxels(Chunk::SIZE) {}

Voxel Chunk::getVoxel(int x, int y, int z) const
{
	if (x < 0 || x >= Chunk::WIDTH || y < 0 || y >= Chunk::HEIGHT || z < 0 || z >= Chunk::DEPTH)
		return 0;
	return voxels[x + y * Chunk::WIDTH + z * Chunk::WIDTH * Chunk::HEIGHT];
}

void Chunk::setVoxel(int x, int y, int z, Voxel voxel)
{
	if (x < 0 || x >= Chunk::WIDTH || y < 0 || y >= Chunk::HEIGHT || z < 0 || z >= Chunk::DEPTH)
		return;
	voxels[x + y * Chunk::WIDTH + z * Chunk::WIDTH * Chunk::HEIGHT] = voxel;
}

bool Chunk::isBlockActive(int x, int y, int z) const
{
	uint8_t isActive, r, g, b, blockType;
    unpackVoxelData(getVoxel(x, y, z), isActive, r, g, b, blockType);
    return isActive != 0;
}

// Chunk& World::getChunk(int x, int z) {
// 	glm::ivec2 chunkCoord(x, z);
// 	if (chunks.find(chunkCoord) == chunks.end()) {
// 		chunks[chunkCoord] = Chunk();
// 		generateTerrain(chunks[chunkCoord]);
// 	}
// 	return chunks[chunkCoord];
// }

World::World() : playerChunk(glm::ivec2(0,0)) {}

void World::generateWorldMesh(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices) {
	for (auto& [coord, chunk] : chunks) {
		generateChunkMesh(chunk, vertices, indices);
	}
}

void World::updateChunks(const glm::vec3& cameraPosition) {
    glm::ivec2 newPlayerChunk = glm::ivec2(cameraPosition.x / Chunk::WIDTH, cameraPosition.z / Chunk::DEPTH);

    if (newPlayerChunk == playerChunk) {
        return; // No need to update if the player chunk hasn't changed
    }

    playerChunk = newPlayerChunk;

    // Calculate the chunks to load
    std::vector<glm::ivec2> chunksToLoad;
    std::vector<glm::ivec2> chunksToUnload;

    // Determine which chunks need to be loaded
    for (int dx = -CHUNK_RADIUS; dx <= CHUNK_RADIUS; ++dx) {
        for (int dz = -CHUNK_RADIUS; dz <= CHUNK_RADIUS; ++dz) {
            glm::ivec2 chunkCoord = playerChunk + glm::ivec2(dx, dz);
            if (chunks.find(chunkCoord) == chunks.end()) {
                chunksToLoad.push_back(chunkCoord);
            }
        }
    }

    // Determine which chunks need to be unloaded
    for (auto& [coord, chunk] : chunks) {
        if (glm::distance(glm::vec2(coord), glm::vec2(playerChunk)) > CHUNK_RADIUS) {
            chunksToUnload.push_back(coord);
        }
    }

    // Load new chunks
    for (const glm::ivec2& coord : chunksToLoad) {
        Chunk& chunk = chunks[coord];
        generateTerrain(chunk); // Generate terrain data
    }

    // Unload old chunks
    for (const glm::ivec2& coord : chunksToUnload) {
        chunks.erase(coord);
    }
}

void World::generateChunkMesh(Chunk& chunk, std::vector<Vertex>& vertices, std::vector<uint32_t>& indices) {
    for (int x = 0; x < Chunk::WIDTH; ++x) {
        for (int y = 0; y < Chunk::HEIGHT; ++y) {
            for (int z = 0; z < Chunk::DEPTH; ++z) {
                if (chunk.isBlockActive(x, y, z)) {
                    generateBlockFaces(vertices, indices, chunk, x, y, z);
                }
            }
        }
    }
}

void World::generateBlockFaces(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices, const Chunk& chunk, int x, int y, int z) {
    glm::vec3 blockPos(x, y, z);
    uint8_t isActive, r, g, b, blockType;
    unpackVoxelData(chunk.getVoxel(x, y, z), isActive, r, g, b, blockType);
    glm::vec3 color(r / 255.0f, g / 255.0f, b / 255.0f);

    if (isFaceVisible(chunk, x, y, z, Direction::Front))
        createFace(vertices, indices, blockPos, glm::vec3(0, 0, 1), color);
    if (isFaceVisible(chunk, x, y, z, Direction::Back))
        createFace(vertices, indices, blockPos, glm::vec3(0, 0, -1), color);
    if (isFaceVisible(chunk, x, y, z, Direction::Left))
        createFace(vertices, indices, blockPos, glm::vec3(-1, 0, 0), color);
    if (isFaceVisible(chunk, x, y, z, Direction::Right))
        createFace(vertices, indices, blockPos, glm::vec3(1, 0, 0), color);
    if (isFaceVisible(chunk, x, y, z, Direction::Top))
        createFace(vertices, indices, blockPos, glm::vec3(0, 1, 0), color);
    if (isFaceVisible(chunk, x, y, z, Direction::Bottom))
        createFace(vertices, indices, blockPos, glm::vec3(0, -1, 0), color);
}

void World::generateTerrain(Chunk& chunk) {
    const float frequency = 0.05f;
    const float amplitude = 20.0f;

    for (int x = 0; x < Chunk::WIDTH; ++x) {
        for (int z = 0; z < Chunk::DEPTH; ++z) {
            // Basic sine wave terrain with added randomness
            float height = amplitude * (std::sin(frequency * x) + std::sin(frequency * z)) +
                           (rand() % 10 - 5) +  // Random value to add noise
                           (Chunk::HEIGHT / 4);

            int intHeight = static_cast<int>(height);

            for (int y = 0; y < intHeight; ++y) {
                uint32_t voxelData = packVoxelData(1, 100, 255, 100, 1);  // Example voxel data (active, color, block type)
                chunk.setVoxel(x, y, z, voxelData);
            }
        }
    }
}

bool World::isFaceVisible(const Chunk& chunk, int x, int y, int z, Direction direction) {
    switch (direction) {
        case Direction::Front:  return !chunk.isBlockActive(x, y, z + 1);
        case Direction::Back:   return !chunk.isBlockActive(x, y, z - 1);
        case Direction::Left:   return !chunk.isBlockActive(x - 1, y, z);
        case Direction::Right:  return !chunk.isBlockActive(x + 1, y, z);
        case Direction::Top:    return !chunk.isBlockActive(x, y + 1, z);
        case Direction::Bottom: return !chunk.isBlockActive(x, y - 1, z);
    }
    return false;
}

void World::createFace(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices, const glm::vec3& blockPos, const glm::vec3& normal, const glm::vec3& color) {
    static const glm::vec3 faceVertices[4] = {
        {0.0f, 0.0f, 0.0f},
        {1.0f, 0.0f, 0.0f},
        {1.0f, 1.0f, 0.0f},
        {0.0f, 1.0f, 0.0f}
    };

    uint32_t startIndex = vertices.size();
    for (int i = 0; i < 4; ++i) {
        vertices.push_back({ blockPos + faceVertices[i], color, {0.f,0.f}});
    }

    indices.push_back(startIndex);
    indices.push_back(startIndex + 1);
    indices.push_back(startIndex + 2);
    indices.push_back(startIndex);
    indices.push_back(startIndex + 2);
    indices.push_back(startIndex + 3);

    (void)normal;
}

