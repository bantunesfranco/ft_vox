#include "SelfTest.hpp"
#include "PlayerStore.hpp"

#include <bit>
#include <cmath>
#include <fstream>
#include <limits>

namespace fs = std::filesystem;

namespace {

// What savePlayer writes for a default inventory, as raw bytes to damage.
std::string goodBytes(const fs::path& dir) {
	PlayerRecord record;
	record.eye = {1.0f, 2.0f, 3.0f};
	std::string error;
	const fs::path file = dir / "good.bin";
	if (!savePlayer(file, record, error))
		return {};
	std::ifstream in(file, std::ios::binary);
	return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

std::optional<PlayerRecord> loadBytes(const fs::path& dir, const std::string& bytes) {
	const fs::path file = dir / "damaged.bin";
	{
		std::ofstream out(file, std::ios::binary | std::ios::trunc);
		out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
	}
	return loadPlayer(file);
}

} // namespace

SELFTEST(PlayerStore, roundtrip) {
	const selftest::TempDir dir;
	PlayerRecord record;
	record.eye = {1.5f, 70.62f, -3.5f};
	record.yaw = 90.0f;
	record.pitch = -20.0f;
	record.inventory = Inventory::fromState(Inventory().slots(), {ItemId::Dirt, 5}, 3);

	// The cursor's dirt goes back into the slots on save.
	Inventory expected = record.inventory;
	expected.returnCursorStack();

	const fs::path file = singleplayerFile(dir.path() / "world");
	std::string error;
	CHECK(savePlayer(file, record, error));
	CHECK(error.empty());
	CHECK(!fs::exists(file.string() + ".tmp"));

	const std::optional<PlayerRecord> loaded = loadPlayer(file);
	CHECK(loaded.has_value());
	if (!loaded)
		return;
	CHECK(loaded->eye == record.eye);
	CHECK(loaded->yaw == 90.0f);
	CHECK(loaded->pitch == -20.0f);
	CHECK(loaded->inventory.cursorStack().empty());
	CHECK_EQ(loaded->inventory.selectedSlot(), 3);
	for (std::size_t i = 0; i < Inventory::SLOT_COUNT; ++i) {
		CHECK(loaded->inventory.slots()[i].item == expected.slots()[i].item);
		CHECK_EQ(loaded->inventory.slots()[i].count, expected.slots()[i].count);
	}
	int dirt = 0;
	for (const ItemStack& stack : loaded->inventory.slots())
		dirt += stack.item == ItemId::Dirt ? stack.count : 0;
	CHECK(dirt >= 5);
}

SELFTEST(PlayerStore, file_names) {
	const fs::path world = "w";
	CHECK(playerFile(world, "Bob") == world / "players" / "426f62.bin");
	CHECK(playerFile(world, "a b") == world / "players" / "612062.bin");
	CHECK(playerFile(world, "\xff") == world / "players" / "ff.bin");   // bytes hex-encode unsigned
	CHECK(singleplayerFile(world) == world / "player.bin");
}

SELFTEST(PlayerStore, damaged_files_are_new_players) {
	const selftest::TempDir dir;
	const std::string good = goodBytes(dir.path());
	CHECK_EQ(good.size(), std::size_t{4 + 4 + 5 * 4 + Inventory::ENCODED_SIZE});
	CHECK(loadBytes(dir.path(), good).has_value());

	CHECK(!loadPlayer(dir.path() / "missing.bin").has_value());
	CHECK(!loadBytes(dir.path(), "").has_value());

	std::string damaged = good;
	damaged[0] = 'X';
	CHECK(!loadBytes(dir.path(), damaged).has_value());

	damaged = good;
	damaged[4] = 2;                                       // version 2
	CHECK(!loadBytes(dir.path(), damaged).has_value());

	CHECK(!loadBytes(dir.path(), good.substr(0, good.size() - 1)).has_value());
	CHECK(!loadBytes(dir.path(), good + '\0').has_value());

	damaged = good;                                       // x = NaN (quiet NaN bit pattern, little-endian)
	const auto nan = std::bit_cast<std::uint32_t>(std::numeric_limits<float>::quiet_NaN());
	for (int i = 0; i < 4; ++i)
		damaged[8 + static_cast<std::size_t>(i)] = static_cast<char>((nan >> (8 * i)) & 0xFF);
	CHECK(!loadBytes(dir.path(), damaged).has_value());

	damaged = good;                                       // yaw = +Inf (the 4th float, at offset 20)
	const auto inf = std::bit_cast<std::uint32_t>(std::numeric_limits<float>::infinity());
	for (int i = 0; i < 4; ++i)
		damaged[20 + static_cast<std::size_t>(i)] = static_cast<char>((inf >> (8 * i)) & 0xFF);
	CHECK(!loadBytes(dir.path(), damaged).has_value());

	damaged = good;                                       // inventory byte 0 is "selected"
	damaged[4 + 4 + 5 * 4] = 9;
	CHECK(!loadBytes(dir.path(), damaged).has_value());
}
