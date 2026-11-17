#ifndef PLAYER_PHYSICS_HPP
#define PLAYER_PHYSICS_HPP

// ============================================================================
//  PlayerPhysics : walking, jumping and falling, independent of App and World
//  (collision comes in as a callback) so it can be tested.
//
//  Numbers follow Minecraft's feel, in blocks and seconds: walk 4.3, sprint
//  5.6, sneak 1.3; a jump clears ~1.25 blocks; falls cap at 60 blocks/s.
//  Velocity eases towards the wished velocity (quickly on the ground, slowly
//  in the air), so jumps keep their run-up and steering mid-air is limited.
// ============================================================================

#include <functional>

#include <glm/glm.hpp>

namespace PlayerPhysics {

inline constexpr float GRAVITY        = 32.0f;   // blocks / s^2
inline constexpr float JUMP_SPEED     = 9.2f;    // ~1.25 blocks at 60 fps (v^2 / 2g = 1.32 continuous)
inline constexpr float TERMINAL_SPEED = 60.0f;
inline constexpr float WALK_SPEED     = 4.3f;
inline constexpr float SPRINT_SPEED   = 5.6f;
inline constexpr float SNEAK_SPEED    = 1.3f;
inline constexpr float GROUND_ACCEL   = 14.0f;   // 1/s: how fast velocity reaches the wish
inline constexpr float AIR_ACCEL      = 2.0f;

struct Input {
	glm::vec2 wish{0.0f};      // horizontal direction (x, z), length 0 or 1
	bool jump   = false;       // held or pressed: jumps whenever on the ground
	bool sprint = false;
	bool sneak  = false;       // slow, and never walks off an edge
};

struct Body {
	glm::vec3 eye{0.0f};
	glm::vec3 velocity{0.0f};
	bool grounded = false;
};

// Whether the player's box, with its eye at the given position, hits something.
using Collides = std::function<bool(const glm::vec3& eye)>;

// The player box against a block grid (`solid` decides per block).
[[nodiscard]] bool boxHitsBlocks(const glm::vec3& eye, const std::function<bool(const glm::ivec3&)>& solid);

void step(Body& body, const Input& input, float dt, const Collides& collides);

} // namespace PlayerPhysics

#endif
