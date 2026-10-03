#include "World.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>

#define STB_PERLIN_IMPLEMENTATION
#include <ranges>

#include "stb_perlin.h"

/* ============================================================
 * Helpers
 * ============================================================ */

namespace
{
    constexpr int BASE_HEIGHT = ChunkColumn::HEIGHT / 8;
    constexpr int MIN_HEIGHT  = 1;
    constexpr int MAX_HEIGHT  = ChunkColumn::HEIGHT - 3;

    constexpr int MAX_CHUNKS_PER_UPDATE = 32;

    [[nodiscard]]
    int floorDiv(const int value, const int divisor) noexcept
    {
        const int q = value / divisor;
        const int r = value % divisor;

        if (r != 0 && ((r < 0) != (divisor < 0)))
            return q - 1;

        return q;
    }

    [[nodiscard]]
    float terrainNoise(const int worldX, const int worldZ) noexcept
    {
        constexpr int SEED = 0;

        return stb_perlin_noise3_seed(
            static_cast<float>(worldX) * 0.05f,
            0.0f,
            static_cast<float>(worldZ) * 0.05f,
            0,
            0,
            0,
            SEED
        );
    }
}


/* ============================================================
 * Accessors
 * ============================================================ */

const std::unordered_map<ChunkCoord,std::unique_ptr<ChunkColumn>>& World::getChunks() const noexcept {
    return chunks;
}

const BlockRegistry& World::getBlockRegistry() const noexcept {
    return blockRegistry;
}

const std::vector<ChunkRenderState>& World::getRenderStates() const noexcept {
    return renderStates;
}

std::vector<ChunkRenderState>& World::getRenderStates() noexcept {
    return renderStates;
}

std::vector<GpuMesh> World::pruneRenderStates() {
    std::vector<GpuMesh> retiredMeshes;
    std::lock_guard lock(chunk_mutex);

    for (auto it = renderStates.begin(); it != renderStates.end();) {
        if (chunks.contains(it->coord)) {
            ++it;
            continue;
        }

        retiredMeshes.push_back(it->gpuMesh);
        it = renderStates.erase(it);
    }

    for (auto it = meshedSections.begin(); it != meshedSections.end();) {
        if (chunks.contains(it->first)) {
            ++it;
            continue;
        }

        it = meshedSections.erase(it);
    }

    return retiredMeshes;
}

/* ============================================================
 * Chunk streaming
 * ============================================================ */

