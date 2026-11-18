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

// The same with block shapes: `inset` gives, per block, how far its sides sit
// inside the cell in 1/16 block (0 = full cube, 1 = cactus), or -1 to pass through.
[[nodiscard]] bool boxHitsShapes(const glm::vec3& eye, const std::function<int(const glm::ivec3&)>& inset);

// The player's eye lifted straight up, to the first clear spot within `maxLift` blocks, for a
// spawn or login inside terrain. Unchanged when already clear; lifted by `maxLift` when none is.
[[nodiscard]] glm::vec3 liftOutOfBlocks(glm::vec3 eye, const std::function<bool(const glm::ivec3&)>& solid,
                                        int maxLift = 256);

void step(Body& body, const Input& input, float dt, const Collides& collides);

// Minecraft's sprint rule: the sprint key while moving forward, or forward
// pressed twice within DOUBLE_TAP seconds, starts it; it lasts until forward
// is released or the player sneaks.
class Sprint {
public:
	static constexpr double DOUBLE_TAP = 0.3;

	void forwardPressed(double now) noexcept;
	// Call once per frame; returns whether the player sprints this frame.
	bool update(bool forwardHeld, bool sprintKey, bool sneak) noexcept;

private:
	bool   _sprinting = false;
	bool   _doubleTapped = false;
	double _lastForwardPress = -1.0;
};

} // namespace PlayerPhysics

#endif
