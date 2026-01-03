#include "World.hpp"
#include "stb_perlin.h"

#include <cmath>
#include <ranges>

inline int floorDiv(const int x, const int d) {
    int q = x / d;
    if (const int r = x % d; r && ((r < 0) != (d < 0)))
        --q;
    return q;
}

/* ========================== Chunk ========================== */

Chunk::Chunk(): isMeshDirty(true), voxels(Chunk::SIZE) {}

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

bool Chunk::isBlockActive(int x, int y, int z) const {
    return isActive(getVoxel(x, y, z));
}

/* ========================== World ========================== */

World::World(const std::unordered_map<BlockType, uint32_t>& indices): textureIndices(indices) {}

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

    if (newChunk == playerChunk && !chunks.empty())
        return;

    playerChunk = newChunk;

    std::vector<glm::ivec2> toLoad;
    std::vector<glm::ivec2> toUnload;

    {
        std::lock_guard lock(chunk_mutex);

        for (int dx = -CHUNK_RADIUS; dx <= CHUNK_RADIUS; ++dx)
            for (int dz = -CHUNK_RADIUS; dz <= CHUNK_RADIUS; ++dz) {
                if (glm::ivec2 c = playerChunk + glm::ivec2(dx, dz); !chunks.contains(c))
                    toLoad.push_back(c);
            }

        for (auto& c : chunks | std::views::keys)
            if (glm::distance(glm::vec2(c), glm::vec2(playerChunk)) > CHUNK_RADIUS + 1)
                toUnload.push_back(c);
    }

    for (auto& c : toLoad) {
        threadPool.enqueue([this, c] {
            Chunk chunk;
            generateTerrain(chunk, c);
            generateChunkMeshGreedy(chunk, c);

            std::lock_guard lock(chunk_mutex);
            chunks.emplace(c, std::move(chunk));
        });
    }

    {
        std::lock_guard lock(chunk_mutex);
        for (auto& c : toUnload)
            chunks.erase(c);
    }
}

/* ===================== Terrain ===================== */

void World::generateTerrain(Chunk& chunk, const glm::ivec2& coord) {
    // std::mt19937 rng(coord.x * 73856093 ^ coord.y * 19349663);

    for (int x = 0; x < Chunk::WIDTH; ++x)
    {
        for (int z = 0; z < Chunk::DEPTH; ++z) {
            const int h = Chunk::HEIGHT / 4 +
                    static_cast<int>(10 * std::sin(0.1f * (x + coord.x * 16)) +
                        10 * std::cos(0.1f * (z + coord.y * 16)));

            for (int y = 0; y < h; ++y) {
                auto type = (y == h - 1) ? BlockType::Grass : BlockType::Stone;
                chunk.setVoxel(x, y, z, packVoxelData(1, 255, 255, 255, static_cast<uint8_t>(type)));
            }
        }
    }

}

