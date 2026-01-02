#include "World.hpp"
#include "glad/glad.h"
#include <iostream>
#include <cmath>
#include <random>
#include <ranges>

inline int floorDiv(const int x, const int d) {
    int q = x / d;
    if (const int r = x % d; r != 0 && ((r < 0) != (d < 0)))
        --q;
    return q;
}

bool World::isBlockActiveWorld(const int wx, const int wy, const int wz) const {
    if (wy < 0 || wy >= Chunk::HEIGHT)
        return false;

    int cx = floorDiv(wx, Chunk::WIDTH);
    int cz = floorDiv(wz, Chunk::DEPTH);

    const auto it = chunks.find({cx, cz});
    if (it == chunks.end())
        return false; // missing chunk = air

    const int lx = wx - cx * Chunk::WIDTH;
    const int lz = wz - cz * Chunk::DEPTH;

    return it->second.isBlockActive(lx, wy, lz);
}

Chunk::Chunk() : visibility(-1), isVisible(false), isMeshDirty(false), queryID(0), voxels(Chunk::SIZE)
{
    glGenQueries(1, &queryID);
}

Chunk::~Chunk()
{
    glDeleteQueries(1, &queryID);
}

Voxel Chunk::getVoxel(const int x, const int y, const int z) const
{
	if (x < 0 || x >= Chunk::WIDTH || y < 0 || y >= Chunk::HEIGHT || z < 0 || z >= Chunk::DEPTH)
		return 0;
	return voxels[x + y * Chunk::WIDTH + z * Chunk::WIDTH * Chunk::HEIGHT];
}

void Chunk::setVoxel(const int x, const int y, const int z, const Voxel voxel)
{
	if (x < 0 || x >= Chunk::WIDTH || y < 0 || y >= Chunk::HEIGHT || z < 0 || z >= Chunk::DEPTH)
		return;
	voxels[x + y * Chunk::WIDTH + z * Chunk::WIDTH * Chunk::HEIGHT] = voxel;
    isMeshDirty = true;
}

bool Chunk::isBlockActive(const int x, const int y, const int z) const
{
    return isActive(getVoxel(x, y, z));
}

World::World(const std::unordered_map<BlockType, uint32_t>& indices): playerChunk(0, 0), textureIndices(indices) {}

void World::generateWorldMesh(const std::unique_ptr<Renderer>& renderer, const std::unique_ptr<Camera>& camera, std::vector<Vertex>& vertices, std::vector<uint32_t>& indices) {

    frustum.updateFrustum(camera->proj, camera->view);
    camera->lastPosition = camera->pos;
    camera->lastDirection = camera->dir;
    // Collect chunks to render outside the mutex
    std::vector<std::pair<glm::ivec2, Chunk*>> visibleChunks;
    (void)renderer;

    {
        std::lock_guard<std::mutex> lock(chunk_mutex);
        for (auto& [coord, chunk] : chunks) {
            // Compute padded bounding box
            constexpr float epsilon = 0.1f * Chunk::WIDTH;
            glm::vec3 minPos(coord.x * Chunk::WIDTH - epsilon, 0 - epsilon, coord.y * Chunk::DEPTH - epsilon);
            glm::vec3 maxPos = minPos + glm::vec3(Chunk::WIDTH + 2*epsilon, Chunk::HEIGHT + 2*epsilon, Chunk::DEPTH + 2*epsilon);

            // Frustum culling
            chunk.isVisible = frustum.isBoxInFrustum(minPos, maxPos);
            if (chunk.isVisible) {
                visibleChunks.emplace_back(coord, &chunk);
            }
        }
    }

    // Generate meshes and fill vertex/index buffers outside the mutex
    for (auto& [coord, chunkPtr] : visibleChunks) {
        Chunk& chunk = *chunkPtr;

        if (chunk.isMeshDirty) {
            generateChunkMesh(chunk, chunk.cachedVertices, chunk.cachedIndices, coord);
            chunk.isMeshDirty = false;
        }

        // Append to the main vertex/index arrays
        const auto vertexOffset = static_cast<uint32_t>(vertices.size());
        indices.reserve(indices.size() + chunk.cachedIndices.size());
        vertices.insert(vertices.end(), chunk.cachedVertices.begin(), chunk.cachedVertices.end());
        for (const uint32_t index : chunk.cachedIndices)
            indices.emplace_back(index + vertexOffset);
    }
}

void World::updateChunks(const glm::vec3& playerPos, ThreadPool& threadPool) {
    const auto newPlayerChunk = glm::ivec2(floorDiv(playerPos.x , Chunk::WIDTH), floorDiv(playerPos.z, Chunk::DEPTH));
    static bool first_gen = true;

    // No need to update if the player chunk hasn't changed
    if (!first_gen && newPlayerChunk == playerChunk) return;

    first_gen = false;
    playerChunk = newPlayerChunk;

    std::vector<glm::ivec2> chunksToLoad;
    std::vector<glm::ivec2> chunksToUnload;
    chunksToLoad.reserve(32);
    chunksToUnload.reserve(32);

    // Determine which chunks need to be loaded
    for (int dx = -CHUNK_RADIUS; dx <= CHUNK_RADIUS; ++dx) {
        for (int dz = -CHUNK_RADIUS; dz <= CHUNK_RADIUS; ++dz) {
            std::lock_guard<std::mutex> lock(chunk_mutex);
            if (glm::ivec2 chunkCoord = playerChunk + glm::ivec2(dx, dz); !chunks.contains(chunkCoord))
                chunksToLoad.push_back(chunkCoord);
        }
    }

    // Determine which chunks need to be unloaded
    constexpr float maxDistanceSquared = (CHUNK_RADIUS + 1.5f) * (CHUNK_RADIUS + 1.5f);
    for (const auto& coord : chunks | std::views::keys) {
        if (glm::distance(glm::vec2(coord), glm::vec2(playerChunk)) > maxDistanceSquared)
            chunksToUnload.push_back(coord);
    }

    // Load new chunks
    for (const glm::ivec2& coord : chunksToLoad) {
        if (!chunks.contains(coord)) {
            threadPool.enqueue([this, coord] {
                Chunk newChunk;
                generateTerrain(newChunk, coord);

                std::lock_guard<std::mutex> lock(chunk_mutex);
                chunks.emplace(coord, std::move(newChunk));
                markChunkAndNeighborsDirty(coord);
            });
        }
    }

    // Unload old chunks
    for (const glm::ivec2& coord : chunksToUnload) {
        std::lock_guard<std::mutex> lock(chunk_mutex);
        chunks.erase(coord);
        markChunkAndNeighborsDirty(coord);
    }
}


