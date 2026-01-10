#include "World.hpp"
#include "GreedyMesher.hpp"

#define STB_PERLIN_IMPLEMENTATION
#include "stb_perlin.h"

#define GLM_ENABLE_EXPERIMENTAL
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
        if (constexpr int MAX_CHUNKS_PER_UPDATE = 12; spawned++ >= MAX_CHUNKS_PER_UPDATE)
            break;

        threadPool.enqueue([this, c]
        {
            Chunk chunk;
            chunk.worldMin = {c.x * Chunk::WIDTH, 0.0f, c.y * Chunk::DEPTH};
            chunk.worldMax = chunk.worldMin + glm::vec3(Chunk::WIDTH,Chunk::HEIGHT,Chunk::DEPTH);
            chunk.worldCenter = (chunk.worldMin + chunk.worldMax) * 0.5f;

            generateTerrain(chunk, c);
            // generateChunkMesh(chunk, c);

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

void World::generateChunkGreedyMesh(Chunk& chunk, const glm::ivec2& coord) const
{
    GLuint blockTypeToTex[256] = {};
    for (auto [blockType, index] : textureIndices)
        blockTypeToTex[static_cast<int>(blockType)] = index;

    GreedyMesher::generateGreedyMesh(chunk, coord, blockTypeToTex);
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
