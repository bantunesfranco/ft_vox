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

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/hash.hpp>

#define STB_PERLIN_IMPLEMENTATION
#include "stb_perlin.h"


/* ============================================================
 * Helpers
 * ============================================================ */

namespace
{
    constexpr int BASE_HEIGHT = ChunkColumn::HEIGHT / 8;
    constexpr int MIN_HEIGHT  = 1;
    constexpr int MAX_HEIGHT  = ChunkColumn::HEIGHT - 3;
    constexpr int SEA_LEVEL = 40;

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
    float noiseAt(const int worldX, const int worldZ, const float frequency, const int seed) noexcept
    {
        return stb_perlin_noise3_seed(
            static_cast<float>(worldX) * frequency,
            0.0f,
            static_cast<float>(worldZ) * frequency,
            0,
            0,
            0,
            seed
        );
    }

    [[nodiscard]]
    float terrainNoise(const int worldX, const int worldZ) noexcept
    {
        return noiseAt(worldX, worldZ, 0.010f, 0) * 0.65f
            + noiseAt(worldX, worldZ, 0.025f, 1) * 0.25f
            + noiseAt(worldX, worldZ, 0.060f, 2) * 0.10f;
    }

    [[nodiscard]]
    float smoothStep(const float edge0, const float edge1, const float value) noexcept
    {
        const float t = std::clamp((value - edge0) / (edge1 - edge0), 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    }

    struct TerrainProfile {
        float seaWeight;
        float mountainWeight;
        float snowBiomeWeight;
        float desertBiomeWeight;
        int height;
        bool isSea;
    };

    [[nodiscard]]
    TerrainProfile terrainProfileAt(const int worldX, const int worldZ) noexcept
    {
        const float noise = terrainNoise(worldX, worldZ);
        const float continentalness = noiseAt(worldX, worldZ, 0.0018f, 31);
        const float seaWeight = smoothStep(-0.12f, -0.48f, continentalness);
        const float plainsHeight = static_cast<float>(BASE_HEIGHT + 2) + noise * 22.0f;
        const float snowyPlainsHeight = static_cast<float>(BASE_HEIGHT + 4) + noise * 18.0f;
        const float seaFloorHeight = 17.0f + noise * 7.0f;
        float mountainWeight = 0.0f;
        float snowBiomeWeight = 0.0f;
        float desertBiomeWeight = 0.0f;
        float landHeight = 0.0f;

        if (seaWeight <= 0.5f) {
            const float mountainField = noiseAt(worldX, worldZ, 0.0025f, 47);
            mountainWeight = smoothStep(0.16f, 0.52f, mountainField) * (1.0f - seaWeight);
            const float snowField = noiseAt(worldX, worldZ, 0.0016f, 59);
            snowBiomeWeight = smoothStep(0.08f, 0.42f, snowField) * (1.0f - seaWeight);
            const float desertField = noiseAt(worldX, worldZ, 0.0014f, 83);
            desertBiomeWeight = smoothStep(0.06f, 0.42f, desertField)
                * (1.0f - seaWeight)
                * (1.0f - snowBiomeWeight)
                * (1.0f - mountainWeight);

            const float duneNoise = noiseAt(worldX, worldZ, 0.018f, 101);
            const float desertHeight = static_cast<float>(BASE_HEIGHT + 1) + noise * 10.0f
                + std::abs(duneNoise) * 6.0f;
            const float ridgeNoise = noiseAt(worldX, worldZ, 0.006f, 71);
            const float ridgeShape = 1.0f - std::abs(ridgeNoise);
            const float mountainHeight = 54.0f + ridgeShape * 72.0f + noise * 10.0f;
            const float nonMountainHeight = plainsHeight * (1.0f - snowBiomeWeight)
                + snowyPlainsHeight * snowBiomeWeight;
            const float biomeLandHeight = nonMountainHeight * (1.0f - desertBiomeWeight)
                + desertHeight * desertBiomeWeight;
            landHeight = biomeLandHeight * (1.0f - mountainWeight)
                + mountainHeight * mountainWeight;
        }

        const float coastalBlend = smoothStep(0.0f, 0.5f, seaWeight);
        const float oceanFloorBlend = smoothStep(0.5f, 1.0f, seaWeight);
        const float terrainHeight = seaWeight < 0.5f
            ? landHeight * (1.0f - coastalBlend) + static_cast<float>(SEA_LEVEL) * coastalBlend
            : static_cast<float>(SEA_LEVEL) * (1.0f - oceanFloorBlend)
                + seaFloorHeight * oceanFloorBlend;
        const int height = std::clamp(static_cast<int>(std::round(terrainHeight)), MIN_HEIGHT, MAX_HEIGHT);

        return {
            seaWeight,
            mountainWeight,
            snowBiomeWeight,
            desertBiomeWeight,
            height,
            seaWeight >= 0.5f && height < SEA_LEVEL
        };
    }

    struct CaveRandom {
        std::uint32_t state;

        [[nodiscard]] std::uint32_t next() noexcept
        {
            state ^= state << 13u;
            state ^= state >> 17u;
            state ^= state << 5u;
            return state;
        }

        [[nodiscard]] float unit() noexcept
        {
            return static_cast<float>(next() >> 8u) * (1.0f / 16'777'216.0f);
        }

        [[nodiscard]] float range(const float minimum, const float maximum) noexcept
        {
            return minimum + unit() * (maximum - minimum);
        }
    };

    [[nodiscard]]
    std::uint32_t caveRegionSeed(const int regionX, const int regionZ) noexcept
    {
        std::uint32_t seed = static_cast<std::uint32_t>(regionX) * 0x9E3779B9u
            ^ static_cast<std::uint32_t>(regionZ) * 0x85EBCA6Bu
            ^ 0xC2B2AE35u;
        seed ^= seed >> 16u;
        seed *= 0x7FEB352Du;
        seed ^= seed >> 15u;
        seed *= 0x846CA68Bu;
        seed ^= seed >> 16u;
        return seed == 0 ? 1u : seed;
    }

    void generateCaveMasks(
        const ChunkCoord& coord,
        std::vector<std::uint8_t>& caveMask,
        std::vector<std::uint8_t>& entranceMask
    ) {
        constexpr int regionSize = 64;
        constexpr int featureMargin = 352;
        constexpr std::size_t horizontalArea = ChunkSection::SIZE * ChunkSection::SIZE;
        constexpr std::size_t maskSize = ChunkColumn::HEIGHT * horizontalArea;
        caveMask.assign(maskSize, 0);
        entranceMask.assign(maskSize, 0);

        const int chunkMinX = coord.x * ChunkSection::SIZE;
        const int chunkMinZ = coord.z * ChunkSection::SIZE;
        const int chunkMaxX = chunkMinX + ChunkSection::SIZE - 1;
        const int chunkMaxZ = chunkMinZ + ChunkSection::SIZE - 1;
        const int firstRegionX = floorDiv(chunkMinX - featureMargin, regionSize);
        const int lastRegionX = floorDiv(chunkMaxX + featureMargin, regionSize);
        const int firstRegionZ = floorDiv(chunkMinZ - featureMargin, regionSize);
        const int lastRegionZ = floorDiv(chunkMaxZ + featureMargin, regionSize);

        const auto stampEllipsoid = [&](const float centerX, const float centerY, const float centerZ,
            const float radiusX, const float radiusY, const float radiusZ, const bool entrance) {
            const int minX = std::max(chunkMinX, static_cast<int>(std::floor(centerX - radiusX)));
            const int maxX = std::min(chunkMaxX, static_cast<int>(std::ceil(centerX + radiusX)));
            const int minZ = std::max(chunkMinZ, static_cast<int>(std::floor(centerZ - radiusZ)));
            const int maxZ = std::min(chunkMaxZ, static_cast<int>(std::ceil(centerZ + radiusZ)));
            const int minY = std::max(8, static_cast<int>(std::floor(centerY - radiusY)));
            const int maxY = std::min(ChunkColumn::HEIGHT - 1,
                static_cast<int>(std::ceil(centerY + radiusY)));
            if (minX > maxX || minZ > maxZ || minY > maxY)
                return;

            for (int worldZ = minZ; worldZ <= maxZ; ++worldZ) {
                for (int worldX = minX; worldX <= maxX; ++worldX) {
                    const float dx = (static_cast<float>(worldX) + 0.5f - centerX) / radiusX;
                    const float dz = (static_cast<float>(worldZ) + 0.5f - centerZ) / radiusZ;
                    for (int y = minY; y <= maxY; ++y) {
                        const float dy = (static_cast<float>(y) + 0.5f - centerY) / radiusY;
                        if (dx * dx + dy * dy + dz * dz > 1.0f)
                            continue;

                        const int localX = worldX - chunkMinX;
                        const int localZ = worldZ - chunkMinZ;
                        const std::size_t index = static_cast<std::size_t>(y) * horizontalArea
                            + static_cast<std::size_t>(localZ * ChunkSection::SIZE + localX);
                        caveMask[index] = 1;
                        if (entrance)
                            entranceMask[index] = 1;
                    }
                }
            }
        };

        const auto carveWorm = [&](CaveRandom& random, float x, float y, float z,
            glm::vec3 direction, const float length, float radius, const bool entrance,
            const bool descend = false) {
            std::vector<glm::vec3> path;
            float travelled = 0.0f;
            while (travelled <= length) {
                path.emplace_back(x, y, z);
                const bool openToSurface = entrance && travelled <= 20.0f;
                stampEllipsoid(x, y, z, radius, radius, radius, openToSurface);

                direction.x += random.range(-0.12f, 0.12f);
                direction.y += random.range(-0.08f, 0.08f);
                direction.z += random.range(-0.12f, 0.12f);
                if (descend && travelled < length * 0.75f)
                    direction.y = std::min(direction.y, -0.20f);
                if (entrance && travelled < 20.0f)
                    direction.y = std::min(direction.y, -0.30f);
                if (y < 12.0f)
                    direction.y = std::max(direction.y, 0.16f);
                else if (y > 104.0f)
                    direction.y = std::min(direction.y, -0.16f);
                direction = glm::normalize(direction);

                x += direction.x * 1.6f;
                y += direction.y * 1.6f;
                z += direction.z * 1.6f;
                travelled += 1.6f;
                radius = std::clamp(radius + random.range(-0.10f, 0.10f), 2.35f, 3.45f);
            }
            return path;
        };

        const auto carveConnector = [&](const glm::vec3& start, const glm::vec3& end,
            const float radius) {
            const glm::vec3 delta = end - start;
            const float distance = glm::length(delta);
            if (distance < 1.0f)
                return;
            const int steps = std::max(1, static_cast<int>(std::ceil(distance / 1.6f)));
            glm::vec3 side = glm::cross(glm::normalize(delta), glm::vec3(0.0f, 1.0f, 0.0f));
            if (glm::length(side) < 0.01f)
                side = glm::vec3(1.0f, 0.0f, 0.0f);
            else
                side = glm::normalize(side);

            for (int step = 0; step <= steps; ++step) {
                const float t = static_cast<float>(step) / static_cast<float>(steps);
                const float bend = std::sin(t * 3.14159265f) * std::sin(t * 6.2831853f) * 2.0f;
                const glm::vec3 point = start + delta * t + side * bend;
                stampEllipsoid(point.x, point.y, point.z, radius, radius, radius, false);
            }
        };

        for (int regionZ = firstRegionZ; regionZ <= lastRegionZ; ++regionZ) {
            for (int regionX = firstRegionX; regionX <= lastRegionX; ++regionX) {
                CaveRandom wormRandom{caveRegionSeed(regionX, regionZ)};
                if (wormRandom.unit() < 0.56f) {
                    const float startX = static_cast<float>(regionX * regionSize) + wormRandom.range(0.0f, regionSize);
                    const float startZ = static_cast<float>(regionZ * regionSize) + wormRandom.range(0.0f, regionSize);
                    const TerrainProfile startProfile = terrainProfileAt(
                        static_cast<int>(std::floor(startX)), static_cast<int>(std::floor(startZ)));
                    const bool entrance = !startProfile.isSea
                        && startProfile.height >= SEA_LEVEL - 2
                        && wormRandom.unit() < 0.18f;
                    const float angle = wormRandom.range(0.0f, 6.2831853f);
                    glm::vec3 direction{std::cos(angle), wormRandom.range(-0.14f, 0.14f), std::sin(angle)};
                    if (entrance)
                        direction.y = -0.52f;
                    direction = glm::normalize(direction);

                    const float startY = entrance
                        ? static_cast<float>(startProfile.height) - 1.0f
                        : std::max(16.0f, std::min(
                            static_cast<float>(startProfile.height)
                                - wormRandom.range(14.0f, 28.0f),
                            wormRandom.range(28.0f, 58.0f)));
                    const float radius = wormRandom.range(2.55f, 3.35f);
                    const float length = wormRandom.range(150.0f, 200.0f);
                    const std::vector<glm::vec3> mainPath = carveWorm(
                        wormRandom, startX, startY, startZ, direction, length, radius, entrance, true);

                    const auto stampRoomOnPath = [&](const std::size_t pathIndex) {
                        const glm::vec3 center = mainPath[pathIndex];
                        const float roomRadius = wormRandom.range(8.0f, 12.0f);
                        const float roomHeight = wormRandom.range(6.0f, 9.0f);
                        const float roomAngle = wormRandom.range(0.0f, 6.2831853f);
                        stampEllipsoid(center.x, center.y, center.z,
                            roomRadius, roomHeight, roomRadius, false);
                        stampEllipsoid(center.x + std::cos(roomAngle) * roomRadius * 0.42f,
                            center.y + wormRandom.range(-1.5f, 1.5f),
                            center.z + std::sin(roomAngle) * roomRadius * 0.42f,
                            roomRadius * 0.62f, roomHeight * 0.78f, roomRadius * 0.62f, false);
                    };

                    if (mainPath.size() > 24) {
                        stampRoomOnPath(mainPath.size() / 3);
                        stampRoomOnPath(mainPath.size() * 2 / 3);
                    }

                    if (mainPath.size() > 12) {
                        constexpr std::array<float, 3> branchPositions{0.28f, 0.55f, 0.80f};
                        for (const float branchPosition : branchPositions) {
                            if (wormRandom.unit() >= 0.68f)
                                continue;
                            const std::size_t branchIndex = static_cast<std::size_t>(
                                branchPosition * static_cast<float>(mainPath.size() - 1));
                            const glm::vec3 branchStart = mainPath[branchIndex];
                            const float branchAngle = wormRandom.range(0.0f, 6.2831853f);
                            glm::vec3 branchDirection{
                                std::cos(branchAngle), wormRandom.range(-0.22f, 0.22f),
                                std::sin(branchAngle)};
                            branchDirection = glm::normalize(branchDirection);
                            const float branchLength = wormRandom.range(45.0f, 75.0f);
                            const float branchRadius = wormRandom.range(2.35f, 2.9f);
                            carveWorm(wormRandom, branchStart.x, branchStart.y, branchStart.z,
                                branchDirection, branchLength, branchRadius, false);
                        }

                        if (mainPath.size() > 40 && wormRandom.unit() < 0.55f)
                            carveConnector(mainPath[mainPath.size() / 4],
                                mainPath[mainPath.size() * 3 / 4],
                                wormRandom.range(2.45f, 3.0f));
                    }
                }

                CaveRandom chamberRandom{caveRegionSeed(regionX, regionZ) ^ 0xA511E9B3u};
                const float centerX = static_cast<float>(regionX * regionSize)
                    + chamberRandom.range(0.0f, regionSize);
                const float centerZ = static_cast<float>(regionZ * regionSize)
                    + chamberRandom.range(0.0f, regionSize);
                const TerrainProfile profile = terrainProfileAt(
                    static_cast<int>(std::floor(centerX)), static_cast<int>(std::floor(centerZ)));
                const float chamberChance = 0.15f + profile.mountainWeight * 0.29f;
                if (profile.isSea || chamberRandom.unit() >= chamberChance)
                    continue;

                const float centerY = profile.mountainWeight > 0.30f
                    ? static_cast<float>(profile.height)
                        - chamberRandom.range(20.0f, 42.0f + profile.mountainWeight * 22.0f)
                    : chamberRandom.range(12.0f, static_cast<float>(std::max(13, profile.height - 9)));
                if (centerY < 8.0f || centerY > ChunkColumn::HEIGHT - 8.0f)
                    continue;

                const float radiusX = chamberRandom.range(10.0f, 17.0f)
                    + profile.mountainWeight * 7.0f;
                const float radiusZ = chamberRandom.range(10.0f, 17.0f)
                    + profile.mountainWeight * 7.0f;
                const float radiusY = chamberRandom.range(7.0f, 11.0f)
                    + profile.mountainWeight * 5.0f;
                const float angle = chamberRandom.range(0.0f, 6.2831853f);
                stampEllipsoid(centerX, centerY, centerZ,
                    radiusX, radiusY, radiusZ, false);
                stampEllipsoid(centerX + std::cos(angle) * radiusX * 0.48f,
                    centerY + chamberRandom.range(-2.0f, 2.0f),
                    centerZ + std::sin(angle) * radiusZ * 0.48f,
                    radiusX * 0.58f, radiusY * 0.78f, radiusZ * 0.58f, false);

                if (chamberRandom.unit() < 0.72f) {
                    const float tunnelAngle = chamberRandom.range(0.0f, 6.2831853f);
                    glm::vec3 direction{
                        std::cos(tunnelAngle), chamberRandom.range(-0.16f, 0.16f),
                        std::sin(tunnelAngle)};
                    const float tunnelLength = chamberRandom.range(38.0f, 82.0f);
                    const float tunnelRadius = chamberRandom.range(2.45f, 3.0f);
                    carveWorm(chamberRandom, centerX, centerY, centerZ,
                        glm::normalize(direction), tunnelLength, tunnelRadius, false);
                }
            }
        }
    }

    void appendSectionMesh(MeshData& columnMesh, const MeshData& sectionMesh, const int sectionY)
    {
        const auto baseVertex = static_cast<std::uint32_t>(columnMesh.vertices.size());

        for (const PackedVertex vertex : sectionMesh.vertices) {
            const auto x = static_cast<std::uint8_t>(vertex & 0x1Fu);
            const auto localY = static_cast<std::uint16_t>((vertex >> 5u) & 0x1FFu);
            const auto z = static_cast<std::uint8_t>((vertex >> 14u) & 0x1Fu);
            const auto normal = static_cast<NormalDir>((vertex >> 19u) & 0x07u);
            const auto texture = static_cast<std::uint8_t>((vertex >> 22u) & 0x3Fu);
            const auto ao = static_cast<std::uint8_t>((vertex >> 28u) & 0x03u);
            const auto corner = static_cast<std::uint8_t>((vertex >> 30u) & 0x03u);

            columnMesh.vertices.push_back(packVertex(
                x,
                static_cast<std::uint16_t>(localY + sectionY * ChunkSection::SIZE),
                z,
                normal,
                texture,
                ao,
                corner
            ));
        }

        for (const std::uint32_t index : sectionMesh.indices)
            columnMesh.indices.push_back(baseVertex + index);
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
    retiredMeshes.swap(pendingRetiredMeshes);

    for (auto it = renderStates.begin(); it != renderStates.end();) {
        if (chunks.contains(it->coord)) {
            ++it;
            continue;
        }

        retiredMeshes.push_back(it->gpuMesh);
        retiredMeshes.push_back(it->transparentGpuMesh);
        it = renderStates.erase(it);
    }

    for (auto it = meshedSections.begin(); it != meshedSections.end();) {
        if (chunks.contains(it->first)) {
            ++it;
            continue;
        }

        it = meshedSections.erase(it);
    }

    for (auto it = meshWaitFrames.begin(); it != meshWaitFrames.end();) {
        if (chunks.contains(it->first)) {
            ++it;
            continue;
        }

        it = meshWaitFrames.erase(it);
    }

    return retiredMeshes;
}

bool World::getBlockAt(const glm::ivec3& worldPos, BlockId& block) const {
    if (worldPos.y < 0 || worldPos.y >= ChunkColumn::HEIGHT)
        return false;

    const int chunkX = floorDiv(worldPos.x, ChunkSection::SIZE);
    const int chunkZ = floorDiv(worldPos.z, ChunkSection::SIZE);
    const int localX = worldPos.x - chunkX * ChunkSection::SIZE;
    const int localZ = worldPos.z - chunkZ * ChunkSection::SIZE;

    std::lock_guard lock(chunk_mutex);
    const auto it = chunks.find({chunkX, chunkZ});
    if (it == chunks.end() || !it->second)
        return false;

    block = it->second->getUnchecked(localX, worldPos.y, localZ);
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

    for (int y = ChunkColumn::HEIGHT - 1; y >= 0; --y) {
        if (!blockRegistry.isOpaque(it->second->getUnchecked(localX, y, localZ)))
            continue;

        height = y;
        return true;
    }

    return false;
}

bool World::setBlockAt(const glm::ivec3& worldPos, const BlockId block) {
    if (worldPos.y < 0 || worldPos.y >= ChunkColumn::HEIGHT || block >= BLOCK_TYPE_COUNT)
        return false;

    const int chunkX = floorDiv(worldPos.x, ChunkSection::SIZE);
    const int chunkZ = floorDiv(worldPos.z, ChunkSection::SIZE);
    const int localX = worldPos.x - chunkX * ChunkSection::SIZE;
    const int localZ = worldPos.z - chunkZ * ChunkSection::SIZE;

    std::lock_guard lock(chunk_mutex);
    const auto columnIt = chunks.find({chunkX, chunkZ});
    if (columnIt == chunks.end() || !columnIt->second)
        return false;

    ChunkColumn& column = *columnIt->second;
    if (column.getUnchecked(localX, worldPos.y, localZ) == block)
        return false;

    column.setUnchecked(localX, worldPos.y, localZ, block);

    static constexpr std::array<glm::ivec3, 7> affectedOffsets = {{
        {0, 0, 0}, {1, 0, 0}, {-1, 0, 0}, {0, 1, 0},
        {0, -1, 0}, {0, 0, 1}, {0, 0, -1}
    }};

    for (const glm::ivec3& offset : affectedOffsets) {
        const glm::ivec3 affected = worldPos + offset;
        if (affected.y < 0 || affected.y >= ChunkColumn::HEIGHT)
            continue;

        const int affectedChunkX = floorDiv(affected.x, ChunkSection::SIZE);
        const int affectedChunkZ = floorDiv(affected.z, ChunkSection::SIZE);
        const ChunkCoord affectedCoord{affectedChunkX, affectedChunkZ};
        const auto maskIt = meshedSections.find(affectedCoord);
        if (maskIt != meshedSections.end())
            maskIt->second = 0;

        for (auto stateIt = renderStates.begin(); stateIt != renderStates.end();) {
            if (stateIt->coord != affectedCoord) {
                ++stateIt;
                continue;
            }

            pendingRetiredMeshes.push_back(std::move(stateIt->gpuMesh));
            pendingRetiredMeshes.push_back(std::move(stateIt->transparentGpuMesh));
            stateIt = renderStates.erase(stateIt);
        }
    }

    saveChunk({chunkX, chunkZ}, {localX, worldPos.y, localZ}, block);

    return true;
}

void World::saveChunk(const ChunkCoord& coord, const glm::ivec3& localPos, BlockId block) const {
    const std::filesystem::path savePath = "saves/world/chunk_" + std::to_string(coord.x) + "_" + std::to_string(coord.z) + ".bin";

    if (!std::filesystem::exists(savePath)) {
        std::filesystem::create_directories(savePath.parent_path());
    }

    // Open in binary mode with append
    std::ofstream saveFile(savePath, std::ios::binary | std::ios::app);
    if (!saveFile.is_open()) {
        std::cerr << "Failed to open chunk save file: " << savePath << std::endl;
        return;
    }

    // Write the raw bytes of the glm::ivec3 (contains 3 ints = 12 bytes)
    saveFile.write(reinterpret_cast<const char*>(&localPos), sizeof(localPos));

    // Write the raw bytes of the block ID
    saveFile.write(reinterpret_cast<const char*>(&block), sizeof(block));

    saveFile.close();
}

std::unordered_map<glm::ivec3, BlockId> World::loadChunk(const ChunkCoord& coord) const {
    std::unordered_map<glm::ivec3, BlockId> blocks{};

    const std::filesystem::path savePath = "saves/world/chunk_" + std::to_string(coord.x) + "_" + std::to_string(coord.z) + ".bin";
    if (!std::filesystem::exists(savePath))
        return blocks;

    std::ifstream saveFile(savePath, std::ios::binary);
    if (!saveFile.is_open())
        throw std::runtime_error("Failed to open chunk save file: " + savePath.string());

    // Temporary variables to hold the data we read per block
    glm::ivec3 localPos;
    BlockId block;

    // Loop directly by attempting to read the first piece of data (localPos)
    while (saveFile.read(reinterpret_cast<char*>(&localPos), sizeof(localPos))) {

        // Read the remaining parameters in the exact same order they were saved
        if (!saveFile.read(reinterpret_cast<char*>(&block), sizeof(block))) {
            // File might be corrupted or cut short mid-block write
            std::cerr << "Warning: Corrupted block entry encountered in " << savePath << std::endl;
            break;
        }

        if (localPos.x < 0 || localPos.x >= ChunkSection::SIZE
            || localPos.y < 0 || localPos.y >= ChunkColumn::HEIGHT
            || localPos.z < 0 || localPos.z >= ChunkSection::SIZE
            || block >= BLOCK_TYPE_COUNT) {
            std::cerr << "Warning: Invalid block entry encountered in " << savePath << std::endl;
            continue;
        }

        blocks[localPos] = block;
    }

    saveFile.close();
    return blocks;
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

                    generateTerrain(*column);

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

void World::buildRenderStates(const glm::vec3& playerPosition, const glm::vec3& playerForward) {
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
    struct MeshCandidate {
        ChunkCoord coord;
        ChunkColumn* column;
        float priority;
    };
    std::vector<MeshCandidate> candidates;
    constexpr auto MESH_BUDGET = std::chrono::milliseconds(2);
    constexpr float CHUNK_SIZE = static_cast<float>(ChunkSection::SIZE);

    glm::vec2 forward{playerForward.x, playerForward.z};
    if (glm::dot(forward, forward) > 1e-6f)
        forward = glm::normalize(forward);
    else
        forward = {1.0f, 0.0f};

    {
        std::lock_guard lock(chunk_mutex);

        candidates.reserve(chunks.size());

        for (auto& [coord, column] : chunks){
            if (!column)
                continue;

            const auto sectionIt = meshedSections.find(coord);
            if (sectionIt != meshedSections.end() && sectionIt->second == 0xFFFFu) {
                meshWaitFrames.erase(coord);
                continue;
            }

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
            const bool intersectsForwardHalfPlane =
                centerX * forward.x + centerZ * forward.y + forwardExtent >= 0.0f;
            const std::uint32_t waitedFrames = ++meshWaitFrames[coord];

            // A nearby safety radius comes first; forward chunks get a boost, while age prevents starvation.
            const float safetyBonus = distanceInChunks <= 2.0f ? 100.0f : 0.0f;
            const float viewBonus = intersectsForwardHalfPlane ? 8.0f : 0.0f;
            const float ageBonus = std::min(static_cast<float>(waitedFrames) * 0.25f, 256.0f);
            candidates.push_back({coord, column.get(), distanceInChunks - safetyBonus - viewBonus - ageBonus});
        }
    }

    std::sort(candidates.begin(), candidates.end(), [](const MeshCandidate& lhs, const MeshCandidate& rhs) {
        if (lhs.priority != rhs.priority)
            return lhs.priority < rhs.priority;
        if (lhs.coord.x != rhs.coord.x)
            return lhs.coord.x < rhs.coord.x;
        return lhs.coord.z < rhs.coord.z;
    });

    const auto meshStart = std::chrono::steady_clock::now();
    bool meshedAnyChunk = false;
    for (const MeshCandidate& candidate : candidates)
    {
        if (meshedAnyChunk && std::chrono::steady_clock::now() - meshStart >= MESH_BUDGET)
            break;

        const ChunkCoord& coord = candidate.coord;
        ChunkColumn* column = candidate.column;

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
            neighborhood.setCenter(column);

            for (int dz = -1; dz <= 1; ++dz) {
                for (int dx = -1; dx <= 1; ++dx) {
                    const ChunkCoord neighborCoord{coord.x + dx, coord.z + dz};
                    const auto it = chunks.find(neighborCoord);

                    if (it != chunks.end() && it->second)
                        neighborhood.setNeighbor(dx, dz, it->second.get());
                }
            }
        }

        std::uint16_t& sectionMask = meshedSections[coord];
        ChunkRenderState opaqueState{};
        opaqueState.coord = coord;
        opaqueState.sectionY = 0;
        opaqueState.worldPosition = {
            static_cast<float>(coord.x * ChunkSection::SIZE),
            0.0f,
            static_cast<float>(coord.z * ChunkSection::SIZE)
        };

        MeshData sectionOpaqueMesh;
        MeshData sectionTransparentMesh;
        int lowestOpaqueSection = ChunkColumn::SECTION_COUNT;
        int highestOpaqueSection = -1;

        for (std::uint8_t sectionY = 0; sectionY < ChunkColumn::SECTION_COUNT; ++sectionY)
        {
            ChunkMesher::build(
                column->section(sectionY),
                sectionY,
                neighborhood,
                blockRegistry,
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
                    static_cast<float>(sectionY * ChunkSection::SIZE),
                    opaqueState.worldPosition.z
                };
                transparentState.worldMin = transparentState.worldPosition;
                transparentState.worldMax = {
                    transparentState.worldPosition.x + static_cast<float>(ChunkSection::SIZE),
                    transparentState.worldPosition.y + static_cast<float>(ChunkSection::SIZE),
                    transparentState.worldPosition.z + static_cast<float>(ChunkSection::SIZE)
                };
                transparentState.transparentMesh = std::move(sectionTransparentMesh);
                renderStates.push_back(std::move(transparentState));
            }

            sectionMask |= static_cast<std::uint16_t>(1u << sectionY);
        }

        if (!opaqueState.mesh.empty()) {
            opaqueState.worldMin = {
                opaqueState.worldPosition.x,
                static_cast<float>(lowestOpaqueSection * ChunkSection::SIZE),
                opaqueState.worldPosition.z
            };
            opaqueState.worldMax = {
                opaqueState.worldPosition.x + static_cast<float>(ChunkSection::SIZE),
                static_cast<float>((highestOpaqueSection + 1) * ChunkSection::SIZE),
                opaqueState.worldPosition.z + static_cast<float>(ChunkSection::SIZE)
            };
            renderStates.push_back(std::move(opaqueState));
        }

        meshWaitFrames.erase(coord);
        meshedAnyChunk = true;
    }
}



void World::generateTerrain(ChunkColumn& column) {
    /*
     * The entire column is initially Air because ChunkSection's
     * storage is zero initialized and Air == 0.
     */

    auto coord = column.coord();
    auto blocks = loadChunk(coord);
    std::vector<std::uint8_t> caveMask;
    std::vector<std::uint8_t> entranceMask;
    generateCaveMasks(coord, caveMask, entranceMask);

    for (int x = 0; x < ChunkSection::SIZE; ++x)
    {
        for (int z = 0; z < ChunkSection::SIZE; ++z)
        {
            const int worldX = coord.x * ChunkSection::SIZE + x;
            const int worldZ = coord.z * ChunkSection::SIZE + z;
            const TerrainProfile terrain = terrainProfileAt(worldX, worldZ);
            const float seaWeight = terrain.seaWeight;
            const float mountainWeight = terrain.mountainWeight;
            const float snowBiomeWeight = terrain.snowBiomeWeight;
            const float desertBiomeWeight = terrain.desertBiomeWeight;
            const int height = terrain.height;
            const bool isSea = terrain.isSea;
            const bool isShore = !isSea
                && seaWeight >= 0.30f
                && height >= SEA_LEVEL
                && height <= SEA_LEVEL + 4;

            const bool isDesert = desertBiomeWeight >= 0.5f;
            const bool isSnowBiome = snowBiomeWeight >= 0.5f;
            const bool isHighMountain = mountainWeight >= 0.5f && height >= 82;
            const bool hasSnow = !isSea && (isSnowBiome || (mountainWeight >= 0.5f && height >= 104));
            const int waterSurface = isSea ? SEA_LEVEL : height;
            const int columnTop = isSea ? waterSurface : (hasSnow ? height + 1 : height);

            /*
             * Fill from the bottom up.
             *
             * ChunkColumn::setUnchecked() handles section
             * selection internally.
             */
            for (int y = MIN_HEIGHT; y <= columnTop; ++y)
            {
                BlockType type;

                if (isSea && y > height)
                    type = BlockType::Water;
                else if (hasSnow && y > height)
                    type = BlockType::Snow;
                else if (y == height)
                    type = (isSea || isShore || isDesert)
                        ? BlockType::Sand
                        : (isHighMountain ? BlockType::Stone : BlockType::Grass);
                else if (isSea && y >= height - 4)
                    type = BlockType::Sand;
                else if ((isShore && y >= height - 2) || (isDesert && y >= height - 7))
                    type = BlockType::Sand;
                else if (isHighMountain && y >= height - 2)
                    type = BlockType::Dirt;
                else if (y >= height - 4)
                    type = BlockType::Dirt;
                else
                    type = BlockType::Stone;

                const std::size_t caveIndex = static_cast<std::size_t>(y)
                    * ChunkSection::SIZE * ChunkSection::SIZE
                    + static_cast<std::size_t>(z * ChunkSection::SIZE + x);
                const bool isEntrance = entranceMask[caveIndex] != 0;
                const bool coveredStone = type == BlockType::Stone && !isSea
                    && y <= height - 6;
                if (caveMask[caveIndex] != 0 && !isSea
                    && ((isEntrance && type != BlockType::Water) || coveredStone))
                    continue;

                column.setUnchecked(x, y, z, toBlockId(type));
            }
        }
    }

    // Apply edits after terrain generation so placements above natural terrain
    // and removals of generated blocks are both restored exactly.
    for (const auto& [localPos, block] : blocks)
        column.setUnchecked(localPos.x, localPos.y, localPos.z, block);
}
