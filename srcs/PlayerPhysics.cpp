#include "PlayerPhysics.hpp"

#include <algorithm>
#include <cmath>

#include "PlayerCollision.hpp"

namespace PlayerPhysics {

bool boxHitsBlocks(const glm::vec3& eye, const std::function<bool(const glm::ivec3&)>& solid) {
	using namespace PlayerCollision;
	const int minX = static_cast<int>(std::floor(eye.x - HALF_WIDTH + EPSILON));
	const int maxX = static_cast<int>(std::floor(eye.x + HALF_WIDTH - EPSILON));
	const int minY = static_cast<int>(std::floor(eye.y - EYE_HEIGHT + EPSILON));
	const int maxY = static_cast<int>(std::floor(eye.y + HEIGHT - EYE_HEIGHT - EPSILON));
	const int minZ = static_cast<int>(std::floor(eye.z - HALF_WIDTH + EPSILON));
	const int maxZ = static_cast<int>(std::floor(eye.z + HALF_WIDTH - EPSILON));
	for (int y = minY; y <= maxY; ++y)
		for (int z = minZ; z <= maxZ; ++z)
			for (int x = minX; x <= maxX; ++x)
				if (solid({x, y, z}))
					return true;
	return false;
}

namespace {

constexpr float GROUND_PROBE = 0.04f;   // how far below the feet counts as standing
constexpr float MAX_SUBSTEP  = 0.25f;   // never move further than this between collision tests

bool supported(const glm::vec3& eye, const Collides& collides) {
	return collides(eye - glm::vec3(0.0f, GROUND_PROBE, 0.0f));
}

// Moves along one axis in sub-steps; stops at the first collision. Sneaking
// on the ground also stops before a step that would leave the feet unsupported.
// Returns false if blocked.
bool moveAxis(Body& body, const int axis, const float amount, const bool holdEdge, const Collides& collides) {
	const int steps = std::max(1, static_cast<int>(std::ceil(std::abs(amount) / MAX_SUBSTEP)));
	const float part = amount / static_cast<float>(steps);
	const auto blocked = [&](const glm::vec3& candidate) {
		return collides(candidate) || (holdEdge && !supported(candidate, collides));
	};
	for (int i = 0; i < steps; ++i) {
		glm::vec3 candidate = body.eye;
		candidate[axis] += part;
		if (blocked(candidate)) {
			// Close the gap to the obstacle (or edge) by halving the step.
			float reach = part;
			for (int k = 0; k < 8; ++k) {
				reach *= 0.5f;
				candidate = body.eye;
				candidate[axis] += reach;
				if (!blocked(candidate))
					body.eye = candidate;
			}
			return false;
		}
		body.eye = candidate;
	}
	return true;
}

} // namespace

void step(Body& body, const Input& input, const float dt, const Collides& collides) {
	body.grounded = supported(body.eye, collides);

	// Horizontal: ease towards the wished velocity (exponential, frame-rate independent).
	const float speed = input.sneak ? SNEAK_SPEED : input.sprint ? SPRINT_SPEED : WALK_SPEED;
	const glm::vec2 target = input.wish * speed;
	const float blend = 1.0f - std::exp(-(body.grounded ? GROUND_ACCEL : AIR_ACCEL) * dt);
	body.velocity.x += (target.x - body.velocity.x) * blend;
	body.velocity.z += (target.y - body.velocity.z) * blend;

	const bool holdEdge = input.sneak && body.grounded;
	if (!moveAxis(body, 0, body.velocity.x * dt, holdEdge, collides))
		body.velocity.x = 0.0f;
	if (!moveAxis(body, 2, body.velocity.z * dt, holdEdge, collides))
		body.velocity.z = 0.0f;

	// Vertical: jump whenever on the ground with jump held, else fall.
	if (input.jump && body.grounded) {
		body.velocity.y = JUMP_SPEED;
		body.grounded = false;
	} else if (body.grounded && body.velocity.y <= 0.0f) {
		body.velocity.y = 0.0f;
		return;
	}
	body.velocity.y = std::max(body.velocity.y - GRAVITY * dt, -TERMINAL_SPEED);

	if (!moveAxis(body, 1, body.velocity.y * dt, false, collides)) {
		if (body.velocity.y < 0.0f)
			body.grounded = true;       // landed
		body.velocity.y = 0.0f;         // or hit a ceiling
	} else {
		body.grounded = false;
	}
}

} // namespace PlayerPhysics
