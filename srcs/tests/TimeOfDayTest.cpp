#include "SelfTest.hpp"
#include "TimeOfDay.hpp"

#include <cmath>

SELFTEST(TimeOfDay, daylight_curve) {
	CHECK_EQ(TimeOfDay::daylight(6000), 1.0f);                       // noon
	const float midnight = TimeOfDay::daylight(18000);
	CHECK(midnight >= 0.15f && midnight <= 0.25f);
	for (const std::uint32_t edge : {0u, 12000u}) {                   // sunrise, sunset
		CHECK(TimeOfDay::daylight(edge) > midnight);
		CHECK(TimeOfDay::daylight(edge) < 1.0f);
	}
	float worst = 0.0f;
	for (std::uint32_t t = 0; t < TimeOfDay::TICKS_PER_DAY; t += 10)
		worst = std::max(worst, std::abs(TimeOfDay::daylight(t + 10) - TimeOfDay::daylight(t)));
	CHECK(worst <= 0.01f);                                            // no jumps
	CHECK_EQ(TimeOfDay::daylight(TimeOfDay::TICKS_PER_DAY + 6000), 1.0f);   // wraps
}

SELFTEST(TimeOfDay, sun_overhead_at_noon) {
	CHECK(TimeOfDay::sunDirection(6000).y > 0.99f);
	CHECK(TimeOfDay::sunDirection(18000).y < -0.99f);
	CHECK(std::abs(glm::length(TimeOfDay::sunDirection(1234)) - 1.0f) < 1e-4f);
}

SELFTEST(TimeOfDay, night_sky_dark) {
	const glm::vec3 day(0.075f, 0.33f, 0.61f);
	const TimeOfDay::SkyColours night = TimeOfDay::skyColours(18000, day);
	CHECK(night.zenith.r < 0.15f && night.zenith.g < 0.15f && night.zenith.b < 0.15f);
	const TimeOfDay::SkyColours noon = TimeOfDay::skyColours(6000, day);
	CHECK(glm::length(noon.zenith - day) < 1e-4f);                   // the day colour overhead at noon
	const TimeOfDay::SkyColours dusk = TimeOfDay::skyColours(12000, day);
	CHECK(dusk.horizon.r > dusk.horizon.b);                           // orange at sunset
}
