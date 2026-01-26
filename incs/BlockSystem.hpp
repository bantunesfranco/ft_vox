#ifndef BLOCKSYSTEM_HPP
#define BLOCKSYSTEM_HPP

#include <map>
#include <glm/glm.hpp>
#include "Chunk.hpp"
#include "World.hpp"

enum class BlockFace {
    TOP,
    BOTTOM,
    FRONT,
    BACK,
    LEFT,
    RIGHT
};

struct RaycastHit {
    glm::ivec3 blockPos;      // World position of hit block
    glm::ivec3 adjacentPos;   // Position where new block would be placed
    BlockFace face;           // Which face was hit
    float distance;           // Distance from ray origin
    bool isValid;
};

class BlockSystem {
public:
    // Constructor
    explicit BlockSystem(float maxReachDistance = 5.0f);
    
    // Raycasting
    RaycastHit raycastBlocks(
        const glm::vec3& rayOrigin,
        const glm::vec3& rayDirection,
        World& world
    );
    
    // Block operations
    bool placeBlock(
        const glm::vec3& rayOrigin,
        const glm::vec3& rayDirection,
        World& world,
        Voxel blockType
    );
    
    bool destroyBlock(
        const glm::vec3& rayOrigin,
        const glm::vec3& rayDirection,
        World& world
    );
    
    // Getters
    [[nodiscard]] const RaycastHit& getLastRaycastHit() const { return lastHit; }
    [[nodiscard]] float getMaxReachDistance() const { return maxReachDistance; }
    
private:
    float maxReachDistance;
    RaycastHit lastHit;

    // Helper functions
    static Voxel getVoxelFromWorld(
        const glm::ivec3& worldPos,
        World& world
    );

    static void setVoxelInWorld(
        const glm::ivec3& worldPos,
        Voxel voxel,
        World& world
    );

    static glm::ivec3 getChunkCoords(const glm::ivec3& worldPos);
    static glm::ivec3 getLocalCoords(const glm::ivec3& worldPos);
    static glm::ivec3 getAdjacentBlockPos(const glm::ivec3& hitPos, BlockFace face);
    static BlockFace detectFace(const glm::ivec3& blockPos, const glm::vec3& hitPoint);
};

#endif // BLOCKSYSTEM_HPP