void World::updateChunks(const glm::vec3& playerPos, ThreadPool& threadPool) {
    const glm::ivec2 newPlayerChunk{
        floorDiv(static_cast<int>(std::floor(playerPos.x)), ChunkSection::SIZE),
        floorDiv(static_cast<int>(std::floor(playerPos.z)), ChunkSection::SIZE)
    };

    playerChunk = newPlayerChunk;

    std::vector<ChunkCoord> loadQueue;
    std::vector<ChunkCoord> unloadQueue;

    /*
     * ----------------------------------------------------------
     * Determine chunks that need loading.
     * ----------------------------------------------------------
     *
     * We walk the rings around the player so chunks close to the
     * player enter the queue first.
     */
    loadQueue.reserve(static_cast<std::size_t>(CHUNK_LOAD_DIAMETER * CHUNK_LOAD_DIAMETER));

    for (int radius = 0; radius <= CHUNK_LOAD_RADIUS; ++radius)
    {
        for (int dx = -radius; dx <= radius; ++dx)
        {
            for (int dz = -radius; dz <= radius; ++dz)
            {
                if (std::abs(dx) != radius && std::abs(dz) != radius)
                    continue;

                const ChunkCoord coord{playerChunk.x + dx, playerChunk.y + dz};

                std::lock_guard lock(chunk_mutex);

                if (chunks.contains(coord))
                    continue;

                if (generatingChunks.contains(coord))
                    continue;

                loadQueue.push_back(coord);
            }
        }
    }

    /*
     * Sort by squared distance.
     *
     * This makes the queue strictly nearest-first rather than
     * merely ring-first.
     */
    std::ranges::sort(
        loadQueue,
        [this](const ChunkCoord& a, const ChunkCoord& b)
        {
            const std::int64_t ax = static_cast<std::int64_t>(a.x) - playerChunk.x;
            const std::int64_t az = static_cast<std::int64_t>(a.z) - playerChunk.y;
            const std::int64_t bx = static_cast<std::int64_t>(b.x) - playerChunk.x;
            const std::int64_t bz = static_cast<std::int64_t>(b.z) - playerChunk.y;

            const std::int64_t distanceA = ax * ax + az * az;
            const std::int64_t distanceB = bx * bx + bz * bz;

            return distanceA < distanceB;
        }
    );

    /*
     * ----------------------------------------------------------
     * Determine chunks that should be unloaded.
     * ----------------------------------------------------------
     *
     * Keep a one-chunk safety border around the render radius.
     */
    {
        std::lock_guard lock(chunk_mutex);

        unloadQueue.reserve(chunks.size());

        for (const auto &coord: chunks | std::views::keys)
        {
            const std::int64_t dx = static_cast<std::int64_t>(coord.x) - playerChunk.x;
            const std::int64_t dz = static_cast<std::int64_t>(coord.z) - playerChunk.y;

            if (std::abs(dx) > CHUNK_LOAD_RADIUS || std::abs(dz) > CHUNK_LOAD_RADIUS)
                unloadQueue.push_back(coord);
        }
    }

    /*
     * ----------------------------------------------------------
     * Schedule generation.
     * ----------------------------------------------------------
     *
     * Workers NEVER modify the chunks map directly until their
     * generated ChunkColumn is complete.
     *
     * The worker only owns its local unique_ptr while generating.
     */
    int spawned = 0;

    for (const ChunkCoord coord : loadQueue)
    {
        if (spawned >= MAX_CHUNKS_PER_UPDATE)
            break;

        {
            std::lock_guard lock(chunk_mutex);

            if (chunks.contains(coord) || generatingChunks.contains(coord))
                continue;

            generatingChunks.insert(coord);
        }

        ++spawned;

        threadPool.enqueueFireAndForget(
            [this, coord]
            {
                try {
                    auto column = std::make_unique<ChunkColumn>(coord);

                    generateTerrain(*column, {coord.x, coord.z});

                    /*
                     * Publish the fully generated column.
                     *
                     * Nothing else can observe this ChunkColumn
                     * before it has been completely generated.
                     */
                    {
                        std::lock_guard lock(chunk_mutex);

                        /*
                         * The player may have moved while this job
                         * was running. The chunk can still be useful
                         * if it remains inside the current unload
                         * radius, so don't make the worker depend on
                         * the player's current position.
                         */
                        chunks[coord] = std::move(column);
                        generatingChunks.erase(coord);
                    }
                }
                catch (std::exception const& e) {
                    std::lock_guard lock(chunk_mutex);

                    if (chunks.contains(coord))
                        chunks.erase(coord);
                    if (generatingChunks.contains(coord))
                        generatingChunks.erase(coord);

                    std::cerr
                        << "Failed to generate chunk at " << coord.x << ", " << coord.z
                        << " : " << e.what()
                        << std::endl;
                }
            }
        );
    }

    /*
     * ----------------------------------------------------------
     * Unload.
     * ----------------------------------------------------------
     *
     * Never erase a chunk that is still being generated.
     */
    {
        std::lock_guard lock(chunk_mutex);

        int unloaded = 0;

        for (const ChunkCoord& coord : unloadQueue)
        {
            if (unloaded >= MAX_CHUNKS_PER_UPDATE)
                break;

            if (generatingChunks.contains(coord))
                continue;

            if (chunks.erase(coord) != 0)
                ++unloaded;
        }
    }
}

/* ============================================================
 * Terrain generation
 * ============================================================ */

