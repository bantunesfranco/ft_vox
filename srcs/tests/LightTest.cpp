#include "SelfTest.hpp"
#include "Light.hpp"
#include "TestWorld.hpp"
#include "worldgen/TerrainGenerator.hpp"

#include <algorithm>
#include <cmath>
#include <chrono>
#include <cstdio>
#include <memory>

namespace {

constexpr BlockId STONE     = toBlockId(BlockType::Stone);
constexpr BlockId GLOWSTONE = toBlockId(BlockType::Glowstone);
constexpr BlockId LEAVES    = toBlockId(BlockType::OakLeaves);
constexpr int FLOOR = 10;            // storage y of the top floor block

// The floor every hand-built world here has (see TestWorld.hpp).
struct LitWorld : TestWorld {
	LitWorld() : TestWorld(FLOOR) {}
};

int sky(const ColumnLight& l, const int x, const int y, const int z) { return ColumnLight::sky(l.at(x, y, z)); }
int block(const ColumnLight& l, const int x, const int y, const int z) { return ColumnLight::block(l.at(x, y, z)); }

ColumnWindow generatedWindow(const worldgen::TerrainGenerator& gen, const ChunkCoord centre,
                             std::array<std::unique_ptr<ChunkColumn>, 9>& storage) {
	ColumnWindow window{};
	for (int dz = -1; dz <= 1; ++dz)
		for (int dx = -1; dx <= 1; ++dx) {
			const auto i = static_cast<std::size_t>((dz + 1) * 3 + (dx + 1));
			storage[i] = std::make_unique<ChunkColumn>(ChunkCoord{centre.x + dx, centre.z + dz});
			gen.generate(*storage[i]);
			window[i] = storage[i].get();
		}
	return window;
}

} // namespace

SELFTEST(Light, open_sky) {
	const LitWorld w;
	const auto l = w.light();
	CHECK_EQ(sky(*l, 8, FLOOR + 1, 8), 15);
	CHECK_EQ(block(*l, 8, FLOOR + 1, 8), 0);
	CHECK_EQ(sky(*l, 0, ChunkColumn::HEIGHT - 1, 15), 15);
}

SELFTEST(Light, overhang) {
	LitWorld w;
	for (int z = 7; z <= 9; ++z)
		for (int x = 7; x <= 9; ++x)
			w.set(x, FLOOR + 3, z, STONE);              // roof two cells above the floor
	const auto l = w.light();
	CHECK_EQ(sky(*l, 7, FLOOR + 1, 8), 14);             // under an edge
	CHECK_EQ(sky(*l, 8, FLOOR + 1, 8), 13);             // under the centre
}

SELFTEST(Light, sealed_cave) {
	LitWorld w;
	w.hollowBox(6, FLOOR - 6, 6, 8, FLOOR - 4, 8);
	const auto l = w.light();
	CHECK_EQ(sky(*l, 7, FLOOR - 5, 7), 0);
	CHECK_EQ(block(*l, 7, FLOOR - 5, 7), 0);
}

SELFTEST(Light, glowstone_falloff) {
	LitWorld w;
	w.hollowBox(4, FLOOR - 5, 8, 10, FLOOR - 5, 8);     // a sealed 7 x 1 x 1 tunnel
	w.set(4, FLOOR - 5, 8, GLOWSTONE);
	const auto l = w.light();
	CHECK_EQ(block(*l, 5, FLOOR - 5, 8), 14);
	CHECK_EQ(block(*l, 6, FLOOR - 5, 8), 13);
	CHECK_EQ(block(*l, 7, FLOOR - 5, 8), 12);
	CHECK_EQ(sky(*l, 7, FLOOR - 5, 8), 0);
}

