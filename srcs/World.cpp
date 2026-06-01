#include "World.hpp"

#define STB_PERLIN_IMPLEMENTATION
#include "stb_perlin.h"

#define GLM_ENABLE_EXPERIMENTAL
#include <iostream>

#include "glm/gtx/norm.hpp"

#include <ranges>

/* ========================== Chunk ========================== */

Chunk::Chunk(): voxels(SIZE) {}

Voxel Chunk::getVoxel(const int x, const int y, const int z) const {
    if (static_cast<unsigned>(x) >= WIDTH || static_cast<unsigned>(y) >= HEIGHT || static_cast<unsigned>(z) >= DEPTH)
        return 0;
    return voxels[x + y * WIDTH + z * WIDTH * HEIGHT];
}

void Chunk::setVoxel(const int x, const int y, const int z, const Voxel voxel) {
    if (static_cast<unsigned>(x) >= WIDTH || static_cast<unsigned>(y) >= HEIGHT || static_cast<unsigned>(z) >= DEPTH)
        return;
    voxels[x + y * WIDTH + z * WIDTH * HEIGHT] = voxel;
    isMeshDirty = true;
}

bool Chunk::isBlockActive(const int x, const int y, const int z) const {
    return isActive(getVoxel(x, y, z));
}

/* ========================== World ========================== */

World::World(const std::unordered_map<BlockType, uint32_t>& indices) : ubo(0), textureIndices(indices)
{
    glCreateBuffers(1, &ubo);
    glNamedBufferData(ubo, sizeof(WorldUBO), nullptr, GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_UNIFORM_BUFFER, 0, ubo);

    worldUBO = {
        .MVP = glm::mat4(1.f),
        .light = {69.0f, 420.0f, 69.0f, 30.f},
        .ambientData = {0.5f, 0.0f, 0.0f, 0.0f},
    };
}

bool World::isBlockActiveWorld(const int wx, const int wy, const int wz) const {
    if (static_cast<unsigned>(wy) >= Chunk::HEIGHT)
        return false;

    int cx = floorDiv(wx, Chunk::WIDTH);
    int cz = floorDiv(wz, Chunk::DEPTH);

    const auto it = chunks.find({cx, cz});
    if (it == chunks.end())
        return false;

    const int lx = wx - cx * Chunk::WIDTH;
    const int lz = wz - cz * Chunk::DEPTH;

    return it->second->isBlockActive(lx, wy, lz);
}

/* ===================== Chunk Streaming ===================== */

void World::updateChunks(const glm::vec3& playerPos, ThreadPool& threadPool) {
    const glm::ivec2 newChunk(
        floorDiv(static_cast<int>(playerPos.x), Chunk::WIDTH),
        floorDiv(static_cast<int>(playerPos.z), Chunk::DEPTH)
    );

    if (newChunk == playerChunk) return;

    playerChunk = newChunk;

    std::vector<glm::ivec2> loadQueue;
    std::vector<glm::ivec2> unloadQueue;

    {
        std::lock_guard lock(chunk_mutex);
        loadQueue.reserve(chunks.size());
        unloadQueue.reserve(chunks.size());
    }

    // -------- generate spiral load order --------static
    for (int r = 0; r <= CHUNK_RADIUS; ++r) {
        for (int dx = -r; dx <= r; ++dx) {
            for (int dz = -r; dz <= r; ++dz) {
                if (std::abs(dx) != r && std::abs(dz) != r)
                    continue;

                glm::ivec2 c = playerChunk + glm::ivec2(dx, dz);

                {
                    std::lock_guard lock(chunk_mutex);

                    if (!chunks.contains(c) && !generatingChunks.contains(c)) {
                        generatingChunks.insert(c);
                        loadQueue.push_back(c);
                    }
                }
            }
        }
    }

    // TODO: check if this is actually needed
    std::ranges::sort(loadQueue,
      [this](const glm::ivec2& a, const glm::ivec2& b) {
          return glm::distance2(glm::vec2(a), glm::vec2(playerChunk)) < glm::distance2(glm::vec2(b), glm::vec2(playerChunk));
    });

    // -------- unload far chunks --------
    {
        std::lock_guard lock(chunk_mutex);
        for (const auto& coord : chunks | std::views::keys) {
            if (glm::distance(glm::vec2(coord), glm::vec2(playerChunk)) > CHUNK_RADIUS + 1)
                unloadQueue.push_back(coord);
        }
    }

    // -------- enqueue chunk generation --------
    constexpr int MAX_CHUNKS_PER_UPDATE = 32;
    int spawned = 0;
    for (auto& c : loadQueue) {
        if (spawned++ >= MAX_CHUNKS_PER_UPDATE) break;

        threadPool.enqueueFireAndForget([this, c]
        {
            try {
                auto chunk = std::make_unique<Chunk>();

                generateTerrain(*chunk, c);

                const float dist = glm::distance(glm::vec2(c), glm::vec2(this->playerChunk));
                if (dist < 8.0f)
                    generateChunkMeshGreedy(*chunk, c);
                else if (dist < 16.0f)
                    generateChunkMeshLOD(*chunk, c, Chunk::LOD1);
                else
                    generateChunkMeshLOD(*chunk, c, Chunk::LOD2);

                {
                    std::lock_guard lock(chunk_mutex);
                    chunks[c] = std::move(chunk);
                    generatingChunks.erase(c);
                }
            }
            catch (...) {
                std::cout << "you fucked up generating chunk at: " << c.x << ", " << c.y << std::endl;
            }
        });
    }

    // -------- erase unloaded chunks (batched to prevent frame spikes) --------
    {
        std::lock_guard lock(chunk_mutex);
        int unloaded = 0;
        for (auto& c : unloadQueue) {
            if (generatingChunks.contains(c)) continue;
            if (unloaded++ >= MAX_CHUNKS_PER_UPDATE)
                break;
            chunks.erase(c);
        }
    }

    // Swap render snapshot (lock only during swap)
    {
        std::lock_guard lock(chunk_mutex);
        renderChunks.clear();
        renderChunks.reserve(chunks.size());
        for (auto& chunk : chunks | std::views::values)
            renderChunks.push_back(chunk.get());
    }
}

