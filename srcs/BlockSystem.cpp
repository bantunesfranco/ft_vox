#include "BlockSystem.hpp"
#include <cmath>
#include <algorithm>

#define GLM_ENABLE_EXPERIMENTAL
#include <iostream>

#include "glm/gtx/norm.hpp"

BlockSystem::BlockSystem(const float maxReachDistance)
    : maxReachDistance(maxReachDistance),
    lastHit{glm::ivec3(0), glm::ivec3(0), BlockFace::TOP, 0.0f, false}
{
}

inline glm::ivec3 BlockSystem::getChunkCoords(const glm::ivec3& worldPos)
{
    return {
        floorDiv(worldPos.x, Chunk::WIDTH),
        floorDiv(worldPos.y, Chunk::HEIGHT),
        floorDiv(worldPos.z, Chunk::DEPTH)
    };
}

inline glm::ivec3 BlockSystem::getLocalCoords(const glm::ivec3& worldPos)
{
    return {
        ((worldPos.x % Chunk::WIDTH) + Chunk::WIDTH) % Chunk::WIDTH,
        ((worldPos.y % Chunk::HEIGHT) + Chunk::HEIGHT) % Chunk::HEIGHT,
        ((worldPos.z % Chunk::DEPTH) + Chunk::DEPTH) % Chunk::DEPTH
    };
}

Voxel BlockSystem::getVoxelFromWorld(const glm::ivec3& worldPos, World& world)
{
    const int chunkX = floorDiv(worldPos.x, Chunk::WIDTH);
    const int chunkZ = floorDiv(worldPos.z, Chunk::DEPTH);
    const glm::ivec2 key(chunkX, chunkZ);

    std::lock_guard lock(world.chunk_mutex);

    const auto& chunks = world.getChunks();
    const auto it = chunks.find(key);
    if (it == chunks.end())
        return 0;

    const int localX = ((worldPos.x % Chunk::WIDTH) + Chunk::WIDTH) % Chunk::WIDTH;
    const int localY = ((worldPos.y % Chunk::HEIGHT) + Chunk::HEIGHT) % Chunk::HEIGHT;
    const int localZ = ((worldPos.z % Chunk::DEPTH) + Chunk::DEPTH) % Chunk::DEPTH;

    return it->second.getVoxel(localX, localY, localZ);
}

void BlockSystem::setVoxelInWorld(const glm::ivec3& worldPos, const Voxel voxel, World& world)
{
    const int chunkX = floorDiv(worldPos.x, Chunk::WIDTH);
    const int chunkZ = floorDiv(worldPos.z, Chunk::DEPTH);
    const glm::ivec2 key(chunkX, chunkZ);

    const int localX = ((worldPos.x % Chunk::WIDTH) + Chunk::WIDTH) % Chunk::WIDTH;
    const int localZ = ((worldPos.z % Chunk::DEPTH) + Chunk::DEPTH) % Chunk::DEPTH;

    std::lock_guard lock(world.chunk_mutex);

    auto& chunks = world.getChunks();
    const auto it = chunks.find(key);
    if (it == chunks.end())
        return;

    it->second.setVoxel(localX, ((worldPos.y % Chunk::HEIGHT) + Chunk::HEIGHT) % Chunk::HEIGHT, localZ, voxel);
    it->second.markMeshDirty();

    // Mark adjacent chunks dirty if boundary block
    auto markDirty = [&chunks](const glm::ivec2& coord) {
        if (const auto iter = chunks.find(coord); iter != chunks.end())
            iter->second.markMeshDirty();
    };

    if (localX == 0) markDirty(glm::ivec2(chunkX - 1, chunkZ));
    if (localX == Chunk::WIDTH - 1) markDirty(glm::ivec2(chunkX + 1, chunkZ));
    if (localZ == 0) markDirty(glm::ivec2(chunkX, chunkZ - 1));
    if (localZ == Chunk::DEPTH - 1) markDirty(glm::ivec2(chunkX, chunkZ + 1));
}

inline glm::ivec3 BlockSystem::getAdjacentBlockPos(const glm::ivec3& hitPos, const BlockFace face)
{
    static constexpr glm::ivec3 offsets[] = {
        {0, 1, 0},   // TOP
        {0, -1, 0},  // BOTTOM
        {0, 0, 1},   // FRONT
        {0, 0, -1},  // BACK
        {1, 0, 0},   // RIGHT
        {-1, 0, 0}   // LEFT
    };
    return hitPos + offsets[static_cast<int>(face)];
}

