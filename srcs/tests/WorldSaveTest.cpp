#include "SelfTest.hpp"
#include "RegionStore.hpp"
#include "ThreadPool.hpp"
#include "World.hpp"

#include <chrono>
#include <thread>

namespace fs = std::filesystem;

namespace {

const glm::ivec3 EDIT_POS{3, 100, 3};

// Streams chunks around `center` until the column holding `probe` is loaded.
bool waitLoaded(World& world, ThreadPool& pool, const glm::vec3 center, const glm::ivec3 probe) {
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
	BlockId block = AIR_BLOCK;
	while (std::chrono::steady_clock::now() < deadline) {
		world.updateChunks(center, pool);
		if (world.getBlockAt(probe, block))
			return true;
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
	}
	return false;
}

bool waitUnloaded(World& world, ThreadPool& pool, const glm::ivec3 probe) {
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
	BlockId block = AIR_BLOCK;
	while (std::chrono::steady_clock::now() < deadline) {
		world.updateChunks({100000.0f, 100.0f, 100000.0f}, pool);
		if (!world.getBlockAt(probe, block))
			return true;
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
	}
	return false;
}

// A block different from whatever is at EDIT_POS now.
BlockId changeEditBlock(World& world) {
	BlockId current = AIR_BLOCK;
	(void)world.getBlockAt(EDIT_POS, current);
	const BlockId edit = current == toBlockId(BlockType::Snow) ? toBlockId(BlockType::Sand) : toBlockId(BlockType::Snow);
	CHECK(world.setBlockAt(EDIT_POS, edit));
	return edit;
}

std::size_t filesIn(const fs::path& dir) {
	std::size_t n = 0;
	std::error_code ec;
	for (auto it = fs::recursive_directory_iterator(dir, ec); it != fs::recursive_directory_iterator(); ++it)
		++n;
	return n;
}

} // namespace

// The pool is declared after the world in every test: it is destroyed (and
// joined) first, so no job outlives the World it points to.

SELFTEST(WorldSave, edits_survive_reopen) {
	const selftest::TempDir dir;
	const auto store = std::make_shared<RegionStore>(dir.path());
	BlockId edit = AIR_BLOCK;
	{
		World world;
		ThreadPool pool(2);
		world.open(1, store);
		CHECK(waitLoaded(world, pool, {8, 100, 8}, EDIT_POS));
		edit = changeEditBlock(world);
		world.flush(pool);
	}
	World world;
	ThreadPool pool(2);
	world.open(1, store);
	CHECK_EQ(world.seed(), 1ull);
	CHECK(waitLoaded(world, pool, {8, 100, 8}, EDIT_POS));
	BlockId block = AIR_BLOCK;
	CHECK(world.getBlockAt(EDIT_POS, block));
	CHECK_EQ(block, edit);
}

SELFTEST(WorldSave, generated_columns_saved) {
	const selftest::TempDir dir;
	const auto store = std::make_shared<RegionStore>(dir.path());
	World world;
	ThreadPool pool(2);
	world.open(1, store);
	CHECK(waitLoaded(world, pool, {8, 100, 8}, EDIT_POS));
	world.flush(pool);
	CHECK(store->load({0, 0}) != nullptr);
}

SELFTEST(WorldSave, null_store_writes_nothing) {
	const selftest::TempDir dir;
	World world;
	ThreadPool pool(2);
	world.open(1, nullptr);
	CHECK(waitLoaded(world, pool, {8, 100, 8}, EDIT_POS));
	(void)changeEditBlock(world);
	world.flush(pool);
	CHECK_EQ(filesIn(dir.path()), std::size_t{0});
}

SELFTEST(WorldSave, unload_saves_dirty) {
	const selftest::TempDir dir;
	const auto store = std::make_shared<RegionStore>(dir.path());
	World world;
	ThreadPool pool(2);
	world.open(1, store);
	CHECK(waitLoaded(world, pool, {8, 100, 8}, EDIT_POS));
	const BlockId edit = changeEditBlock(world);
	CHECK(waitUnloaded(world, pool, EDIT_POS));
	world.flush(pool);   // waits for the save the unload started; (0,0) itself is no longer loaded
	const auto saved = store->load({0, 0});
	CHECK(saved != nullptr);
	if (saved)
		CHECK_EQ(saved->getUnchecked(EDIT_POS.x, ChunkColumn::toStorageY(EDIT_POS.y), EDIT_POS.z), edit);
}