void World::buildRenderStates() {
    /*
     * Snapshot the currently loaded columns.
     *
     * We intentionally do NOT hold chunk_mutex while meshing.
     * Meshing all sections can be relatively expensive.
     *
     * NOTE:
     * This assumes ChunkColumn objects remain alive while they are
     * loaded. If chunks can be erased concurrently, the snapshot
     * should use shared_ptr instead of raw pointers.
     */
    std::vector<std::pair<ChunkCoord, ChunkColumn*>> loadedChunks;

    {
        std::lock_guard lock(chunk_mutex);

        loadedChunks.reserve(chunks.size());

        for (auto& [coord, column] : chunks){
            if (!column)
                continue;

            loadedChunks.emplace_back(coord, column.get());
        }
    }

    for (const auto& [coord, column] : loadedChunks)
    {
        const auto sectionIt = meshedSections.find(coord);
        if (sectionIt != meshedSections.end() && sectionIt->second == 0xFFFFu)
            continue;

        /*
         * Check that the complete 3x3 horizontal neighborhood exists.
         * We include the center chunk here as well.
         */
        bool neighborsReady = true;

        {
            std::lock_guard lock(chunk_mutex);

            for (int dz = -1; dz <= 1 && neighborsReady; ++dz) {
                for (int dx = -1; dx <= 1; ++dx) {
                    const ChunkCoord neighborCoord{coord.x + dx, coord.z + dz};

                    if (!chunks.contains(neighborCoord)) {
                        neighborsReady = false;
                        break;
                    }
                }
            }
        }

        if (!neighborsReady)
            continue;

        /*
         * Build the complete 3x3 neighborhood.
         */
        ChunkNeighborhood neighborhood;

        {
            std::lock_guard lock(chunk_mutex);

            for (int dz = -1; dz <= 1; ++dz) {
                for (int dx = -1; dx <= 1; ++dx) {
                    const ChunkCoord neighborCoord{coord.x + dx, coord.z + dz};
                    const auto it = chunks.find(neighborCoord);

                    if (it != chunks.end() && it->second)
                        neighborhood.setNeighbor(dx, dz, it->second.get());
                }
            }

            neighborhood.setCenter(column);
        }

        /*
         * One render state represents one 16x16x16 section.
         * The packed vertex stores section-local coordinates:
         *
         *      X = [0, 16]
         *      Y = [0, 16]
         *      Z = [0, 16]
         *
         * The section's worldPosition provides the section offset.
         */
        std::uint16_t& sectionMask = meshedSections[coord];

        for (std::uint8_t sectionY = 0; sectionY < ChunkColumn::SECTION_COUNT; ++sectionY)
        {
            const std::uint16_t sectionBit = static_cast<std::uint16_t>(1u << sectionY);

            if ((sectionMask & sectionBit) != 0)
                continue;

            ChunkRenderState renderState{};

            renderState.coord = coord;

            /*
             * This render state represents this vertical section.
             */
            renderState.sectionY = sectionY;

            renderState.worldPosition = {
                static_cast<float>(coord.x * ChunkSection::SIZE),
                static_cast<float>(sectionY * ChunkSection::SIZE),
                static_cast<float>(coord.z * ChunkSection::SIZE)
            };

            renderState.worldMin = renderState.worldPosition;

            renderState.worldMax = {
                renderState.worldPosition.x + static_cast<float>(ChunkSection::SIZE),
                renderState.worldPosition.y + static_cast<float>(ChunkSection::SIZE),
                renderState.worldPosition.z + static_cast<float>(ChunkSection::SIZE)
            };

            /*
             * Build this section.
             * The mesher produces section-local vertices.
             *
             * DO NOT add sectionY to the packed Y coordinate.
             */
            ChunkMesher::build(column->section(sectionY), sectionY, neighborhood, blockRegistry, renderState.mesh);
            sectionMask |= sectionBit;

            /* Don't create an empty render state */
            if (!renderState.mesh.empty())
                renderStates.push_back(std::move(renderState));
        }
    }
}



void World::generateTerrain(ChunkColumn& column, const glm::ivec2& coord) {
    /*
     * The entire column is initially Air because ChunkSection's
     * storage is zero initialized and Air == 0.
     */

    for (int x = 0; x < ChunkSection::SIZE; ++x)
    {
        for (int z = 0; z < ChunkSection::SIZE; ++z)
        {
            const int worldX = coord.x * ChunkSection::SIZE + x;
            const int worldZ = coord.y * ChunkSection::SIZE + z;
            const float noise = terrainNoise(worldX, worldZ);

            int height = BASE_HEIGHT + static_cast<int>(noise * 25.0f);
            height = std::clamp(height, MIN_HEIGHT, MAX_HEIGHT);

            /*
             * Fill from the bottom up.
             *
             * ChunkColumn::setUnchecked() handles section
             * selection internally.
             */
            for (int y = MIN_HEIGHT; y <= height; ++y)
            {
                BlockType type;

                if (y == height)
                    type = BlockType::Grass;
                else if (y >= height - 4)
                    type = BlockType::Dirt;
                else
                    type = BlockType::Stone;

                column.setUnchecked(x, y, z, toBlockId(type));
            }
        }
    }
}
