#include "worldgen/TerrainGenerator.hpp"

#include <algorithm>
#include <array>
#include <cmath>

#include "worldgen/Spline.hpp"
#include "worldgen/Structures.hpp"

namespace worldgen {
namespace {

// Density grid: one node every CELL_XZ blocks horizontally and CELL_Y vertically.
constexpr int CELL_XZ = 4;
constexpr int CELL_Y  = 8;
constexpr int NODES_XZ = ChunkSection::SIZE / CELL_XZ + 1;          // 5
constexpr int NODES_Y  = ChunkColumn::HEIGHT / CELL_Y + 1;          // 41

// Cave grid: one node every CELL_CAVE blocks in every direction.
constexpr int CELL_CAVE    = 4;
constexpr int CAVE_NODES_Y = ChunkColumn::HEIGHT / CELL_CAVE + 1;   // 81

constexpr BlockId STONE   = toBlockId(BlockType::Stone);
constexpr BlockId BEDROCK = toBlockId(BlockType::Bedrock);
constexpr BlockId ICE     = toBlockId(BlockType::Ice);

int floorDiv(const int value, const int divisor) noexcept {
	const int q = value / divisor;
	const int r = value % divisor;
	return (r != 0 && ((r < 0) != (divisor < 0))) ? q - 1 : q;
}

int nodeWorldY(const int gy) noexcept { return ChunkColumn::MIN_WORLD_Y + gy * CELL_Y; }

// Fractal noise sits mostly within +-0.6: stretch it to use the whole [-1, 1].
float stretch(const float v) noexcept { return std::clamp(v * 1.6f, -1.0f, 1.0f); }

// The one interpolation used everywhere (generate() and surfaceHeight() must
// agree to the bit, so they share it). Split in two steps: the x step depends
// only on the cell and fx, so a block column computes it once per cell.
struct XLerps {
	float x00, x10, x01, x11;
};

XLerps lerpX(const float c000, const float c100, const float c010, const float c110,
             const float c001, const float c101, const float c011, const float c111,
             const float fx) noexcept {
	return {c000 + (c100 - c000) * fx, c010 + (c110 - c010) * fx,
	        c001 + (c101 - c001) * fx, c011 + (c111 - c011) * fx};
}

float lerpYZ(const XLerps& x, const float fy, const float fz) noexcept {
	const float y0 = x.x00 + (x.x10 - x.x00) * fy;
	const float y1 = x.x01 + (x.x11 - x.x01) * fy;
	return y0 + (y1 - y0) * fz;
}

float trilinear(const float c000, const float c100, const float c010, const float c110,
                const float c001, const float c101, const float c011, const float c111,
                const float fx, const float fy, const float fz) noexcept {
	return lerpYZ(lerpX(c000, c100, c010, c110, c001, c101, c011, c111, fx), fy, fz);
}

float fraction(const int local, const int cell) noexcept {
	return static_cast<float>(local % cell) / static_cast<float>(cell);
}

// ---- shape tables (tune here) -------------------------------------------------------

// Biome borders: the lookup position moves by a smooth warp (up to ~BIOME_WARP_BLOCKS)
// plus a per-block jitter (+-BIOME_DITHER_BLOCKS / 2), so surfaces mix over a few blocks.
constexpr float BIOME_WARP_FREQUENCY = 1.0f / 14.0f;
constexpr float BIOME_WARP_BLOCKS    = 5.0f;
constexpr float BIOME_DITHER_BLOCKS  = 10.0f;

// Ground height from continentalness: deep ocean .. coast .. far inland.
constexpr Spline<9> CONTINENTAL{{{
	{-1.00f, 14.0f}, {-0.60f, 26.0f}, {-0.35f, 40.0f}, {-0.18f, 53.0f}, {-0.08f, 60.0f},
	{0.00f, 64.0f}, {0.25f, 71.0f}, {0.60f, 80.0f}, {1.00f, 90.0f}}}};

// How mountainous: rugged (low erosion) inland terrain.
constexpr Spline<5> MOUNTAIN{{{
	{-1.00f, 1.00f}, {-0.55f, 0.85f}, {-0.30f, 0.40f}, {-0.10f, 0.00f}, {1.00f, 0.00f}}}};

// Gentle hills everywhere else: stronger where erosion is moderate.
constexpr Spline<4> HILLS{{{
	{-0.40f, 1.00f}, {0.20f, 0.80f}, {0.60f, 0.35f}, {1.00f, 0.15f}}}};

// The column's blocks as raw arrays, for the generation loops: no uniform
// section check on every access. Sections up to `denseUpTo` are made dense
// (generate() compacts the column when done); above it a section still all
// air is read from a shared air array and must not be written.
class DenseBlocks {
public:
	DenseBlocks(ChunkColumn& column, const int denseUpTo) {
		static_assert(AIR_BLOCK == 0, "a zeroed array reads as air");
		static const ChunkSection::Blocks air{};
		for (int s = 0; s < ChunkColumn::SECTION_COUNT; ++s) {
			ChunkSection& section = column.section(s);
			const auto i = static_cast<std::size_t>(s);
			if (s > denseUpTo && section.isUniform() && section.uniformBlock() == AIR_BLOCK) {
				_read[i] = air.data();
				_write[i] = nullptr;
			} else {
				_write[i] = section.makeDense().data();
				_read[i] = _write[i];
			}
		}
	}

