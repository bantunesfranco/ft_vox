#include "SelfTest.hpp"
#include "Light.hpp"
#include "ThreadPool.hpp"
#include "World.hpp"
#include "worldgen/TerrainGenerator.hpp"

#include <algorithm>
#include <chrono>
#include <functional>
#include <memory>
#include <thread>
#include <vector>

namespace {

constexpr std::uint64_t SEED = 20261116;

std::unique_ptr<ChunkColumn> generated(const worldgen::TerrainGenerator& gen, const ChunkCoord c) {
	auto column = std::make_unique<ChunkColumn>(c);
	gen.generate(*column);
	return column;
}

// The 5 x 5 columns around (0, 0), in the given order.
std::vector<ChunkCoord> patch(const bool reversed) {
	std::vector<ChunkCoord> coords;
	for (int z = -2; z <= 2; ++z)
		for (int x = -2; x <= 2; ++x)
			coords.push_back({x, z});
	if (reversed)
		std::reverse(coords.begin(), coords.end());
	return coords;
}

// Runs frames until `done` holds (light and mesh jobs finish in the background).
bool pump(World& world, ThreadPool& pool, const std::function<bool()>& done) {
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
	while (std::chrono::steady_clock::now() < deadline) {
		world.buildRenderStates({8.0f, 100.0f, 8.0f}, {1.0f, 0.0f, 0.0f}, pool);
		if (done())
			return true;
		std::this_thread::sleep_for(std::chrono::milliseconds(2));
	}
	return false;
}

int blockLightAt(const World& world, const glm::ivec3 p) {
	const ChunkCoord c{p.x >> 4, p.z >> 4};
	const auto light = world.lightOf(c);
	return light ? ColumnLight::block(light->at(p.x & 15, ChunkColumn::toStorageY(p.y), p.z & 15)) : -1;
}

} // namespace

SELFTEST(LightWorld, columns_get_light) {
	const worldgen::TerrainGenerator gen(SEED);
	World world;
	world.open(SEED, nullptr);
	ThreadPool pool(2);
	for (const ChunkCoord c : patch(false))
		world.adoptColumn(generated(gen, c));
	CHECK(pump(world, pool, [&] { return world.lightIdle(); }));
	for (int z = -1; z <= 1; ++z)
		for (int x = -1; x <= 1; ++x)
			CHECK(world.lightOf({x, z}) != nullptr);              // a full 3 x 3 around it
	CHECK(world.lightOf({-2, -2}) == nullptr);                   // on the edge: no full window
}

SELFTEST(LightWorld, edit_relights_neighbours) {
	const worldgen::TerrainGenerator gen(SEED);
	World world;
	world.open(SEED, nullptr);
	ThreadPool pool(2);
	for (const ChunkCoord c : patch(false))
		world.adoptColumn(generated(gen, c));
	CHECK(pump(world, pool, [&] { return world.lightIdle(); }));

	// Glowstone in open air on the corner shared by columns (0,0), (1,0), (0,1), (1,1).
	int y = 0;
	for (int dz = 0; dz <= 1; ++dz)
		for (int dx = 0; dx <= 1; ++dx)
			y = std::max(y, gen.surfaceHeight(15 + dx, 15 + dz));
	y += 12;                                              // above any tree
	const glm::ivec3 lamp{15, y, 15};
	CHECK(world.setBlockAt(lamp, toBlockId(BlockType::Glowstone)));
	CHECK(pump(world, pool, [&] { return world.lightIdle() && blockLightAt(world, {16, y, 15}) == 14; }));
	CHECK_EQ(blockLightAt(world, {16, y, 15}), 14);              // column (1, 0)
	CHECK_EQ(blockLightAt(world, {15, y, 16}), 14);              // column (0, 1)
	CHECK_EQ(blockLightAt(world, {16, y, 16}), 13);              // column (1, 1)
	CHECK_EQ(blockLightAt(world, {14, y, 15}), 14);              // column (0, 0)

	CHECK(world.setBlockAt(lamp, AIR_BLOCK));
	CHECK(pump(world, pool, [&] { return world.lightIdle() && blockLightAt(world, {16, y, 16}) == 0; }));
	CHECK_EQ(blockLightAt(world, {16, y, 15}), 0);
	CHECK_EQ(blockLightAt(world, {15, y, 16}), 0);
	CHECK_EQ(blockLightAt(world, {16, y, 16}), 0);
	CHECK_EQ(blockLightAt(world, {14, y, 15}), 0);
}

SELFTEST(LightWorld, stale_light_dropped) {
	// A column replaced while its light may be computing: the light that
	// stays matches the new blocks.
	const worldgen::TerrainGenerator gen(SEED);
	World world;
	world.open(SEED, nullptr);
	ThreadPool pool(2);
	for (const ChunkCoord c : patch(false))
		world.adoptColumn(generated(gen, c));
	world.buildRenderStates({8.0f, 100.0f, 8.0f}, {1.0f, 0.0f, 0.0f}, pool);   // light jobs start

	auto replacement = generated(gen, {0, 0});
	const int y = gen.surfaceHeight(8, 8) + 12;
	replacement->setUnchecked(8, ChunkColumn::toStorageY(y), 8, toBlockId(BlockType::Glowstone));
	world.adoptColumn(std::move(replacement));

	CHECK(pump(world, pool, [&] { return world.lightIdle(); }));
	CHECK_EQ(blockLightAt(world, {9, y, 8}), 14);
}

SELFTEST(LightWorld, same_light_any_order) {
	const worldgen::TerrainGenerator gen(SEED);
	ThreadPool pool(2);
	World a, b;
	a.open(SEED, nullptr);
	b.open(SEED, nullptr);
	for (const ChunkCoord c : patch(false))
		a.adoptColumn(generated(gen, c));
	for (const ChunkCoord c : patch(true))
		b.adoptColumn(generated(gen, c));
	CHECK(pump(a, pool, [&] { return a.lightIdle(); }));
	CHECK(pump(b, pool, [&] { return b.lightIdle(); }));
	int different = 0;
	for (int cz = -1; cz <= 1; ++cz)
		for (int cx = -1; cx <= 1; ++cx) {
			const auto la = a.lightOf({cx, cz}), lb = b.lightOf({cx, cz});
			if (!la || !lb) {
				++different;
				continue;
			}
			for (int y = 0; y < ChunkColumn::HEIGHT; ++y)
				for (int z = 0; z < 16; ++z)
					for (int x = 0; x < 16; ++x)
						different += la->at(x, y, z) == lb->at(x, y, z) ? 0 : 1;
		}
	CHECK_EQ(different, 0);
}
