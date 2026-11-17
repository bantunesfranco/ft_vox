#include "TimeOfDay.hpp"

#include <cmath>

namespace TimeOfDay {
namespace {

constexpr float TAU = 6.28318530718f;

// Sun height: 1 at noon, 0 at sunrise / sunset, -1 at midnight.
float sunHeight(const std::uint32_t ticks) noexcept {
	const auto t = static_cast<float>(ticks % TICKS_PER_DAY);
	return std::cos(TAU * (t - 6000.0f) / static_cast<float>(TICKS_PER_DAY));
}

float smoothstep(const float edge0, const float edge1, const float x) noexcept {
	const float t = glm::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
	return t * t * (3.0f - 2.0f * t);
}

// 0 at night .. 1 in daytime, changing around sunrise and sunset.
float dayness(const std::uint32_t ticks) noexcept { return smoothstep(-0.2f, 0.3f, sunHeight(ticks)); }

const glm::vec3 NIGHT_ZENITH{0.01f, 0.012f, 0.04f};
const glm::vec3 NIGHT_HORIZON{0.03f, 0.04f, 0.09f};
const glm::vec3 SUNSET{0.95f, 0.52f, 0.25f};

} // namespace

float daylight(const std::uint32_t ticks) noexcept {
	return 0.2f + 0.8f * dayness(ticks);
}

SkyColours skyColours(const std::uint32_t ticks, const glm::vec3 dayZenith) noexcept {
	const float d = dayness(ticks);
	const glm::vec3 dayHorizon = glm::mix(dayZenith, glm::vec3(1.0f), 0.5f);
	SkyColours colours{glm::mix(NIGHT_ZENITH, dayZenith, d), glm::mix(NIGHT_HORIZON, dayHorizon, d)};
	// An orange horizon while the sun is close to it.
	const float glow = 0.7f * (1.0f - smoothstep(0.0f, 0.4f, std::abs(sunHeight(ticks))));
	colours.horizon = glm::mix(colours.horizon, SUNSET, glow);
	return colours;
}

glm::vec3 sunDirection(const std::uint32_t ticks) noexcept {
	const auto t = static_cast<float>(ticks % TICKS_PER_DAY);
	const float angle = TAU * (t - 6000.0f) / static_cast<float>(TICKS_PER_DAY);
	return {std::sin(angle), std::cos(angle), 0.0f};       // rises in -x, sets in +x
}

} // namespace TimeOfDay
