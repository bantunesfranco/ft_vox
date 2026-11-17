#include "SelfTest.hpp"
#include "worldgen/Caves.hpp"
#include "worldgen/TerrainGenerator.hpp"

#include <cstdio>
#include <memory>

using namespace worldgen;

namespace {

std::unique_ptr<ChunkColumn> make(const TerrainGenerator& gen, const ChunkCoord c,
                                  const TerrainGenerator::Stages stages = TerrainGenerator::Stages::All) {
	auto column = std::make_unique<ChunkColumn>(c);
	gen.generate(*column, stages);
	return column;
}

// Land columns near the origin (surface well above sea level).
template <typename F>
int forLandColumns(const TerrainGenerator& gen, const int wanted, F&& visit) {
	int found = 0;
	for (int z = -40; z < 40 && found < wanted; z += 3)
		for (int x = -40; x < 40 && found < wanted; x += 3)
			if (gen.surfaceHeight(x * 16 + 8, z * 16 + 8) > SEA_LEVEL + 4) {
				visit(ChunkCoord{x, z});
				++found;
			}
	return found;
}

} // namespace

SELFTEST(Caves, cave_fraction) {
	const TerrainGenerator gen(20261116);
	long carved = 0, total = 0;
	const int columns = forLandColumns(gen, 64, [&](const ChunkCoord c) {
		const auto solidOnly = make(gen, c, TerrainGenerator::Stages::Terrain);
		const auto withCaves = make(gen, c);
		for (int z = 0; z < 16; ++z)
			for (int x = 0; x < 16; ++x) {
				const int top = gen.surfaceHeight(c.x * 16 + x, c.z * 16 + z) - 8;
				for (int y = DEEP_WATER_LEVEL; y < top; ++y) {
					const int sy = ChunkColumn::toStorageY(y);
					if (solidOnly->getUnchecked(x, sy, z) == AIR_BLOCK || solidOnly->getUnchecked(x, sy, z) == WATER_BLOCK)
						continue;
					++total;
					const BlockId b = withCaves->getUnchecked(x, sy, z);
					carved += (b == AIR_BLOCK || b == WATER_BLOCK) ? 1 : 0;
				}
			}
	});
	const double fraction = total ? static_cast<double>(carved) / static_cast<double>(total) : 0.0;
	std::printf("     %d columns, %.1f %% carved\n", columns, fraction * 100.0);
	CHECK(columns >= 32);
	CHECK(fraction >= 0.03 && fraction <= 0.20);
}

SELFTEST(Caves, no_air_under_water) {
	const TerrainGenerator gen(20261116);
	int columns = 0, holes = 0;
	for (int z = -60; z < 60 && columns < 64; z += 5)
		for (int x = -60; x < 60 && columns < 64; x += 5) {
			const int h = gen.surfaceHeight(x * 16 + 8, z * 16 + 8);
			if (h > SEA_LEVEL + 2)
				continue;                              // only ocean and coast
			++columns;
			const auto column = make(gen, {x, z});
			for (int lz = 0; lz < 16; ++lz)
				for (int lx = 0; lx < 16; ++lx)
					for (int sy = 1; sy < ChunkColumn::HEIGHT; ++sy)
						if (column->getUnchecked(lx, sy, lz) == WATER_BLOCK && column->getUnchecked(lx, sy - 1, lz) == AIR_BLOCK)
							++holes;
		}
	std::printf("     %d ocean/coast columns, %d air-under-water blocks\n", columns, holes);
	CHECK(columns >= 16);
	CHECK_EQ(holes, 0);
}

SELFTEST(Caves, deep_water) {
	const TerrainGenerator gen(20261116);
	long water = 0;
	forLandColumns(gen, 64, [&](const ChunkCoord c) {
		const auto column = make(gen, c);
		for (int z = 0; z < 16; ++z)
			for (int x = 0; x < 16; ++x)
				for (int y = ChunkColumn::MIN_WORLD_Y + 4; y < DEEP_WATER_LEVEL; ++y)
					water += column->getUnchecked(x, ChunkColumn::toStorageY(y), z) == WATER_BLOCK ? 1 : 0;
	});
	std::printf("     %ld deep cave water blocks\n", water);
	CHECK(water > 0);
}

SELFTEST(Caves, deterministic) {
	const TerrainGenerator a(5), b(5);
	int different = 0;
	for (int i = 0; i < 8; ++i) {
		const auto ca = make(a, {i * 9 - 30, i * 5});
		const auto cb = make(b, {i * 9 - 30, i * 5});
		for (int s = 0; s < ChunkColumn::SECTION_COUNT; ++s)
			different += ca->section(s).blocks() == cb->section(s).blocks() ? 0 : 1;
	}
	CHECK_EQ(different, 0);
}
