#pragma once
#ifndef WORLDGEN_STRUCTURES_HPP
# define WORLDGEN_STRUCTURES_HPP

// ============================================================================
//  worldgen::placeStructures : trees, cactus and boulders
//
//  Each world position (x, z) may be the base of one structure, decided by a
//  hash of (seed, x, z) against the biome's density. A column places every
//  structure overlapping it, including those based up to MARGIN blocks over
//  its edge, from (seed, coordinates) alone, so canopies match across borders.
//  Ground comes from TerrainGenerator::surfaceHeight (before caves); bases on
//  ravines, cave entrances, water or slopes steeper than 2 blocks are skipped.
//  Leaves only fill air; logs fill air or leaves; terrain is never replaced.
// ============================================================================

#include <cstdint>

#include "Chunk.hpp"

namespace worldgen {

class TerrainGenerator;

constexpr int STRUCTURE_MARGIN = 3;    // widest reach of a structure from its base

void placeStructures(const TerrainGenerator& gen, std::uint64_t seed, ChunkColumn& column);

} // namespace worldgen

#endif /* WORLDGEN_STRUCTURES_HPP */
