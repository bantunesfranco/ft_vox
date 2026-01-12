#include "World.hpp"
#include "App.hpp"

#define STB_PERLIN_IMPLEMENTATION
#include "stb_perlin.h"

#define GLM_ENABLE_EXPERIMENTAL
#include <iostream>

#include "glm/gtx/norm.hpp"

#include <ranges>

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

    return it->second.isBlockActive(lx, wy, lz);
}

bool World::isBoxInFrustum(const glm::vec3& min, const glm::vec3& max) const
{
    return frustum.isBoxInFrustum(min, max);
}

void World::updateFrustum(const glm::mat4& proj_mat, const glm::mat4& view_mat)
{
    return frustum.updateFrustum(proj_mat, view_mat);
}

/* ===================== Chunk Streaming ===================== */

void World::updateChunks(const glm::vec3& playerPos, ThreadPool& threadPool) {
    const glm::ivec2 newChunk(
        floorDiv(static_cast<int>(playerPos.x), Chunk::WIDTH),
        floorDiv(static_cast<int>(playerPos.z), Chunk::DEPTH)
    );

    playerChunk = newChunk;

    std::vector<glm::ivec2> loadQueue;
    std::vector<glm::ivec2> unloadQueue;
    loadQueue.reserve(CHUNK_DIAMETER * CHUNK_DIAMETER);
    unloadQueue.reserve(chunks.size());

    // -------- generate spiral load order --------
    for (int r = 0; r <= CHUNK_RADIUS; ++r) {
        for (int dx = -r; dx <= r; ++dx) {
            for (int dz = -r; dz <= r; ++dz) {
                if (std::abs(dx) != r && std::abs(dz) != r)
                    continue;

                if (glm::ivec2 c = playerChunk + glm::ivec2(dx, dz); !chunks.contains(c))
                    loadQueue.push_back(c);
            }
        }
    }

    // -------- unload far chunks --------
    {
        std::lock_guard lock(chunk_mutex);
        for (const auto& coord : chunks | std::views::keys) {
            if (glm::distance(glm::vec2(coord), glm::vec2(playerChunk)) > CHUNK_RADIUS + 1)
                unloadQueue.push_back(coord);
        }
    }

    // -------- enqueue chunk generation --------
    int spawned = 0;
    for (auto& c : loadQueue) {
        if (constexpr int MAX_CHUNKS_PER_UPDATE = 32; spawned++ >= MAX_CHUNKS_PER_UPDATE)
            break;

        threadPool.enqueue([this, c]
        {
            Chunk chunk;
            chunk.worldMin = {c.x * Chunk::WIDTH, 0.0f, c.y * Chunk::DEPTH};
            chunk.worldMax = chunk.worldMin + glm::vec3(Chunk::WIDTH,Chunk::HEIGHT,Chunk::DEPTH);
            chunk.worldCenter = (chunk.worldMin + chunk.worldMax) * 0.5f;

            generateTerrain(chunk, c);
            generateChunkGreedyMesh(chunk, c);

            {
                std::lock_guard lock(chunk_mutex);
                chunks.emplace(c, std::move(chunk));
            }
        });
    }

    // -------- erase unloaded chunks --------
    {
        std::lock_guard lock(chunk_mutex);
        for (auto& c : unloadQueue)
            chunks.erase(c);
    }
}

/* ===================== Terrain ===================== */

