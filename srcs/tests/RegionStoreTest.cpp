#include "SelfTest.hpp"
#include "RegionStore.hpp"

#include <fstream>
#include <random>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

namespace {

// Deterministic content that exercises long runs and single blocks.
std::unique_ptr<ChunkColumn> makeColumn(const ChunkCoord coord, const std::uint32_t seed) {
	auto column = std::make_unique<ChunkColumn>(coord);
	std::mt19937 rng(seed);
	for (int y = 0; y < ChunkColumn::HEIGHT; ++y)
		for (int z = 0; z < 16; ++z)
			for (int x = 0; x < 16; ++x) {
				const BlockId block = y < 60 ? toBlockId(BlockType::Stone)
					: (rng() % 4 == 0 ? static_cast<BlockId>(rng() % BLOCK_TYPE_COUNT) : AIR_BLOCK);
				column->setUnchecked(x, y, z, block);
			}
	return column;
}

bool sameBlocks(const ChunkColumn& a, const ChunkColumn& b) {
	for (int s = 0; s < ChunkColumn::SECTION_COUNT; ++s)
		if (a.section(s).blocks() != b.section(s).blocks())
			return false;
	return a.coord() == b.coord();
}

fs::path onlyRegionFile(const fs::path& dir) {
	for (const auto& entry : fs::directory_iterator(dir))
		return entry.path();
	return {};
}

} // namespace

SELFTEST(RegionStore, roundtrip) {
	const selftest::TempDir dir;
	RegionStore store(dir.path());
	const auto column = makeColumn({3, -7}, 1);
	CHECK(store.save(*column));
	const auto loaded = store.load({3, -7});
	CHECK(loaded != nullptr);
	if (loaded)
		CHECK(sameBlocks(*column, *loaded));
}

SELFTEST(RegionStore, absent_is_null) {
	const selftest::TempDir dir;
	RegionStore store(dir.path());
	CHECK(store.load({0, 0}) == nullptr);           // no file at all
	CHECK(store.save(*makeColumn({0, 0}, 2)));
	CHECK(store.load({1, 0}) == nullptr);           // file, but no entry
}

SELFTEST(RegionStore, resave_keeps_latest) {
	const selftest::TempDir dir;
	RegionStore store(dir.path());
	auto column = makeColumn({5, 5}, 3);
	CHECK(store.save(*column));
	column->setUnchecked(1, 200, 1, toBlockId(BlockType::Snow));
	CHECK(store.save(*column));
	const auto loaded = store.load({5, 5});
	CHECK(loaded && loaded->getUnchecked(1, 200, 1) == toBlockId(BlockType::Snow));
}

SELFTEST(RegionStore, many_columns_one_region) {
	const selftest::TempDir dir;
	RegionStore store(dir.path());
	// Region (-1, 2): cx in [-32, -1], cz in [64, 95].
	for (int cz = 64; cz < 96; ++cz)
		for (int cx = -32; cx < 0; ++cx) {
			ChunkColumn column({cx, cz});
			column.setUnchecked(0, 0, 0, static_cast<BlockId>(1 + (cx + cz + 1000) % (BLOCK_TYPE_COUNT - 1)));
			CHECK(store.save(column));
		}
	int bad = 0;
	for (int cz = 64; cz < 96; ++cz)
		for (int cx = -32; cx < 0; ++cx) {
			const auto loaded = store.load({cx, cz});
			const BlockId expected = static_cast<BlockId>(1 + (cx + cz + 1000) % (BLOCK_TYPE_COUNT - 1));
			if (!loaded || loaded->getUnchecked(0, 0, 0) != expected)
				++bad;
		}
	CHECK_EQ(bad, 0);
	int files = 0;
	for ([[maybe_unused]] const auto& entry : fs::directory_iterator(dir.path()))
		++files;
	CHECK_EQ(files, 1);
}

SELFTEST(RegionStore, truncated_tail) {
	const selftest::TempDir dir;
	{
		RegionStore store(dir.path());
		for (int i = 0; i < 3; ++i)
			CHECK(store.save(*makeColumn({i, 0}, 10 + static_cast<std::uint32_t>(i))));
	}
	const fs::path file = onlyRegionFile(dir.path());
	fs::resize_file(file, fs::file_size(file) - 10);   // a crash in the middle of the last write

	RegionStore store(dir.path());
	CHECK(store.load({0, 0}) != nullptr);
	CHECK(store.load({1, 0}) != nullptr);
	CHECK(store.load({2, 0}) == nullptr);
}

SELFTEST(RegionStore, garbage_file) {
	const selftest::TempDir dir;
	{
		std::ofstream file(dir.path() / "r.0.0.bin", std::ios::binary);
		std::mt19937 rng(5);
		for (int i = 0; i < 5000; ++i)
			file.put(static_cast<char>(rng()));
	}
	RegionStore store(dir.path());
	CHECK(store.load({0, 0}) == nullptr);
	CHECK(store.load({7, 9}) == nullptr);
	// Saving over a damaged file starts it again rather than failing forever.
	const auto column = makeColumn({4, 4}, 6);
	CHECK(store.save(*column));
	const auto loaded = store.load({4, 4});
	CHECK(loaded && sameBlocks(*column, *loaded));
}

SELFTEST(RegionStore, concurrent_saves) {
	const selftest::TempDir dir;
	RegionStore store(dir.path());
	std::vector<std::thread> threads;
	for (int t = 0; t < 4; ++t)
		threads.emplace_back([&store, t] {
			for (int i = 0; i < 64; ++i) {
				ChunkColumn column({t * 8 + i % 8, i / 8});   // all in region (0, 0)
				column.setUnchecked(2, 3, 4, static_cast<BlockId>(1 + t));
				store.save(column);
			}
		});
	for (std::thread& thread : threads)
		thread.join();

	int bad = 0;
	for (int t = 0; t < 4; ++t)
		for (int i = 0; i < 64; ++i) {
			const auto loaded = store.load({t * 8 + i % 8, i / 8});
			if (!loaded || loaded->getUnchecked(2, 3, 4) != static_cast<BlockId>(1 + t))
				++bad;
		}
	CHECK_EQ(bad, 0);
}