/* ===================== Terrain ===================== */

void World::generateTerrain(Chunk& chunk, const glm::ivec2& coord)
{
    constexpr int BASE_HEIGHT = Chunk::HEIGHT / 8;
    constexpr int MAX_Y = Chunk::HEIGHT - 3;
    constexpr int MIN_Y = 1;

    for (int x = 0; x < Chunk::WIDTH; ++x) {
        for (int z = 0; z < Chunk::DEPTH; ++z) {

            const int wx = coord.x * Chunk::WIDTH + x;
            const int wz = coord.y * Chunk::DEPTH + z;

            const int seed = 0; // time(nullptr);
            const float n = stb_perlin_noise3_seed( wx * 0.05f, 0.0f, wz * 0.05f, 0, 0, 0, seed);

            int height = BASE_HEIGHT + static_cast<int>(n * 25.0f);
            height = std::clamp(height, MIN_Y, MAX_Y);

            for (int y = MIN_Y; y <= height; ++y) {
                BlockType type =
                    (y == height)        ? BlockType::Grass :
                    (y >= height - 4)    ? BlockType::Dirt  :
                                           BlockType::Stone;

                chunk.setVoxel(x, y, z, packVoxelData(1, 255, 255, 255, static_cast<uint8_t>(type)));
            }
        }
    }

    chunk.isMeshDirty = true;
}

