#include "SelfTest.hpp"
#include "worldgen/Structures.hpp"
#include "worldgen/TerrainGenerator.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

using namespace worldgen;

namespace {

constexpr BlockId SAND   = toBlockId(BlockType::Sand);
constexpr BlockId CACTUS = toBlockId(BlockType::Cactus);

std::unique_ptr<ChunkColumn> make(const TerrainGenerator& gen, const ChunkCoord c) {
	auto column = std::make_unique<ChunkColumn>(c);
	gen.generate(*column);
	return column;
}

bool isLog(const BlockId b) {
	const BlockType t = toBlockType(b);
	return t == BlockType::OakLog || t == BlockType::BirchLog || t == BlockType::SpruceLog;
}

bool isLeaves(const BlockId b) {
	const BlockType t = toBlockType(b);
	return t == BlockType::OakLeaves || t == BlockType::BirchLeaves || t == BlockType::SpruceLeaves;
}

// Columns whose centre is in `biome`, scanning outward from the origin.
std::vector<ChunkCoord> columnsIn(const TerrainGenerator& gen, const Biome biome, const int wanted) {
	std::vector<ChunkCoord> found;
	for (int z = -200; z < 200 && static_cast<int>(found.size()) < wanted; z += 3)
		for (int x = -200; x < 200 && static_cast<int>(found.size()) < wanted; x += 3)
			if (gen.biomeAt(x * 16 + 8, z * 16 + 8) == biome)
				found.push_back({x, z});
	return found;
}

} // namespace

SELFTEST(Structures, forest_has_trees) {
	const TerrainGenerator gen(20261116);
	const auto columns = columnsIn(gen, Biome::Forest, 16);
	long logs = 0;
	for (const ChunkCoord c : columns) {
		const auto column = make(gen, c);
		for (int sy = 0; sy < ChunkColumn::HEIGHT; ++sy)
			for (int z = 0; z < 16; ++z)
				for (int x = 0; x < 16; ++x)
					logs += isLog(column->getUnchecked(x, sy, z)) ? 1 : 0;
	}
	std::printf("     %zu forest columns, %ld log blocks\n", columns.size(), logs);
	CHECK(columns.size() >= 8);
	CHECK(logs * 2 >= static_cast<long>(columns.size()));
}

SELFTEST(Structures, trees_cross_borders) {
	// Leaves near the west edge of column B whose trunk stands near the east
	// edge of its neighbour A: the canopy was placed across the border.
	const TerrainGenerator gen(20261116);
	const auto columns = columnsIn(gen, Biome::Forest, 16);
	int crossing = 0;
	for (const ChunkCoord c : columns) {
		const auto a = make(gen, c);
		const auto b = make(gen, {c.x + 1, c.z});
		for (int sy = 1; sy < ChunkColumn::HEIGHT - 1; ++sy)
			for (int z = 0; z < 16; ++z)
				for (int bx = 0; bx < 2; ++bx) {
					if (!isLeaves(b->getUnchecked(bx, sy, z)))
						continue;
					bool trunk = false;
					for (int ax = 14; ax < 16 && !trunk; ++ax)
						for (int dz = -2; dz <= 2 && !trunk; ++dz)
							for (int dy = -3; dy <= 1 && !trunk; ++dy) {
								const int az = z + dz, ay = sy + dy;
								if (az >= 0 && az < 16 && ay >= 0 && ay < ChunkColumn::HEIGHT)
									trunk = isLog(a->getUnchecked(ax, ay, az));
							}
					crossing += trunk ? 1 : 0;
				}
	}
	std::printf("     %d leaves next to a trunk in the neighbouring column\n", crossing);
	CHECK(crossing > 0);
}

