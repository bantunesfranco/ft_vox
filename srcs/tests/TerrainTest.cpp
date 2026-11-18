#include "SelfTest.hpp"
#include "worldgen/TerrainGenerator.hpp"

#include <array>
#include <chrono>
#include <cstdio>
#include <memory>
#include <vector>

using namespace worldgen;

namespace {

std::unique_ptr<ChunkColumn> generated(const TerrainGenerator& gen, const ChunkCoord c,
                                       const TerrainGenerator::Stages stages = TerrainGenerator::Stages::All) {
	auto column = std::make_unique<ChunkColumn>(c);
	gen.generate(*column, stages);
	return column;
}

bool sameBlocks(const ChunkColumn& a, const ChunkColumn& b) {
	for (int s = 0; s < ChunkColumn::SECTION_COUNT; ++s)
		if (!(a.section(s) == b.section(s)))
			return false;
	return true;
}

bool solid(const BlockId b) {
	return b != AIR_BLOCK && b != WATER_BLOCK && b != toBlockId(BlockType::Ice);
}

// Cells where the solid/air pattern differs between plane x = ax of A and x = bx of B.
int planeMismatch(const ChunkColumn& a, const int ax, const ChunkColumn& b, const int bx) {
	int n = 0;
	for (int y = 0; y < ChunkColumn::HEIGHT; ++y)
		for (int z = 0; z < 16; ++z)
			n += solid(a.getUnchecked(ax, y, z)) != solid(b.getUnchecked(bx, y, z)) ? 1 : 0;
	return n;
}

} // namespace

SELFTEST(Terrain, generation_deterministic) {
	const TerrainGenerator a(7), b(7);
	int different = 0;
	for (int i = 0; i < 16; ++i) {
		const ChunkCoord c{i * 13 - 50, i * -7 + 20};
		different += sameBlocks(*generated(a, c), *generated(b, c)) ? 0 : 1;
	}
	CHECK_EQ(different, 0);
}

SELFTEST(Terrain, seed_changes_world) {
	const TerrainGenerator a(7), b(8);
	int same = 0;
	for (int i = 0; i < 8; ++i) {
		const ChunkCoord c{i * 40, -i * 40};
		same += sameBlocks(*generated(a, c), *generated(b, c)) ? 1 : 0;
	}
	CHECK(same <= 1);
}

SELFTEST(Terrain, border_continuity) {
	// A grid misaligned between columns makes borders much rougher than the
	// inside of a column: compare the two.
	const TerrainGenerator gen(11);
	int border = 0, inside = 0;
	for (int i = 0; i < 8; ++i) {
		const ChunkCoord left{i * 31 - 120, i * 17 - 60};
		const auto a = generated(gen, left);
		const auto b = generated(gen, {left.x + 1, left.z});
		border += planeMismatch(*a, 15, *b, 0);
		inside += planeMismatch(*a, 7, *a, 8);
	}
	std::printf("     border mismatch %d, inside mismatch %d\n", border, inside);
	CHECK(border <= inside * 2 + 64);
}

SELFTEST(Terrain, biome_coverage) {
	const TerrainGenerator gen(20261116);
	std::array<int, static_cast<std::size_t>(Biome::Count)> count{};
	for (int z = -4096; z < 4096; z += 64)
		for (int x = -4096; x < 4096; x += 64)
			++count[static_cast<std::size_t>(gen.biomeAt(x, z))];
	for (std::size_t i = 0; i < count.size(); ++i) {
		std::printf("     %-12s %d\n", biomeName(static_cast<Biome>(i)), count[i]);
		CHECK(count[i] > 0);
	}
}

SELFTEST(Terrain, height_range) {
	const TerrainGenerator gen(20261116);
	int highest = -1000, lowest = 1000;
	for (int z = -4096; z < 4096; z += 128)
		for (int x = -4096; x < 4096; x += 128) {
			const int h = gen.surfaceHeight(x, z);
			highest = std::max(highest, h);
			lowest = std::min(lowest, h);
		}
	std::printf("     surface from %d to %d\n", lowest, highest);
	CHECK(highest >= 150);
	CHECK(lowest <= 45);
	CHECK(highest < ChunkColumn::MAX_WORLD_Y);
}

