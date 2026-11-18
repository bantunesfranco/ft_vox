#pragma once
#ifndef TIME_OF_DAY_HPP
# define TIME_OF_DAY_HPP

// ============================================================================
//  TimeOfDay : Minecraft's clock. 24000 ticks per day, 20 ticks per second
//  (a 20-minute day); tick 0 = sunrise, 6000 = noon, 12000 = sunset,
//  18000 = midnight. Everything here is a pure function of the tick.
// ============================================================================

#include <cstdint>

#include <glm/glm.hpp>

namespace TimeOfDay {

constexpr std::uint32_t TICKS_PER_DAY = 24000;
constexpr std::uint32_t TICKS_PER_SECOND = 20;
constexpr std::uint32_t NEW_WORLD = 1000;          // early morning

// How much sky light there is: 1 at noon, ~0.2 at midnight, smooth between.
[[nodiscard]] float daylight(std::uint32_t ticks) noexcept;

struct SkyColours {
	glm::vec3 zenith;      // overhead
	glm::vec3 horizon;     // also the colour of the distance haze
};
// `dayZenith`: the sky overhead at noon (the debug colour picker).
[[nodiscard]] SkyColours skyColours(std::uint32_t ticks, glm::vec3 dayZenith) noexcept;

// Unit vector towards the sun (overhead at noon); the moon is opposite.
[[nodiscard]] glm::vec3 sunDirection(std::uint32_t ticks) noexcept;

} // namespace TimeOfDay

#endif /* TIME_OF_DAY_HPP */