SELFTEST(Structures, structures_deterministic) {
	const TerrainGenerator a(11), b(11);
	const auto columns = columnsIn(a, Biome::Forest, 6);
	int different = 0;
	for (const ChunkCoord c : columns) {
		const auto ca = make(a, c);
		const auto cb = make(b, c);
		for (int s = 0; s < ChunkColumn::SECTION_COUNT; ++s)
			different += ca->section(s).blocks() == cb->section(s).blocks() ? 0 : 1;
	}
	CHECK(!columns.empty());
	CHECK_EQ(different, 0);
}

SELFTEST(Structures, no_trees_in_water) {
	// Nothing stands on water and no structure block replaces water.
	const TerrainGenerator gen(20261116);
	int columns = 0, wet = 0;
	for (int z = -60; z < 60 && columns < 48; z += 5)
		for (int x = -60; x < 60 && columns < 48; x += 5) {
			const int h = gen.surfaceHeight(x * 16 + 8, z * 16 + 8);
			if (h < SEA_LEVEL - 6 || h > SEA_LEVEL + 6)
				continue;                              // coasts, where water meets land
			++columns;
			const auto column = make(gen, {x, z});
			for (int sy = 1; sy < ChunkColumn::HEIGHT; ++sy)
				for (int lz = 0; lz < 16; ++lz)
					for (int lx = 0; lx < 16; ++lx) {
						const BlockId b = column->getUnchecked(lx, sy, lz);
						if ((isLog(b) || b == CACTUS) && column->getUnchecked(lx, sy - 1, lz) == WATER_BLOCK)
							++wet;
					}
		}
	std::printf("     %d coast columns, %d structures on water\n", columns, wet);
	CHECK(columns >= 16);
	CHECK_EQ(wet, 0);
}

SELFTEST(Structures, cactus_on_sand) {
	const TerrainGenerator gen(20261116);
	const auto columns = columnsIn(gen, Biome::Desert, 24);
	int cacti = 0, misplaced = 0;
	for (const ChunkCoord c : columns) {
		const auto column = make(gen, c);
		for (int sy = 1; sy < ChunkColumn::HEIGHT; ++sy)
			for (int z = 0; z < 16; ++z)
				for (int x = 0; x < 16; ++x) {
					if (column->getUnchecked(x, sy, z) != CACTUS || column->getUnchecked(x, sy - 1, z) == CACTUS)
						continue;
					++cacti;                               // the bottom block of a cactus
					misplaced += column->getUnchecked(x, sy - 1, z) == SAND ? 0 : 1;
				}
	}
	std::printf("     %zu desert columns, %d cacti, %d not on sand\n", columns.size(), cacti, misplaced);
	CHECK(columns.size() >= 8);
	CHECK(cacti > 0);
	CHECK_EQ(misplaced, 0);
}

SELFTEST(Structures, trees_never_touch) {
	// Trunk bases (a log on non-log ground) are at least 3 blocks apart, so
	// two trees never stand side by side.
	const TerrainGenerator gen(20261116);
	int trunks = 0, touching = 0;
	for (const Biome biome : {Biome::Forest, Biome::Taiga}) {
		for (const ChunkCoord c : columnsIn(gen, biome, 12)) {
			const auto column = make(gen, c);
			std::vector<std::array<int, 2>> bases;
			for (int z = 0; z < 16; ++z)
				for (int x = 0; x < 16; ++x)
					for (int sy = 1; sy < ChunkColumn::HEIGHT; ++sy)
						if (isLog(column->getUnchecked(x, sy, z)) && !isLog(column->getUnchecked(x, sy - 1, z))) {
							bases.push_back({x, z});
							break;
						}
			trunks += static_cast<int>(bases.size());
			for (std::size_t i = 0; i < bases.size(); ++i)
				for (std::size_t j = i + 1; j < bases.size(); ++j)
					touching += std::max(std::abs(bases[i][0] - bases[j][0]), std::abs(bases[i][1] - bases[j][1])) <= 2 ? 1 : 0;
		}
	}
	std::printf("     %d trunks, %d pairs closer than 3 blocks\n", trunks, touching);
	CHECK(trunks > 20);
	CHECK_EQ(touching, 0);
}
