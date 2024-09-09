#include "World.hpp"
#include "glad/gl.h"
#include <iostream>

Chunk::Chunk() : visibility(-1), isVisible(false), isMeshDirty(false), queryID(0), voxels(Chunk::SIZE)
{
    glGenQueries(1, &queryID);
}

Chunk::~Chunk()
{
    glDeleteQueries(1, &queryID);
}

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
    isMeshDirty = true;
}

bool Chunk::isBlockActive(int x, int y, int z) const
{
	uint8_t isActive, r, g, b, blockType;
    unpackVoxelData(getVoxel(x, y, z), isActive, r, g, b, blockType);
    return isActive != 0;
}

World::World(std::unordered_map<BlockType, GLint> textures) : playerChunk(glm::ivec2(0,0)), textures(textures) {}

void World::generateWorldMesh(Renderer *renderer, Camera *camera, std::vector<Vertex>& vertices, std::vector<uint32_t>& indices) {
    Frustum frustum;
    static bool updateCulling = true;

    if (camera->hasMovedOrRotated()) {
        frustum.updateFrustum(camera->proj, camera->view);
        updateCulling = true;
        camera->lastPosition = camera->pos;
        camera->lastDirection = camera->dir;
    }

    std::lock_guard<std::mutex> lock(chunk_mutex);
    for (auto& [coord, chunk] : chunks) {
        glm::vec3 minPos(coord.x * Chunk::WIDTH, 0, coord.y * Chunk::DEPTH);
        glm::vec3 maxPos = minPos + glm::vec3(Chunk::WIDTH, Chunk::HEIGHT, Chunk::DEPTH);

        // Perform frustum culling
        if (updateCulling || chunk.visibility == -1) {
            chunk.isVisible = frustum.isBoxInFrustum(minPos, maxPos);
            chunk.visibility = chunk.isVisible;
        }

        if (chunk.isVisible) {
            // Begin occlusion query
            glBeginQuery(GL_SAMPLES_PASSED, chunk.queryID);
            renderer->renderBoundingBox(minPos, maxPos); // Render bounding box for occlusion query
            glEndQuery(GL_SAMPLES_PASSED);


            GLuint available = 0;
            glGetQueryObjectuiv(chunk.queryID, GL_QUERY_RESULT_AVAILABLE, &available);

            if (available) {
                GLuint sampleCount = 0;
                glGetQueryObjectuiv(chunk.queryID, GL_QUERY_RESULT, &sampleCount);
                chunk.isVisible = (sampleCount > 0);
            }
            else 
                chunk.isVisible = chunk.visibility == 1;
                
            // If sample count is 0, the chunk is occluded
            if (chunk.isMeshDirty){
                generateChunkMesh(chunk, chunk.cachedVertices, chunk.cachedIndices, coord);
                chunk.isMeshDirty = false;
            }

            uint32_t vertexOffset = static_cast<uint32_t>(vertices.size());
            vertices.insert(vertices.end(), chunk.cachedVertices.begin(), chunk.cachedVertices.end());
            for (uint32_t index : chunk.cachedIndices)
                indices.push_back(index + vertexOffset);
        }
    }

    updateCulling = false;
}


void World::updateChunks(const glm::vec3& cameraPosition, ThreadPool& threadPool) {
    glm::ivec2 newPlayerChunk = glm::ivec2(cameraPosition.x / Chunk::WIDTH, cameraPosition.z / Chunk::DEPTH);
    static bool first_gen = true;

    if (!first_gen && newPlayerChunk == playerChunk) {
        return; // No need to update if the player chunk hasn't changed
    }

    first_gen = false;
    playerChunk = newPlayerChunk;

    // Calculate the chunks to load
    std::vector<glm::ivec2> chunksToLoad;
    std::vector<glm::ivec2> chunksToUnload;

    // Determine which chunks need to be loaded
    for (int dx = -CHUNK_RADIUS; dx <= CHUNK_RADIUS; ++dx) {
        for (int dz = -CHUNK_RADIUS; dz <= CHUNK_RADIUS; ++dz) {
            glm::ivec2 chunkCoord = playerChunk + glm::ivec2(dx, dz);
            if (chunks.find(chunkCoord) == chunks.end())
                chunksToLoad.push_back(chunkCoord);
        }
    }

    // Determine which chunks need to be unloaded
    float maxDistanceSquared = (CHUNK_RADIUS + 1.5f) * (CHUNK_RADIUS + 1.5f);
    for (auto& [coord, chunk] : chunks) {
        if (glm::distance(glm::vec2(coord), glm::vec2(playerChunk)) > maxDistanceSquared)
            chunksToUnload.push_back(coord);
    }

    // Load new chunks
    for (const glm::ivec2& coord : chunksToLoad) {
        if (chunks.find(coord) == chunks.end()) {
            threadPool.enqueue([this, coord] {
                Chunk newChunk;
                generateTerrain(newChunk, coord);
                {
                    std::lock_guard<std::mutex> lock(chunk_mutex);
                    chunks.emplace(coord, std::move(newChunk));
                }
            });
        }
    }

    // Unload old chunks
    for (const glm::ivec2& coord : chunksToUnload) {
        std::lock_guard<std::mutex> lock(chunk_mutex);
        chunks.erase(coord);
    }
}

void World::generateChunkMesh(Chunk& chunk, std::vector<Vertex>& vertices, std::vector<uint32_t>& indices, const glm::ivec2& coord) {
    for (int x = 0; x < Chunk::WIDTH; ++x) {
        for (int y = 0; y < Chunk::HEIGHT; ++y) {
            for (int z = 0; z < Chunk::DEPTH; ++z) {
                if (chunk.isBlockActive(x, y, z))
                    generateBlockFaces(vertices, indices, chunk, x, y, z, coord);
            }
        }
    }
}