/* ===================== TRUE GREEDY MESHING ===================== */
void World::generateChunkMeshGreedy(Chunk& chunk, const glm::ivec2& coord) const {
    constexpr int W = Chunk::WIDTH;
    constexpr int H = Chunk::HEIGHT;
    constexpr int D = Chunk::DEPTH;

    chunk.cachedVertices.clear();
    chunk.cachedIndices.clear();

    // Reserve for greedy meshing (much less than face culling)

    chunk.cachedVertices.reserve(Chunk::SIZE * 6);  // Reduced from 12
    chunk.cachedIndices.reserve(Chunk::SIZE * 9);   // Reduced from 18

    // Cache solidity with padding (NO bounds checks)
    bool solid[W + 2][H + 2][D + 2] = {};
    for (int x = 0; x < W; ++x) {
        for (int y = 0; y < H; ++y) {
            for (int z = 0; z < D; ++z)
                solid[x + 1][y + 1][z + 1] = chunk.isBlockActive(x, y, z);
        }
    }

    // Block type lookup
    GLuint blockTypeToTex[256] = {};
    for (auto [blockType, index] : textureIndices)
        blockTypeToTex[static_cast<int>(blockType)] = index;

    // AO helper
    auto AO = [](const bool s1, const bool s2, const bool c) -> float {
        if (s1 && s2) return 0.25f;
        return 1.0f - 0.25f * static_cast<float>(s1 + s2 + c);
    };

    // Helper to emit a quad with packed vertices
    auto emitQuad = [&](const glm::vec3& v0, const glm::vec3& v1, const glm::vec3& v2, const glm::vec3& v3,
                        const glm::vec3& normal, float ao0, float ao1, float ao2, float ao3,
                        uint32_t tex) {
        uint32_t base = chunk.cachedVertices.size();
        chunk.cachedVertices.push_back(makeVertex(v0, normal, {0,0}, tex, ao0));
        chunk.cachedVertices.push_back(makeVertex(v1, normal, {0,1}, tex, ao1));
        chunk.cachedVertices.push_back(makeVertex(v2, normal, {1,1}, tex, ao2));
        chunk.cachedVertices.push_back(makeVertex(v3, normal, {1,0}, tex, ao3));
        chunk.cachedIndices.insert(chunk.cachedIndices.end(),
            {base, base+1, base+2, base, base+2, base+3});
    };

    // ========== GREEDY MESHING: +X Face ==========
    {
        std::vector<uint8_t> merged(H * D, 0);
        std::vector<uint32_t> texMask(H * D, 0);

        for (int x = 0; x < W; ++x) {
            // Build mask for this X slice
            for (int y = 0; y < H; ++y) {
                for (int z = 0; z < D; ++z) {
                    int idx = y * D + z;
                    merged[idx] = 0;
                    if (solid[x + 1][y + 1][z + 1] && !solid[x + 2][y + 1][z + 1]) {
                        uint8_t blockType = getBlockType(chunk.getVoxel(x, y, z));
                        if (blockType > 0) {
                            merged[idx] = blockType;
                            texMask[idx] = blockTypeToTex[blockType];
                        }
                    }
                }
            }

            // Greedy rectangle merging on the 2D mask (Y-Z plane)
            for (int y = 0; y < H; ++y) {
                for (int z = 0; z < D; ++z) {
                    int idx = y * D + z;
                    if (merged[idx] == 0) continue;

                    uint8_t blockType = merged[idx];
                    uint32_t tex = texMask[idx];

                    // Find max width in Z direction
                    int maxW = 1;
                    while (z + maxW < D && merged[y * D + z + maxW] == blockType &&
                           texMask[y * D + z + maxW] == tex)
                        maxW++;

                    // Find max height in Y direction
                    int maxH = 1;
                    bool canExpand = true;
                    while (y + maxH < H && canExpand) {
                        for (int w = 0; w < maxW; ++w) {
                            if (merged[(y + maxH) * D + z + w] != blockType ||
                                texMask[(y + maxH) * D + z + w] != tex) {
                                canExpand = false;
                                break;
                            }
                        }
                        if (canExpand) maxH++;
                    }

                    // Mark rectangle as used
                    for (int dy = 0; dy < maxH; ++dy) {
                        for (int dz = 0; dz < maxW; ++dz) {
                            merged[(y + dy) * D + z + dz] = 0;
                        }
                    }

                    // Compute AO for quad corners
                    float ao0 = AO(solid[x+2][y  ][z  ], solid[x+2][y+1][z  ], solid[x+2][y  ][z  ]);
                    float ao1 = AO(solid[x+2][y+maxH][z  ], solid[x+2][y+1][z  ], solid[x+2][y+maxH][z  ]);
                    float ao2 = AO(solid[x+2][y+maxH][z+maxW], solid[x+2][y+1][z+maxW], solid[x+2][y+maxH][z+maxW]);
                    float ao3 = AO(solid[x+2][y  ][z+maxW], solid[x+2][y+1][z+maxW], solid[x+2][y  ][z+maxW]);

                    // Emit merged quad
                    glm::vec3 v0{static_cast<float>(x+1), static_cast<float>(y), static_cast<float>(z)};
                    glm::vec3 v1{static_cast<float>(x+1), static_cast<float>(y+maxH), static_cast<float>(z)};
                    glm::vec3 v2{static_cast<float>(x+1), static_cast<float>(y+maxH), static_cast<float>(z+maxW)};
                    glm::vec3 v3{static_cast<float>(x+1), static_cast<float>(y), static_cast<float>(z+maxW)};
                    emitQuad(v0, v1, v2, v3, {1,0,0}, ao0, ao1, ao2, ao3, tex);
                }
            }
        }
    }

    // ========== GREEDY MESHING: -X Face ==========
    {
        std::vector<uint8_t> merged(H * D, 0);
        std::vector<uint32_t> texMask(H * D, 0);

        for (int x = 0; x < W; ++x) {
            for (int y = 0; y < H; ++y) {
                for (int z = 0; z < D; ++z) {
                    int idx = y * D + z;
                    merged[idx] = 0;
                    if (solid[x + 1][y + 1][z + 1] && !solid[x][y + 1][z + 1]) {
                        uint8_t blockType = getBlockType(chunk.getVoxel(x, y, z));
                        if (blockType > 0) {
                            merged[idx] = blockType;
                            texMask[idx] = blockTypeToTex[blockType];
                        }
                    }
                }
            }

            for (int y = 0; y < H; ++y) {
                for (int z = 0; z < D; ++z) {
                    int idx = y * D + z;
                    if (merged[idx] == 0) continue;

                    uint8_t blockType = merged[idx];
                    uint32_t tex = texMask[idx];

                    int maxW = 1;
                    while (z + maxW < D && merged[y * D + z + maxW] == blockType &&
                           texMask[y * D + z + maxW] == tex)
                        maxW++;

                    int maxH = 1;
                    bool canExpand = true;
                    while (y + maxH < H && canExpand) {
                        for (int w = 0; w < maxW; ++w) {
                            if (merged[(y + maxH) * D + z + w] != blockType ||
                                texMask[(y + maxH) * D + z + w] != tex) {
                                canExpand = false;
                                break;
                            }
                        }
                        if (canExpand) maxH++;
                    }

                    for (int dy = 0; dy < maxH; ++dy) {
                        for (int dz = 0; dz < maxW; ++dz) {
                            merged[(y + dy) * D + z + dz] = 0;
                        }
                    }

                    float ao0 = AO(solid[x][y  ][z  ], solid[x][y+1][z  ], solid[x][y  ][z  ]);
                    float ao1 = AO(solid[x][y  ][z+maxW], solid[x][y+1][z+maxW], solid[x][y  ][z+maxW]);
                    float ao2 = AO(solid[x][y+maxH][z+maxW], solid[x][y+1][z+maxW], solid[x][y+maxH][z+maxW]);
                    float ao3 = AO(solid[x][y+maxH][z  ], solid[x][y+1][z  ], solid[x][y+maxH][z  ]);

                    glm::vec3 v0{static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)};
                    glm::vec3 v1{static_cast<float>(x), static_cast<float>(y), static_cast<float>(z+maxW)};
                    glm::vec3 v2{static_cast<float>(x), static_cast<float>(y+maxH), static_cast<float>(z+maxW)};
                    glm::vec3 v3{static_cast<float>(x), static_cast<float>(y+maxH), static_cast<float>(z)};
                    emitQuad(v0, v1, v2, v3, {-1,0,0}, ao0, ao1, ao2, ao3, tex);
                }
            }
        }
    }

    // ========== GREEDY MESHING: +Y Face ==========
    {
        std::vector<uint8_t> merged(W * D, 0);
        std::vector<uint32_t> texMask(W * D, 0);

        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                for (int z = 0; z < D; ++z) {
                    int idx = x * D + z;
                    merged[idx] = 0;
                    if (solid[x + 1][y + 1][z + 1] && !solid[x + 1][y + 2][z + 1]) {
                        uint8_t blockType = getBlockType(chunk.getVoxel(x, y, z));
                        if (blockType > 0) {
                            merged[idx] = blockType;
                            texMask[idx] = blockTypeToTex[blockType];
                        }
                    }
                }
            }

            for (int x = 0; x < W; ++x) {
                for (int z = 0; z < D; ++z) {
                    int idx = x * D + z;
                    if (merged[idx] == 0) continue;

                    uint8_t blockType = merged[idx];
                    uint32_t tex = texMask[idx];

                    int maxW = 1;
                    while (z + maxW < D && merged[x * D + z + maxW] == blockType &&
                           texMask[x * D + z + maxW] == tex)
                        maxW++;

                    int maxH = 1;
                    bool canExpand = true;
                    while (x + maxH < W && canExpand) {
                        for (int w = 0; w < maxW; ++w) {
                            if (merged[(x + maxH) * D + z + w] != blockType ||
                                texMask[(x + maxH) * D + z + w] != tex) {
                                canExpand = false;
                                break;
                            }
                        }
                        if (canExpand) maxH++;
                    }

                    for (int dy = 0; dy < maxH; ++dy) {
                        for (int dz = 0; dz < maxW; ++dz) {
                            merged[(x + dy) * D + z + dz] = 0;
                        }
                    }

                    float ao0 = AO(solid[x  ][y+2][z  ], solid[x+1][y+2][z  ], solid[x  ][y+2][z  ]);
                    float ao1 = AO(solid[x  ][y+2][z+maxW], solid[x+1][y+2][z+maxW], solid[x  ][y+2][z+maxW]);
                    float ao2 = AO(solid[x+maxH][y+2][z+maxW], solid[x+1][y+2][z+maxW], solid[x+maxH][y+2][z+maxW]);
                    float ao3 = AO(solid[x+maxH][y+2][z  ], solid[x+1][y+2][z  ], solid[x+maxH][y+2][z  ]);

                    glm::vec3 v0{static_cast<float>(x), static_cast<float>(y+1), static_cast<float>(z)};
                    glm::vec3 v1{static_cast<float>(x), static_cast<float>(y+1), static_cast<float>(z+maxW)};
                    glm::vec3 v2{static_cast<float>(x+maxH), static_cast<float>(y+1), static_cast<float>(z+maxW)};
                    glm::vec3 v3{static_cast<float>(x+maxH), static_cast<float>(y+1), static_cast<float>(z)};
                    emitQuad(v0, v1, v2, v3, {0,1,0}, ao0, ao1, ao2, ao3, tex);
                }
            }
        }
    }

    // ========== GREEDY MESHING: -Y Face ==========
    {
        std::vector<uint8_t> merged(W * D, 0);
        std::vector<uint32_t> texMask(W * D, 0);

        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                for (int z = 0; z < D; ++z) {
                    int idx = x * D + z;
                    merged[idx] = 0;
                    if (solid[x + 1][y + 1][z + 1] && !solid[x + 1][y][z + 1]) {
                        uint8_t blockType = getBlockType(chunk.getVoxel(x, y, z));
                        if (blockType > 0) {
                            merged[idx] = blockType;
                            texMask[idx] = blockTypeToTex[blockType];
                        }
                    }
                }
            }

            for (int x = 0; x < W; ++x) {
                for (int z = 0; z < D; ++z) {
                    int idx = x * D + z;
                    if (merged[idx] == 0) continue;

                    uint8_t blockType = merged[idx];
                    uint32_t tex = texMask[idx];

                    int maxW = 1;
                    while (z + maxW < D && merged[x * D + z + maxW] == blockType &&
                           texMask[x * D + z + maxW] == tex)
                        maxW++;

                    int maxH = 1;
                    bool canExpand = true;
                    while (x + maxH < W && canExpand) {
                        for (int w = 0; w < maxW; ++w) {
                            if (merged[(x + maxH) * D + z + w] != blockType ||
                                texMask[(x + maxH) * D + z + w] != tex) {
                                canExpand = false;
                                break;
                            }
                        }
                        if (canExpand) maxH++;
                    }

                    for (int dy = 0; dy < maxH; ++dy) {
                        for (int dz = 0; dz < maxW; ++dz) {
                            merged[(x + dy) * D + z + dz] = 0;
                        }
                    }

                    float ao0 = AO(solid[x  ][y][z  ], solid[x+1][y][z  ], solid[x  ][y][z  ]);
                    float ao1 = AO(solid[x+maxH][y][z  ], solid[x+1][y][z  ], solid[x+maxH][y][z  ]);
                    float ao2 = AO(solid[x+maxH][y][z+maxW], solid[x+1][y][z+maxW], solid[x+maxH][y][z+maxW]);
                    float ao3 = AO(solid[x  ][y][z+maxW], solid[x+1][y][z+maxW], solid[x  ][y][z+maxW]);

                    glm::vec3 v0{static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)};
                    glm::vec3 v1{static_cast<float>(x+maxH), static_cast<float>(y), static_cast<float>(z)};
                    glm::vec3 v2{static_cast<float>(x+maxH), static_cast<float>(y), static_cast<float>(z+maxW)};
                    glm::vec3 v3{static_cast<float>(x), static_cast<float>(y), static_cast<float>(z+maxW)};
                    emitQuad(v0, v1, v2, v3, {0,-1,0}, ao0, ao1, ao2, ao3, tex);
                }
            }
        }
    }

    // ========== GREEDY MESHING: +Z Face ==========
    {
        std::vector<uint8_t> merged(W * H, 0);
        std::vector<uint32_t> texMask(W * H, 0);

        for (int z = 0; z < D; ++z) {
            for (int x = 0; x < W; ++x) {
                for (int y = 0; y < H; ++y) {
                    int idx = x * H + y;
                    merged[idx] = 0;
                    if (solid[x + 1][y + 1][z + 1] && !solid[x + 1][y + 1][z + 2]) {
                        uint8_t blockType = getBlockType(chunk.getVoxel(x, y, z));
                        if (blockType > 0) {
                            merged[idx] = blockType;
                            texMask[idx] = blockTypeToTex[blockType];
                        }
                    }
                }
            }

            for (int x = 0; x < W; ++x) {
                for (int y = 0; y < H; ++y) {
                    int idx = x * H + y;
                    if (merged[idx] == 0) continue;

                    uint8_t blockType = merged[idx];
                    uint32_t tex = texMask[idx];

                    int maxH = 1;
                    while (y + maxH < H && merged[x * H + y + maxH] == blockType &&
                           texMask[x * H + y + maxH] == tex)
                        maxH++;

                    int maxW = 1;
                    bool canExpand = true;
                    while (x + maxW < W && canExpand) {
                        for (int h = 0; h < maxH; ++h) {
                            if (merged[(x + maxW) * H + y + h] != blockType ||
                                texMask[(x + maxW) * H + y + h] != tex) {
                                canExpand = false;
                                break;
                            }
                        }
                        if (canExpand) maxW++;
                    }

                    for (int dx = 0; dx < maxW; ++dx) {
                        for (int dy = 0; dy < maxH; ++dy) {
                            merged[(x + dx) * H + y + dy] = 0;
                        }
                    }

                    float ao0 = AO(solid[x  ][y+1][z+2], solid[x+1][y  ][z+2], solid[x  ][y  ][z+2]);
                    float ao1 = AO(solid[x+maxW][y+1][z+2], solid[x+1][y  ][z+2], solid[x+maxW][y  ][z+2]);
                    float ao2 = AO(solid[x+maxW][y+maxH][z+2], solid[x+1][y+maxH][z+2], solid[x+maxW][y+1][z+2]);
                    float ao3 = AO(solid[x  ][y+maxH][z+2], solid[x+1][y+maxH][z+2], solid[x  ][y+1][z+2]);

                    glm::vec3 v0{static_cast<float>(x), static_cast<float>(y), static_cast<float>(z+1)};
                    glm::vec3 v1{static_cast<float>(x+maxW), static_cast<float>(y), static_cast<float>(z+1)};
                    glm::vec3 v2{static_cast<float>(x+maxW), static_cast<float>(y+maxH), static_cast<float>(z+1)};
                    glm::vec3 v3{static_cast<float>(x), static_cast<float>(y+maxH), static_cast<float>(z+1)};
                    emitQuad(v0, v1, v2, v3, {0,0,1}, ao0, ao1, ao2, ao3, tex);
                }
            }
        }
    }

    // ========== GREEDY MESHING: -Z Face ==========
    {
        std::vector<uint8_t> merged(W * H, 0);
        std::vector<uint32_t> texMask(W * H, 0);

        for (int z = 0; z < D; ++z) {
            for (int x = 0; x < W; ++x) {
                for (int y = 0; y < H; ++y) {
                    int idx = x * H + y;
                    merged[idx] = 0;
                    if (solid[x + 1][y + 1][z + 1] && !solid[x + 1][y + 1][z]) {
                        uint8_t blockType = getBlockType(chunk.getVoxel(x, y, z));
                        if (blockType > 0) {
                            merged[idx] = blockType;
                            texMask[idx] = blockTypeToTex[blockType];
                        }
                    }
                }
            }

            for (int x = 0; x < W; ++x) {
                for (int y = 0; y < H; ++y) {
                    int idx = x * H + y;
                    if (merged[idx] == 0) continue;

                    uint8_t blockType = merged[idx];
                    uint32_t tex = texMask[idx];

                    int maxH = 1;
                    while (y + maxH < H && merged[x * H + y + maxH] == blockType &&
                           texMask[x * H + y + maxH] == tex)
                        maxH++;

                    int maxW = 1;
                    bool canExpand = true;
                    while (x + maxW < W && canExpand) {
                        for (int h = 0; h < maxH; ++h) {
                            if (merged[(x + maxW) * H + y + h] != blockType ||
                                texMask[(x + maxW) * H + y + h] != tex) {
                                canExpand = false;
                                break;
                            }
                        }
                        if (canExpand) maxW++;
                    }

                    for (int dx = 0; dx < maxW; ++dx) {
                        for (int dy = 0; dy < maxH; ++dy) {
                            merged[(x + dx) * H + y + dy] = 0;
                        }
                    }

                    float ao0 = AO(solid[x  ][y+1][z], solid[x+1][y  ][z], solid[x  ][y  ][z]);
                    float ao1 = AO(solid[x  ][y+maxH][z], solid[x+1][y+maxH][z], solid[x  ][y+1][z]);
                    float ao2 = AO(solid[x+maxW][y+maxH][z], solid[x+1][y+maxH][z], solid[x+maxW][y+1][z]);
                    float ao3 = AO(solid[x+maxW][y+1][z], solid[x+1][y  ][z], solid[x+maxW][y  ][z]);

                    glm::vec3 v0{static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)};
                    glm::vec3 v1{static_cast<float>(x), static_cast<float>(y+maxH), static_cast<float>(z)};
                    glm::vec3 v2{static_cast<float>(x+maxW), static_cast<float>(y+maxH), static_cast<float>(z)};
                    glm::vec3 v3{static_cast<float>(x+maxW), static_cast<float>(y), static_cast<float>(z)};
                    emitQuad(v0, v1, v2, v3, {0,0,-1}, ao0, ao1, ao2, ao3, tex);
                }
            }
        }
    }

    // Apply chunk offset
    glm::vec3 offset(coord.x * W, 0.0f, coord.y * D);
    for (auto& v : chunk.cachedVertices)
        v.position += offset;

    chunk.isMeshDirty = false;
}