	[[nodiscard]] BlockId get(const int x, const int y, const int z) const noexcept { return _read[section(y)][at(x, y, z)]; }

	void set(const int x, const int y, const int z, const BlockId block) noexcept {
		assert(_write[section(y)] != nullptr);
		_write[section(y)][at(x, y, z)] = block;
	}

private:
	std::array<const BlockId*, ChunkColumn::SECTION_COUNT> _read{};
	std::array<BlockId*, ChunkColumn::SECTION_COUNT> _write{};

	[[nodiscard]] static std::size_t section(const int y) noexcept { return static_cast<std::size_t>(y >> 4); }

	[[nodiscard]] static std::size_t at(const int x, const int y, const int z) noexcept {
		return static_cast<std::size_t>(x + (z << 4) + ((y & 15) << 8));
	}
};

} // namespace

TerrainGenerator::TerrainGenerator(const std::uint64_t seed)
	: _seed(seed),
	  _continentalness(mixSeed(seed, 1), 4, 1.0f / 1600.0f),
	  _erosion(mixSeed(seed, 2), 3, 1.0f / 900.0f),
	  _ridges(mixSeed(seed, 3), 3, 1.0f / 380.0f),
	  _temperature(mixSeed(seed, 4), 3, 1.0f / 1100.0f),
	  _humidity(mixSeed(seed, 5), 3, 1.0f / 900.0f),
	  _detail(mixSeed(seed, 6), 3, 1.0f / 40.0f),
	  _surfaceNoise(mixSeed(seed, 7), 2, 1.0f / 24.0f),
	  _mesa(mixSeed(seed, 8), 2, 1.0f / 110.0f),
	  _biomeWarp(mixSeed(seed, 9)),
	  _biomeDitherSeed(mixSeed(seed, 10)),
	  _caves(mixSeed(seed, 20)) {}

Climate TerrainGenerator::climateAt(const int x, const int z) const noexcept {
	const float fx = static_cast<float>(x);
	const float fz = static_cast<float>(z);
	// Ridged noise: sharp crests where the noise crosses zero.
	const float ridged = std::clamp(1.0f - std::abs(_ridges.at(fx, fz)) * 2.2f, 0.0f, 1.0f);
	return {
		stretch(_continentalness.at(fx, fz)),
		stretch(_erosion.at(fx, fz)),
		ridged,
		stretch(_temperature.at(fx, fz)),
		stretch(_humidity.at(fx, fz)),
	};
}

TerrainGenerator::Shape TerrainGenerator::shapeAt(const int x, const int z) const noexcept {
	const Climate c = climateAt(x, z);
	const float inland = smoothstep(-0.05f, 0.35f, c.continentalness);

	float base = CONTINENTAL.at(c.continentalness);

	// Mountain ranges: tall along the ridges, rising out of the plains.
	const float mountain = MOUNTAIN.at(c.erosion) * inland;
	base += mountain * (28.0f + 125.0f * c.ridges);

	// Hills on the rest of the land.
	base += (1.0f - mountain) * inland * (c.ridges - 0.35f) * 16.0f * HILLS.at(c.erosion);

	// Badlands: hot and dry inland, stepped mesas cut by canyons.
	const float badlands = smoothstep(0.15f, 0.45f, c.temperature) * smoothstep(0.05f, -0.3f, c.humidity)
	                     * inland * (1.0f - mountain);
	if (badlands > 0.0f) {
		// Mesas: where the mesa noise crosses its thresholds the ground jumps
		// (steep cliffs), elsewhere it is flat; heights snap to 6-block steps.
		const float m = _mesa.at(static_cast<float>(x), static_cast<float>(z));
		float mesa = smoothstep(-0.05f, 0.05f, m) * 36.0f + smoothstep(0.22f, 0.28f, m) * 22.0f;
		mesa = std::floor(mesa / 6.0f) * 6.0f;
		base += badlands * (mesa - 6.0f);
	}

	base = std::clamp(base, static_cast<float>(ChunkColumn::MIN_WORLD_Y + 16),
	                  static_cast<float>(ChunkColumn::MAX_WORLD_Y - 24));
	const float wildness = 2.5f + 24.0f * mountain + 7.0f * badlands;
	return {c, base, wildness, mountain, badlands};
}

float TerrainGenerator::detailAt(const Shape& shape, const int x, const int y, const int z) const noexcept {
	const float fx = static_cast<float>(x), fy = static_cast<float>(y), fz = static_cast<float>(z);
	const float rough = _detail.at(fx, fy * 1.4f, fz);
	if (shape.badlands < 0.05f)
		return rough;
	// Canyon walls: the same noise stretched vertically gives cliffs, not bumps.
	const float walls = _detail.at(fx * 1.3f, fy * 0.2f, fz * 1.3f);
	return rough + (walls - rough) * shape.badlands;
}

float TerrainGenerator::nodeDensity(const Shape& shape, const int x, const int gy, const int z) const noexcept {
	const int y = nodeWorldY(gy);
	const float v = shape.baseHeight - static_cast<float>(y);
	// Far from the surface the detail cannot change the sign: skip the noise.
	if (std::abs(v) > shape.wildness + 8.0f)
		return v;
	return v + detailAt(shape, x, y, z) * shape.wildness;
}

Biome TerrainGenerator::biomeOf(const Shape& s) const noexcept {
	return chooseBiome(s.climate, s.baseHeight, s.mountain, s.badlands);
}

glm::ivec2 TerrainGenerator::biomeLookup(const int x, const int z) const noexcept {
	// Borders blend: the biome is read a few blocks away, along a smooth warp
	// (irregular border lines) plus a per-block hash (a dithered edge).
	const float fx = static_cast<float>(x) * BIOME_WARP_FREQUENCY;
	const float fz = static_cast<float>(z) * BIOME_WARP_FREQUENCY;
	const std::uint32_t h = hash3(_biomeDitherSeed, x, 0, z);
	// Uniform in -0.5 .. 0.5: next to the border half the blocks take the other
	// biome, thinning out linearly to none BIOME_DITHER_BLOCKS / 2 away.
	const float hx = static_cast<float>(h & 0xFFFFu) * (1.0f / 65535.0f) - 0.5f;
	const float hz = static_cast<float>(h >> 16) * (1.0f / 65535.0f) - 0.5f;
	const float dx = _biomeWarp.at(fx, fz) * BIOME_WARP_BLOCKS + hx * BIOME_DITHER_BLOCKS;
	const float dz = _biomeWarp.at(fx + 71.3f, fz - 37.9f) * BIOME_WARP_BLOCKS + hz * BIOME_DITHER_BLOCKS;
	return {x + static_cast<int>(std::floor(dx + 0.5f)), z + static_cast<int>(std::floor(dz + 0.5f))};
}

Biome TerrainGenerator::biomeAt(const int x, const int z) const noexcept {
	const glm::ivec2 at = biomeLookup(x, z);
	return biomeOf(shapeAt(at.x, at.y));
}

void TerrainGenerator::generate(ChunkColumn& column, const Stages stages) const {
	const auto has = [stages](const Stages s) {
		return (static_cast<unsigned>(stages) & static_cast<unsigned>(s)) != 0u;
	};
	if (!has(Stages::Terrain))
		return;                          // later stages build on the terrain
	const ChunkCoord coord = column.coord();
	const int baseX = coord.x * ChunkSection::SIZE;
	const int baseZ = coord.z * ChunkSection::SIZE;

	// 1. Density nodes (corners shared with the neighbouring columns).
	std::array<Shape, NODES_XZ * NODES_XZ> nodeShapes{};
	std::array<float, NODES_XZ * NODES_Y * NODES_XZ> density{};
	const auto node = [](const int i, const int gy, const int k) {
		return static_cast<std::size_t>((k * NODES_Y + gy) * NODES_XZ + i);
	};
	for (int k = 0; k < NODES_XZ; ++k)
		for (int i = 0; i < NODES_XZ; ++i) {
			const int wx = baseX + i * CELL_XZ;
			const int wz = baseZ + k * CELL_XZ;
			const Shape shape = shapeAt(wx, wz);
			nodeShapes[static_cast<std::size_t>(k * NODES_XZ + i)] = shape;
			for (int gy = 0; gy < NODES_Y; ++gy)
				density[node(i, gy, k)] = nodeDensity(shape, wx, gy, wz);
		}

	// 2. Stone where the interpolated density is positive, water below sea level.
	static_assert(ChunkColumn::HEIGHT % CELL_Y == 0, "the cells must fill the column exactly");
	// Stone only in cells with a positive corner: the highest written block is
	// under the cell above the highest positive node, or the sea surface.
	int highest = ChunkColumn::toStorageY(SEA_LEVEL);
	for (std::size_t n = 0; n < density.size(); ++n)
		if (density[n] > 0.0f) {
			const int gy = static_cast<int>(n / NODES_XZ % NODES_Y);
			highest = std::max(highest, std::min(ChunkColumn::HEIGHT - 1, gy * CELL_Y + CELL_Y - 1));
		}
	DenseBlocks blocks(column, highest / ChunkSection::SIZE);
	for (int z = 0; z < ChunkSection::SIZE; ++z)
		for (int x = 0; x < ChunkSection::SIZE; ++x) {
			const int i = x / CELL_XZ, k = z / CELL_XZ;
			const float fx = fraction(x, CELL_XZ), fz = fraction(z, CELL_XZ);
			for (int gy = 0; gy + 1 < NODES_Y; ++gy) {
				const XLerps cell = lerpX(
					density[node(i, gy, k)],         density[node(i + 1, gy, k)],
					density[node(i, gy + 1, k)],     density[node(i + 1, gy + 1, k)],
					density[node(i, gy, k + 1)],     density[node(i + 1, gy, k + 1)],
					density[node(i, gy + 1, k + 1)], density[node(i + 1, gy + 1, k + 1)], fx);
				for (int sy = gy * CELL_Y; sy < gy * CELL_Y + CELL_Y; ++sy) {
					const float d = lerpYZ(cell, fraction(sy, CELL_Y), fz);
					if (d > 0.0f)
						blocks.set(x, sy, z, STONE);
					else if (ChunkColumn::toWorldY(sy) <= SEA_LEVEL)
						blocks.set(x, sy, z, WATER_BLOCK);
				}
			}
		}

	// 3. Surface rules, bedrock, ice, per block column.
	const std::uint64_t bedrockSeed = mixSeed(_seed, 99);
	for (int z = 0; z < ChunkSection::SIZE; ++z)
		for (int x = 0; x < ChunkSection::SIZE; ++x) {
			const int wx = baseX + x, wz = baseZ + z;
			const glm::ivec2 at = biomeLookup(wx, wz);       // the same lookup as biomeAt()
			const Shape shape = shapeAt(at.x, at.y);
			const Biome biome = biomeOf(shape);
			const float variation = _surfaceNoise.at(static_cast<float>(wx), static_cast<float>(wz));

			// Top down: every solid run starting under air or water gets the
			// biome's surface layers (also under overhangs).
			int depth = 0;
			bool underwater = false;
			for (int sy = ChunkColumn::HEIGHT - 1; sy >= 0; --sy) {
				const BlockId current = blocks.get(x, sy, z);
				if (current != STONE) {
					depth = 0;
					underwater = current == WATER_BLOCK;
					continue;
				}
				if (depth < 20) {
					const BlockId surface = surfaceBlock(biome, depth, ChunkColumn::toWorldY(sy), underwater, variation,
					                                     shape.climate.temperature);
					if (surface != AIR_BLOCK)
						blocks.set(x, sy, z, surface);
				}
				++depth;
			}

			const int bedrockTop = static_cast<int>(hash3(bedrockSeed, wx, 0, wz) % 3u);
			for (int sy = 0; sy <= bedrockTop; ++sy)
				blocks.set(x, sy, z, BEDROCK);

			const int seaTop = ChunkColumn::toStorageY(SEA_LEVEL);
			if (shape.climate.temperature < -0.45f && blocks.get(x, seaTop, z) == WATER_BLOCK)
				blocks.set(x, seaTop, z, ICE);
		}

	// 4. Caves, after the surface rules so cave floors stay stone.
	if (has(Stages::Caves))
		carveCaves(column);

	// 5. Trees, cactus, boulders (they read the ground from surfaceHeight).
	if (has(Stages::Structures))
		placeStructures(*this, mixSeed(_seed, 30), column);

	// Sections left all one block (deep stone, open sky) store just the id.
	column.compact();
}

void TerrainGenerator::carveCaves(ChunkColumn& column) const {
	DenseBlocks blocks(column, -1);          // carves only solid blocks: sections still air stay so
	const ChunkCoord coord = column.coord();
	const int baseX = coord.x * ChunkSection::SIZE;
	const int baseZ = coord.z * ChunkSection::SIZE;
	const auto isOpen = [](const BlockId b) { return b == AIR_BLOCK || b == WATER_BLOCK; };

	// Top solid block of every block column; columns under sea level stay sealed.
	std::array<int, ChunkSection::SIZE * ChunkSection::SIZE> tops{};
	int maxTop = -1;
	for (int z = 0; z < ChunkSection::SIZE; ++z)
		for (int x = 0; x < ChunkSection::SIZE; ++x) {
			int top = 0;
			for (int sy = ChunkColumn::HEIGHT - 1; sy > 0; --sy)
				if (!isOpen(blocks.get(x, sy, z))) {
					top = sy;
					break;
				}
			tops[static_cast<std::size_t>(z * ChunkSection::SIZE + x)] = top;
			if (ChunkColumn::toWorldY(top) >= SEA_LEVEL)
				maxTop = std::max(maxTop, top);
		}
	if (maxTop < 0)
		return;

	// Carver noise on a 4x4x4 grid aligned to world coordinates, up to 8 blocks
	// above the highest surface.
	const int topNode = std::min(CAVE_NODES_Y - 1, (maxTop + 8) / CELL_CAVE + 1);
	std::array<CaveCarver::Sample, NODES_XZ * CAVE_NODES_Y * NODES_XZ> samples{};
	const auto node = [](const int i, const int gy, const int k) {
		return static_cast<std::size_t>((k * CAVE_NODES_Y + gy) * NODES_XZ + i);
	};
	for (int k = 0; k < NODES_XZ; ++k)
		for (int i = 0; i < NODES_XZ; ++i)
			for (int gy = 1; gy <= topNode; ++gy)
				samples[node(i, gy, k)] = _caves.sample(baseX + i * CELL_CAVE,
				                                        ChunkColumn::MIN_WORLD_Y + gy * CELL_CAVE,
				                                        baseZ + k * CELL_CAVE);

	static_assert(ChunkColumn::HEIGHT % CELL_CAVE == 0, "the cave cells must fill the column exactly");
	const int lowest = ChunkColumn::toStorageY(ChunkColumn::MIN_WORLD_Y + 5);
	const int deepLava = ChunkColumn::toStorageY(DEEP_LAVA_LEVEL);
	for (int z = 0; z < ChunkSection::SIZE; ++z)
		for (int x = 0; x < ChunkSection::SIZE; ++x) {
			const int top = tops[static_cast<std::size_t>(z * ChunkSection::SIZE + x)];
			if (ChunkColumn::toWorldY(top) < SEA_LEVEL)
				continue;
			const int wx = baseX + x, wz = baseZ + z;
			const float ravine = _caves.ravineStrength(wx, wz);
			const bool entrance = _caves.entrance(wx, wz);
			const int i = x / CELL_CAVE, k = z / CELL_CAVE;
			const float fx = fraction(x, CELL_CAVE), fz = fraction(z, CELL_CAVE);

			// The x step of each field's interpolation, per cell, computed when
			// first needed (cellGy: the cell they belong to).
			std::array<XLerps, 3> cellLerps{};
			std::array<bool, 3> cellHas{};
			int cellGy = -1;
			// Top down; nothing is carved within 4 blocks under water (overhangs
			// on the coast hold water that must not drain into a cave).
			int sinceWater = 1000;
			for (int sy = top; sy >= lowest; --sy) {
				const BlockId current = blocks.get(x, sy, z);
				++sinceWater;
				if (current == WATER_BLOCK)
					sinceWater = 0;
				if (isOpen(current) || current == BEDROCK || sinceWater <= 4)
					continue;
				const int gy = sy / CELL_CAVE;
				const float fy = fraction(sy, CELL_CAVE);
				if (gy != cellGy) {
					cellGy = gy;
					cellHas = {};
				}
				const auto lerp = [&](const std::size_t slot, float CaveCarver::Sample::*field) {
					if (!cellHas[slot]) {
						cellLerps[slot] = lerpX(
							samples[node(i, gy, k)].*field,         samples[node(i + 1, gy, k)].*field,
							samples[node(i, gy + 1, k)].*field,     samples[node(i + 1, gy + 1, k)].*field,
							samples[node(i, gy, k + 1)].*field,     samples[node(i + 1, gy, k + 1)].*field,
							samples[node(i, gy + 1, k + 1)].*field, samples[node(i + 1, gy + 1, k + 1)].*field, fx);
						cellHas[slot] = true;
					}
					return lerpYZ(cellLerps[slot], fy, fz);
				};
				// The carve rule in order, interpolating only what each step needs.
				int depth = top - sy;
				if (entrance)
					depth = std::max(depth, CaveCarver::SURFACE_SEAL);   // tunnels may break through
				bool carve = CaveCarver::ravineCarves(ravine, depth);
				if (!carve && depth >= CaveCarver::SURFACE_SEAL) {
					carve = CaveCarver::nearZero(lerp(0, &CaveCarver::Sample::tunnelA))
					     && CaveCarver::nearZero(lerp(1, &CaveCarver::Sample::tunnelB));
					if (!carve)
						carve = CaveCarver::cheeseCarves(lerp(2, &CaveCarver::Sample::cheese), depth);
				}
				if (carve)
					blocks.set(x, sy, z, sy < deepLava ? LAVA_BLOCK : AIR_BLOCK);
			}
		}
}

int TerrainGenerator::surfaceHeight(const int x, const int z) const {
	const int gx = floorDiv(x, CELL_XZ) * CELL_XZ;
	const int gz = floorDiv(z, CELL_XZ) * CELL_XZ;
	const float fx = fraction(x - gx, CELL_XZ);
	const float fz = fraction(z - gz, CELL_XZ);

	const std::array<Shape, 4> shapes{shapeAt(gx, gz), shapeAt(gx + CELL_XZ, gz),
	                                  shapeAt(gx, gz + CELL_XZ), shapeAt(gx + CELL_XZ, gz + CELL_XZ)};
	const auto nodes = [&](const int gy) {
		return std::array<float, 4>{nodeDensity(shapes[0], gx, gy, gz), nodeDensity(shapes[1], gx + CELL_XZ, gy, gz),
		                            nodeDensity(shapes[2], gx, gy, gz + CELL_XZ),
		                            nodeDensity(shapes[3], gx + CELL_XZ, gy, gz + CELL_XZ)};
	};

	// Top down, one grid cell at a time, the same interpolation as generate().
	std::array<float, 4> upper = nodes(NODES_Y - 1);
	for (int gy = NODES_Y - 2; gy >= 0; --gy) {
		const std::array<float, 4> lower = nodes(gy);
		for (int sy = gy * CELL_Y + CELL_Y - 1; sy >= gy * CELL_Y; --sy) {
			const float d = trilinear(lower[0], lower[1], upper[0], upper[1],
			                          lower[2], lower[3], upper[2], upper[3],
			                          fx, fraction(sy, CELL_Y), fz);
			if (d > 0.0f)
				return ChunkColumn::toWorldY(sy);
		}
		upper = lower;
	}
	return ChunkColumn::MIN_WORLD_Y;    // bedrock is always there
}

glm::ivec2 TerrainGenerator::findSpawn() const {
	// Outward square rings around the origin, every 32 blocks: the first spot
	// on dry land that is not a beach.
	for (int ring = 0; ring <= 64; ++ring) {
		for (int dz = -ring; dz <= ring; ++dz)
			for (int dx = -ring; dx <= ring; ++dx) {
				if (std::max(std::abs(dx), std::abs(dz)) != ring)
					continue;
				const int x = dx * 32 + 8, z = dz * 32 + 8;
				const Biome biome = biomeAt(x, z);
				if (biome != Biome::Ocean && biome != Biome::Beach && surfaceHeight(x, z) > SEA_LEVEL)
					return {x, z};
			}
	}
	return {8, 8};
}

} // namespace worldgen