/* ===================== Greedy Meshing ===================== */
void World::generateChunkMeshGreedy(Chunk& chunk, const glm::ivec2& coord) const {
    // if (!chunk.isMeshDirty)
    //     return;
    //
    // chunk.cachedVertices.clear();
    // chunk.cachedIndices.clear();
    //
    // constexpr int dims[3] = {Chunk::WIDTH, Chunk::HEIGHT, Chunk::DEPTH};
    //
    // for (int axis = 0; axis < 3; ++axis) {
    //     const int u = (axis + 1) % 3;
    //     const int v = (axis + 2) % 3;
    //     std::vector<int> mask(dims[u] * dims[v]);
    //
    //     for (int slice = 0; slice <= dims[axis]; ++slice) {
    //         for (int i = 0; i < dims[u]; ++i) {
    //             for (int j = 0; j < dims[v]; ++j) {
    //                 const int x = (axis == 0) ? slice : i;
    //                 const int y = (axis == 1) ? slice : ((axis == 0) ? i : j);
    //                 const int z = (axis == 2) ? slice : j;
    //
    //                 bool solidA = (slice < dims[axis]) && chunk.isBlockActive(x, y, z);
    //                 bool solidB = (slice > 0) && chunk.isBlockActive(x - (axis == 0), y - (axis == 1), z - (axis == 2));
    //
    //                 if (solidA != solidB) {
    //                     mask[i + j * dims[u]] = solidA ? getBlockType(chunk.getVoxel(x, y, z))
    //                                                    : getBlockType(chunk.getVoxel(x - (axis == 0), y - (axis == 1), z - (axis == 2)));
    //                 } else {
    //                     mask[i + j * dims[u]] = -1;
    //                 }
    //             }
    //         }
    //         applyGreedy2D(mask, dims[u], dims[v], axis, slice, chunk, coord);
    //     }
    // }
    //
    // chunk.isMeshDirty = false;
    chunk.cachedVertices.clear();
    chunk.cachedIndices.clear();

    for (int x = 0; x < Chunk::WIDTH; ++x)
    {
        for (int y = 0; y < Chunk::HEIGHT; ++y)
        {
            for (int z = 0; z < Chunk::DEPTH; ++z) {
                if (!chunk.isBlockActive(x, y, z))
                    continue;

                const uint32_t texIndex = textureIndices.at(static_cast<BlockType>(getBlockType(chunk.getVoxel(x, y, z))));
                auto base = static_cast<uint32_t>(chunk.cachedVertices.size());

                // +X face
                if (x+1 >= Chunk::WIDTH || !chunk.isBlockActive(x+1, y, z))
                {
                    chunk.cachedVertices.push_back({{x+1, y,   z}, {0,0}, texIndex});
                    chunk.cachedVertices.push_back({{x+1, y+1, z}, {0,1}, texIndex});
                    chunk.cachedVertices.push_back({{x+1, y+1, z+1}, {1,1}, texIndex});
                    chunk.cachedVertices.push_back({{x+1, y,   z+1}, {1,0}, texIndex});
                    chunk.cachedIndices.insert(chunk.cachedIndices.end(), {base, base+1, base+2, base, base+2, base+3});
                    base += 4;
                }

                // -X face
                if (x+1 >= Chunk::WIDTH || !chunk.isBlockActive(x-1, y, z))
                {
                    chunk.cachedVertices.push_back({{x, y,   z}, {0,0}, texIndex});
                    chunk.cachedVertices.push_back({{x, y,   z+1}, {1,0}, texIndex});
                    chunk.cachedVertices.push_back({{x, y+1, z+1}, {1,1}, texIndex});
                    chunk.cachedVertices.push_back({{x, y+1, z}, {0,1}, texIndex});
                    chunk.cachedIndices.insert(chunk.cachedIndices.end(), {base, base+1, base+2, base, base+2, base+3});
                    base += 4;
                }

                // +Y face
                if (x+1 >= Chunk::WIDTH || !chunk.isBlockActive(x, y+1, z))
                {
                    chunk.cachedVertices.push_back({{x, y+1, z}, {0,0}, texIndex});
                    chunk.cachedVertices.push_back({{x, y+1, z+1}, {0,1}, texIndex});
                    chunk.cachedVertices.push_back({{x+1, y+1, z+1}, {1,1}, texIndex});
                    chunk.cachedVertices.push_back({{x+1, y+1, z}, {1,0}, texIndex});
                    chunk.cachedIndices.insert(chunk.cachedIndices.end(), {base, base+1, base+2, base, base+2, base+3});
                    base += 4;
                }

                // -Y face
                if (x+1 >= Chunk::WIDTH || !chunk.isBlockActive(x, y-1, z))
                {
                    chunk.cachedVertices.push_back({{x, y, z}, {0,0}, texIndex});
                    chunk.cachedVertices.push_back({{x+1, y, z}, {1,0}, texIndex});
                    chunk.cachedVertices.push_back({{x+1, y, z+1}, {1,1}, texIndex});
                    chunk.cachedVertices.push_back({{x, y, z+1}, {0,1}, texIndex});
                    chunk.cachedIndices.insert(chunk.cachedIndices.end(), {base, base+1, base+2, base, base+2, base+3});
                    base += 4;
                }

                // +Z face
                if (x+1 >= Chunk::WIDTH || !chunk.isBlockActive(x, y, z+1))
                {
                    chunk.cachedVertices.push_back({{x, y,   z+1}, {0,0}, texIndex});
                    chunk.cachedVertices.push_back({{x+1, y,   z+1}, {1,0}, texIndex});
                    chunk.cachedVertices.push_back({{x+1, y+1, z+1}, {1,1}, texIndex});
                    chunk.cachedVertices.push_back({{x, y+1, z+1}, {0,1}, texIndex});
                    chunk.cachedIndices.insert(chunk.cachedIndices.end(), {base, base+1, base+2, base, base+2, base+3});
                    base += 4;
                }

                // -Z face
                if (x+1 >= Chunk::WIDTH || !chunk.isBlockActive(x, y, z-1))
                {
                    chunk.cachedVertices.push_back({{x, y,   z}, {0,0}, texIndex});
                    chunk.cachedVertices.push_back({{x, y+1, z}, {0,1}, texIndex});
                    chunk.cachedVertices.push_back({{x+1, y+1, z}, {1,1}, texIndex});
                    chunk.cachedVertices.push_back({{x+1, y,   z}, {1,0}, texIndex});
                    chunk.cachedIndices.insert(chunk.cachedIndices.end(), {base, base+1, base+2, base, base+2, base+3});
                }
            }
        }
    }

    const glm::vec3 chunkOffset(coord.x * Chunk::WIDTH, 0, coord.y * Chunk::DEPTH);
    for (auto& v : chunk.cachedVertices) v.position += chunkOffset;
    chunk.isMeshDirty = false;
}