void World::generateTerrain(Chunk& chunk, const glm::ivec2& coord)
{
    constexpr int MIN_Y = 1;
    constexpr int MAX_Y = Chunk::HEIGHT - 1;
    constexpr int BASE_HEIGHT = Chunk::HEIGHT / 8;
    constexpr int SNOW_HEIGHT = Chunk::HEIGHT / 4; // above this, snow

    const int seed = 1337; // deterministic seed

    // Precompute 2D noise for height & biome
    int heightMap[Chunk::WIDTH][Chunk::DEPTH];
    const int baseWX = coord.x * Chunk::WIDTH;
    const int baseWZ = coord.y * Chunk::DEPTH;
    for (int x = 0; x < Chunk::WIDTH; ++x) {
        for (int z = 0; z < Chunk::DEPTH; ++z) {
            const int wx = baseWX + x;
            const int wz = baseWZ + z;

            const float largeHill = stb_perlin_noise3_seed(wx * 0.005f, 0.0f, wz * 0.005f, 0,0,0, seed) * 96.0f; // mountains
            const float smallBump = stb_perlin_noise3_seed(wx * 0.05f, 0.0f, wz * 0.05f, 0,0,0, seed) * 16.0f;   // hills & bumps

            int height = BASE_HEIGHT + static_cast<int>(largeHill + smallBump);
            heightMap[x][z] = std::clamp(height, MIN_Y, MAX_Y);
        }
    }

    static const uint32_t STONE = packVoxelData(1,255,255,255,static_cast<uint8_t>(BlockType::Stone));
    static const uint32_t DIRT  = packVoxelData(1,255,255,255,static_cast<uint8_t>(BlockType::Dirt));
    static const uint32_t GRASS = packVoxelData(1,255,255,255,static_cast<uint8_t>(BlockType::Grass));
    static const uint32_t SNOW  = packVoxelData(1,255,255,255,static_cast<uint8_t>(BlockType::Snow));

    for (int x = 0; x < Chunk::WIDTH; ++x) {
        for (int z = 0; z < Chunk::DEPTH; ++z) {
            const int height = heightMap[x][z];

            // stone
            int y = MIN_Y;
            for (; y < height - 4; ++y)
                chunk.setVoxel(x, y, z, STONE);

            // dirt
            for (; y < height; ++y)
                chunk.setVoxel(x, y, z, DIRT);

            // top block
            chunk.setVoxel(x, height, z, (height >= SNOW_HEIGHT) ? SNOW : GRASS);
        }
    }

    chunk.isMeshDirty = true;
}

// void World::generateTerrain(Chunk& chunk, const glm::ivec2& coord)
// {
//     constexpr int BASE_HEIGHT = Chunk::HEIGHT / 4;
//     constexpr int MAX_Y = Chunk::HEIGHT - 3;
//     constexpr int MIN_Y = 1;
//
//     const int seed = 0;
//
//     // Precompute 2D noise for height & biome
//     float heightMap[Chunk::WIDTH][Chunk::DEPTH];
//
//     for (int x = 0; x < Chunk::WIDTH; ++x) {
//         for (int z = 0; z < Chunk::DEPTH; ++z) {
//             const int wx = coord.x * Chunk::WIDTH + x;
//             const int wz = coord.y * Chunk::DEPTH + z;
//
//             float largeHill = stb_perlin_noise3_seed(wx * 0.005f, 0.0f, wz * 0.005f, 0,0,0, seed) * 80.0f; // mountains
//             float smallBump = stb_perlin_noise3_seed(wx * 0.05f, 0.0f, wz * 0.05f, 0,0,0, seed) * 10.0f;   // hills & bumps
//
//             int height = BASE_HEIGHT + static_cast<int>(largeHill + smallBump);
//             heightMap[x][z] = std::clamp(height, MIN_Y, MAX_Y);
//         }
//     }
//
//     // Generate voxels
//     for (int x = 0; x < Chunk::WIDTH; ++x) {
//         for (int z = 0; z < Chunk::DEPTH; ++z) {
//             int topY = static_cast<int>(heightMap[x][z]);
//             for (int y = MIN_Y; y <= topY; ++y) {
//                 BlockType blockType = BlockType::Stone;
//
//                 // Top layers
//                 if (y == topY) {
//                     if (y > Chunk::HEIGHT * 0.75f) blockType = BlockType::Snow; // snow only high
//                     else blockType = BlockType::Grass; // most of the map
//                 }
//                 else if (y >= topY - 4) {
//                     blockType = BlockType::Dirt;
//                 }
//
//                 // Caves only below surface
//                 // if (y < topY - 2) {
//                 //     float caveNoise = stb_perlin_noise3_seed(
//                 //         (coord.x * Chunk::WIDTH + x) * 0.1f, y * 0.1f,
//                 //         (coord.y * Chunk::DEPTH + z) * 0.1f,
//                 //         0,0,0, seed);
//                 //     if (caveNoise > 0.55f) continue;
//                 // }
//                 //
//                 // // Ores
//                 // if (y < 40) {
//                 //     float oreNoise = stb_perlin_noise3_seed(
//                 //         (coord.x * Chunk::WIDTH + x) * 0.2f, y * 0.2f,
//                 //         (coord.y * Chunk::DEPTH + z) * 0.2f,
//                 //         0,0,0, seed);
//                 //     if (oreNoise > 0.75f) blockType = BlockType::IronOre;
//                 // }
//
//                 chunk.setVoxel(x, y, z, packVoxelData(1, 255, 255, 255, static_cast<uint8_t>(blockType)));
//             }
//         }
//     }
//
//     chunk.isMeshDirty = true;
// }