SELFTEST(Light, opaque_stops) {
	LitWorld w;
	w.hollowBox(6, FLOOR - 5, 8, 6, FLOOR - 5, 8);
	w.hollowBox(8, FLOOR - 5, 8, 8, FLOOR - 5, 8);      // one stone block between the pockets
	w.set(6, FLOOR - 5, 8, GLOWSTONE);
	const auto l = w.light();
	CHECK_EQ(block(*l, 8, FLOOR - 5, 8), 0);
}

SELFTEST(Light, leaves_and_water_dim) {
	LitWorld w;
	// A 1 x 1 shaft in a stone pillar, open at the top: the only light comes
	// straight down through what fills its top.
	for (int y = FLOOR + 1; y <= FLOOR + 8; ++y)
		for (int z = 3; z <= 5; ++z)
			for (int x = 3; x <= 5; ++x)
				w.set(x, y, z, (x == 4 && z == 4) ? AIR_BLOCK : STONE);
	w.set(4, FLOOR + 8, 4, LEAVES);
	for (int y = FLOOR + 1; y <= FLOOR + 8; ++y)
		for (int z = 11; z <= 13; ++z)
			for (int x = 11; x <= 13; ++x)
				w.set(x, y, z, (x == 12 && z == 12) ? AIR_BLOCK : STONE);
	w.set(12, FLOOR + 8, 12, WATER_BLOCK);
	w.set(12, FLOOR + 7, 12, WATER_BLOCK);
	const auto l = w.light();
	CHECK_EQ(sky(*l, 4, FLOOR + 7, 4), 14);              // one leaf above
	CHECK_EQ(sky(*l, 12, FLOOR + 6, 12), 13);            // two water above
}

SELFTEST(Light, crosses_columns) {
	LitWorld w;
	w.hollowBox(-3, FLOOR - 5, 8, 3, FLOOR - 5, 8);     // tunnel from the west neighbour into the centre
	w.set(-1, FLOOR - 5, 8, GLOWSTONE);                  // west column, local x 15
	const auto l = w.light();
	CHECK_EQ(block(*l, 0, FLOOR - 5, 8), 14);
	CHECK_EQ(block(*l, 2, FLOOR - 5, 8), 12);
}

SELFTEST(Light, world_limits) {
	LitWorld w;
	w.set(8, ChunkColumn::HEIGHT - 1, 8, GLOWSTONE);
	w.set(9, 0, 9, GLOWSTONE);
	const auto l = w.light();
	CHECK_EQ(block(*l, 7, ChunkColumn::HEIGHT - 1, 8), 14);
	CHECK_EQ(sky(*l, 7, ChunkColumn::HEIGHT - 1, 8), 15);
	CHECK_EQ(block(*l, 0, ChunkColumn::HEIGHT - 1, 0), 0);
}

SELFTEST(Light, deterministic) {
	const worldgen::TerrainGenerator gen(20261116);
	std::array<std::unique_ptr<ChunkColumn>, 9> storage;
	const BlockRegistry registry;
	const ColumnWindow window = generatedWindow(gen, {3, -2}, storage);
	const auto a = computeLight(window, registry);
	const auto b = computeLight(window, registry);
	int different = 0;
	for (int y = 0; y < ChunkColumn::HEIGHT; ++y)
		for (int z = 0; z < 16; ++z)
			for (int x = 0; x < 16; ++x)
				different += a->at(x, y, z) == b->at(x, y, z) ? 0 : 1;
	CHECK_EQ(different, 0);
}

SELFTEST(Light, uniform_sections_cheap) {
	const worldgen::TerrainGenerator gen(20261116);
	const BlockRegistry registry;
	std::size_t heap = 0;
	for (int i = 0; i < 8; ++i) {
		std::array<std::unique_ptr<ChunkColumn>, 9> storage;
		heap += computeLight(generatedWindow(gen, {i * 7 - 20, i * 3}, storage), registry)->heapBytes();
	}
	const std::size_t blocksBytes = 8u * ChunkColumn::SECTION_COUNT * ChunkSection::BLOCK_COUNT * sizeof(BlockId);
	std::printf("     light uses %.1f %% of the blocks' memory\n", 100.0 * static_cast<double>(heap) / static_cast<double>(blocksBytes));
	CHECK(heap * 4 <= blocksBytes);
}