void World::generateBlockFaces(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices, const Chunk& chunk, int x, int y, int z, const glm::ivec2& coord) {
    glm::vec3 blockPos(x + coord.x * Chunk::WIDTH, y, z + coord.y * Chunk::DEPTH);
    uint8_t isActive, r, g, b, blockType;
    unpackVoxelData(chunk.getVoxel(x, y, z), isActive, r, g, b, blockType);

    if (isFaceVisible(chunk, x, y, z, Direction::Front))
        createFace(vertices, indices, blockPos, Direction::Front, textures[static_cast<BlockType>(blockType)]);
    if (isFaceVisible(chunk, x, y, z, Direction::Back))
        createFace(vertices, indices, blockPos, Direction::Back, textures[static_cast<BlockType>(blockType)]);
    if (isFaceVisible(chunk, x, y, z, Direction::Left))
        createFace(vertices, indices, blockPos, Direction::Left, textures[static_cast<BlockType>(blockType)]);
    if (isFaceVisible(chunk, x, y, z, Direction::Right))
        createFace(vertices, indices, blockPos, Direction::Right, textures[static_cast<BlockType>(blockType)]);
    if (isFaceVisible(chunk, x, y, z, Direction::Top))
        createFace(vertices, indices, blockPos, Direction::Top, textures[static_cast<BlockType>(blockType)]);
    if (isFaceVisible(chunk, x, y, z, Direction::Bottom))
        createFace(vertices, indices, blockPos, Direction::Bottom, textures[static_cast<BlockType>(blockType)]);
}

void World::generateTerrain(Chunk& chunk, const glm::ivec2& coord) {
    const float frequency = 0.05f;
    const float amplitude = 20.0f;

    for (int x = 0; x < Chunk::WIDTH; ++x) {
        for (int z = 0; z < Chunk::DEPTH; ++z) {
            // Basic sine wave terrain with added randomness
            float height = amplitude * (std::sin(frequency * x + coord.x) + std::sin(frequency * z + coord.y)) +
                           (rand() % 10 - 5) +  // Random value to add noise
                           (Chunk::HEIGHT / 4);

            int intHeight = static_cast<int>(height);

            for (int y = -Chunk::HEIGHT/4; y < intHeight; ++y) {
                BlockType blocktype = rand()%2 ? BlockType::Grass : BlockType::Stone;
                uint32_t voxelData = packVoxelData(1, 100, 255, 100, static_cast<uint8_t>(blocktype));  // Example voxel data (active, color, block type)
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

void World::createFace(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices, const glm::vec3& blockPos, const Direction direction, const GLuint textureID) {
    glm::vec3 v0 = glm::vec3(0.0f);
    glm::vec3 v1 = glm::vec3(0.0f);
    glm::vec3 v2 = glm::vec3(0.0f);
    glm::vec3 v3 = glm::vec3(0.0f);

    if (direction == Direction::Front) { // Front face
        v0 = glm::vec3(0.0f, 0.0f, 1.0f);
        v1 = glm::vec3(1.0f, 0.0f, 1.0f);
        v2 = glm::vec3(1.0f, 1.0f, 1.0f);
        v3 = glm::vec3(0.0f, 1.0f, 1.0f);
    } else if (direction == Direction::Back) { // Back face
        v0 = glm::vec3(1.0f, 0.0f, 0.0f);
        v1 = glm::vec3(0.0f, 0.0f, 0.0f);
        v2 = glm::vec3(0.0f, 1.0f, 0.0f);
        v3 = glm::vec3(1.0f, 1.0f, 0.0f);
    } else if (direction == Direction::Left) { // Left face
        v0 = glm::vec3(0.0f, 0.0f, 0.0f);
        v1 = glm::vec3(0.0f, 0.0f, 1.0f);
        v2 = glm::vec3(0.0f, 1.0f, 1.0f);
        v3 = glm::vec3(0.0f, 1.0f, 0.0f);
    } else if (direction == Direction::Right) { // Right face
        v0 = glm::vec3(1.0f, 0.0f, 1.0f);
        v1 = glm::vec3(1.0f, 0.0f, 0.0f);
        v2 = glm::vec3(1.0f, 1.0f, 0.0f);
        v3 = glm::vec3(1.0f, 1.0f, 1.0f);
    } else if (direction == Direction::Top) { // Top face
        v0 = glm::vec3(0.0f, 1.0f, 1.0f);
        v1 = glm::vec3(1.0f, 1.0f, 1.0f);
        v2 = glm::vec3(1.0f, 1.0f, 0.0f);
        v3 = glm::vec3(0.0f, 1.0f, 0.0f);
    } else if (direction == Direction::Bottom) { // Bottom face
        v0 = glm::vec3(0.0f, 0.0f, 0.0f);
        v1 = glm::vec3(1.0f, 0.0f, 0.0f);
        v2 = glm::vec3(1.0f, 0.0f, 1.0f);
        v3 = glm::vec3(0.0f, 0.0f, 1.0f);
    }

    uint32_t startIndex = vertices.size();
    indices.push_back(startIndex);
    indices.push_back(startIndex + 1);
    indices.push_back(startIndex + 2);
    indices.push_back(startIndex);
    indices.push_back(startIndex + 2);
    indices.push_back(startIndex + 3);

    vertices.push_back({ blockPos + v0, {0.f, 0.f}, textureID });
    vertices.push_back({ blockPos + v1, {1.f, 0.f}, textureID });
    vertices.push_back({ blockPos + v2, {1.f, 1.f}, textureID });
    vertices.push_back({ blockPos + v3, {0.f, 1.f}, textureID });
}

