#pragma once
#ifndef WORLDGEN_BIOMES_HPP
# define WORLDGEN_BIOMES_HPP

// ============================================================================
//  worldgen biomes: which biome a climate gives, and what its ground is made of
// ============================================================================

#include <cstdint>

#include "Block.hpp"

namespace worldgen {

struct Climate;

enum class Biome : std::uint8_t {
	Ocean, Beach, Plains, Forest, Desert, SnowyPlains, Taiga, Mountains, Badlands, Count
};

[[nodiscard]] const char* biomeName(Biome biome) noexcept;

// `surfaceHeight`: the column's base terrain height (before 3D detail).
// `mountainFactor` / `badlandsFactor`: how strongly mountain / canyon shaping applies (0..1).
[[nodiscard]] Biome chooseBiome(const Climate& climate, float surfaceHeight, float mountainFactor,
                                float badlandsFactor) noexcept;

// The block at `depth` blocks below the top of a solid run (0 = the top block),
// for a surface at world height `y`, or AIR_BLOCK to keep the default (stone).
// `variation` in [-1, 1] varies soil depth per column.
[[nodiscard]] BlockId surfaceBlock(Biome biome, int depth, int y, bool underwater, float variation) noexcept;

} // namespace worldgen

#endif /* WORLDGEN_BIOMES_HPP */
