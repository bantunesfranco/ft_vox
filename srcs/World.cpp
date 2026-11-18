#include "World.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>
#include <filesystem>
#include <ranges>
#include <fstream>
#include <chrono>

#include <glm/gtx/hash.hpp>

#include "FrameProfiler.hpp"
#include "RegionStore.hpp"

#include "worldgen/TerrainGenerator.hpp"


/* ============================================================
 * Helpers
 * ============================================================ */

namespace
{
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

    void appendSectionMesh(MeshData& columnMesh, const MeshData& sectionMesh, const int sectionY)
    {
        // Section-local y becomes column y; everything else (word b too) is kept.
        constexpr std::uint32_t Y_MASK = 0x1FFu << 5u;
        for (PackedVertex vertex : sectionMesh.vertices) {
            const std::uint32_t localY = (vertex.a & Y_MASK) >> 5u;
            vertex.a = (vertex.a & ~Y_MASK)
                     | (((localY + static_cast<std::uint32_t>(sectionY * ChunkSection::SIZE)) & 0x1FFu) << 5u);
            columnMesh.vertices.push_back(vertex);
        }
    }
}


/* ============================================================
 * Accessors
 * ============================================================ */

std::shared_ptr<const ColumnLight> World::lightOf(const ChunkCoord& coord) const {
    std::lock_guard lock(chunk_mutex);
    const auto it = lights.find(coord);
    return it == lights.end() ? nullptr : it->second;
}

bool World::lightIdle() const {
    std::lock_guard lock(chunk_mutex);
    if (lightJobs.inFlight() > 0)
        return false;
    {
        std::lock_guard results(resultMutex);
        if (!lightResults.empty())
            return false;
    }
    for (const auto& coord : chunks | std::views::keys) {
        bool windowLoaded = true;
        for (int dz = -1; dz <= 1 && windowLoaded; ++dz)
            for (int dx = -1; dx <= 1 && windowLoaded; ++dx)
                windowLoaded = chunks.contains({coord.x + dx, coord.z + dz});
        if (!windowLoaded)
            continue;
        const ColumnJobTracker::Status* status = lightJobs.find(coord);
        if (!status || !status->current())
            return false;
    }
    return true;
}

void World::invalidateLightLocked(const ChunkCoord& coord) {
    for (int dz = -1; dz <= 1; ++dz)
        for (int dx = -1; dx <= 1; ++dx)
            lightJobs.invalidate({coord.x + dx, coord.z + dz});   // a job in flight is now stale
}

void World::applyLightResults() {
    std::vector<LightResult> finished;
    {
        std::lock_guard results(resultMutex);
        finished.swap(lightResults);
    }
    if (finished.empty())
        return;
    const std::uint64_t currentEpoch = epoch.load(std::memory_order_acquire);
    std::lock_guard lock(chunk_mutex);
    for (LightResult& result : finished) {
        if (result.epoch != currentEpoch)
            continue;                                   // from before clear()
        // Dropped when the column was unloaded or its blocks changed meanwhile (a new job follows).
        if (!lightJobs.finish(result.coord, result.version, result.light && chunks.contains(result.coord)))
            continue;
        lights[result.coord] = std::move(result.light);
        // Its faces and its neighbours' border faces show this light.
        for (int dz = -1; dz <= 1; ++dz)
            for (int dx = -1; dx <= 1; ++dx)
                invalidateMeshLocked({result.coord.x + dx, result.coord.z + dz});
    }
}