inline BlockFace BlockSystem::detectFace(const glm::ivec3& blockPos, const glm::vec3& hitPoint)
{
    const glm::vec3 offset = hitPoint - (glm::vec3(blockPos) + 0.5f);
    const float absX = std::abs(offset.x);
    const float absY = std::abs(offset.y);
    const float absZ = std::abs(offset.z);

    if (absX > absY && absX > absZ)
        return (offset.x > 0) ? BlockFace::RIGHT : BlockFace::LEFT;
    if (absY > absZ)
        return (offset.y > 0) ? BlockFace::TOP : BlockFace::BOTTOM;
    return (offset.z > 0) ? BlockFace::FRONT : BlockFace::BACK;
}

RaycastHit BlockSystem::raycastBlocks(const glm::vec3& rayOrigin, const glm::vec3& rayDirection, World& world)
{
    RaycastHit hit{glm::ivec3(0), glm::ivec3(0), BlockFace::TOP, maxReachDistance + 1.0f, false};

    const glm::vec3 rayDir = glm::normalize(rayDirection);
    constexpr float stepSize = 0.05f;

    for (float distance = 0.0f; distance <= maxReachDistance; distance += stepSize) {
        const glm::vec3 rayPos = rayOrigin + rayDir * distance;
        const auto blockPos = glm::ivec3(glm::floor(rayPos));

        if (getVoxelFromWorld(blockPos, world) != 0) {
            hit.blockPos = blockPos;
            hit.distance = distance;
            hit.face = detectFace(blockPos, rayPos);
            hit.adjacentPos = getAdjacentBlockPos(blockPos, hit.face);
            hit.isValid = true;
            lastHit = hit;
            return hit;
        }
    }

    lastHit = hit;
    return hit;
}

bool BlockSystem::placeBlock(const glm::vec3& rayOrigin, const glm::vec3& rayDirection, World& world, const Voxel blockType)
{
    const RaycastHit hit = raycastBlocks(rayOrigin, rayDirection, world);
    if (!hit.isValid) {
        std::cout << "Place failed: No valid hit" << std::endl;
        return false;
    }

    const glm::ivec3 placePos = hit.adjacentPos;

    std::cout << "=== PLACE BLOCK ===" << std::endl;
    std::cout << "Hit block: (" << hit.blockPos.x << ", " << hit.blockPos.y << ", " << hit.blockPos.z << ")" << std::endl;
    std::cout << "Hit face: " << static_cast<int>(hit.face) << std::endl;
    std::cout << "Place position: (" << placePos.x << ", " << placePos.y << ", " << placePos.z << ")" << std::endl;

    // Early exits for invalid positions
    if (placePos.y < 0 || placePos.y >= Chunk::HEIGHT) {
        std::cout << "Place failed: Y out of bounds (" << placePos.y << ")" << std::endl;
        return false;
    }

    if (getVoxelFromWorld(placePos, world) != 0) {
        std::cout << "Place failed: Block already exists at position" << std::endl;
        return false;
    }

    // Check collision with player (use squared distance to avoid sqrt)
    constexpr float minDistSq = 1.5f;
    const float distSq = glm::distance2(glm::vec3(placePos), rayOrigin);
    if (distSq < minDistSq) {
        std::cout << "Place failed: Too close to player (distance²: " << distSq << " < " << minDistSq << ")" << std::endl;
        return false;
    }

    const int chunkX = floorDiv(placePos.x, Chunk::WIDTH);
    const int chunkZ = floorDiv(placePos.z, Chunk::DEPTH);
    const glm::ivec2 chunkKey(chunkX, chunkZ);

    std::cout << "Block placed at world pos, chunk: (" << chunkKey.x << ", " << chunkKey.y << ")" << std::endl;
    setVoxelInWorld(placePos, blockType, world);
    return true;
}

bool BlockSystem::destroyBlock(const glm::vec3& rayOrigin, const glm::vec3& rayDirection, World& world)
{
    const RaycastHit hit = raycastBlocks(rayOrigin, rayDirection, world);

    if (!hit.isValid)
        return false;

    setVoxelInWorld(hit.blockPos, 0, world);
    return true;
}