#ifndef PLAYER_COLLISION_HPP
#define PLAYER_COLLISION_HPP

#include <glm/glm.hpp>

namespace PlayerCollision {
inline constexpr float HALF_WIDTH = 0.3f;
inline constexpr float EYE_HEIGHT = 1.62f;
inline constexpr float HEIGHT = 1.8f;
inline constexpr float EPSILON = 0.001f;

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