SELFTEST(Terrain, surface_height_matches_generation) {
	const TerrainGenerator gen(3);
	int wrong = 0;
	for (int i = 0; i < 12; ++i) {
		const ChunkCoord c{i * 23 - 90, i * 11 - 40};
		const auto column = generated(gen, c, TerrainGenerator::Stages::Terrain);
		for (int z = 0; z < 16; z += 5)
			for (int x = 0; x < 16; x += 5) {
				int top = ChunkColumn::MIN_WORLD_Y - 1;
				for (int sy = ChunkColumn::HEIGHT - 1; sy >= 0; --sy)
					if (solid(column->getUnchecked(x, sy, z))) {
						top = ChunkColumn::toWorldY(sy);
						break;
					}
				wrong += top == gen.surfaceHeight(c.x * 16 + x, c.z * 16 + z) ? 0 : 1;
			}
	}
	CHECK_EQ(wrong, 0);
}

SELFTEST(Terrain, plains_walkable) {
	// Plains must not be noise soup: almost no solid block hangs over air.
	const TerrainGenerator gen(20261116);
	int columns = 0, overhangs = 0;
	for (int z = -2048; z < 2048 && columns < 24; z += 96)
		for (int x = -2048; x < 2048 && columns < 24; x += 96) {
			if (gen.biomeAt(x, z) != Biome::Plains)
				continue;
			const ChunkCoord c{x >> 4, z >> 4};
			const auto column = generated(gen, c, TerrainGenerator::Stages::Terrain);   // shape, not caves
			++columns;
			for (int lz = 0; lz < 16; ++lz)
				for (int lx = 0; lx < 16; ++lx)
					for (int sy = ChunkColumn::toStorageY(SEA_LEVEL); sy < ChunkColumn::HEIGHT - 1; ++sy)
						if (solid(column->getUnchecked(lx, sy + 1, lz)) && !solid(column->getUnchecked(lx, sy, lz))) {
							++overhangs;
							break;
						}
		}
	std::printf("     %d plains columns, %d of %d block columns overhang\n", columns, overhangs, columns * 256);
	CHECK(columns >= 4);
	CHECK(overhangs * 100 <= columns * 256);   // <= 1 %
}

SELFTEST(Terrain, spawn_on_land) {
	for (const std::uint64_t seed : {1ull, 2ull, 3ull, 20261116ull, 999ull}) {
		const TerrainGenerator gen(seed);
		const glm::ivec2 spawn = gen.findSpawn();
		CHECK(gen.surfaceHeight(spawn.x, spawn.y) >= SEA_LEVEL);
		CHECK(std::abs(spawn.x) <= 2048 && std::abs(spawn.y) <= 2048);
	}
}

SELFTEST(Terrain, generation_speed) {
#ifdef NDEBUG
	const TerrainGenerator gen(20261116);
	const auto start = std::chrono::steady_clock::now();
	for (int i = 0; i < 64; ++i)
		(void)generated(gen, {i % 8 - 4, i / 8 - 4});
	const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() / 64.0;
	std::printf("     %.2f ms per column\n", ms);
	CHECK(ms <= 2.0);
#endif
}

namespace {

// Biome borders crossed by rows of a scan: (x, z) is the first block of the
// row in the eastern biome, and those biomes.
struct Border {
	int x, z;
	Biome west, east;
};

std::vector<Border> bordersOf(const TerrainGenerator& gen, const int wanted) {
	std::vector<Border> found;
	for (int z = -3000; z < 3000 && static_cast<int>(found.size()) < wanted; z += 97)
		for (int x = -3000; x < 3000 && static_cast<int>(found.size()) < wanted; x += 48) {
			const Biome west = gen.biomeAt(x, z), east = gen.biomeAt(x + 48, z);
			if (west == east)
				continue;
			int at = x + 1;
			while (gen.biomeAt(at, z) == west)
				++at;
			found.push_back({at, z, west, east});
		}
	return found;
}

BlockId topBlock(const ChunkColumn& column, const int x, const int z) {
	for (int sy = ChunkColumn::HEIGHT - 1; sy >= 0; --sy) {
		const BlockId b = column.getUnchecked(x, sy, z);
		if (b != AIR_BLOCK)
			return b;
	}
	return AIR_BLOCK;
}

} // namespace

