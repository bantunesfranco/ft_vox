#pragma once
#ifndef WORLDGEN_CAVES_HPP
# define WORLDGEN_CAVES_HPP

// ============================================================================
//  worldgen::CaveCarver : noise caves, evaluated per block like the terrain
//  (no searching of neighbouring regions, so they cost about what terrain does)
//
//    cheese     big caverns with pillars: a low-frequency 3D noise above a threshold
//    spaghetti  long winding tunnels: two 3D noises both near zero
//    ravines    deep narrow cracks: a band of a 2D ridge noise, narrowing downwards
//
//  Noise values live on a 4x4x4 block grid aligned to world coordinates and
//  are interpolated in between; the carve test runs per block.
// ============================================================================

#include <cstdint>

#include "worldgen/Noise.hpp"

namespace worldgen {

constexpr int DEEP_LAVA_LEVEL = -50;    // carved space below this fills with lava

class CaveCarver {
public:
	explicit CaveCarver(std::uint64_t seed);

	// Raw noise values at a world position (what the grid stores).
	struct Sample {
		float cheese;
		float tunnelA;
		float tunnelB;
	};
	[[nodiscard]] Sample sample(int x, int y, int z) const noexcept;

	// ---- the carve rule, in the order it is applied (tune here) ---------------
	// `depth`: blocks below the column's surface. The generator checks them in
	// this order and stops at the first that decides, so it only interpolates
	// the noise values it needs.
	static constexpr float CHEESE_THRESHOLD    = 0.34f;   // caverns where cheese noise is above this
	static constexpr float CHEESE_NEAR_SURFACE = 0.012f;  // threshold rises per block above depth 24
	static constexpr float TUNNEL_WIDTH        = 0.075f;  // both tunnel noises within +-this
	static constexpr int   SURFACE_SEAL        = 4;       // blocks under the surface left alone
	static constexpr int   RAVINE_DEPTH        = 56;      // deepest a ravine cuts, in blocks

	// 1. Ravines (no 3D noise): the band narrows with depth. `ravine`: ravineStrength(x, z).
	[[nodiscard]] static bool ravineCarves(const float ravine, const int depth) noexcept {
		return ravine > 0.0f && depth < RAVINE_DEPTH
		    && ravine > 0.15f + 0.85f * static_cast<float>(depth) / static_cast<float>(RAVINE_DEPTH);
	}
	// 2. Nothing else carves within SURFACE_SEAL of the surface.
	// 3. Spaghetti: both tunnel noises near zero (a line).
	[[nodiscard]] static bool nearZero(const float tunnel) noexcept {
		return tunnel < TUNNEL_WIDTH && tunnel > -TUNNEL_WIDTH;
	}
	// 4. Cheese: big caverns, rarer close to the surface.
	[[nodiscard]] static bool cheeseCarves(const float cheese, const int depth) noexcept {
		const int nearSurface = depth < 24 ? 24 - depth : 0;
		return cheese > CHEESE_THRESHOLD + CHEESE_NEAR_SURFACE * static_cast<float>(nearSurface);
	}

	// 0 outside ravines, up to 1 at a ravine's centre line (2D, per column).
	[[nodiscard]] float ravineStrength(int x, int z) const noexcept;

	// Rare spots where tunnels may break through to the surface (2D, per column).
	[[nodiscard]] bool entrance(int x, int z) const noexcept;

private:
	Octaves _cheese;
	Octaves _tunnelA;
	Octaves _tunnelB;
	Octaves _ravine;
	Octaves _ravineRegion;
	Octaves _entrance;
};

} // namespace worldgen

#endif /* WORLDGEN_CAVES_HPP */
