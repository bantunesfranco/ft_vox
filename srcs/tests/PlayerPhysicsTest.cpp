#include "SelfTest.hpp"
#include "PlayerCollision.hpp"
#include "PlayerPhysics.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

using namespace PlayerPhysics;

namespace {

constexpr float DT = 1.0f / 60.0f;

// Solid ground under y = 0 everywhere.
const Collides FLOOR = [](const glm::vec3& eye) {
	return boxHitsBlocks(eye, [](const glm::ivec3& b) { return b.y < 0; });
};

// Ground under y = 0 only for x < 5; beyond it a drop to y = -20.
const Collides LEDGE = [](const glm::vec3& eye) {
	return boxHitsBlocks(eye, [](const glm::ivec3& b) { return b.y < -20 || (b.y < 0 && b.x < 5); });
};

const Collides NOTHING = [](const glm::vec3&) { return false; };

Body standing(const float x = 0.5f) {
	return Body{{x, PlayerCollision::EYE_HEIGHT, 0.5f}, glm::vec3(0.0f), true};
}

float feet(const Body& b) { return b.eye.y - PlayerCollision::EYE_HEIGHT; }

// Horizontal speed after walking along +x for `seconds`.
float speedAfter(const Input& input, const float seconds) {
	Body body = standing();
	for (float t = 0.0f; t < seconds; t += DT)
		step(body, input, DT, FLOOR);
	return body.velocity.x;
}

} // namespace

SELFTEST(PlayerPhysics, jump_height) {
	Body body = standing();
	step(body, Input{.jump = true}, DT, FLOOR);
	float highest = feet(body), airtime = DT;
	while (!body.grounded && airtime < 3.0f) {
		step(body, Input{}, DT, FLOOR);
		highest = std::max(highest, feet(body));
		airtime += DT;
	}
	std::printf("     jump %.2f blocks, %.2f s in the air\n", highest, airtime);
	CHECK(highest >= 1.15f && highest <= 1.30f);    // clears one block, not two
	CHECK(airtime >= 0.45f && airtime <= 0.70f);
	CHECK(std::abs(feet(body)) < 0.01f);            // landed back on the floor
}

SELFTEST(PlayerPhysics, held_jump_keeps_jumping) {
	Body body = standing();
	int takeoffs = 0;
	for (float t = 0.0f; t < 3.0f; t += DT) {
		const bool before = body.grounded;
		step(body, Input{.jump = true}, DT, FLOOR);
		takeoffs += before && !body.grounded ? 1 : 0;
	}
	std::printf("     %d jumps in 3 s with space held\n", takeoffs);
	CHECK(takeoffs >= 4);
}

SELFTEST(PlayerPhysics, terminal_velocity) {
	Body body{{0.5f, 500.0f, 0.5f}, glm::vec3(0.0f), false};
	for (float t = 0.0f; t < 10.0f; t += DT)
		step(body, Input{}, DT, NOTHING);
	CHECK(body.velocity.y >= -TERMINAL_SPEED - 0.01f);
	CHECK(body.velocity.y <= -TERMINAL_SPEED + 0.5f);
}

SELFTEST(PlayerPhysics, walk_sprint_sneak_speeds) {
	const float walk = speedAfter(Input{.wish = {1.0f, 0.0f}}, 2.0f);
	const float sprint = speedAfter(Input{.wish = {1.0f, 0.0f}, .sprint = true}, 2.0f);
	const float sneak = speedAfter(Input{.wish = {1.0f, 0.0f}, .sneak = true}, 2.0f);
	std::printf("     walk %.2f, sprint %.2f, sneak %.2f blocks/s\n", walk, sprint, sneak);
	CHECK(std::abs(walk - WALK_SPEED) < 0.05f);
	CHECK(std::abs(sprint - SPRINT_SPEED) < 0.05f);
	CHECK(std::abs(sneak - SNEAK_SPEED) < 0.05f);
}

SELFTEST(PlayerPhysics, momentum) {
	// Not instant: one frame of walking is well below full speed.
	CHECK(speedAfter(Input{.wish = {1.0f, 0.0f}}, DT) < WALK_SPEED * 0.5f);

	// On the ground, letting go stops within half a second.
	Body ground = standing();
	ground.velocity.x = WALK_SPEED;
	for (float t = 0.0f; t < 0.5f; t += DT)
		step(ground, Input{}, DT, FLOOR);
	CHECK(std::abs(ground.velocity.x) < 0.1f);

	// In the air, the run-up is mostly kept.
	Body air{{0.5f, 100.0f, 0.5f}, {WALK_SPEED, 0.0f, 0.0f}, false};
	for (float t = 0.0f; t < 0.3f; t += DT)
		step(air, Input{}, DT, NOTHING);
	std::printf("     after 0.3 s: %.2f blocks/s in the air\n", air.velocity.x);
	CHECK(air.velocity.x > WALK_SPEED * 0.4f);
}

SELFTEST(PlayerPhysics, sneak_stops_at_edge) {
	Body sneaking = standing(3.0f);
	for (float t = 0.0f; t < 3.0f; t += DT)
		step(sneaking, Input{.wish = {1.0f, 0.0f}, .sneak = true}, DT, LEDGE);
	std::printf("     sneaking: x %.2f, feet %.2f\n", sneaking.eye.x, feet(sneaking));
	CHECK(sneaking.grounded);
	CHECK(std::abs(feet(sneaking)) < 0.01f);
	CHECK(sneaking.eye.x > 5.0f);                  // goes right up to the edge (over it, held by the toes)

	Body walking = standing(3.0f);
	for (float t = 0.0f; t < 3.0f; t += DT)
		step(walking, Input{.wish = {1.0f, 0.0f}}, DT, LEDGE);
	CHECK(feet(walking) < -1.0f);                 // walked off and fell
}

SELFTEST(PlayerPhysics, walls_stop_movement) {
	// A wall at x >= 3: walking into it stops at its face and kills the speed.
	const Collides wall = [](const glm::vec3& eye) {
		return boxHitsBlocks(eye, [](const glm::ivec3& b) { return b.y < 0 || b.x >= 3; });
	};
	Body body = standing();
	for (float t = 0.0f; t < 2.0f; t += DT)
		step(body, Input{.wish = {1.0f, 0.0f}}, DT, wall);
	CHECK(body.eye.x <= 3.0f - PlayerCollision::HALF_WIDTH + 0.01f);
	CHECK(body.eye.x >= 3.0f - PlayerCollision::HALF_WIDTH - 0.3f);
	CHECK(std::abs(body.velocity.x) < 0.01f);
}
