#pragma once
#ifndef WORLDGEN_TERRAIN_GENERATOR_HPP
# define WORLDGEN_TERRAIN_GENERATOR_HPP

// ============================================================================
//  worldgen::TerrainGenerator : the world, as a pure function of (seed, column)
//
//  Shape: density(x,y,z) = baseHeight(x,z) - y + detail3D(x,y,z) * wildness(x,z),
//  solid where > 0. baseHeight and wildness come from 2D climate noise
//  (continentalness, erosion, ridges) through Splines; detail3D gives cliffs,
//  overhangs and arches, strongly in mountains and badlands, barely in plains.
//  Density is evaluated on a 4 x 8 x 4 block grid aligned to world
//  coordinates (shared by neighbouring columns, so borders match) and
//  trilinearly interpolated in between.
//
//  Then: surface rules per biome (Biomes.hpp), water below sea level, bedrock.
//
//  Thread-safe and deterministic: every result depends only on the seed and
//  the coordinates (see Noise.hpp for why it is identical on every build).
// ============================================================================

#include <cstdint>

#include <glm/glm.hpp>

#include "Chunk.hpp"
#include "worldgen/Biomes.hpp"
#include "worldgen/Caves.hpp"
#include "worldgen/Noise.hpp"

namespace worldgen {

constexpr int SEA_LEVEL = 62;

struct Climate {
	float continentalness;   // -1 deep ocean .. +1 far inland
	float erosion;           // -1 rugged (mountains) .. +1 flat
	float ridges;            //  0 valley .. 1 peak
	float temperature;       // -1 frozen .. +1 hot
	float humidity;          // -1 dry .. +1 wet
};

class TerrainGenerator {
public:
	explicit TerrainGenerator(std::uint64_t seed);

	// Generation stages, as flags (tests generate terrain alone).
	enum class Stages : unsigned {
		Terrain    = 1u,      // shape, water, surface rules, bedrock
		Caves      = 2u,
		Structures = 4u,
		All        = 7u,
	};

	// Fills a freshly constructed (all air) column.
	void generate(ChunkColumn& column, Stages stages = Stages::All) const;

	[[nodiscard]] Climate climateAt(int x, int z) const noexcept;
	[[nodiscard]] Biome   biomeAt(int x, int z) const noexcept;

	// Highest solid terrain block at (x, z), as generate() makes it (before
	// caves and structures).
	[[nodiscard]] int surfaceHeight(int x, int z) const;

	// A land position near the origin to start players on (x, z).
	[[nodiscard]] glm::ivec2 findSpawn() const;

	[[nodiscard]] std::uint64_t seed() const noexcept { return _seed; }

private:
	struct Shape {
		Climate climate;
		float baseHeight;
		float wildness;      // amplitude of the 3D detail, in blocks
		float mountain;      // 0..1, how much mountain shaping applies
		float badlands;      // 0..1, how much canyon / mesa shaping applies
	};
	[[nodiscard]] Shape shapeAt(int x, int z) const noexcept;
	[[nodiscard]] float detailAt(const Shape& shape, int x, int y, int z) const noexcept;
	// Density at a grid node (world x, z multiple of 4; grid level gy).
	[[nodiscard]] float nodeDensity(const Shape& shape, int x, int gy, int z) const noexcept;
	[[nodiscard]] Biome biomeOf(const Shape& shape) const noexcept;
	void carveCaves(ChunkColumn& column) const;

	std::uint64_t _seed;
	Octaves _continentalness;
	Octaves _erosion;
	Octaves _ridges;
	Octaves _temperature;
	Octaves _humidity;
	Octaves _detail;
	Octaves _surfaceNoise;     // varies soil depth, clay patches
	Octaves _mesa;             // badlands plateaus
	CaveCarver _caves;
};

} // namespace worldgen

#endif /* WORLDGEN_TERRAIN_GENERATOR_HPP */
