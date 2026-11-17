#include "SelfTest.hpp"
#include "Light.hpp"
#include "worldgen/TerrainGenerator.hpp"

#include <chrono>
#include <cstdio>
#include <memory>

namespace {

constexpr BlockId STONE     = toBlockId(BlockType::Stone);
constexpr BlockId GLOWSTONE = toBlockId(BlockType::Glowstone);
constexpr BlockId LEAVES    = toBlockId(BlockType::OakLeaves);
constexpr int FLOOR = 10;            // storage y of the top floor block

// Nine all-air columns around (0, 0) with a stone floor up to FLOOR. Blocks
// are set in centre-relative coordinates: x, z in -16..31.
struct TestWorld {
	std::array<std::unique_ptr<ChunkColumn>, 9> columns;
	BlockRegistry registry;

	TestWorld() {
		for (int dz = -1; dz <= 1; ++dz)
			for (int dx = -1; dx <= 1; ++dx)
				columns[static_cast<std::size_t>((dz + 1) * 3 + (dx + 1))] = std::make_unique<ChunkColumn>(ChunkCoord{dx, dz});
		for (int z = -16; z < 32; ++z)
			for (int x = -16; x < 32; ++x)
				for (int y = 0; y <= FLOOR; ++y)
					set(x, y, z, STONE);
	}
	void set(const int x, const int y, const int z, const BlockId block) {
		const int cx = (x + 16) / 16, cz = (z + 16) / 16;
		columns[static_cast<std::size_t>(cz * 3 + cx)]->setUnchecked((x + 16) % 16, y, (z + 16) % 16, block);
	}
	// A solid box of stone with a hollow inside [x0, x1] x [y0, y1] x [z0, z1].
	void hollowBox(const int x0, const int y0, const int z0, const int x1, const int y1, const int z1) {
		for (int z = z0 - 1; z <= z1 + 1; ++z)
			for (int y = y0 - 1; y <= y1 + 1; ++y)
				for (int x = x0 - 1; x <= x1 + 1; ++x) {
					const bool inside = x >= x0 && x <= x1 && y >= y0 && y <= y1 && z >= z0 && z <= z1;
					set(x, y, z, inside ? AIR_BLOCK : STONE);
				}
	}
	[[nodiscard]] std::shared_ptr<const ColumnLight> light() const {
		ColumnWindow window{};
		for (std::size_t i = 0; i < 9; ++i)
			window[i] = columns[i].get();
		return computeLight(window, registry);
	}
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
	const TestWorld w;
	const auto l = w.light();
	CHECK_EQ(sky(*l, 8, FLOOR + 1, 8), 15);
	CHECK_EQ(block(*l, 8, FLOOR + 1, 8), 0);
	CHECK_EQ(sky(*l, 0, ChunkColumn::HEIGHT - 1, 15), 15);
}

SELFTEST(Light, overhang) {
	TestWorld w;
	for (int z = 7; z <= 9; ++z)
		for (int x = 7; x <= 9; ++x)
			w.set(x, FLOOR + 3, z, STONE);              // roof two cells above the floor
	const auto l = w.light();
	CHECK_EQ(sky(*l, 7, FLOOR + 1, 8), 14);             // under an edge
	CHECK_EQ(sky(*l, 8, FLOOR + 1, 8), 13);             // under the centre
}

SELFTEST(Light, sealed_cave) {
	TestWorld w;
	w.hollowBox(6, FLOOR - 6, 6, 8, FLOOR - 4, 8);
	const auto l = w.light();
	CHECK_EQ(sky(*l, 7, FLOOR - 5, 7), 0);
	CHECK_EQ(block(*l, 7, FLOOR - 5, 7), 0);
}

SELFTEST(Light, glowstone_falloff) {
	TestWorld w;
	w.hollowBox(4, FLOOR - 5, 8, 10, FLOOR - 5, 8);     // a sealed 7 x 1 x 1 tunnel
	w.set(4, FLOOR - 5, 8, GLOWSTONE);
	const auto l = w.light();
	CHECK_EQ(block(*l, 5, FLOOR - 5, 8), 14);
	CHECK_EQ(block(*l, 6, FLOOR - 5, 8), 13);
	CHECK_EQ(block(*l, 7, FLOOR - 5, 8), 12);
	CHECK_EQ(sky(*l, 7, FLOOR - 5, 8), 0);
}

SELFTEST(Light, opaque_stops) {
	TestWorld w;
	w.hollowBox(6, FLOOR - 5, 8, 6, FLOOR - 5, 8);
	w.hollowBox(8, FLOOR - 5, 8, 8, FLOOR - 5, 8);      // one stone block between the pockets
	w.set(6, FLOOR - 5, 8, GLOWSTONE);
	const auto l = w.light();
	CHECK_EQ(block(*l, 8, FLOOR - 5, 8), 0);
}

SELFTEST(Light, leaves_and_water_dim) {
	TestWorld w;
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
	TestWorld w;
	w.hollowBox(-3, FLOOR - 5, 8, 3, FLOOR - 5, 8);     // tunnel from the west neighbour into the centre
	w.set(-1, FLOOR - 5, 8, GLOWSTONE);                  // west column, local x 15
	const auto l = w.light();
	CHECK_EQ(block(*l, 0, FLOOR - 5, 8), 14);
	CHECK_EQ(block(*l, 2, FLOOR - 5, 8), 12);
}

SELFTEST(Light, world_limits) {
	TestWorld w;
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
	const worldgen::TerrainGenerator gen(20261116);
	const BlockRegistry registry;
	double total = 0.0;
	constexpr int RUNS = 16;
	for (int i = 0; i < RUNS; ++i) {
		std::array<std::unique_ptr<ChunkColumn>, 9> storage;
		const ColumnWindow window = generatedWindow(gen, {i * 5 - 40, i * 4 - 30}, storage);
		const auto start = std::chrono::steady_clock::now();
		const auto light = computeLight(window, registry);
		total += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
		CHECK(light != nullptr);
	}
	std::printf("     %.2f ms per column\n", total / RUNS);
	CHECK(total / RUNS <= 0.5);
}