/* ===================== LOD MESH GENERATION ===================== */
void World::generateChunkMeshLOD(Chunk& chunk, const glm::ivec2& coord, Chunk::LODLevel lodLevel) const {
    constexpr int W = Chunk::WIDTH;
    constexpr int H = Chunk::HEIGHT;
    constexpr int D = Chunk::DEPTH;

    chunk.cachedVertices.clear();
    chunk.cachedIndices.clear();

    // Determine sampling step based on LOD level
    int stepX = 1, stepY = 1, stepZ = 1;
    switch (lodLevel) {
        case Chunk::LOD0: stepX = stepY = stepZ = 1; break;  // Sample every voxel
        case Chunk::LOD1: stepX = stepY = stepZ = 2; break;  // Sample every 2nd voxel
        case Chunk::LOD2: stepX = stepY = stepZ = 4; break;  // Sample every 4th voxel
    }

    // Reduced reserve based on LOD (fewer vertices for lower LODs)
    int reserveFactor = 1 << (lodLevel * 2);  // 1, 4, 16 for LOD0, LOD1, LOD2
    chunk.cachedVertices.reserve(W * H * D * 6 / reserveFactor);
    chunk.cachedIndices.reserve(W * H * D * 9 / reserveFactor);

    // Cache solidity with padding
    int maxX = (W + stepX) / stepX + 2;
    int maxY = (H + stepY) / stepY + 2;
    int maxZ = (D + stepZ) / stepZ + 2;
    
    std::vector<bool> solid(maxX * maxY * maxZ, false);
    
    auto isSolid = [&](int x, int y, int z) -> bool {
        if (x < 0 || y < 0 || z < 0 || x >= W || y >= H || z >= D)
            return false;
        return chunk.isBlockActive(x, y, z);
    };

    // Fill solidity map with stride
    for (int x = 0; x < W; x += stepX) {
        for (int y = 0; y < H; y += stepY) {
            for (int z = 0; z < D; z += stepZ) {
                int idx = (x / stepX + 1) + (y / stepY + 1) * maxX + (z / stepZ + 1) * maxX * maxY;
                solid[idx] = isSolid(x, y, z);
            }
        }
    }

    // Block type lookup
    GLuint blockTypeToTex[256] = {};
    for (auto [blockType, index] : textureIndices)
        blockTypeToTex[static_cast<int>(blockType)] = index;

    // // AO helper
    // auto AO = [](const bool s1, const bool s2, const bool c) -> float {
    //     if (s1 && s2) return 0.25f;
    //     return 1.0f - 0.25f * static_cast<float>(s1 + s2 + c);
    // };

    // Helper to emit quad
    auto emitQuad = [&](const glm::vec3& v0, const glm::vec3& v1, const glm::vec3& v2, const glm::vec3& v3,
                        const glm::vec3& normal, float ao0, float ao1, float ao2, float ao3,
                        uint32_t tex) {
        uint32_t base = chunk.cachedVertices.size();
        chunk.cachedVertices.push_back(makeVertex(v0, normal, {0,0}, tex, ao0));
        chunk.cachedVertices.push_back(makeVertex(v1, normal, {0,1}, tex, ao1));
        chunk.cachedVertices.push_back(makeVertex(v2, normal, {1,1}, tex, ao2));
        chunk.cachedVertices.push_back(makeVertex(v3, normal, {1,0}, tex, ao3));
        chunk.cachedIndices.insert(chunk.cachedIndices.end(),
            {base, base+1, base+2, base, base+2, base+3});
    };

    // Simplified mesh generation for LOD (skip greedy merging, just emit faces)
    for (int x = 0; x < W; x += stepX) {
        for (int y = 0; y < H; y += stepY) {
            for (int z = 0; z < D; z += stepZ) {
                if (!isSolid(x, y, z))
                    continue;

                const uint8_t blockType = getBlockType(chunk.getVoxel(x, y, z));
                if (blockType == 0) continue;

                const uint32_t tex = blockTypeToTex[blockType];
                const auto fx = static_cast<float>(x);
                const auto fy = static_cast<float>(y);
                const auto fz = static_cast<float>(z);
                const auto fstepX = static_cast<float>(stepX);
                const auto fstepY = static_cast<float>(stepY);
                const auto fstepZ = static_cast<float>(stepZ);

                // Check visible faces (only emit if adjacent is empty)
                if (!isSolid(x + stepX, y, z)) {
                    float ao0 = 0.8f; float ao1 = 0.8f; float ao2 = 0.8f; float ao3 = 0.8f;
                    glm::vec3 v0{fx+fstepX, fy, fz};
                    glm::vec3 v1{fx+fstepX, fy+fstepY, fz};
                    glm::vec3 v2{fx+fstepX, fy+fstepY, fz+fstepZ};
                    glm::vec3 v3{fx+fstepX, fy, fz+fstepZ};
                    emitQuad(v0, v1, v2, v3, {1,0,0}, ao0, ao1, ao2, ao3, tex);
                }

                if (!isSolid(x - stepX, y, z)) {
                    float ao0 = 0.8f; float ao1 = 0.8f; float ao2 = 0.8f; float ao3 = 0.8f;
                    glm::vec3 v0{fx, fy, fz};
                    glm::vec3 v1{fx, fy, fz+fstepZ};
                    glm::vec3 v2{fx, fy+fstepY, fz+fstepZ};
                    glm::vec3 v3{fx, fy+fstepY, fz};
                    emitQuad(v0, v1, v2, v3, {-1,0,0}, ao0, ao1, ao2, ao3, tex);
                }

                if (!isSolid(x, y + stepY, z)) {
                    float ao0 = 0.9f; float ao1 = 0.9f; float ao2 = 0.9f; float ao3 = 0.9f;
                    glm::vec3 v0{fx, fy+fstepY, fz};
                    glm::vec3 v1{fx, fy+fstepY, fz+fstepZ};
                    glm::vec3 v2{fx+fstepX, fy+fstepY, fz+fstepZ};
                    glm::vec3 v3{fx+fstepX, fy+fstepY, fz};
                    emitQuad(v0, v1, v2, v3, {0,1,0}, ao0, ao1, ao2, ao3, tex);
                }

                if (!isSolid(x, y - stepY, z)) {
                    float ao0 = 0.7f; float ao1 = 0.7f; float ao2 = 0.7f; float ao3 = 0.7f;
                    glm::vec3 v0{fx, fy, fz};
                    glm::vec3 v1{fx+fstepX, fy, fz};
                    glm::vec3 v2{fx+fstepX, fy, fz+fstepZ};
                    glm::vec3 v3{fx, fy, fz+fstepZ};
                    emitQuad(v0, v1, v2, v3, {0,-1,0}, ao0, ao1, ao2, ao3, tex);
                }

                if (!isSolid(x, y, z + stepZ)) {
                    float ao0 = 0.85f; float ao1 = 0.85f; float ao2 = 0.85f; float ao3 = 0.85f;
                    glm::vec3 v0{fx, fy, fz+fstepZ};
                    glm::vec3 v1{fx+fstepX, fy, fz+fstepZ};
                    glm::vec3 v2{fx+fstepX, fy+fstepY, fz+fstepZ};
                    glm::vec3 v3{fx, fy+fstepY, fz+fstepZ};
                    emitQuad(v0, v1, v2, v3, {0,0,1}, ao0, ao1, ao2, ao3, tex);
                }

                if (!isSolid(x, y, z - stepZ)) {
                    float ao0 = 0.75f; float ao1 = 0.75f; float ao2 = 0.75f; float ao3 = 0.75f;
                    glm::vec3 v0{fx, fy, fz};
                    glm::vec3 v1{fx, fy+fstepY, fz};
                    glm::vec3 v2{fx+fstepX, fy+fstepY, fz};
                    glm::vec3 v3{fx+fstepX, fy, fz};
                    emitQuad(v0, v1, v2, v3, {0,0,-1}, ao0, ao1, ao2, ao3, tex);
                }
            }
        }
    }

    // Apply chunk offset
    glm::vec3 offset(coord.x * W, 0.0f, coord.y * D);
    for (auto& v : chunk.cachedVertices)
        v.position += offset;

    chunk.lodLevel = lodLevel;
    chunk.isMeshDirty = false;
}

