#include "SelfTest.hpp"
#include "BlockSystem.hpp"
#include "GameSession.hpp"
#include "PlayerCollision.hpp"
#include "PlayerPhysics.hpp"
#include "PlayerStore.hpp"
#include "ThreadPool.hpp"
#include "World.hpp"

#include <array>
#include <chrono>
#include <memory>
#include <thread>

namespace fs = std::filesystem;

namespace {

const glm::vec3 START{8.5f, 0.0f, 8.5f};

// The pool is declared after the world: it is destroyed (and joined) first,
// so no job outlives the World it points to.
struct Game {
	World       world;
	ThreadPool  pool{2};
	BlockSystem blocks;

	Game() { world.open(1, nullptr); }

	[[nodiscard]] bool solid(const glm::ivec3& p) const {
		BlockId block = AIR_BLOCK;
		return world.getBlockAt(p, block) && world.getBlockRegistry().get(block).solid;
	}
};

// Updates the session until it hands out its spawn; nullopt after 20 s.
std::optional<SpawnPoint> waitSpawn(GameSession& session) {
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
	while (std::chrono::steady_clock::now() < deadline) {
		session.update(START, 0.016f);
		if (auto spawn = session.pollSpawn())
			return spawn;
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
	}
	return std::nullopt;
}

bool sameInventory(const Inventory& a, const Inventory& b) {
	if (a.selectedSlot() != b.selectedSlot())
		return false;
	for (std::size_t i = 0; i < Inventory::SLOT_COUNT; ++i)
		if (a.slots()[i].item != b.slots()[i].item || a.slots()[i].count != b.slots()[i].count)
			return false;
	return true;
}

} // namespace

SELFTEST(Session, local_place_consumes_and_break_adds) {
	Game game;
	LocalSession session(game.world, game.pool, game.blocks, START, std::nullopt);
	CHECK(waitSpawn(session).has_value());

	int surface = 0;
	CHECK(game.world.getSurfaceHeight(8, 8, surface));
	const glm::ivec3 top{8, surface, 8};
	BlockId broken = AIR_BLOCK;
	CHECK(game.world.getBlockAt(top, broken));
	const ItemId drop = ItemRegistry::fromBlock(broken);
	CHECK(drop != ItemId::Empty);

	session.requestBreakBlock(top);
	BlockId now = AIR_BLOCK;
	CHECK(game.world.getBlockAt(top, now));
	CHECK_EQ(now, AIR_BLOCK);
	std::uint32_t held = 0;
	for (const ItemStack& stack : session.inventory().slots())
		if (stack.item == drop)
			held += stack.count;
	CHECK_EQ(held, 1u);

	// From above the hole, looking down: the new block fills it.
	const ItemStack before = session.inventory().selectedStack();
	session.requestPlaceBlock({8.5f, static_cast<float>(surface) + 3.0f, 8.5f}, {0.0f, -1.0f, 0.0f});
	CHECK(game.world.getBlockAt(top, now));
	CHECK_EQ(now, ItemRegistry::get(before.item).placedBlock);
	CHECK_EQ(session.inventory().selectedStack().count, static_cast<std::uint16_t>(before.count - 1));

	const std::vector<SessionEvent> events = session.drainEvents();
	CHECK_EQ(events.size(), std::size_t{2});
	if (events.size() == 2) {
		CHECK(events[0].type == SessionEvent::Type::BlockBroken);
		CHECK(events[1].type == SessionEvent::Type::BlockPlaced);
	}
}

SELFTEST(Session, local_break_full_inventory_refused) {
	const selftest::TempDir dir;
	const fs::path file = singleplayerFile(dir.path());
	std::array<ItemStack, Inventory::SLOT_COUNT> full{};
	full.fill({ItemId::Glowstone, ItemRegistry::get(ItemId::Glowstone).maxStack});
	PlayerRecord record;
	record.eye = {8.5f, 200.0f, 8.5f};
	record.inventory = Inventory::fromState(full, {}, 0);
	std::string error;
	CHECK(savePlayer(file, record, error));

	Game game;
	LocalSession session(game.world, game.pool, game.blocks, START, file);
	CHECK(waitSpawn(session).has_value());

	glm::ivec3 stone{8, ChunkColumn::MAX_WORLD_Y - 1, 8};
	BlockId block = AIR_BLOCK;
	while (stone.y > ChunkColumn::MIN_WORLD_Y && !(game.world.getBlockAt(stone, block) && block == toBlockId(BlockType::Stone)))
		--stone.y;
	CHECK_EQ(block, toBlockId(BlockType::Stone));

	session.requestBreakBlock(stone);
	CHECK(game.world.getBlockAt(stone, block));
	CHECK_EQ(block, toBlockId(BlockType::Stone));
	CHECK(session.drainEvents().empty());
}