void World::scheduleLightJobs(const glm::vec3& playerPosition, ThreadPool& threadPool) {
    const std::size_t maxInFlight = std::max<std::size_t>(2, threadPool.getWorkerCount() * 2);
    if (!lightJobs.hasRoom(maxInFlight))
        return;

    struct Candidate {
        ChunkCoord coord;
        float distance;
    };
    std::vector<Candidate> candidates;
    std::lock_guard lock(chunk_mutex);
    for (const auto& coord : chunks | std::views::keys) {
        const ColumnJobTracker::Status& status = lightJobs.track(coord);
        if (status.inFlight || status.current())
            continue;
        bool windowLoaded = true;
        for (int dz = -1; dz <= 1 && windowLoaded; ++dz)
            for (int dx = -1; dx <= 1 && windowLoaded; ++dx)
                windowLoaded = chunks.contains({coord.x + dx, coord.z + dz});
        if (!windowLoaded)
            continue;
        const float cx = (static_cast<float>(coord.x) + 0.5f) * static_cast<float>(ChunkSection::SIZE) - playerPosition.x;
        const float cz = (static_cast<float>(coord.z) + 0.5f) * static_cast<float>(ChunkSection::SIZE) - playerPosition.z;
        candidates.push_back({coord, cx * cx + cz * cz});
    }

    const std::size_t slots = std::min(candidates.size(), lightJobs.room(maxInFlight));
    std::ranges::partial_sort(candidates, candidates.begin() + static_cast<std::ptrdiff_t>(slots),
        [](const Candidate& a, const Candidate& b) { return a.distance < b.distance; });

    const std::uint64_t jobEpoch = epoch.load(std::memory_order_acquire);
    for (std::size_t i = 0; i < slots; ++i) {
        const ChunkCoord coord = candidates[i].coord;
        // The job's own references: these columns stay alive and unchanged
        // while it runs (writers copy a shared column first, see setBlockAt).
        std::array<ColumnPtr, 9> window{};
        for (int dz = -1; dz <= 1; ++dz)
            for (int dx = -1; dx <= 1; ++dx)
                window[static_cast<std::size_t>((dz + 1) * 3 + (dx + 1))] = chunks.at({coord.x + dx, coord.z + dz});
        const std::uint64_t version = lightJobs.start(coord);
        threadPool.enqueueFireAndForget([this, coord, version, jobEpoch, window] {
            Trace::Scope span("light column");
            LightResult result{coord, version, jobEpoch, nullptr};
            try {
                ColumnWindow columns{};
                for (std::size_t c = 0; c < columns.size(); ++c)
                    columns[c] = window[c].get();
                result.light = computeLight(columns, blockRegistry);
            } catch (const std::exception& e) {
                std::cerr << "Failed to light chunk " << coord.x << ", " << coord.z << ": " << e.what() << std::endl;
            }
            std::lock_guard results(resultMutex);
            lightResults.push_back(std::move(result));
        });
    }
}

const BlockRegistry& World::getBlockRegistry() const noexcept {
    return blockRegistry;
}

World::MemoryStats World::memoryStats() const {
    const auto capacityBytes = [](const MeshData& mesh) {
        return mesh.vertices.capacity() * sizeof(PackedVertex);
    };
    std::lock_guard lock(chunk_mutex);
    MemoryStats stats;
    stats.columns = chunks.size();
    for (const ColumnPtr& column : chunks | std::views::values)
        stats.blockBytes += column->heapBytes();
    for (const auto& light : lights | std::views::values)
        stats.lightBytes += light->heapBytes();
    for (const ChunkRenderState& state : renderStates)
        stats.meshCpuBytes += capacityBytes(state.mesh) + capacityBytes(state.transparentMesh);
    return stats;
}

const std::vector<ChunkRenderState>& World::getRenderStates() const noexcept {
    return renderStates;
}

std::vector<ChunkRenderState>& World::getRenderStates() noexcept {
    return renderStates;
}

// Retires the meshes of columns that are gone, plus meshes replaced since the
// last call. Linear: one pass, no erase from the middle of a vector.
std::vector<GpuMesh> World::pruneRenderStates() {
    std::vector<GpuMesh> retiredMeshes;
    std::lock_guard lock(chunk_mutex);
    retiredMeshes.swap(pendingRetiredMeshes);

    std::erase_if(renderStates, [&](ChunkRenderState& state) {
        if (chunks.contains(state.coord))
            return false;
        retiredMeshes.push_back(state.gpuMesh);
        retiredMeshes.push_back(state.transparentGpuMesh);
        return true;
    });
    meshJobs.eraseIf([this](const ChunkCoord& coord, const ColumnJobTracker::Status&) {
        return !chunks.contains(coord);
    });
    std::erase_if(lights, [this](const auto& entry) { return !chunks.contains(entry.first); });
    // A status whose job is running stays until the result comes back; an
    // unloaded idle one goes.
    lightJobs.eraseIf([this](const ChunkCoord& coord, const ColumnJobTracker::Status& status) {
        return !chunks.contains(coord) && !status.inFlight;
    });

    return retiredMeshes;
}