SELFTEST(Light, speed) {
#ifdef NDEBUG                                   // timing means nothing unoptimised
	const worldgen::TerrainGenerator gen(20261116);
	const BlockRegistry registry;
	double total = 0.0;
	constexpr int RUNS = 16;
	for (int i = 0; i < RUNS; ++i) {
		std::array<std::unique_ptr<ChunkColumn>, 9> storage;
		const ColumnWindow window = generatedWindow(gen, {i * 5 - 40, i * 4 - 30}, storage);
		double best = 1e9;                         // fastest of three: the code's cost, not machine load
		for (int repeat = 0; repeat < 3; ++repeat) {
			const auto start = std::chrono::steady_clock::now();
			const auto light = computeLight(window, registry);
			best = std::min(best, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
			CHECK(light != nullptr);
		}
		total += best;
	}
	std::printf("     %.2f ms per column\n", total / RUNS);
	CHECK(total / RUNS <= 0.5);
#endif
}

namespace {
constexpr std::uint16_t maskOf(const int dx, const int dz) { return static_cast<std::uint16_t>(1u << ((dz + 1) * 3 + (dx + 1))); }
}

SELFTEST(Light, change_mask_none_or_first) {
	LitWorld w;
	const auto a = w.light(), b = w.light();
	CHECK_EQ(lightChangeMask(nullptr, *a), 0x1FF);       // first light: every mesh around shows it
	CHECK_EQ(lightChangeMask(a.get(), *b), 0);           // same blocks, same light: nothing to remesh
}

SELFTEST(Light, change_mask_inside) {
	LitWorld w;
	w.hollowBox(6, FLOOR - 6, 6, 9, FLOOR - 4, 9);       // sealed, far from the borders
	const auto before = w.light();
	w.set(7, FLOOR - 5, 7, GLOWSTONE);
	const auto after = w.light();
	CHECK_EQ(lightChangeMask(before.get(), *after), maskOf(0, 0));
}

SELFTEST(Light, change_mask_border_and_corner) {
	LitWorld w;
	w.hollowBox(12, FLOOR - 6, 6, 15, FLOOR - 4, 9);     // reaches the east border (x = 15)
	const auto before = w.light();
	w.set(14, FLOOR - 5, 7, GLOWSTONE);
	const auto east = w.light();
	CHECK_EQ(lightChangeMask(before.get(), *east), maskOf(0, 0) | maskOf(1, 0));

	LitWorld c;
	c.hollowBox(0, FLOOR - 6, 0, 3, FLOOR - 4, 3);       // the north-west corner (x = 0, z = 0)
	const auto dark = c.light();
	c.set(1, FLOOR - 5, 1, GLOWSTONE);
	const auto lit = c.light();
	CHECK_EQ(lightChangeMask(dark.get(), *lit),
	         maskOf(0, 0) | maskOf(-1, 0) | maskOf(0, -1) | maskOf(-1, -1));
}

SELFTEST(Light, brightness_curve) {
	const auto near = [](const float a, const float b) { return std::abs(a - b) < 1e-5f; };
	CHECK(near(lightBrightness(15, 0, 1.0f), 1.0f));            // noon, open sky
	CHECK(near(lightBrightness(0, 0, 1.0f), 0.05f));            // pitch black
	CHECK(near(lightBrightness(0, 15, 0.2f), 1.0f));            // a lamp at midnight
	// Midnight sky: level 3 -> 0.2 / (4 - 0.6) through the curve.
	CHECK(near(lightBrightness(15, 0, 0.2f), 0.05f + 0.95f * (0.2f / 3.4f)));
	CHECK(near(lightBrightness(15, 7, 0.2f), lightBrightness(0, 7, 0.2f)));   // the brighter of the two
}