SELFTEST(Session, local_player_saved_and_restored) {
	const selftest::TempDir dir;
	const fs::path file = singleplayerFile(dir.path());
	Game game;

	glm::vec3 eye{};
	Inventory inventory;
	{
		LocalSession session(game.world, game.pool, game.blocks, START, file);
		const auto spawn = waitSpawn(session);
		CHECK(spawn.has_value());
		if (!spawn)
			return;
		eye = spawn->eye;
		CHECK(eye.x == 8.5f && eye.z == 8.5f);
		session.inventoryAction({InventoryAction::Kind::Select, 2, false});
		session.inventoryAction({InventoryAction::Kind::Click, 1, false});    // the torches go on the cursor...
		session.inventoryAction({InventoryAction::Kind::Click, 20, false});   // ...and into slot 20
		inventory = session.inventory();
		session.onLocalPlayerMoved(eye, 45.0f, -10.0f);
	}
	CHECK(fs::exists(file));
	CHECK(inventory.slots()[20].item == ItemId::Torch);

	LocalSession session(game.world, game.pool, game.blocks, START, file);
	const auto spawn = waitSpawn(session);
	CHECK(spawn.has_value());
	if (!spawn)
		return;
	CHECK(spawn->eye == eye);
	CHECK_EQ(spawn->yaw, 45.0f);
	CHECK_EQ(spawn->pitch, -10.0f);
	CHECK(sameInventory(session.inventory(), inventory));
}

SELFTEST(Session, local_spawn_lifted_when_built_over) {
	const selftest::TempDir dir;
	const fs::path file = singleplayerFile(dir.path());
	Game game;

	int surface = 0;
	{
		LocalSession session(game.world, game.pool, game.blocks, START, std::nullopt);
		CHECK(waitSpawn(session).has_value());
		CHECK(game.world.getSurfaceHeight(8, 8, surface));
	}
	PlayerRecord record;
	record.eye = {8.5f, static_cast<float>(surface) - 10.0f + PlayerCollision::EYE_HEIGHT, 8.5f};
	const auto solid = [&game](const glm::ivec3& p) { return game.solid(p); };
	CHECK(PlayerPhysics::boxHitsBlocks(record.eye, solid));       // really inside the ground
	std::string error;
	CHECK(savePlayer(file, record, error));

	LocalSession session(game.world, game.pool, game.blocks, START, file);
	const auto spawn = waitSpawn(session);
	CHECK(spawn.has_value());
	if (!spawn)
		return;
	CHECK(spawn->eye.x == record.eye.x && spawn->eye.z == record.eye.z);
	CHECK(spawn->eye.y > record.eye.y);
	CHECK(!PlayerPhysics::boxHitsBlocks(spawn->eye, solid));
}

SELFTEST(Session, local_spawn_waits_for_every_column_under_the_box) {
	const selftest::TempDir dir;
	const fs::path file = singleplayerFile(dir.path());
	PlayerRecord record;
	record.eye = {15.9f, 100.0f, 8.5f};                    // the box reaches into column (1, 0)
	std::string error;
	CHECK(savePlayer(file, record, error));

	Game game;
	LocalSession session(game.world, game.pool, game.blocks, START, file);
	game.world.adoptColumn(std::make_unique<ChunkColumn>(ChunkCoord{0, 0}));
	CHECK(!session.pollSpawn().has_value());               // the eye's column alone is not enough
	game.world.adoptColumn(std::make_unique<ChunkColumn>(ChunkCoord{1, 0}));
	const auto spawn = session.pollSpawn();
	CHECK(spawn.has_value());
	if (spawn)
		CHECK(spawn->eye == record.eye);                   // empty columns: nothing to rise out of
}