// void World::applyGreedy2D(
//     const std::vector<int>& mask,
//     const int width, const int height, const int axis,
//     const int slice,
//     Chunk& chunk, const glm::ivec2& coord
// ) const {
//
//     std::vector<uint8_t> used(width * height, 0);
//
//     for (int y = 0; y < height; ++y) {
//         for (int x = 0; x < width; ++x) {
//             const int idx = x + y * width;
//             if (used[idx] || mask[idx] < 0)
//                 continue;
//
//             int blockType = mask[idx];
//
//             // Find width
//             int w = 1;
//             while (x + w < width && mask[idx + w] == blockType && !used[idx + w])
//                 ++w;
//
//             // Find height
//             int h = 1;
//             bool stop = false;
//             while (y + h < height && !stop) {
//                 for (int k = 0; k < w; ++k) {
//                     if (mask[(x + k) + (y + h) * width] != blockType ||
//                         used[(x + k) + (y + h) * width]) {
//                         stop = true;
//                         break;
//                     }
//                 }
//                 if (!stop) ++h;
//             }
//
//             // Mark used
//             for (int dy = 0; dy < h; ++dy)
//                 for (int dx = 0; dx < w; ++dx)
//                     used[(x + dx) + (y + dy) * width] = 1;
//
//             // Create quad vertices
//             glm::vec3 p[4];
//             switch (axis) {
//                 case 0:
//                     p[0] = {slice, y, x};
//                     p[1] = {slice, y, x + w};
//                     p[2] = {slice, y + h, x + w};
//                     p[3] = {slice, y + h, x};
//                     break;
//                 case 1:
//                     p[0] = {x, slice, y};
//                     p[1] = {x + w, slice, y};
//                     p[2] = {x + w, slice, y + h};
//                     p[3] = {x, slice, y + h};
//                     break;
//                 case 2:
//                     p[0] = {x, y, slice};
//                     p[1] = {x + w, y, slice};
//                     p[2] = {x + w, y + h, slice};
//                     p[3] = {x, y + h, slice};
//                     break;
//                 default: throw std::logic_error("unrecognized axis");
//             }
//
//             // Offset by chunk position
//             const glm::vec3 chunkOffset(coord.x * Chunk::WIDTH, 0, coord.y * Chunk::DEPTH);
//             for (auto& v : p) v += chunkOffset;
//
//             const uint32_t texIndex = textureIndices.at(static_cast<BlockType>(blockType));
//             auto base = static_cast<uint32_t>(chunk.cachedVertices.size());
//
//             chunk.cachedVertices.push_back({p[0], {0, 0}, texIndex});
//             chunk.cachedVertices.push_back({p[1], {1, 0}, texIndex});
//             chunk.cachedVertices.push_back({p[2], {1, 1}, texIndex});
//             chunk.cachedVertices.push_back({p[3], {0, 1}, texIndex});
//
//             // Determine if face is back-facing (flip winding)
//             bool flipFace = false;
//             if (axis == 0) flipFace = (mask[idx] != getBlockType(chunk.getVoxel(slice, y, x)));
//             if (axis == 1) flipFace = (mask[idx] != getBlockType(chunk.getVoxel(x, slice, y)));
//             if (axis == 2) flipFace = (mask[idx] != getBlockType(chunk.getVoxel(x, y, slice)));
//
//             if (flipFace) {
//                 chunk.cachedIndices.insert(chunk.cachedIndices.end(),
//                     {base, base + 2, base + 1, base, base + 3, base + 2});
//             } else {
//                 chunk.cachedIndices.insert(chunk.cachedIndices.end(),
//                     {base, base + 1, base + 2, base, base + 2, base + 3});
//             }
//         }
//     }
// }