bool World::getBlockAt(const glm::ivec3& worldPos, BlockId& block) const {
    if (worldPos.y < ChunkColumn::MIN_WORLD_Y || worldPos.y >= ChunkColumn::MAX_WORLD_Y)
        return false;

    const int chunkX = floorDiv(worldPos.x, ChunkSection::SIZE);
    const int chunkZ = floorDiv(worldPos.z, ChunkSection::SIZE);
    const int localX = worldPos.x - chunkX * ChunkSection::SIZE;
    const int localZ = worldPos.z - chunkZ * ChunkSection::SIZE;

    std::lock_guard lock(chunk_mutex);
    const auto it = chunks.find({chunkX, chunkZ});
    if (it == chunks.end() || !it->second)
        return false;

    block = it->second->getUnchecked(localX, ChunkColumn::toStorageY(worldPos.y), localZ);
    return true;
}

bool World::getSurfaceHeight(const int worldX, const int worldZ, int& height) const {
    const int chunkX = floorDiv(worldX, ChunkSection::SIZE);
    const int chunkZ = floorDiv(worldZ, ChunkSection::SIZE);
    const int localX = worldX - chunkX * ChunkSection::SIZE;
    const int localZ = worldZ - chunkZ * ChunkSection::SIZE;

    std::lock_guard lock(chunk_mutex);
    const auto it = chunks.find({chunkX, chunkZ});
    if (it == chunks.end() || !it->second)
        return false;

    for (int storageY = ChunkColumn::HEIGHT - 1; storageY >= 0; --storageY) {
        if (!blockRegistry.isOpaque(it->second->getUnchecked(localX, storageY, localZ)))
            continue;

        height = ChunkColumn::toWorldY(storageY);
        return true;
    }

    return false;
}

bool World::setBlockAt(const glm::ivec3& worldPos, const BlockId block) {
    if (worldPos.y < ChunkColumn::MIN_WORLD_Y || worldPos.y >= ChunkColumn::MAX_WORLD_Y
        || block >= BLOCK_TYPE_COUNT)
        return false;

    const int chunkX = floorDiv(worldPos.x, ChunkSection::SIZE);
    const int chunkZ = floorDiv(worldPos.z, ChunkSection::SIZE);
    const int localX = worldPos.x - chunkX * ChunkSection::SIZE;
    const int localZ = worldPos.z - chunkZ * ChunkSection::SIZE;

    std::lock_guard lock(chunk_mutex);
    const auto columnIt = chunks.find({chunkX, chunkZ});
    if (columnIt == chunks.end() || !columnIt->second)
        return false;

    // Copy-on-write: a mesh job may still be reading this column.
    if (columnIt->second.use_count() > 1)
        columnIt->second = std::make_shared<ChunkColumn>(*columnIt->second);
    ChunkColumn& column = *columnIt->second;
    const int storageY = ChunkColumn::toStorageY(worldPos.y);
    const BlockId currentBlock = column.getUnchecked(localX, storageY, localZ);
    if (currentBlock == toBlockId(BlockType::Bedrock) && block != currentBlock)
        return false;
    if (currentBlock == block)
        return false;

    column.setUnchecked(localX, storageY, localZ, block);

    static const std::array<glm::ivec3, 7> affectedOffsets = {{
        {0, 0, 0}, {1, 0, 0}, {-1, 0, 0}, {0, 1, 0},
        {0, -1, 0}, {0, 0, 1}, {0, 0, -1}
    }};

    for (const glm::ivec3& offset : affectedOffsets) {
        const glm::ivec3 affected = worldPos + offset;
        if (affected.y < ChunkColumn::MIN_WORLD_Y || affected.y >= ChunkColumn::MAX_WORLD_Y)
            continue;

        const int affectedChunkX = floorDiv(affected.x, ChunkSection::SIZE);
        const int affectedChunkZ = floorDiv(affected.z, ChunkSection::SIZE);
        invalidateMeshLocked({affectedChunkX, affectedChunkZ});
    }

    dirty.insert({chunkX, chunkZ});
    invalidateLightLocked({chunkX, chunkZ});

    return true;
}

