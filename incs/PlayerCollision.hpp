#ifndef PLAYER_COLLISION_HPP
#define PLAYER_COLLISION_HPP

#include <cmath>

#include <glm/glm.hpp>

#include "Chunk.hpp"

namespace PlayerCollision {
inline constexpr float HALF_WIDTH = 0.3f;
inline constexpr float EYE_HEIGHT = 1.62f;
inline constexpr float HEIGHT = 1.8f;
inline constexpr float EPSILON = 0.001f;

// How far from the origin (x, z) and beyond the world's height (y) an eye may be.
inline constexpr float MAX_HORIZONTAL = 3.0e7f;
inline constexpr float WORLD_Y_MARGIN = 512.0f;

// A position a player can really have: finite, and within the limits above. The
// server kicks a PlayerMove outside them; a saved player outside them is damaged.
[[nodiscard]] inline bool plausibleEye(const glm::vec3& eye) noexcept {
    return std::isfinite(eye.x) && std::isfinite(eye.y) && std::isfinite(eye.z)
        && std::abs(eye.x) <= MAX_HORIZONTAL && std::abs(eye.z) <= MAX_HORIZONTAL
        && eye.y >= static_cast<float>(ChunkColumn::MIN_WORLD_Y) - WORLD_Y_MARGIN
        && eye.y <= static_cast<float>(ChunkColumn::MAX_WORLD_Y) + WORLD_Y_MARGIN;
}

// The columns the player's box stands in (one to four, at chunk edges), inclusive.
struct ColumnSpan {
    ChunkCoord lo;
    ChunkCoord hi;
};

[[nodiscard]] inline ColumnSpan columnsUnderBox(const glm::vec3& eye) noexcept {
    constexpr float size = static_cast<float>(ChunkSection::SIZE);
    const auto column = [size](const float v) { return static_cast<std::int32_t>(std::floor(v / size)); };
    return {{column(eye.x - HALF_WIDTH), column(eye.z - HALF_WIDTH)},
            {column(eye.x + HALF_WIDTH), column(eye.z + HALF_WIDTH)}};
}

[[nodiscard]] inline bool overlapsBlock(const glm::vec3& eyePosition, const glm::ivec3& blockPosition) noexcept {
    const glm::vec3 playerMin{
        eyePosition.x - HALF_WIDTH,
        eyePosition.y - EYE_HEIGHT,
        eyePosition.z - HALF_WIDTH
    };
    const glm::vec3 playerMax{
        eyePosition.x + HALF_WIDTH,
        eyePosition.y + HEIGHT - EYE_HEIGHT,
        eyePosition.z + HALF_WIDTH
    };
    const glm::vec3 blockMin(blockPosition);
    const glm::vec3 blockMax = blockMin + glm::vec3(1.0f);

    return playerMin.x < blockMax.x - EPSILON && playerMax.x > blockMin.x + EPSILON
        && playerMin.y < blockMax.y - EPSILON && playerMax.y > blockMin.y + EPSILON
        && playerMin.z < blockMax.z - EPSILON && playerMax.z > blockMin.z + EPSILON;
}
}

#endif
