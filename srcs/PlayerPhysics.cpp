#include "PlayerPhysics.hpp"

#include <algorithm>
#include <cmath>

#include "PlayerCollision.hpp"

namespace PlayerPhysics {

bool boxHitsShapes(const glm::vec3& eye, const std::function<int(const glm::ivec3&)>& inset) {
	using namespace PlayerCollision;
	const float minX = eye.x - HALF_WIDTH, maxX = eye.x + HALF_WIDTH;
	const float minZ = eye.z - HALF_WIDTH, maxZ = eye.z + HALF_WIDTH;
	const int x0 = static_cast<int>(std::floor(minX + EPSILON)), x1 = static_cast<int>(std::floor(maxX - EPSILON));
	const int y0 = static_cast<int>(std::floor(eye.y - EYE_HEIGHT + EPSILON));
	const int y1 = static_cast<int>(std::floor(eye.y + HEIGHT - EYE_HEIGHT - EPSILON));
	const int z0 = static_cast<int>(std::floor(minZ + EPSILON)), z1 = static_cast<int>(std::floor(maxZ - EPSILON));
	for (int y = y0; y <= y1; ++y)
		for (int z = z0; z <= z1; ++z)
			for (int x = x0; x <= x1; ++x) {
				const int in = inset({x, y, z});
				if (in < 0)
					continue;
				if (in == 0)
					return true;                       // a full cube: the cell overlap is enough
				const float side = static_cast<float>(in) / 16.0f;
				const float bx0 = static_cast<float>(x) + side, bx1 = static_cast<float>(x + 1) - side;
				const float bz0 = static_cast<float>(z) + side, bz1 = static_cast<float>(z + 1) - side;
				if (minX < bx1 - EPSILON && maxX > bx0 + EPSILON && minZ < bz1 - EPSILON && maxZ > bz0 + EPSILON)
					return true;
			}
	return false;
}

bool boxHitsBlocks(const glm::vec3& eye, const std::function<bool(const glm::ivec3&)>& solid) {
	return boxHitsShapes(eye, [&solid](const glm::ivec3& p) { return solid(p) ? 0 : -1; });
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

void Sprint::forwardPressed(const double now) noexcept {
	if (_lastForwardPress >= 0.0 && now - _lastForwardPress <= DOUBLE_TAP)
		_doubleTapped = true;
	_lastForwardPress = now;
}

bool Sprint::update(const bool forwardHeld, const bool sprintKey, const bool sneak) noexcept {
	if (!forwardHeld || sneak) {
		_sprinting = false;
		_doubleTapped = false;
	} else if (sprintKey || _doubleTapped) {
		_sprinting = true;
		_doubleTapped = false;
	}
	return _sprinting;
}

} // namespace PlayerPhysics
