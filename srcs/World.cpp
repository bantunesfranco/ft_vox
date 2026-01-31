#include "World.hpp"
#include "App.hpp"
#include "BlockSystem.hpp"

#define STB_PERLIN_IMPLEMENTATION
#include "stb_perlin.h"

#define GLM_ENABLE_EXPERIMENTAL
#include <iostream>

#include "glm/gtx/norm.hpp"

#include <ranges>

inline RenderType blockRenderType(const BlockType t)
{
    switch (t) {
        case BlockType::Water: return RenderType::Transparent;
        case BlockType::Air:   return RenderType::Air;
        default:               return RenderType::Opaque;
    }
}

inline bool shouldRenderFace(const RenderType self, const RenderType neighbor)
{
    if (self == RenderType::Air) return false;      // Never render air
    if (neighbor == RenderType::Air) return true;   // Always render into air
    return self != neighbor;                        // Render if types differ
}

World::World(std::array<uint32_t, 256>& indices) : ubo(0), textureIndices(indices)
{
    glCreateBuffers(1, &ubo);
    glNamedBufferData(ubo, sizeof(WorldUBO), nullptr, GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_UNIFORM_BUFFER, 0, ubo);

    worldUBO = {
        .MVP = glm::mat4(1.f),
        .light = {0.0f, 200.0f, 0.0f, 500.f},
        .cameraPos = {0.0f, 0.0f, 0.0f, 0.4f},
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
void World::updateChunks(const glm::vec3& playerPos, ThreadPool& threadPool)
{
    playerChunk = {
        floorDiv(static_cast<int>(playerPos.x), Chunk::WIDTH),
        floorDiv(static_cast<int>(playerPos.z), Chunk::DEPTH)
    };

    // =========================================================
    // LOAD CHUNKS
    // =========================================================
    forEachChunkSpiral(playerChunk, CHUNK_RADIUS, [&](ChunkCoord c)
    {
        {
            std::lock_guard lock(state_mutex);
            auto& state = chunkStates[c];
            if (state != ChunkState::Unloaded)
                return;

            state = ChunkState::Loading;
        }

        threadPool.enqueue([this, c]
        {
            Chunk chunk;
            chunk.worldMin = {c.x * Chunk::WIDTH, 0.0f, c.y * Chunk::DEPTH};
            chunk.worldMax = chunk.worldMin +
                glm::vec3(Chunk::WIDTH, Chunk::HEIGHT, Chunk::DEPTH);

            generateTerrain(chunk, c);
            generateChunkGreedyMesh(chunk, c);

            {
                std::lock_guard lock(chunk_mutex);
                chunks.emplace(c, std::move(chunk));
            }
            {
                std::lock_guard lock(state_mutex);
                chunkStates[c] = ChunkState::Loaded;
            }
        });
    });

    // =========================================================
    // REGENERATE DIRTY CHUNKS
    // =========================================================
    {
        std::lock_guard stateLock(state_mutex);

        for (auto& [c, state] : chunkStates) {
            if (state != ChunkState::Loaded)
                continue;

            bool dirty = false;
            {
                std::lock_guard chunkLock(chunk_mutex);
                dirty = chunks.at(c).isMeshDirty;
            }

            if (!dirty)
                continue;

            state = ChunkState::Meshing;

            threadPool.enqueue([this, c]
            {
                Chunk copy;
                {
                    std::lock_guard lock(chunk_mutex);
                    copy = chunks.at(c);
                }

                generateChunkGreedyMesh(copy, c);

                {
                    std::lock_guard lock(chunk_mutex);
                    auto& dst = chunks.at(c);
                    dst.cachedOpaqueVertices = std::move(copy.cachedOpaqueVertices);
                    dst.cachedOpaqueIndices = std::move(copy.cachedOpaqueIndices);
                    dst.cachedTransparentIndices = std::move(copy.cachedTransparentIndices);
                    dst.cachedTransparentVertices = std::move(copy.cachedTransparentVertices);
                    dst.isMeshDirty = copy.isMeshDirty;
                    dst.aoCalculated.store(copy.aoCalculated);
                }
                {
                    std::lock_guard lock(state_mutex);
                    chunkStates[c] = ChunkState::Loaded;
                }
            });
        }
    }

    // =========================================================
    // UNLOAD FAR CHUNKS
    // =========================================================
    {
        std::lock_guard stateLock(state_mutex);

        for (auto& [c, state] : chunkStates) {
            if (state != ChunkState::Loaded)
                continue;

            if (glm::distance(glm::vec2(c), glm::vec2(playerChunk))
                <= CHUNK_RADIUS + 1)
                continue;

            state = ChunkState::Unloading;

            threadPool.enqueue([this, c]
            {
                {
                    std::lock_guard lock(chunk_mutex);
                    chunks.erase(c);
                }
                {
                    std::lock_guard lock(state_mutex);
                    chunkStates[c] = ChunkState::Unloaded;
                }
            });
        }
    }
}

/* ===================== Terrain ===================== */
void World::generateTerrain(Chunk& chunk, const ChunkCoord& coord)
{
    static TerrainGenerator generator;
    generator.generateChunk(chunk, coord);
}

/* ===================== Greedy Meshing ===================== */
void World::buildMask(
    const RenderType targetType,
    const int axis,
    RenderType renderType[Chunk::WIDTH + 2][Chunk::HEIGHT + 2][Chunk::DEPTH + 2],
    uint8_t blockTypes[Chunk::WIDTH + 2][Chunk::HEIGHT + 2][Chunk::DEPTH + 2],
    const int dims[3],
    int x[3], const int q[3],
    std::vector<MaskEntry>& mask
)
{
    const int u = (axis + 1) % 3;
    const int v = (axis + 2) % 3;

    int n = 0;

    for (x[v] = 0; x[v] < dims[v]; ++x[v])
        for (x[u] = 0; x[u] < dims[u]; ++x[u])
        {
            const int cx = x[0];
            const int cy = x[1];
            const int cz = x[2];

            const int nx = cx + q[0];
            const int ny = cy + q[1];
            const int nz = cz + q[2];

            const RenderType self     = renderType[cx+1][cy+1][cz+1];
            const RenderType neighbor = renderType[nx+1][ny+1][nz+1];

            const uint8_t bt = blockTypes[cx + 1][cy + 1][cz + 1];
            const uint8_t btNeighbor = blockTypes[nx + 1][ny + 1][nz + 1];
            bool visible = bt != 0 && self == targetType && shouldRenderFace(self, neighbor);

            if (visible && targetType == RenderType::Transparent && btNeighbor != 0) {
                visible = false;
            }

            mask[n].visible = visible;
            mask[n].blockType = bt;
            ++n;
        }
}

void World::runGreedyPass(
    const RenderType targetType,
    RenderType renderType[Chunk::WIDTH + 2][Chunk::HEIGHT + 2][Chunk::DEPTH + 2],
    uint8_t blockTypes[Chunk::WIDTH + 2][Chunk::HEIGHT + 2][Chunk::DEPTH + 2],
    const MeshTarget target
) const
{
    constexpr int W = Chunk::WIDTH;
    constexpr int H = Chunk::HEIGHT;
    constexpr int D = Chunk::DEPTH;

    constexpr int dims[3] = { W, H, D };

    auto normalToIndex = [](const int axis, const int dir) -> uint8_t {
        return axis * 2 + (dir < 0 ? 1 : 0);
    };

    std::vector<MaskEntry> mask(std::max({W, H, D}) * std::max({W, H, D}));

    for (int axis = 0; axis < 3; ++axis)
    {
        const int u = (axis + 1) % 3;
        const int v = (axis + 2) % 3;

        int x[3] = {0, 0, 0};
        int q[3] = {0, 0, 0};

        for (int dir = -1; dir <= 1; dir += 2)
        {
            q[axis] = dir;

            for (x[axis] = 0; x[axis] < dims[axis]; ++x[axis])
            {
                const int maskSize = dims[u] * dims[v];
                std::fill_n(mask.begin(), maskSize, MaskEntry{false, 0});

                buildMask(targetType, axis, renderType, blockTypes , const_cast<int*>(dims), x, q, mask);

                int n = 0;
                uint32_t base = target.vertices.size();

                for (int j = 0; j < dims[v]; ++j)
                {
                    for (int i = 0; i < dims[u];)
                    {
                        if (!mask[n].visible) { ++i; ++n; continue; }

                        const uint8_t bt = mask[n].blockType;

                        int w = 1;
                        int h = 1;

                        while (i + w < dims[u] &&
                               mask[n + w].visible &&
                               mask[n + w].blockType == bt)
                            ++w;

                        for (; j + h < dims[v]; ++h)
                        {
                            for (int k = 0; k < w; ++k)
                                if (!mask[n + k + h * dims[u]].visible ||
                                    mask[n + k + h * dims[u]].blockType != bt)
                                    goto merge_done;
                        }
                        merge_done:

                        x[u] = i;
                        x[v] = j;

                        float verts[4][3];
                        for (auto& vtx : verts)
                        {
                            vtx[0] = x[0];
                            vtx[1] = x[1];
                            vtx[2] = x[2];
                        }

                        verts[1][u] += w;
                        verts[2][u] += w; verts[2][v] += h;
                        verts[3][v] += h;

                        if (dir > 0)
                            for (auto& vtx : verts) vtx[axis] += 1.0f;

                        float uv[4][2] = {{0, 0},{(float)w, 0},{(float)w, (float)h},{0, (float)h}};

                        const uint16_t tex = textureIndices[bt];
                        const uint8_t normal = normalToIndex(axis, dir);
                        constexpr uint8_t AO_MAX = 3;

                        for (int k = 0; k < 4; ++k)
                            target.vertices.push_back({
                                {verts[k][0], verts[k][1], verts[k][2]},
                                {uv[k][0], uv[k][1]},
                                tex, normal, AO_MAX
                            });

                        if (dir > 0)
                            target.indices.insert(target.indices.end(),
                                {base, base+1, base+2, base, base+2, base+3});
                        else
                            target.indices.insert(target.indices.end(),
                                {base, base+3, base+2, base, base+2, base+1});

                        for (int dy = 0; dy < h; ++dy)
                            for (int dx = 0; dx < w; ++dx)
                                mask[n + dx + dy * dims[u]].visible = false;

                        base += 4;
                        i += w;
                        n += w;
                    }
                }
            }
        }
    }
}

void World::generateChunkGreedyMesh(Chunk& chunk, const ChunkCoord& coord)
{
    constexpr int W = Chunk::WIDTH;
    constexpr int H = Chunk::HEIGHT;
    constexpr int D = Chunk::DEPTH;

    thread_local RenderType renderType[W + 2][H + 2][D + 2] = {};
    thread_local uint8_t    blockTypes[W + 2][H + 2][D + 2] = {};

    const int baseWX = coord.x * W;
    const int baseWZ = coord.y * D;

    // Fill chunk
    for (int x = 0; x < W; ++x)
        for (int y = 0; y < H; ++y)
            for (int z = 0; z < D; ++z)
            {
                const Voxel v = chunk.getVoxel(x, y, z);
                uint8_t bt = getBlockType(v);

                renderType[x+1][y+1][z+1] = bt ? blockRenderType(static_cast<BlockType>(bt)) : RenderType::Air;
                blockTypes[x+1][y+1][z+1] = bt;
            }

    auto sampleBT = [&](const std::vector<Voxel>& voxels, const glm::ivec3& WorldPos) -> uint8_t {
        const auto localPos = BlockSystem::getLocalCoords(WorldPos);

        Voxel v;
        if (!voxels.empty())
            v = voxels[localPos.x + localPos.y * Chunk::WIDTH + localPos.z * Chunk::WIDTH * Chunk::HEIGHT];
        else
            v = TerrainGenerator::sampleVoxel(WorldPos.x, WorldPos.y, WorldPos.z);

        return getBlockType(v);
    };


    std::vector<Voxel> leftVoxels;
    std::vector<Voxel> rightVoxels;
    std::vector<Voxel> backVoxels;
    std::vector<Voxel> frontVoxels;

    {
        std::lock_guard lock(chunk_mutex);

        if (const auto leftChunk = chunks.find({coord.x - 1, coord.y}); leftChunk != chunks.end()) {
            leftVoxels.resize(Chunk::SIZE);
            std::ranges::copy(leftChunk->second.getVoxels(),leftVoxels.begin());
        }

        if (const auto rightChunk = chunks.find({coord.x + 1, coord.y}); rightChunk != chunks.end()) {
            rightVoxels.resize(Chunk::SIZE);
            std::ranges::copy(rightChunk->second.getVoxels(),rightVoxels.begin());
        }

        if (const auto backChunk = chunks.find({coord.x, coord.y - 1}); backChunk != chunks.end()) {
            backVoxels.resize(Chunk::SIZE);
            std::ranges::copy(backChunk->second.getVoxels(),backVoxels.begin());
        }
        if (const auto frontChunk = chunks.find({coord.x, coord.y + 1}); frontChunk != chunks.end()) {
            frontVoxels.resize(Chunk::SIZE);
            std::ranges::copy(frontChunk->second.getVoxels(),frontVoxels.begin());
        }
    }

    for (int y = 0; y < H; ++y)
        for (int z = 0; z < D; ++z)
        {
            auto bt = sampleBT(leftVoxels, {baseWX-1, y, baseWZ+z});
            blockTypes[0][y+1][z+1]     = bt;
            renderType[0][y+1][z+1]     = bt ? blockRenderType(static_cast<BlockType>(bt)) : RenderType::Air;

            bt = sampleBT(rightVoxels, {baseWX+W, y, baseWZ+z});
            blockTypes[W+1][y+1][z+1]   = bt;
            renderType[W+1][y+1][z+1]   = bt ? blockRenderType(static_cast<BlockType>(bt)) : RenderType::Air;
        }

    for (int x = 0; x < W; ++x)
        for (int y = 0; y < H; ++y)
        {
            auto bt = sampleBT(backVoxels, {baseWX+x, y, baseWZ-1});
            blockTypes[x+1][y+1][0]     = bt;
            renderType[x+1][y+1][0]     = bt ? blockRenderType(static_cast<BlockType>(bt)) : RenderType::Air;

            bt = sampleBT(frontVoxels, {baseWX+x, y, baseWZ+D});
            blockTypes[x+1][y+1][D+1]   = bt;
            renderType[x+1][y+1][D+1]   = bt ? blockRenderType(static_cast<BlockType>(bt)) : RenderType::Air;
        }

    chunk.cachedOpaqueVertices.clear();
    chunk.cachedOpaqueIndices.clear();
    chunk.cachedTransparentVertices.clear();
    chunk.cachedTransparentIndices.clear();

    runGreedyPass(RenderType::Opaque,
        renderType, blockTypes,
        {chunk.cachedOpaqueVertices, chunk.cachedOpaqueIndices});

    runGreedyPass(RenderType::Transparent,
        renderType, blockTypes,
        {chunk.cachedTransparentVertices, chunk.cachedTransparentIndices});

    const glm::vec3 offset(coord.x * W, 0.0f, coord.y * D);

    for (auto& v : chunk.cachedOpaqueVertices) v.position += offset;
    for (auto& v : chunk.cachedTransparentVertices) v.position += offset;

    chunk.isMeshDirty = false;
    chunk.aoCalculated = false;
}

// void World::generateChunkGreedyMesh(Chunk& chunk, const ChunkCoord& coord)
// {
//     constexpr int W = Chunk::WIDTH;
//     constexpr int H = Chunk::HEIGHT;
//     constexpr int D = Chunk::DEPTH;
//
//     auto& vertices = chunk.cachedVertices;
//     auto& indices = chunk.cachedIndices;
//
//     vertices.clear();
//     indices.clear();
//     vertices.reserve(Chunk::SIZE * 4);
//     indices.reserve(Chunk::SIZE * 6);
//
//     // Padded arrays for solid and block types
//     thread_local RenderType renderType[W + 2][H + 2][D + 2] = {};
//     thread_local uint8_t    blockTypes[W + 2][H + 2][D + 2] = {};
//
//     const int baseWX = coord.x * W;
//     const int baseWZ = coord.y * D;
//
//     // --- Fill center chunk --- + 1
//     for (int x = 0; x < W; ++x)
//         for (int y = 0; y < H; ++y)
//             for (int z = 0; z < D; ++z)
//             {
//                 Voxel v = chunk.getVoxel(x, y, z);
//                 uint8_t bt = getBlockType(v);
//
//                 RenderType rt = (bt) ? blockRenderType(static_cast<BlockType>(bt)) : RenderType::Air;
//                 renderType[x + 1][y + 1][z + 1] = rt;
//                 blockTypes[x + 1][y + 1][z + 1] = bt;
//             }
//
//     // --- Fill neighboring voxels for padded borders ---
//     auto sampleRT = [&](const int wx, const int y, const int wz)
//     {
//         const Voxel v = TerrainGenerator::sampleVoxel(wx, y, wz);
//         uint8_t bt = getBlockType(v);
//         if (bt == 0) return RenderType::Air;
//         return blockRenderType(static_cast<BlockType>(bt));
//     };
//
//     for (int y = 0; y < H; ++y)
//         for (int z = 0; z < D; ++z)
//         {
//             renderType[0][y + 1][z + 1]     = sampleRT(baseWX - 1, y, baseWZ + z);
//             renderType[W + 1][y + 1][z + 1] = sampleRT(baseWX + W, y, baseWZ + z);
//         }
//
//     for (int x = 0; x < W; ++x)
//         for (int y = 0; y < H; ++y)
//         {
//             renderType[x + 1][y + 1][0]     = sampleRT(baseWX + x, y, baseWZ - 1);
//             renderType[x + 1][y + 1][D + 1] = sampleRT(baseWX + x, y, baseWZ + D);
//         }
//
//     // --- Normal to index mapping ---
//     auto normalToIndex = [](const int axis, const int dir) -> uint8_t {
//         return axis * 2 + (dir < 0 ? 1 : 0);
//     };
//
//     // --- Greedy meshing ---
//     for (int axis = 0; axis < 3; ++axis)
//     {
//         const int u = (axis + 1) % 3;
//         const int v = (axis + 2) % 3;
//
//         constexpr int dims[3] = { W, H, D };
//
//         int x[3] = { 0, 0, 0 };
//         int q[3] = { 0, 0, 0 };
//
//         for (int dir = -1; dir <= 1; dir += 2)
//         {
//             q[axis] = dir;
//
//             struct MaskEntry { bool visible; uint8_t blockType; };
//             thread_local std::vector<MaskEntry> mask;
//             mask.assign(std::max({W, H, D}) * std::max({W, H, D}), {false, 0});
//
//             for (x[axis] = -1; x[axis] < dims[axis]; ++x[axis])
//             {
//                 int n = 0;
//
//                 // Build mask for this slice
//                 for (x[v] = 0; x[v] < dims[v]; ++x[v])
//                     for (x[u] = 0; x[u] < dims[u]; ++x[u])
//                     {
//                         int cx = x[0], cy = x[1], cz = x[2];
//                         int nx = cx + q[0], ny = cy + q[1], nz = cz + q[2];
//
//                         // int nxi = std::clamp(nx + 1, 0, W + 1);
//                         // int nyi = std::clamp(ny + 1, 0, H + 1);
//                         // int nzi = std::clamp(nz + 1, 0, D + 1);
//
//                         const RenderType self     = renderType[cx][cy][cz];
//                         const RenderType neighbor = renderType[nx][ny][nz];
//
//                         const uint8_t bt = blockTypes[cx + 1][cy + 1][cz + 1];
//
//                         mask[n].visible   = bt != 0 && shouldRenderFace(self, neighbor);
//                         mask[n].blockType = bt;
//
//                         ++n;
//                     }
//
//                 n = 0;
//                 uint32_t base = vertices.size();
//
//                 // --- Greedy merge ---
//                 for (int j = 0; j < dims[v]; ++j) {
//                     for (int i = 0; i < dims[u];) {
//                         if (!mask[n].visible) { ++i; ++n; continue; }
//
//                         int w = 1;
//                         const uint8_t bt = mask[n].blockType;
//
//                         while (i + w < dims[u] && mask[n + w].visible && mask[n + w].blockType == bt) ++w;
//
//                         int h = 1;
//                         for (; j + h < dims[v]; ++h)
//                         {
//                             for (int k = 0; k < w; ++k)
//                                 if (!mask[n + k + h * dims[u]].visible || mask[n + k + h * dims[u]].blockType != bt)
//                                     goto done_merge;
//                         }
//                         done_merge:
//
//                         x[u] = i; x[v] = j;
//
//                         float verts[4][3];
//                         for (auto& vert : verts) {
//                             vert[0] = static_cast<float>(x[0]);
//                             vert[1] = static_cast<float>(x[1]);
//                             vert[2] = static_cast<float>(x[2]);
//                         }
//
//                         verts[1][u] += w;
//                         verts[2][u] += w; verts[2][v] += h;
//                         verts[3][v] += h;
//
//                         if (dir > 0) for (auto& vert : verts) vert[axis] += 1.0f;
//
//                         // X and Y
//                         float uv[4][2] = {{0, 0}, {float(w), 0}, {float(w),float(h)}, {0,float(h)}};
//
//                         if (axis == 2) // Z -> swap or flip v
//                         {
//                             uv[0][1] = float(h);
//                             uv[1][1] = float(h);
//                             uv[2][1] = 0;
//                             uv[3][1] = 0;
//                         }
//
//                         const uint16_t tex = textureIndices[bt];
//                         const uint8_t normalIndex = normalToIndex(axis, dir);
//                         constexpr uint8_t AO_MAX = 3;
//
//                         vertices.push_back({ {verts[0][0], verts[0][1], verts[0][2]}, {uv[0][0],uv[0][1]}, tex, normalIndex, AO_MAX });
//                         vertices.push_back({ {verts[1][0], verts[1][1], verts[1][2]}, {uv[1][0],uv[1][1]}, tex, normalIndex, AO_MAX });
//                         vertices.push_back({ {verts[2][0], verts[2][1], verts[2][2]}, {uv[2][0],uv[2][1]}, tex, normalIndex, AO_MAX });
//                         vertices.push_back({ {verts[3][0], verts[3][1], verts[3][2]}, {uv[3][0],uv[3][1]}, tex, normalIndex, AO_MAX });
//
//                         if (dir > 0)
//                             indices.insert(indices.end(), { base, base + 1, base + 2, base, base + 2, base + 3 });
//                         else
//                             indices.insert(indices.end(), { base, base + 3, base + 2, base, base + 2, base + 1 });
//
//                         for (int l = 0; l < h; ++l)
//                             for (int k = 0; k < w; ++k)
//                                 mask[n + k + l * dims[u]].visible = false;
//
//                         base += 4;
//                         i += w;
//                         n += w;
//                     }
//                 }
//             }
//         }
//     }
//
//     // --- Apply chunk offset ---
//     const glm::vec3 offset(coord.x * W, 0.0f, coord.y * D);
//     for (auto& v : vertices) v.position += offset;
//
//     chunk.isMeshDirty = false;
//
//     // // --- Mark neighbor chunks for AO recalculation ---
//     // std::lock_guard lock(chunk_mutex);
//     // for (int dz = -1; dz <= 1; ++dz)
//     //     for (int dx = -1; dx <= 1; ++dx)
//     //         if (dx != 0 || dz != 0)
//     //             if (auto it = chunks.find(coord + ChunkCoord(dx, dz)); it != chunks.end())
//     //                 it->second.aoCalculated = false;
// }