void World::invalidateMeshLocked(const ChunkCoord& coord) {
    meshJobs.invalidate(coord);        // a result in flight is now stale; never meshed: nothing to redo
}

/* ============================================================
 * Multiplayer hooks
 * ============================================================ */

void World::clear() {
    std::lock_guard lock(chunk_mutex);
    epoch.fetch_add(1, std::memory_order_acq_rel);   // running jobs now drop their result

    for (ChunkRenderState& state : renderStates) {
        pendingRetiredMeshes.push_back(std::move(state.gpuMesh));
        pendingRetiredMeshes.push_back(std::move(state.transparentGpuMesh));
    }
    renderStates.clear();
    meshJobs.reset();                  // their results carry the old epoch: never counted back
    readyResults.clear();
    {
        std::lock_guard results(resultMutex);
        meshResults.clear();
    }
    generatingChunks.clear();
    chunks.clear();
    lights.clear();
    lightJobs.reset();                 // their results carry the old epoch
    {
        std::lock_guard results(resultMutex);
        lightResults.clear();
    }
    dirty.clear();
    playerChunk = {std::numeric_limits<int>::max(), std::numeric_limits<int>::max()};
}

void World::adoptColumn(std::unique_ptr<ChunkColumn> column) {
    if (!column)
        return;
    const ChunkCoord coord = column->coord();

    std::lock_guard lock(chunk_mutex);
    chunks[coord] = std::move(column);

    // A replaced column changes its own mesh and the border faces of its
    // neighbours; a new one can only complete neighbourhoods (cheap either way).
    for (int dz = -1; dz <= 1; ++dz)
        for (int dx = -1; dx <= 1; ++dx)
            invalidateMeshLocked({coord.x + dx, coord.z + dz});
    invalidateLightLocked(coord);
}

void World::dropColumn(const ChunkCoord& coord) {
    std::lock_guard lock(chunk_mutex);
    chunks.erase(coord);                             // pruneRenderStates() retires its meshes
    lights.erase(coord);
    lightJobs.erase(coord);
    dirty.erase(coord);
}

World::World() : generator(std::make_shared<worldgen::TerrainGenerator>(0)) {}

void World::open(const std::uint64_t seed, std::shared_ptr<RegionStore> newStore) {
    clear();
    std::lock_guard lock(chunk_mutex);
    worldSeed = seed;
    store = std::move(newStore);
    // Running jobs keep the generator they started with (shared_ptr).
    generator = std::make_shared<worldgen::TerrainGenerator>(seed);
}

int World::generatedSurfaceHeight(const int x, const int z) const {
    std::lock_guard lock(chunk_mutex);
    return generator->surfaceHeight(x, z);
}

glm::ivec2 World::spawnPoint() const {
    std::lock_guard lock(chunk_mutex);
    return generator->findSpawn();
}

void World::enqueueSave(ThreadPool& threadPool, ColumnPtr column) {
    {
        std::lock_guard lock(saveMutex);
        ++savesInFlight;
    }
    threadPool.enqueueFireAndForget([this, columnStore = store, column = std::move(column)] {
        {
            Trace::Scope span("save column");
            if (!columnStore->save(*column))
                std::cerr << "Failed to save chunk " << column->coord().x << ", " << column->coord().z << std::endl;
        }
        std::lock_guard lock(saveMutex);
        --savesInFlight;
        saveDone.notify_all();
    });
}

void World::flush(ThreadPool& threadPool) {
    {
        std::lock_guard lock(chunk_mutex);
        if (store) {
            for (const ChunkCoord& coord : dirty) {
                const auto it = chunks.find(coord);
                if (it != chunks.end())
                    enqueueSave(threadPool, it->second);
            }
        }
        dirty.clear();
    }
    std::unique_lock lock(saveMutex);
    saveDone.wait(lock, [this] { return savesInFlight == 0; });
}