SELFTEST(Terrain, biome_borders_blend) {
	// Along a row crossing a border between two biomes, the biome switches back
	// and forth over a band of a few blocks, not once on a straight contour.
	const TerrainGenerator gen(20261116), again(20261116);
	constexpr int BAND = 16;
	int twoBiome = 0, dithered = 0, wide = 0, differ = 0;
	for (const Border& b : bordersOf(gen, 200)) {
		int switches = 0, first = -1, last = -1;
		bool onlyTwo = true;
		Biome previous = gen.biomeAt(b.x - BAND, b.z);
		for (int x = b.x - BAND; x <= b.x + BAND; ++x) {
			const Biome biome = gen.biomeAt(x, b.z);
			differ += biome == again.biomeAt(x, b.z) ? 0 : 1;
			onlyTwo = onlyTwo && (biome == b.west || biome == b.east);
			if (biome != previous) {
				++switches;
				first = first < 0 ? x : first;
				last = x;
			}
			previous = biome;
		}
		if (!onlyTwo)
			continue;
		++twoBiome;
		dithered += switches > 1 ? 1 : 0;
		wide += last - first > 16 ? 1 : 0;      // more than a few blocks: not one border
	}
	std::printf("     %d two-biome borders, %d dithered, %d wider than 16 blocks\n", twoBiome, dithered, wide);
	CHECK(twoBiome >= 50);
	CHECK(dithered * 10 >= twoBiome * 8);       // nearly every border is a mixed band
	CHECK(wide * 10 <= twoBiome);               // and the band stays narrow
	CHECK_EQ(differ, 0);
}

SELFTEST(Terrain, biome_border_surface_mixes) {
	// The ground itself mixes: sand and grass alternate where desert meets grassland.
	const TerrainGenerator gen(20261116);
	const BlockId sand = toBlockId(BlockType::Sand), grass = toBlockId(BlockType::Grass);
	const auto grassland = [](const Biome b) { return b == Biome::Plains || b == Biome::Forest; };
	int rows = 0, mixed = 0;
	for (const Border& b : bordersOf(gen, 400)) {
		if (!((b.west == Biome::Desert && grassland(b.east)) || (grassland(b.west) && b.east == Biome::Desert)))
			continue;
		if (rows >= 8)
			break;
		std::vector<BlockId> tops;
		for (int cx = (b.x - 16) >> 4; cx <= (b.x + 16) >> 4; ++cx) {
			const auto column = generated(gen, {cx, b.z >> 4}, TerrainGenerator::Stages::Terrain);
			for (int x = 0; x < 16; ++x)
				tops.push_back(topBlock(*column, x, b.z & 15));
		}
		int changes = 0;
		BlockId previous = AIR_BLOCK;
		for (const BlockId t : tops) {
			if (t != sand && t != grass)
				continue;
			changes += previous != AIR_BLOCK && t != previous ? 1 : 0;
			previous = t;
		}
		++rows;
		mixed += changes > 1 ? 1 : 0;
	}
	std::printf("     %d desert border rows, %d with sand and grass mixed\n", rows, mixed);
	CHECK(rows >= 3);
	CHECK(mixed * 4 >= rows * 3);
}

SELFTEST(Terrain, taiga_has_snow) {
	const TerrainGenerator gen(20261116);
	const BlockId snow = toBlockId(BlockType::Snow);
	int columns = 0, snowy = 0, tops = 0;
	for (int z = -3000; z < 3000 && columns < 16; z += 61)
		for (int x = -3000; x < 3000 && columns < 16; x += 61) {
			if (gen.biomeAt(x, z) != Biome::Taiga)
				continue;
			const auto column = generated(gen, {x >> 4, z >> 4}, TerrainGenerator::Stages::Terrain);
			++columns;
			for (int lz = 0; lz < 16; ++lz)
				for (int lx = 0; lx < 16; ++lx) {
					// Only taiga tops: a neighbouring snowy plains would count too.
					if (gen.biomeAt((x >> 4) * 16 + lx, (z >> 4) * 16 + lz) != Biome::Taiga)
						continue;
					++tops;
					snowy += topBlock(*column, lx, lz) == snow ? 1 : 0;
				}
		}
	std::printf("     %d taiga columns, %d of %d tops snowy\n", columns, snowy, tops);
	CHECK(columns >= 8);
	CHECK(snowy * 10 >= tops);       // at least a tenth
	CHECK(snowy < tops);             // not all of it
}