void World::generateChunkGreedyMesh(Chunk& chunk, const glm::ivec2& coord) const
{
    GLuint blockTypeToTex[256] = {};
    for (auto [blockType, index] : textureIndices)
        blockTypeToTex[static_cast<int>(blockType)] = index;

    constexpr int W = Chunk::WIDTH;
    constexpr int H = Chunk::HEIGHT;
    constexpr int D = Chunk::DEPTH;

    chunk.cachedVertices.clear();
    chunk.cachedIndices.clear();
    chunk.cachedVertices.reserve(Chunk::SIZE * 4);
    chunk.cachedIndices.reserve(Chunk::SIZE * 6);

    // Cache solidity with padding
    bool solid[W + 2][H + 2][D + 2] = {};
    uint8_t blockTypes[W][H][D] = {};

    for (int x = 0; x < W; ++x)
        for (int y = 0; y < H; ++y)
            for (int z = 0; z < D; ++z) {
                solid[x + 1][y + 1][z + 1] = chunk.isBlockActive(x, y, z);
                blockTypes[x][y][z] = getBlockType(chunk.getVoxel(x, y, z));
            }

    // AO helper
    auto AO = [](const bool s1, const bool s2, const bool c) -> float {
        if (s1 && s2) return 0.25f;
        return 1.0f - 0.25f * static_cast<float>(s1 + s2 + c);
                };

    // Greedy meshing
    for (int axis = 0; axis < 3; ++axis) {
        for (int dir = -1; dir <= 1; dir += 2) {
            const int u = (axis + 1) % 3;
            const int v = (axis + 2) % 3;

            int dims[3] = { W, H, D };
            int x[3] = { 0, 0, 0 };
            int q[3] = { 0, 0, 0 };
            q[axis] = dir;

            struct MaskEntry {
                bool solid;
                uint8_t blockType;
            };

            MaskEntry mask[std::max({ W, H, D }) * std::max({ W, H, D })];

            for (x[axis] = -1; x[axis] < dims[axis];) {
                int n = 0;

                // Build mask
                for (x[v] = 0; x[v] < dims[v]; ++x[v]) {
                    for (x[u] = 0; x[u] < dims[u]; ++x[u]) {
                        int cx = x[0], cy = x[1], cz = x[2];
                        int nx = cx + q[0], ny = cy + q[1], nz = cz + q[2];

                        bool currentValid = cx >= 0 && cx < W && cy >= 0 && cy < H && cz >= 0 && cz < D;
                        bool neighborValid = nx >= 0 && nx < W && ny >= 0 && ny < H && nz >= 0 && nz < D;
                        bool currentSolid = currentValid && solid[cx + 1][cy + 1][cz + 1];
                        bool neighborSolid = neighborValid && solid[nx + 1][ny + 1][nz + 1];

                        uint8_t bt = currentValid ? blockTypes[cx][cy][cz] : 0;

                        mask[n].solid = currentValid && currentSolid && !neighborSolid && bt != 0;
                        mask[n].blockType = bt;
                        ++n;
                    }
                }

                n = 0;

                // Greedy merge
                for (int j = 0; j < dims[v]; ++j) {
                    for (int i = 0; i < dims[u];) {
                        if (mask[n].solid) {
                            int w = 1;
                            while (i + w < dims[u] &&
                                   mask[n + w].solid &&
                                   mask[n + w].blockType == mask[n].blockType)
                                ++w;

                            int h = 1;
                            bool done = false;
                            for (; j + h < dims[v]; ++h) {
                                for (int k = 0; k < w; ++k) {
                                    if (int idx = n + k + h * dims[u]; !mask[idx].solid ||
                                        mask[idx].blockType != mask[n].blockType) {
                                        done = true;
                                        break;
                                        }
                                }
                                if (done) break;
                            }

                            x[u] = i;
                            x[v] = j;

                            int blockPos[3] = { x[0], x[1], x[2] };

                            float verts[4][3];
                            for (auto & vert : verts) {
                                vert[0] = static_cast<float>(blockPos[0]);
                                vert[1] = static_cast<float>(blockPos[1]);
                                vert[2] = static_cast<float>(blockPos[2]);
                            }

                            verts[1][u] += w;
                            verts[2][u] += w; verts[2][v] += h;
                            verts[3][v] += h;

                            if (dir > 0)
                                for (auto & vert : verts)
                                    vert[axis] += 1.0f;

                            // --- AO calculation ---
                            float ao[4];
                            int ax = blockPos[0] + 1;
                            int ay = blockPos[1] + 1;
                            int az = blockPos[2] + 1;
                            int off = (dir > 0) ? 1 : 0;

                            if (axis == 0) { // X faces
                                int xq = ax + off;
                                if (dir < 0) {
                                    ao[0] = AO(solid[xq][ay][az-1], solid[xq][ay-1][az], solid[xq][ay-1][az-1]);
                                    ao[1] = AO(solid[xq][ay][az+1], solid[xq][ay-1][az], solid[xq][ay-1][az+1]);
                                    ao[2] = AO(solid[xq][ay][az+1], solid[xq][ay+1][az], solid[xq][ay+1][az+1]);
                                    ao[3] = AO(solid[xq][ay][az-1], solid[xq][ay+1][az], solid[xq][ay+1][az-1]);
                                } else {
                                    ao[0] = AO(solid[xq][ay][az+1], solid[xq][ay-1][az], solid[xq][ay-1][az+1]);
                                    ao[1] = AO(solid[xq][ay][az-1], solid[xq][ay-1][az], solid[xq][ay-1][az-1]);
                                    ao[2] = AO(solid[xq][ay][az-1], solid[xq][ay+1][az], solid[xq][ay+1][az-1]);
                                    ao[3] = AO(solid[xq][ay][az+1], solid[xq][ay+1][az], solid[xq][ay+1][az+1]);
                                }
                            }
                            else if (axis == 1) { // Y faces
                                int yq = ay + off;
                                if (dir < 0) {
                                    ao[0] = AO(solid[ax-1][yq][az], solid[ax][yq][az-1], solid[ax-1][yq][az-1]);
                                    ao[1] = AO(solid[ax+1][yq][az], solid[ax][yq][az-1], solid[ax+1][yq][az-1]);
                                    ao[2] = AO(solid[ax+1][yq][az], solid[ax][yq][az+1], solid[ax+1][yq][az+1]);
                                    ao[3] = AO(solid[ax-1][yq][az], solid[ax][yq][az+1], solid[ax-1][yq][az+1]);
                                } else {
                                    ao[0] = AO(solid[ax-1][yq][az], solid[ax][yq][az+1], solid[ax-1][yq][az+1]);
                                    ao[1] = AO(solid[ax+1][yq][az], solid[ax][yq][az+1], solid[ax+1][yq][az+1]);
                                    ao[2] = AO(solid[ax+1][yq][az], solid[ax][yq][az-1], solid[ax+1][yq][az-1]);
                                    ao[3] = AO(solid[ax-1][yq][az], solid[ax][yq][az-1], solid[ax-1][yq][az-1]);
                                }
                            }
                            else { // Z faces
                                int zq = az + off;
                                if (dir < 0) {
                                    ao[0] = AO(solid[ax-1][ay][zq], solid[ax][ay-1][zq], solid[ax-1][ay-1][zq]);
                                    ao[1] = AO(solid[ax+1][ay][zq], solid[ax][ay-1][zq], solid[ax+1][ay-1][zq]);
                                    ao[2] = AO(solid[ax+1][ay][zq], solid[ax][ay+1][zq], solid[ax+1][ay+1][zq]);
                                    ao[3] = AO(solid[ax-1][ay][zq], solid[ax][ay+1][zq], solid[ax-1][ay+1][zq]);
                                } else {
                                    ao[0] = AO(solid[ax+1][ay][zq], solid[ax][ay-1][zq], solid[ax+1][ay-1][zq]);
                                    ao[1] = AO(solid[ax-1][ay][zq], solid[ax][ay-1][zq], solid[ax-1][ay-1][zq]);
                                    ao[2] = AO(solid[ax-1][ay][zq], solid[ax][ay+1][zq], solid[ax-1][ay+1][zq]);
                                    ao[3] = AO(solid[ax+1][ay][zq], solid[ax][ay+1][zq], solid[ax+1][ay+1][zq]);
                                }
                            }

                            uint32_t tex = blockTypeToTex[mask[n].blockType];
                            glm::vec3 normal(q[0], q[1], q[2]);

                            uint32_t base = chunk.cachedVertices.size();

                            chunk.cachedVertices.push_back({ {verts[0][0], verts[0][1], verts[0][2]}, normal, {0, 0}, tex, ao[0] });
                            chunk.cachedVertices.push_back({ {verts[1][0], verts[1][1], verts[1][2]}, normal, {float(w), 0}, tex, ao[1] });
                            chunk.cachedVertices.push_back({ {verts[2][0], verts[2][1], verts[2][2]}, normal, {float(w), float(h)}, tex, ao[2] });
                            chunk.cachedVertices.push_back({ {verts[3][0], verts[3][1], verts[3][2]}, normal, {0, float(h)}, tex, ao[3] });

                            if (dir > 0)
                                chunk.cachedIndices.insert(chunk.cachedIndices.end(),
                                    { base, base + 1, base + 2, base, base + 2, base + 3 });
                            else
                                chunk.cachedIndices.insert(chunk.cachedIndices.end(),
                                    { base, base + 3, base + 2, base, base + 2, base + 1 });

                            for (int l = 0; l < h; ++l)
                                for (int k = 0; k < w; ++k)
                                    mask[n + k + l * dims[u]].solid = false;

                            i += w;
                            n += w;
                        } else {
                            ++i;
                            ++n;
                        }
                    }
                }
                ++x[axis];
            }
        }
    }

    // Apply chunk offset
    glm::vec3 offset(coord.x * W, 0.0f, coord.y * D);
    for (auto& v : chunk.cachedVertices)
        v.position += offset;

    chunk.isMeshDirty = false;
}