World::~World() {
    // Save jobs point at this World: never let one outlive it.
    std::unique_lock lock(saveMutex);
    saveDone.wait(lock, [this] { return savesInFlight == 0; });
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

        const std::uint64_t jobEpoch = epoch.load(std::memory_order_acquire);
        threadPool.enqueueFireAndForget(
            [this, coord, jobEpoch, columnStore = store, columnGenerator = generator]
            {
                try {
                    // A saved column is used as is; otherwise generate it.
                    std::unique_ptr<ChunkColumn> column;
                    if (columnStore) {
                        Trace::Scope span("load column");
                        column = columnStore->load(coord);
                    }
                    const bool generated = column == nullptr;
                    if (generated) {
                        Trace::Scope span("generate column");
                        column = std::make_unique<ChunkColumn>(coord);
                        columnGenerator->generate(*column);
                    }

                    /*
                     * Publish the fully generated column.
                     *
                     * Nothing else can observe this ChunkColumn
                     * before it has been completely generated.
                     */
                    {
                        std::lock_guard lock(chunk_mutex);

                        // clear() ran meanwhile (session changed): this column
                        // belongs to a world that no longer exists.
                        if (epoch.load(std::memory_order_acquire) != jobEpoch)
                            return;

                        /*
                         * The player may have moved while this job
                         * was running. The chunk can still be useful
                         * if it remains inside the current unload
                         * radius, so don't make the worker depend on
                         * the player's current position.
                         */
                        chunks[coord] = std::move(column);
                        generatingChunks.erase(coord);
                        invalidateLightLocked(coord);   // neighbours' windows changed
                        if (generated)
                            dirty.insert(coord);
                    }
                }
                catch (std::exception const& e) {
                    std::lock_guard lock(chunk_mutex);
                    if (epoch.load(std::memory_order_acquire) != jobEpoch)
                        return;

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

            const auto it = chunks.find(coord);
            if (it == chunks.end())
                continue;
            if (dirty.erase(coord) != 0 && store)
                enqueueSave(threadPool, it->second);   // a snapshot: the column is gone from the map
            chunks.erase(it);
            ++unloaded;
        }
    }
}

/* ============================================================
 * Meshing
 * ============================================================ */

namespace
{
    // Every render state of one column: one opaque mesh for the whole column
    // (sections merged), one transparent mesh per section that has any.
    // Pure (reads its inputs only), so it runs on worker threads.
    std::vector<ChunkRenderState> meshColumn(
        const ChunkColumn& column,
        const ChunkNeighborhood& neighborhood,
        const BlockRegistry& registry
    ) {
        const ChunkCoord coord = column.coord();
        std::vector<ChunkRenderState> states;

        ChunkRenderState opaqueState{};
        opaqueState.coord = coord;
        opaqueState.sectionY = 0;
        opaqueState.worldPosition = {
            static_cast<float>(coord.x * ChunkSection::SIZE),
            static_cast<float>(ChunkColumn::MIN_WORLD_Y),
            static_cast<float>(coord.z * ChunkSection::SIZE)
        };

        MeshData sectionOpaqueMesh;
        MeshData sectionTransparentMesh;
        int lowestOpaqueSection = ChunkColumn::SECTION_COUNT;
        int highestOpaqueSection = -1;

        for (std::uint8_t sectionY = 0; sectionY < ChunkColumn::SECTION_COUNT; ++sectionY)
        {
            ChunkMesher::build(
                column.section(sectionY),
                sectionY,
                neighborhood,
                registry,
                sectionOpaqueMesh,
                sectionTransparentMesh
            );

            if (!sectionOpaqueMesh.empty()) {
                appendSectionMesh(opaqueState.mesh, sectionOpaqueMesh, sectionY);
                lowestOpaqueSection = std::min(lowestOpaqueSection, static_cast<int>(sectionY));
                highestOpaqueSection = std::max(highestOpaqueSection, static_cast<int>(sectionY));
            }

            if (!sectionTransparentMesh.empty()) {
                ChunkRenderState transparentState{};
                transparentState.coord = coord;
                transparentState.sectionY = sectionY;
                transparentState.worldPosition = {
                    opaqueState.worldPosition.x,
                    static_cast<float>(ChunkColumn::toWorldY(sectionY * ChunkSection::SIZE)),
                    opaqueState.worldPosition.z
                };
                transparentState.worldMin = transparentState.worldPosition;
                transparentState.worldMax = transparentState.worldPosition + glm::vec3(static_cast<float>(ChunkSection::SIZE));
                transparentState.transparentMesh = std::move(sectionTransparentMesh);
                sectionTransparentMesh = {};
                states.push_back(std::move(transparentState));
            }
        }

        if (!opaqueState.mesh.empty()) {
            opaqueState.worldMin = {
                opaqueState.worldPosition.x,
                static_cast<float>(ChunkColumn::toWorldY(lowestOpaqueSection * ChunkSection::SIZE)),
                opaqueState.worldPosition.z
            };
            opaqueState.worldMax = {
                opaqueState.worldPosition.x + static_cast<float>(ChunkSection::SIZE),
                static_cast<float>(ChunkColumn::toWorldY((highestOpaqueSection + 1) * ChunkSection::SIZE)),
                opaqueState.worldPosition.z + static_cast<float>(ChunkSection::SIZE)
            };
            states.push_back(std::move(opaqueState));
        }
        return states;
    }
}

void World::buildRenderStates(const glm::vec3& playerPosition, const glm::vec3& playerForward,
                              ThreadPool& threadPool) {
    applyLightResults();
    scheduleLightJobs(playerPosition, threadPool);
    applyMeshResults();
    scheduleMeshJobs(playerPosition, playerForward, threadPool);
}

// Swaps a column's render states for freshly meshed ones. The old GPU meshes
// are released by the next pruneRenderStates(). chunk_mutex held.
void World::replaceRenderStates(const ChunkCoord& coord, std::vector<ChunkRenderState> states) {
    std::erase_if(renderStates, [&](const ChunkRenderState& state) {
        if (state.coord != coord)
            return false;
        pendingRetiredMeshes.push_back(state.gpuMesh);
        pendingRetiredMeshes.push_back(state.transparentGpuMesh);
        return true;
    });
    for (ChunkRenderState& state : states)
        renderStates.push_back(std::move(state));
}

void World::applyMeshResults() {
    {
        std::lock_guard lock(resultMutex);
        for (MeshResult& result : meshResults)
            readyResults.push_back(std::move(result));
        meshResults.clear();
    }
    if (readyResults.empty())
        return;

    // Each applied column is uploaded to the GPU this frame: bound the work.
    constexpr std::size_t MAX_APPLIED_PER_FRAME = 8;
    const std::uint64_t currentEpoch = epoch.load(std::memory_order_acquire);

    std::lock_guard lock(chunk_mutex);
    std::size_t applied = 0;
    auto it = readyResults.begin();
    for (; it != readyResults.end() && applied < MAX_APPLIED_PER_FRAME; ++it) {
        if (it->epoch != currentEpoch)
            continue;                                   // from before clear()
        // Version 0 (a failed job) never matches. Dropped when the column was
        // unloaded or changed meanwhile: it gets a new job.
        if (!meshJobs.finish(it->coord, it->version, chunks.contains(it->coord)))
            continue;
        replaceRenderStates(it->coord, std::move(it->states));
        meshJobs.track(it->coord).waited = 0;
        ++applied;
    }
    readyResults.erase(readyResults.begin(), it);
}

void World::scheduleMeshJobs(const glm::vec3& playerPosition, const glm::vec3& playerForward,
                             ThreadPool& threadPool) {
    const std::size_t maxInFlight = std::max<std::size_t>(2, threadPool.getWorkerCount() * 2);
    if (!meshJobs.hasRoom(maxInFlight))
        return;

    constexpr auto CHUNK_SIZE = static_cast<float>(ChunkSection::SIZE);
    glm::vec2 forward{playerForward.x, playerForward.z};
    if (glm::dot(forward, forward) > 1e-6f)
        forward = glm::normalize(forward);
    else
        forward = {1.0f, 0.0f};

    struct Candidate {
        ChunkCoord coord;
        float priority;
    };
    std::vector<Candidate> candidates;

    std::lock_guard lock(chunk_mutex);
    for (const auto& coord : chunks | std::views::keys) {
        ColumnJobTracker::Status& status = meshJobs.track(coord);
        if (status.current() || status.inFlight)
            continue;

        // The whole 3x3 neighbourhood must exist, with light, or border faces are wrong.
        bool neighborsReady = true;
        for (int dz = -1; dz <= 1 && neighborsReady; ++dz)
            for (int dx = -1; dx <= 1 && neighborsReady; ++dx)
                neighborsReady = chunks.contains({coord.x + dx, coord.z + dz})
                              && lights.contains({coord.x + dx, coord.z + dz});
        if (!neighborsReady)
            continue;

        const float minX = static_cast<float>(coord.x) * CHUNK_SIZE;
        const float minZ = static_cast<float>(coord.z) * CHUNK_SIZE;
        const float maxX = minX + CHUNK_SIZE;
        const float maxZ = minZ + CHUNK_SIZE;
        const float dx = std::max({minX - playerPosition.x, 0.0f, playerPosition.x - maxX});
        const float dz = std::max({minZ - playerPosition.z, 0.0f, playerPosition.z - maxZ});
        const float distanceInChunks = std::sqrt(dx * dx + dz * dz) / CHUNK_SIZE;
        const float centerX = (minX + maxX) * 0.5f - playerPosition.x;
        const float centerZ = (minZ + maxZ) * 0.5f - playerPosition.z;
        const float forwardExtent = CHUNK_SIZE * 0.5f * (std::abs(forward.x) + std::abs(forward.y));
        const bool inFront = centerX * forward.x + centerZ * forward.y + forwardExtent >= 0.0f;

        // Nearby first, then what is in front; age prevents starvation.
        const float safetyBonus = distanceInChunks <= 2.0f ? 100.0f : 0.0f;
        const float viewBonus = inFront ? 8.0f : 0.0f;
        const float ageBonus = std::min(static_cast<float>(++status.waited) * 0.25f, 256.0f);
        candidates.push_back({coord, distanceInChunks - safetyBonus - viewBonus - ageBonus});
    }

    const std::size_t slots = std::min(candidates.size(), meshJobs.room(maxInFlight));
    std::ranges::partial_sort(candidates, candidates.begin() + static_cast<std::ptrdiff_t>(slots),
        [](const Candidate& a, const Candidate& b) { return a.priority < b.priority; });

    const std::uint64_t jobEpoch = epoch.load(std::memory_order_acquire);
    for (std::size_t i = 0; i < slots; ++i) {
        const ChunkCoord coord = candidates[i].coord;

        // The job's own references: these columns stay alive (and unchanged,
        // see setBlockAt) while it runs, whatever the main thread does.
        std::array<ColumnPtr, 9> neighbors{};
        std::array<std::shared_ptr<const ColumnLight>, 9> neighborLights{};
        for (int dz = -1; dz <= 1; ++dz)
            for (int dx = -1; dx <= 1; ++dx) {
                const auto n = static_cast<std::size_t>((dz + 1) * 3 + (dx + 1));
                neighbors[n] = chunks.at({coord.x + dx, coord.z + dz});
                neighborLights[n] = lights.at({coord.x + dx, coord.z + dz});
            }

        const std::uint64_t version = meshJobs.start(coord);
        threadPool.enqueueFireAndForget([this, coord, version, jobEpoch, neighbors, neighborLights] {
            Trace::Scope span("mesh column");
            MeshResult result{coord, 0, jobEpoch, {}};
            try {
                ChunkNeighborhood neighborhood(neighbors[4].get());
                for (int dz = -1; dz <= 1; ++dz)
                    for (int dx = -1; dx <= 1; ++dx) {
                        const auto n = static_cast<std::size_t>((dz + 1) * 3 + (dx + 1));
                        neighborhood.setNeighbor(dx, dz, neighbors[n].get());
                        neighborhood.setLight(dx, dz, neighborLights[n].get());
                    }
                result.states = meshColumn(*neighbors[4], neighborhood, blockRegistry);
                result.version = version;
            } catch (const std::exception& e) {
                // Version 0 never matches: the column is simply scheduled again.
                std::cerr << "Failed to mesh chunk " << coord.x << ", " << coord.z
                          << ": " << e.what() << std::endl;
            }
            std::lock_guard results(resultMutex);
            meshResults.push_back(std::move(result));
        });
    }
}

