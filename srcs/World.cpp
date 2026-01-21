#include "World.hpp"
#include "App.hpp"

#define STB_PERLIN_IMPLEMENTATION
#include "stb_perlin.h"

#define GLM_ENABLE_EXPERIMENTAL
#include <iostream>

#include "glm/gtx/norm.hpp"

#include <ranges>

World::World(std::array<uint32_t, 256>& indices) : ubo(0), textureIndices(indices)
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
        if (constexpr int MAX_CHUNKS_PER_UPDATE = 8; spawned++ >= MAX_CHUNKS_PER_UPDATE)
            break;

        threadPool.enqueue([this, c]
        {
            Chunk chunk;
            chunk.worldMin = {c.x * Chunk::WIDTH, 0.0f, c.y * Chunk::DEPTH};
            chunk.worldMax = chunk.worldMin + glm::vec3(Chunk::WIDTH,Chunk::HEIGHT,Chunk::DEPTH);

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

// void World::generateTerrain(Chunk& chunk, const glm::ivec2& coord)
// {
//     constexpr int MIN_Y = 1;
//     constexpr int MAX_Y = Chunk::HEIGHT - 1;
//     constexpr int BASE_HEIGHT = Chunk::HEIGHT / 8;
//     constexpr int SNOW_HEIGHT = Chunk::HEIGHT / 4; // above this, snow
//
//     const int seed = 1337; // deterministic seed
//
//     // Precompute 2D noise for height & biome
//     int heightMap[Chunk::WIDTH][Chunk::DEPTH];
//     const int baseWX = coord.x * Chunk::WIDTH;
//     const int baseWZ = coord.y * Chunk::DEPTH;
//     for (int x = 0; x < Chunk::WIDTH; ++x) {
//         for (int z = 0; z < Chunk::DEPTH; ++z) {
//             const int wx = baseWX + x;
//             const int wz = baseWZ + z;
//
//             const float largeHill = stb_perlin_noise3_seed(wx * 0.005f, 0.0f, wz * 0.005f, 0,0,0, seed) * 96.0f; // mountains
//             const float smallBump = stb_perlin_noise3_seed(wx * 0.05f, 0.0f, wz * 0.05f, 0,0,0, seed) * 16.0f;   // hills & bumps
//
//             int height = BASE_HEIGHT + static_cast<int>(largeHill + smallBump);
//             heightMap[x][z] = std::clamp(height, MIN_Y, MAX_Y);
//         }
//     }
//
//     static const uint32_t STONE = packVoxelData(1,255,255,255,static_cast<uint8_t>(BlockType::Stone));
//     static const uint32_t DIRT  = packVoxelData(1,255,255,255,static_cast<uint8_t>(BlockType::Dirt));
//     static const uint32_t GRASS = packVoxelData(1,255,255,255,static_cast<uint8_t>(BlockType::Grass));
//     static const uint32_t SNOW  = packVoxelData(1,255,255,255,static_cast<uint8_t>(BlockType::Snow));
//
//     for (int x = 0; x < Chunk::WIDTH; ++x) {
//         for (int z = 0; z < Chunk::DEPTH; ++z) {
//             const int height = heightMap[x][z];
//
//             // stone
//             int y = MIN_Y;
//             for (; y < height - 4; ++y)
//                 chunk.setVoxel(x, y, z, STONE);
//
//             // dirt
//             for (; y < height; ++y)
//                 chunk.setVoxel(x, y, z, DIRT);
//
//             // top block
//             chunk.setVoxel(x, height, z, (height >= SNOW_HEIGHT) ? SNOW : GRASS);
//         }
//     }
//
//     chunk.isMeshDirty = true;
// }

void World::generateTerrain(Chunk& chunk, const glm::ivec2& coord)
{
    static TerrainGenerator generator;
    generator.generateChunk(chunk, coord);
}


void World::generateChunkGreedyMesh(Chunk& chunk, const glm::ivec2& coord)
{
    constexpr int W = Chunk::WIDTH;
    constexpr int H = Chunk::HEIGHT;
    constexpr int D = Chunk::DEPTH;

    auto& vertices = chunk.cachedVertices;
    auto& indices = chunk.cachedIndices;

    vertices.clear();
    indices.clear();
    vertices.reserve(Chunk::SIZE * 4);
    indices.reserve(Chunk::SIZE * 6);

    // Cache solidity with padding
    thread_local bool solid[W + 2][H + 2][D + 2] = {};
    thread_local uint8_t blockTypes[W][H][D] = {};

    for (int x = 0; x < W; ++x) {
        for (int y = 0; y < H; ++y) {
            for (int z = 0; z < D; ++z) {
                solid[x + 1][y + 1][z + 1] = chunk.isBlockActive(x, y, z);
                blockTypes[x][y][z] = getBlockType(chunk.getVoxel(x, y, z));
            }
        }
    }

    // Normal direction to index
    auto normalToIndex = [](int axis, int dir) -> uint8_t {
        return axis * 2 + (dir < 0 ? 1 : 0);
    };

    // Greedy meshing
    for (int axis = 0; axis < 3; ++axis) {
        for (int dir = -1; dir <= 1; dir += 2) {
            const int u = (axis + 1) % 3;
            const int v = (axis + 2) % 3;

            const int dims[3] = { W, H, D };
            int x[3] = { 0, 0, 0 };
            int q[3] = { 0, 0, 0 };
            q[axis] = dir;

            struct MaskEntry {
                bool solid;
                uint8_t blockType;
            };

            // Pre-allocate once instead of on stack
            static thread_local std::vector<MaskEntry> maskBuffer;
            maskBuffer.clear();
            maskBuffer.resize(std::max({ W, H, D }) * std::max({ W, H, D }));
            MaskEntry* mask = maskBuffer.data();

            for (x[axis] = -1; x[axis] < dims[axis]; ++x[axis]) {
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
                uint32_t base = vertices.size();

                // Greedy merge
                for (int j = 0; j < dims[v]; ++j) {
                    for (int i = 0; i < dims[u];) {
                        if (mask[n].solid) {
                            int w = 1;
                            const uint8_t blockType = mask[n].blockType;

                            // Fast width merge
                            while (i + w < dims[u] &&
                                   mask[n + w].solid &&
                                   mask[n + w].blockType == blockType)
                                ++w;

                            int h = 1;

                            // Fast height merge
                            for (; j + h < dims[v]; ++h) {
                                for (int k = 0; k < w; ++k) {
                                    const int idx = n + k + h * dims[u];
                                    if (!mask[idx].solid || mask[idx].blockType != blockType)
                                        goto done_merging;
                                }
                            }
                            done_merging:

                            x[u] = i;
                            x[v] = j;

                            float verts[4][3];
                            for (auto& vert : verts) {
                                vert[0] = static_cast<float>(x[0]);
                                vert[1] = static_cast<float>(x[1]);
                                vert[2] = static_cast<float>(x[2]);
                            }

                            verts[1][u] += w;
                            verts[2][u] += w; verts[2][v] += h;
                            verts[3][v] += h;

                            if (dir > 0)
                                for (auto& vert : verts)
                                    vert[axis] += 1.0f;

                            const uint16_t tex = textureIndices[blockType];
                            const uint8_t normalIndex = normalToIndex(axis, dir);
                            constexpr uint8_t AO_MAX = 3;

                            // Batch push vertices (4 at once instead of individually)
                            vertices.push_back({{verts[0][0], verts[0][1], verts[0][2]}, {0, 0}, tex, normalIndex, AO_MAX});
                            vertices.push_back({{verts[1][0], verts[1][1], verts[1][2]}, {float(w), 0}, tex, normalIndex, AO_MAX});
                            vertices.push_back({{verts[2][0], verts[2][1], verts[2][2]}, {float(w), float(h)}, tex, normalIndex, AO_MAX});
                            vertices.push_back({{verts[3][0], verts[3][1], verts[3][2]}, {0, float(h)}, tex, normalIndex, AO_MAX});

                            if (dir > 0)
                                indices.insert(indices.end(),
                                    { base, base + 1, base + 2, base, base + 2, base + 3 });
                            else
                                indices.insert(indices.end(),
                                    { base, base + 3, base + 2, base, base + 2, base + 1 });

                            // Mark merged quads as processed
                            for (int l = 0; l < h; ++l)
                                for (int k = 0; k < w; ++k)
                                    mask[n + k + l * dims[u]].solid = false;

                            base += 4;
                            i += w;
                            n += w;
                        } else {
                            ++i;
                            ++n;
                        }
                    }
                }
            }
        }
    }

    // Apply chunk offset
    const glm::vec3 offset(coord.x * W, 0.0f, coord.y * D);
    for (auto& v : vertices) v.position += offset;

    chunk.isMeshDirty = false;

    {
        std::lock_guard lock(chunk_mutex);
        for (int dz = -1; dz <= 1; ++dz) {
            for (int dx = -1; dx <= 1; ++dx) {
                if (dx == 0 && dz == 0) continue;
                if (auto it = chunks.find(coord + glm::ivec2(dx, dz)); it != chunks.end())
                    it->second.aoCalculated = false;
            }
        }
    }
}

/* ===================== Meshing ===================== */
// void World::generateChunkMesh(Chunk& chunk, const glm::ivec2& coord) const {
//     constexpr int W = Chunk::WIDTH;
//     constexpr int H = Chunk::HEIGHT;
//     constexpr int D = Chunk::DEPTH;
//
//     chunk.cachedVertices.clear();
//     chunk.cachedIndices.clear();
//
//     // Reserve once (huge perf win)
//     chunk.cachedVertices.reserve(Chunk::SIZE * 12);
//     chunk.cachedIndices.reserve(Chunk::SIZE * 18);
//
//     // ----------------------------------------------------
//     // 1. Cache solidity with padding (NO bounds checks)
//     // ----------------------------------------------------
//     bool solid[W + 2][H + 2][D + 2] = {};
//
//     for (int x = 0; x < W; ++x) {
//         for (int y = 0; y < H; ++y) {
//             for (int z = 0; z < D; ++z)
//                 solid[x + 1][y + 1][z + 1] = chunk.isBlockActive(x, y, z);
//         }
//     }
//
//     // ----------------------------------------------------
//     // 2. AO helper (cheap & fast)
//     // ----------------------------------------------------
//     auto AO = [](const bool s1, const bool s2, const bool c) -> float {
//         if (s1 && s2) return 0.25f;
//         return 1.0f - 0.25f * static_cast<float>(s1 + s2 + c);
//     };
//
//     // ----------------------------------------------------
//     // 3. Main loop
//     // ----------------------------------------------------
//     GLuint blockTypeToTex[256] = {};
//     for (auto [blockType, index] : textureIndices)
//         blockTypeToTex[static_cast<int>(blockType)] = index;
//
//     for (int x = 0; x < W; ++x) {
//         for (int y = 0; y < H; ++y) {
//             for (int z = 0; z < D; ++z) {
//                 if (!solid[x + 1][y + 1][z + 1])
//                     continue;
//
//                 const uint8_t blockType = getBlockType(chunk.getVoxel(x, y, z));
//                 if (blockType == 0) continue;
//
//                 const uint32_t tex = blockTypeToTex[blockType];
//
//                 const auto fx = static_cast<float>(x);
//                 const auto fy = static_cast<float>(y);
//                 const auto fz = static_cast<float>(z);
//
//                 // ==================================================
//                 // +X face
//                 // ==================================================
//                 if (!solid[x + 2][y + 1][z + 1]) {
//                     uint32_t base = chunk.cachedVertices.size();
//                     glm::vec3 n(1,0,0);
//
//                     float ao0 = AO(solid[x+2][y  ][z+1], solid[x+2][y+1][z  ], solid[x+2][y  ][z  ]);
//                     float ao1 = AO(solid[x+2][y+2][z+1], solid[x+2][y+1][z  ], solid[x+2][y+2][z  ]);
//                     float ao2 = AO(solid[x+2][y+2][z+2], solid[x+2][y+1][z+2], solid[x+2][y+2][z+1]);
//                     float ao3 = AO(solid[x+2][y  ][z+2], solid[x+2][y+1][z+2], solid[x+2][y  ][z+1]);
//
//                     chunk.cachedVertices.push_back({{fx+1, fy,   fz  }, n, {0,0}, tex, ao0});
//                     chunk.cachedVertices.push_back({{fx+1, fy+1, fz  }, n, {0,1}, tex, ao1});
//                     chunk.cachedVertices.push_back({{fx+1, fy+1, fz+1}, n, {1,1}, tex, ao2});
//                     chunk.cachedVertices.push_back({{fx+1, fy,   fz+1}, n, {1,0}, tex, ao3});
//
//                     chunk.cachedIndices.insert(chunk.cachedIndices.end(),
//                         {base, base+1, base+2, base, base+2, base+3});
//                 }
//
//                 // ==================================================
//                 // -X face
//                 // ==================================================
//                 if (!solid[x][y + 1][z + 1]) {
//                     uint32_t base = chunk.cachedVertices.size();
//                     glm::vec3 n(-1,0,0);
//
//                     float ao0 = AO(solid[x][y  ][z+1], solid[x][y+1][z  ], solid[x][y  ][z  ]);
//                     float ao1 = AO(solid[x][y  ][z+2], solid[x][y+1][z+2], solid[x][y  ][z+1]);
//                     float ao2 = AO(solid[x][y+2][z+2], solid[x][y+1][z+2], solid[x][y+2][z+1]);
//                     float ao3 = AO(solid[x][y+2][z+1], solid[x][y+1][z  ], solid[x][y+2][z  ]);
//
//                     chunk.cachedVertices.push_back({{fx, fy,   fz  }, n, {0,0}, tex, ao0});
//                     chunk.cachedVertices.push_back({{fx, fy,   fz+1}, n, {1,0}, tex, ao1});
//                     chunk.cachedVertices.push_back({{fx, fy+1, fz+1}, n, {1,1}, tex, ao2});
//                     chunk.cachedVertices.push_back({{fx, fy+1, fz  }, n, {0,1}, tex, ao3});
//
//                     chunk.cachedIndices.insert(chunk.cachedIndices.end(),
//                         {base, base+1, base+2, base, base+2, base+3});
//                 }
//
//                 // ==================================================
//                 // +Y face
//                 // ==================================================
//                 if (!solid[x + 1][y + 2][z + 1]) {
//                     uint32_t base = chunk.cachedVertices.size();
//                     glm::vec3 n(0,1,0);
//
//                     float ao0 = AO(solid[x  ][y+2][z+1], solid[x+1][y+2][z  ], solid[x  ][y+2][z  ]);
//                     float ao1 = AO(solid[x  ][y+2][z+2], solid[x+1][y+2][z+2], solid[x  ][y+2][z+1]);
//                     float ao2 = AO(solid[x+2][y+2][z+2], solid[x+1][y+2][z+2], solid[x+2][y+2][z+1]);
//                     float ao3 = AO(solid[x+2][y+2][z+1], solid[x+1][y+2][z  ], solid[x+2][y+2][z  ]);
//
//                     chunk.cachedVertices.push_back({{fx,   fy+1, fz  }, n, {0,0}, tex, ao0});
//                     chunk.cachedVertices.push_back({{fx,   fy+1, fz+1}, n, {0,1}, tex, ao1});
//                     chunk.cachedVertices.push_back({{fx+1, fy+1, fz+1}, n, {1,1}, tex, ao2});
//                     chunk.cachedVertices.push_back({{fx+1, fy+1, fz  }, n, {1,0}, tex, ao3});
//
//                     chunk.cachedIndices.insert(chunk.cachedIndices.end(),
//                         {base, base+1, base+2, base, base+2, base+3});
//                 }
//
//                 // ==================================================
//                 // -Y face
//                 // ==================================================
//                 if (!solid[x + 1][y][z + 1]) {
//                     uint32_t base = chunk.cachedVertices.size();
//                     glm::vec3 n(0,-1,0);
//
//                     float ao0 = AO(solid[x  ][y][z+1], solid[x+1][y][z  ], solid[x  ][y][z  ]);
//                     float ao1 = AO(solid[x+2][y][z+1], solid[x+1][y][z  ], solid[x+2][y][z  ]);
//                     float ao2 = AO(solid[x+2][y][z+2], solid[x+1][y][z+2], solid[x+2][y][z+1]);
//                     float ao3 = AO(solid[x  ][y][z+2], solid[x+1][y][z+2], solid[x  ][y][z+1]);
//
//                     chunk.cachedVertices.push_back({{fx,   fy, fz  }, n, {0,0}, tex, ao0});
//                     chunk.cachedVertices.push_back({{fx+1, fy, fz  }, n, {1,0}, tex, ao1});
//                     chunk.cachedVertices.push_back({{fx+1, fy, fz+1}, n, {1,1}, tex, ao2});
//                     chunk.cachedVertices.push_back({{fx,   fy, fz+1}, n, {0,1}, tex, ao3});
//
//                     chunk.cachedIndices.insert(chunk.cachedIndices.end(),
//                         {base, base+1, base+2, base, base+2, base+3});
//                 }
//
//                 // ==================================================
//                 // +Z face
//                 // ==================================================
//                 if (!solid[x + 1][y + 1][z + 2]) {
//                     uint32_t base = chunk.cachedVertices.size();
//                     glm::vec3 n(0,0,1);
//
//                     float ao0 = AO(solid[x  ][y+1][z+2], solid[x+1][y  ][z+2], solid[x  ][y  ][z+2]);
//                     float ao1 = AO(solid[x+2][y+1][z+2], solid[x+1][y  ][z+2], solid[x+2][y  ][z+2]);
//                     float ao2 = AO(solid[x+2][y+2][z+2], solid[x+1][y+2][z+2], solid[x+2][y+1][z+2]);
//                     float ao3 = AO(solid[x  ][y+2][z+2], solid[x+1][y+2][z+2], solid[x  ][y+1][z+2]);
//
//                     chunk.cachedVertices.push_back({{fx,   fy,   fz+1}, n, {0,0}, tex, ao0});
//                     chunk.cachedVertices.push_back({{fx+1, fy,   fz+1}, n, {1,0}, tex, ao1});
//                     chunk.cachedVertices.push_back({{fx+1, fy+1, fz+1}, n, {1,1}, tex, ao2});
//                     chunk.cachedVertices.push_back({{fx,   fy+1, fz+1}, n, {0,1}, tex, ao3});
//
//                     chunk.cachedIndices.insert(chunk.cachedIndices.end(),
//                         {base, base+1, base+2, base, base+2, base+3});
//                 }
//
//                 // ==================================================
//                 // -Z face
//                 // ==================================================
//                 if (!solid[x + 1][y + 1][z]) {
//                     uint32_t base = chunk.cachedVertices.size();
//                     glm::vec3 n(0,0,-1);
//
//                     float ao0 = AO(solid[x  ][y+1][z], solid[x+1][y  ][z], solid[x  ][y  ][z]);
//                     float ao1 = AO(solid[x  ][y+2][z], solid[x+1][y+2][z], solid[x  ][y+1][z]);
//                     float ao2 = AO(solid[x+2][y+2][z], solid[x+1][y+2][z], solid[x+2][y+1][z]);
//                     float ao3 = AO(solid[x+2][y+1][z], solid[x+1][y  ][z], solid[x+2][y  ][z]);
//
//                     chunk.cachedVertices.push_back({{fx,   fy,   fz}, n, {0,0}, tex, ao0});
//                     chunk.cachedVertices.push_back({{fx,   fy+1, fz}, n, {0,1}, tex, ao1});
//                     chunk.cachedVertices.push_back({{fx+1, fy+1, fz}, n, {1,1}, tex, ao2});
//                     chunk.cachedVertices.push_back({{fx+1, fy,   fz}, n, {1,0}, tex, ao3});
//
//                     chunk.cachedIndices.insert(chunk.cachedIndices.end(),
//                         {base, base+1, base+2, base, base+2, base+3});
//                 }
//             }
//         }
//     }
//
//     // ----------------------------------------------------
//     // 4. Apply chunk offset once
//     // ----------------------------------------------------
//     glm::vec3 offset(coord.x * W, 0.0f, coord.y * D);
//     for (auto& v : chunk.cachedVertices)
//         v.position += offset;
//
//     chunk.isMeshDirty = false;
// }