/* ===================== Meshing ===================== */
void World::generateChunkMesh(Chunk& chunk, const glm::ivec2& coord) const {
    constexpr int W = Chunk::WIDTH;
    constexpr int H = Chunk::HEIGHT;
    constexpr int D = Chunk::DEPTH;

    chunk.cachedVertices.clear();
    chunk.cachedIndices.clear();

    // Reserve once (huge perf win)
    chunk.cachedVertices.reserve(Chunk::SIZE * 12);
    chunk.cachedIndices.reserve(Chunk::SIZE * 18);

    // ----------------------------------------------------
    // 1. Cache solidity with padding (NO bounds checks)
    // ----------------------------------------------------
    bool solid[W + 2][H + 2][D + 2] = {};

    for (int x = 0; x < W; ++x) {
        for (int y = 0; y < H; ++y) {
            for (int z = 0; z < D; ++z)
                solid[x + 1][y + 1][z + 1] = chunk.isBlockActive(x, y, z);
        }
    }

    // ----------------------------------------------------
    // 2. AO helper (cheap & fast)
    // ----------------------------------------------------
    auto AO = [](const bool s1, const bool s2, const bool c) -> float {
        if (s1 && s2) return 0.25f;
        return 1.0f - 0.25f * static_cast<float>(s1 + s2 + c);
    };

    // ----------------------------------------------------
    // 3. Main loop
    // ----------------------------------------------------
    GLuint blockTypeToTex[256] = {};
    for (auto [blockType, index] : textureIndices)
        blockTypeToTex[static_cast<int>(blockType)] = index;

    for (int x = 0; x < W; ++x) {
        for (int y = 0; y < H; ++y) {
            for (int z = 0; z < D; ++z) {
                if (!solid[x + 1][y + 1][z + 1])
                    continue;

                const uint8_t blockType = getBlockType(chunk.getVoxel(x, y, z));
                if (blockType == 0) continue;

                const uint32_t tex = blockTypeToTex[blockType];

                const auto fx = static_cast<float>(x);
                const auto fy = static_cast<float>(y);
                const auto fz = static_cast<float>(z);

                // ==================================================
                // +X face
                // ==================================================
                if (!solid[x + 2][y + 1][z + 1]) {
                    uint32_t base = chunk.cachedVertices.size();
                    glm::vec3 n(1,0,0);

                    float ao0 = AO(solid[x+2][y  ][z+1], solid[x+2][y+1][z  ], solid[x+2][y  ][z  ]);
                    float ao1 = AO(solid[x+2][y+2][z+1], solid[x+2][y+1][z  ], solid[x+2][y+2][z  ]);
                    float ao2 = AO(solid[x+2][y+2][z+2], solid[x+2][y+1][z+2], solid[x+2][y+2][z+1]);
                    float ao3 = AO(solid[x+2][y  ][z+2], solid[x+2][y+1][z+2], solid[x+2][y  ][z+1]);

                    chunk.cachedVertices.push_back({{fx+1, fy,   fz  }, n, {0,0}, tex, ao0});
                    chunk.cachedVertices.push_back({{fx+1, fy+1, fz  }, n, {0,1}, tex, ao1});
                    chunk.cachedVertices.push_back({{fx+1, fy+1, fz+1}, n, {1,1}, tex, ao2});
                    chunk.cachedVertices.push_back({{fx+1, fy,   fz+1}, n, {1,0}, tex, ao3});

                    chunk.cachedIndices.insert(chunk.cachedIndices.end(),
                        {base, base+1, base+2, base, base+2, base+3});
                }

                // ==================================================
                // -X face
                // ==================================================
                if (!solid[x][y + 1][z + 1]) {
                    uint32_t base = chunk.cachedVertices.size();
                    glm::vec3 n(-1,0,0);

                    float ao0 = AO(solid[x][y  ][z+1], solid[x][y+1][z  ], solid[x][y  ][z  ]);
                    float ao1 = AO(solid[x][y  ][z+2], solid[x][y+1][z+2], solid[x][y  ][z+1]);
                    float ao2 = AO(solid[x][y+2][z+2], solid[x][y+1][z+2], solid[x][y+2][z+1]);
                    float ao3 = AO(solid[x][y+2][z+1], solid[x][y+1][z  ], solid[x][y+2][z  ]);

                    chunk.cachedVertices.push_back({{fx, fy,   fz  }, n, {0,0}, tex, ao0});
                    chunk.cachedVertices.push_back({{fx, fy,   fz+1}, n, {1,0}, tex, ao1});
                    chunk.cachedVertices.push_back({{fx, fy+1, fz+1}, n, {1,1}, tex, ao2});
                    chunk.cachedVertices.push_back({{fx, fy+1, fz  }, n, {0,1}, tex, ao3});

                    chunk.cachedIndices.insert(chunk.cachedIndices.end(),
                        {base, base+1, base+2, base, base+2, base+3});
                }

                // ==================================================
                // +Y face
                // ==================================================
                if (!solid[x + 1][y + 2][z + 1]) {
                    uint32_t base = chunk.cachedVertices.size();
                    glm::vec3 n(0,1,0);

                    float ao0 = AO(solid[x  ][y+2][z+1], solid[x+1][y+2][z  ], solid[x  ][y+2][z  ]);
                    float ao1 = AO(solid[x  ][y+2][z+2], solid[x+1][y+2][z+2], solid[x  ][y+2][z+1]);
                    float ao2 = AO(solid[x+2][y+2][z+2], solid[x+1][y+2][z+2], solid[x+2][y+2][z+1]);
                    float ao3 = AO(solid[x+2][y+2][z+1], solid[x+1][y+2][z  ], solid[x+2][y+2][z  ]);

                    chunk.cachedVertices.push_back({{fx,   fy+1, fz  }, n, {0,0}, tex, ao0});
                    chunk.cachedVertices.push_back({{fx,   fy+1, fz+1}, n, {0,1}, tex, ao1});
                    chunk.cachedVertices.push_back({{fx+1, fy+1, fz+1}, n, {1,1}, tex, ao2});
                    chunk.cachedVertices.push_back({{fx+1, fy+1, fz  }, n, {1,0}, tex, ao3});

                    chunk.cachedIndices.insert(chunk.cachedIndices.end(),
                        {base, base+1, base+2, base, base+2, base+3});
                }

                // ==================================================
                // -Y face
                // ==================================================
                if (!solid[x + 1][y][z + 1]) {
                    uint32_t base = chunk.cachedVertices.size();
                    glm::vec3 n(0,-1,0);

                    float ao0 = AO(solid[x  ][y][z+1], solid[x+1][y][z  ], solid[x  ][y][z  ]);
                    float ao1 = AO(solid[x+2][y][z+1], solid[x+1][y][z  ], solid[x+2][y][z  ]);
                    float ao2 = AO(solid[x+2][y][z+2], solid[x+1][y][z+2], solid[x+2][y][z+1]);
                    float ao3 = AO(solid[x  ][y][z+2], solid[x+1][y][z+2], solid[x  ][y][z+1]);

                    chunk.cachedVertices.push_back({{fx,   fy, fz  }, n, {0,0}, tex, ao0});
                    chunk.cachedVertices.push_back({{fx+1, fy, fz  }, n, {1,0}, tex, ao1});
                    chunk.cachedVertices.push_back({{fx+1, fy, fz+1}, n, {1,1}, tex, ao2});
                    chunk.cachedVertices.push_back({{fx,   fy, fz+1}, n, {0,1}, tex, ao3});

                    chunk.cachedIndices.insert(chunk.cachedIndices.end(),
                        {base, base+1, base+2, base, base+2, base+3});
                }

                // ==================================================
                // +Z face
                // ==================================================
                if (!solid[x + 1][y + 1][z + 2]) {
                    uint32_t base = chunk.cachedVertices.size();
                    glm::vec3 n(0,0,1);

                    float ao0 = AO(solid[x  ][y+1][z+2], solid[x+1][y  ][z+2], solid[x  ][y  ][z+2]);
                    float ao1 = AO(solid[x+2][y+1][z+2], solid[x+1][y  ][z+2], solid[x+2][y  ][z+2]);
                    float ao2 = AO(solid[x+2][y+2][z+2], solid[x+1][y+2][z+2], solid[x+2][y+1][z+2]);
                    float ao3 = AO(solid[x  ][y+2][z+2], solid[x+1][y+2][z+2], solid[x  ][y+1][z+2]);

                    chunk.cachedVertices.push_back({{fx,   fy,   fz+1}, n, {0,0}, tex, ao0});
                    chunk.cachedVertices.push_back({{fx+1, fy,   fz+1}, n, {1,0}, tex, ao1});
                    chunk.cachedVertices.push_back({{fx+1, fy+1, fz+1}, n, {1,1}, tex, ao2});
                    chunk.cachedVertices.push_back({{fx,   fy+1, fz+1}, n, {0,1}, tex, ao3});

                    chunk.cachedIndices.insert(chunk.cachedIndices.end(),
                        {base, base+1, base+2, base, base+2, base+3});
                }

                // ==================================================
                // -Z face
                // ==================================================
                if (!solid[x + 1][y + 1][z]) {
                    uint32_t base = chunk.cachedVertices.size();
                    glm::vec3 n(0,0,-1);

                    float ao0 = AO(solid[x  ][y+1][z], solid[x+1][y  ][z], solid[x  ][y  ][z]);
                    float ao1 = AO(solid[x  ][y+2][z], solid[x+1][y+2][z], solid[x  ][y+1][z]);
                    float ao2 = AO(solid[x+2][y+2][z], solid[x+1][y+2][z], solid[x+2][y+1][z]);
                    float ao3 = AO(solid[x+2][y+1][z], solid[x+1][y  ][z], solid[x+2][y  ][z]);

                    chunk.cachedVertices.push_back({{fx,   fy,   fz}, n, {0,0}, tex, ao0});
                    chunk.cachedVertices.push_back({{fx,   fy+1, fz}, n, {0,1}, tex, ao1});
                    chunk.cachedVertices.push_back({{fx+1, fy+1, fz}, n, {1,1}, tex, ao2});
                    chunk.cachedVertices.push_back({{fx+1, fy,   fz}, n, {1,0}, tex, ao3});

                    chunk.cachedIndices.insert(chunk.cachedIndices.end(),
                        {base, base+1, base+2, base, base+2, base+3});
                }
            }
        }
    }

    // ----------------------------------------------------
    // 4. Apply chunk offset once
    // ----------------------------------------------------
    glm::vec3 offset(coord.x * W, 0.0f, coord.y * D);
    for (auto& v : chunk.cachedVertices)
        v.position += offset;

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
