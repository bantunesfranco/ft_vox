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

constexpr int DEEP_WATER_LEVEL = -50;   // carved space below this fills with water

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

	// Whether a block is carved, from interpolated samples. `depth`: blocks
	// below the column's surface; `ravine`: ravineStrength(x, z).
	[[nodiscard]] static bool carved(const Sample& s, int y, int depth, float ravine) noexcept;

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
