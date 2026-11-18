#pragma once
#ifndef WORLDGEN_SPLINE_HPP
# define WORLDGEN_SPLINE_HPP

// ============================================================================
//  worldgen::Spline : a piecewise-linear curve through fixed points, clamped
//  at both ends. Terrain shape is tuned by editing these point tables.
// ============================================================================

#include <array>
#include <cstddef>

namespace worldgen {

template <std::size_t N>
struct Spline {
	struct Point { float x, y; };
	std::array<Point, N> points;   // sorted by x

	[[nodiscard]] constexpr float at(const float x) const noexcept {
		if (x <= points[0].x)
			return points[0].y;
		for (std::size_t i = 1; i < N; ++i) {
			if (x <= points[i].x) {
				const Point a = points[i - 1];
				const Point b = points[i];
				return a.y + (b.y - a.y) * (x - a.x) / (b.x - a.x);
			}
		}
		return points[N - 1].y;
	}
};

// Hermite smoothstep from edge0 to edge1 (edge1 may be below edge0).
[[nodiscard]] constexpr float smoothstep(const float edge0, const float edge1, const float x) noexcept {
	float t = (x - edge0) / (edge1 - edge0);
	t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
	return t * t * (3.0f - 2.0f * t);
}

} // namespace worldgen

#endif /* WORLDGEN_SPLINE_HPP */