void World::generateChunkMesh(const Chunk& chunk, std::vector<Vertex>& vertices, std::vector<uint32_t>& indices, const glm::ivec2& coord) const
{
    // vertices.clear();
    // indices.clear();
    vertices.reserve(vertices.size() + Chunk::SIZE * 4);
    indices.reserve(indices.size() + Chunk::SIZE * 6);

    for (int y = 0; y < Chunk::HEIGHT; ++y) {
        for (int x = 0; x < Chunk::WIDTH; ++x) {
            for (int z = 0; z < Chunk::DEPTH; ++z) {
                if (!chunk.isBlockActive(x, y, z))
                    continue;

                const auto blockType = static_cast<BlockType>(getBlockType(chunk.getVoxel(x, y, z)));
                const auto it = textureIndices.find(blockType);
                if (blockType == BlockType::Air || it == textureIndices.end())
                    continue;

                const glm::vec3 blockPos(x + coord.x * Chunk::WIDTH, y, z + coord.y * Chunk::DEPTH);
                const uint32_t texIndex = it->second;

                // Loop through visible faces only
                for (int dir = 0; dir < 6; ++dir) {
                    if (isFaceVisible(chunk, coord, x, y, z, static_cast<Direction>(dir)))
                        createFace(vertices, indices, blockPos, static_cast<Direction>(dir), texIndex);
                }
            }
        }
    }
}

void World::generateTerrain(Chunk& chunk, const glm::ivec2& coord) {
    std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<int> noise(-5, 5);
    std::uniform_int_distribution<int> blocks(0, 1);

    for (int x = 0; x < Chunk::WIDTH; ++x) {
        for (int z = 0; z < Chunk::DEPTH; ++z) {
            constexpr float amplitude = 20.0f;
            constexpr float frequency = 0.05f;
            const float height = noise(rng) + (Chunk::HEIGHT / 4)
                + amplitude * (std::sin(frequency * x + coord.x)
                + std::sin(frequency * z + coord.y));

            const int intHeight = static_cast<int>(height);
            for (int y = 0; y < intHeight; ++y) {
                BlockType blockType = blocks(rng) ? BlockType::Grass : BlockType::Stone;
                const auto voxelData = packVoxelData(1, 100, 255, 100, static_cast<uint8_t>(blockType));
                chunk.setVoxel(x, y, z, voxelData);
            }
        }
    }
}

bool World::isFaceVisible(const Chunk& chunk, const glm::ivec2& coord,
        const int x, const int y, const int z,
        const Direction direction
) const {
    constexpr int dx[6] = { 0, 0, -1, 1, 0, 0 };
    constexpr int dy[6] = { 0, 0, 0, 0, 1, -1 };
    constexpr int dz[6] = { 1, -1, 0, 0, 0, 0 };

    const int nx = x + dx[static_cast<int>(direction)];
    const int ny = y + dy[static_cast<int>(direction)];
    const int nz = z + dz[static_cast<int>(direction)];

    // Neighbor inside this chunk
    if (static_cast<unsigned>(nx) < Chunk::WIDTH
        && static_cast<unsigned>(ny) < Chunk::HEIGHT
        && static_cast<unsigned>(nz) < Chunk::DEPTH)
    {
        return !chunk.isBlockActive(nx, ny, nz);
    }

    // Neighbor in another chunk → world lookup
    const int wx = coord.x * Chunk::WIDTH + nx;
    const int wz = coord.y * Chunk::DEPTH + nz;

    return !isBlockActiveWorld(wx, ny, wz);
}

void World::createFace(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices, const glm::vec3& blockPos, const Direction direction, const uint32_t texIndex) {
    auto v0 = glm::vec3(0.0f);
    auto v1 = glm::vec3(0.0f);
    auto v2 = glm::vec3(0.0f);
    auto v3 = glm::vec3(0.0f);

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

    vertices.push_back({blockPos + v0, {0.f, 0.f}, texIndex});
    vertices.push_back({blockPos + v1, {1.f, 0.f}, texIndex});
    vertices.push_back({blockPos + v2, {1.f, 1.f}, texIndex});
    vertices.push_back({blockPos + v3, {0.f, 1.f}, texIndex});
}

void World::markChunkAndNeighborsDirty(const glm::ivec2& coord) {
    static constexpr glm::ivec2 offsets[5] = {
        {0,0},{1,0},{-1,0},{0,1},{0,-1}
    };

    for (auto& off : offsets) {
        if (auto it = chunks.find(coord + off); it != chunks.end()) {
            it->second.isMeshDirty = true;
        }
    }
}