void World::applyGreedy2D(
    const std::vector<int>& mask,
    const int width, const int height, const int axis,
    const int slice,
    Chunk& chunk, const glm::ivec2& coord
) const {

    std::vector<uint8_t> used(width * height, 0);

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const int idx = x + y * width;
            if (used[idx] || mask[idx] < 0)
                continue;

            int blockType = mask[idx];

            // Find width
            int w = 1;
            while (x + w < width && mask[idx + w] == blockType && !used[idx + w])
                ++w;

            // Find height
            int h = 1;
            bool stop = false;
            while (y + h < height && !stop) {
                for (int k = 0; k < w; ++k) {
                    if (mask[(x + k) + (y + h) * width] != blockType ||
                        used[(x + k) + (y + h) * width]) {
                        stop = true;
                        break;
                    }
                }
                if (!stop) ++h;
            }

            // Mark used
            for (int dy = 0; dy < h; ++dy)
                for (int dx = 0; dx < w; ++dx)
                    used[(x + dx) + (y + dy) * width] = 1;

            // Create quad vertices
            glm::vec3 p[4];
            switch (axis) {
                case 0:
                    p[0] = {slice, y, x};
                    p[1] = {slice, y, x + w};
                    p[2] = {slice, y + h, x + w};
                    p[3] = {slice, y + h, x};
                    break;
                case 1:
                    p[0] = {x, slice, y};
                    p[1] = {x + w, slice, y};
                    p[2] = {x + w, slice, y + h};
                    p[3] = {x, slice, y + h};
                    break;
                case 2:
                    p[0] = {x, y, slice};
                    p[1] = {x + w, y, slice};
                    p[2] = {x + w, y + h, slice};
                    p[3] = {x, y + h, slice};
                    break;
                default: throw std::logic_error("unrecognized axis");
            }

            // Offset by chunk position
            const glm::vec3 chunkOffset(coord.x * Chunk::WIDTH, 0, coord.y * Chunk::DEPTH);
            for (auto& v : p) v += chunkOffset;

            const uint32_t texIndex = textureIndices.at(static_cast<BlockType>(blockType));
            auto base = static_cast<uint32_t>(chunk.cachedVertices.size());

            chunk.cachedVertices.push_back({p[0], {0, 0}, texIndex});
            chunk.cachedVertices.push_back({p[1], {1, 0}, texIndex});
            chunk.cachedVertices.push_back({p[2], {1, 1}, texIndex});
            chunk.cachedVertices.push_back({p[3], {0, 1}, texIndex});

            // Determine if face is back-facing (flip winding)
            bool flipFace = false;
            if (axis == 0) flipFace = (mask[idx] != getBlockType(chunk.getVoxel(slice, y, x)));
            if (axis == 1) flipFace = (mask[idx] != getBlockType(chunk.getVoxel(x, slice, y)));
            if (axis == 2) flipFace = (mask[idx] != getBlockType(chunk.getVoxel(x, y, slice)));

            if (flipFace) {
                chunk.cachedIndices.insert(chunk.cachedIndices.end(),
                    {base, base + 2, base + 1, base, base + 3, base + 2});
            } else {
                chunk.cachedIndices.insert(chunk.cachedIndices.end(),
                    {base, base + 1, base + 2, base, base + 2, base + 3});
            }
        }
    }
}
