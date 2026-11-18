#include "SelfTest.hpp"
#include "Block.hpp"
#include "Inventory.hpp"

SELFTEST(Inventory, blocks_without_drop_can_always_be_broken) {
	Inventory inventory;
	// Fill every slot with dirt, so a different drop no longer fits.
	while (inventory.add(ItemId::Dirt, 1)) {}

	CHECK(!inventory.canTakeDropOf(toBlockId(BlockType::Stone)));      // full: stone would be lost
	CHECK(inventory.canTakeDropOf(toBlockId(BlockType::Dirt)) == inventory.canAdd(ItemId::Dirt));
	CHECK(inventory.canTakeDropOf(toBlockId(BlockType::OakLeaves)));   // drops nothing
	CHECK(inventory.canTakeDropOf(toBlockId(BlockType::Ice)));

	const Inventory empty;
	CHECK(empty.canTakeDropOf(toBlockId(BlockType::Stone)));
	CHECK(empty.canTakeDropOf(toBlockId(BlockType::SpruceLeaves)));
}

SELFTEST(Inventory, starting_items) {
	const Inventory inventory;
	int torches = 0, glowstone = 0;
	for (const ItemStack& stack : inventory.slots()) {
		torches += stack.item == ItemId::Torch ? stack.count : 0;
		glowstone += stack.item == ItemId::Glowstone ? stack.count : 0;
	}
	CHECK_EQ(torches, 64);
	CHECK_EQ(glowstone, 64);
}

namespace {

std::vector<std::uint8_t> encoded(const Inventory& inventory) {
	std::vector<std::uint8_t> bytes;
	encodeInventory(inventory, bytes);
	return bytes;
}

void setLe16(std::vector<std::uint8_t>& bytes, const std::size_t at, const std::uint16_t value) {
	bytes[at] = static_cast<std::uint8_t>(value & 0xFF);
	bytes[at + 1] = static_cast<std::uint8_t>(value >> 8);
}

} // namespace

SELFTEST(Inventory, encode_roundtrip) {
	const Inventory inventory = Inventory::fromState(Inventory().slots(), {ItemId::Dirt, 3}, 4);
	const std::vector<std::uint8_t> bytes = encoded(inventory);
	CHECK_EQ(bytes.size(), Inventory::ENCODED_SIZE);
	CHECK_EQ(bytes.size(), std::size_t{149});

	const std::optional<Inventory> decoded = decodeInventory(bytes);
	CHECK(decoded.has_value());
	if (!decoded)
		return;
	for (std::size_t i = 0; i < Inventory::SLOT_COUNT; ++i) {
		CHECK(decoded->slots()[i].item == inventory.slots()[i].item);
		CHECK_EQ(decoded->slots()[i].count, inventory.slots()[i].count);
	}
	CHECK(decoded->cursorStack().item == ItemId::Dirt);
	CHECK_EQ(decoded->cursorStack().count, 3);
	CHECK_EQ(decoded->selectedSlot(), 4);
}

SELFTEST(Inventory, decode_rejects_bad) {
	const std::vector<std::uint8_t> good = encoded(Inventory());
	CHECK(decodeInventory(good).has_value());

	std::vector<std::uint8_t> bytes = good;
	bytes[0] = 9;                                                   // selected out of the hotbar
	CHECK(!decodeInventory(bytes).has_value());

	bytes = good;
	setLe16(bytes, 1, static_cast<std::uint16_t>(ItemId::Count));   // unknown item
	CHECK(!decodeInventory(bytes).has_value());

	bytes = good;
	setLe16(bytes, 1 + 2, 65);                                      // 65 on a 64-stack
	CHECK(!decodeInventory(bytes).has_value());

	bytes = good;
	setLe16(bytes, 1 + 4, static_cast<std::uint16_t>(ItemId::Dirt));
	setLe16(bytes, 1 + 4 + 2, 0);                                   // dirt, count 0
	CHECK(!decodeInventory(bytes).has_value());

	bytes = good;
	setLe16(bytes, 1 + 4 * 10 + 2, 5);                              // empty item with a count
	CHECK(!decodeInventory(bytes).has_value());

	bytes = good;
	setLe16(bytes, 1 + 4 * Inventory::SLOT_COUNT, 0xFFFF);          // bad cursor item
	CHECK(!decodeInventory(bytes).has_value());

	bytes = good;
	bytes.pop_back();                                               // 148 bytes
	CHECK_EQ(bytes.size(), std::size_t{148});
	CHECK(!decodeInventory(bytes).has_value());

	bytes = good;
	bytes.push_back(0);                                             // 150 bytes
	CHECK(!decodeInventory(bytes).has_value());
	CHECK(!decodeInventory({}).has_value());
}

SELFTEST(Inventory, apply_action_bounds) {
	Inventory inventory;
	const std::vector<std::uint8_t> before = encoded(inventory);

	CHECK(!applyAction(inventory, {InventoryAction::Kind::Select, 9, false}));
	CHECK(!applyAction(inventory, {InventoryAction::Kind::Click, 36, false}));
	CHECK(!applyAction(inventory, {static_cast<InventoryAction::Kind>(7), 0, false}));
	CHECK(encoded(inventory) == before);

	CHECK(applyAction(inventory, {InventoryAction::Kind::Select, 4, false}));
	CHECK_EQ(inventory.selectedSlot(), 4);

	CHECK(applyAction(inventory, {InventoryAction::Kind::Click, 0, false}));
	CHECK(inventory.cursorStack().item == ItemId::AmethystBlock);
	CHECK(inventory.slots()[0].empty());
	CHECK(applyAction(inventory, {InventoryAction::Kind::Close, 0, false}));
	CHECK(inventory.cursorStack().empty());
	CHECK(inventory.slots()[0].item == ItemId::AmethystBlock);
	CHECK_EQ(inventory.slots()[0].count, 64);
}

SELFTEST(Inventory, encode_layout) {
	std::array<ItemStack, Inventory::SLOT_COUNT> slots{};
	slots[0] = {ItemId::Dirt, 0x0102};
	const Inventory inventory = Inventory::fromState(slots, {ItemId::Stone, 7}, 5);
	const std::vector<std::uint8_t> bytes = encoded(inventory);
	CHECK_EQ(bytes.size(), Inventory::ENCODED_SIZE);

	const auto item = [](const ItemId id) { return static_cast<std::uint16_t>(id); };
	CHECK_EQ(bytes[0], 5);                                                    // selected
	CHECK_EQ(bytes[1], item(ItemId::Dirt) & 0xFF);                            // slot 0 item, little-endian
	CHECK_EQ(bytes[2], item(ItemId::Dirt) >> 8);
	CHECK_EQ(bytes[3], 0x02);                                                 // slot 0 count, little-endian
	CHECK_EQ(bytes[4], 0x01);
	CHECK_EQ(bytes[145], item(ItemId::Stone) & 0xFF);                         // cursor item
	CHECK_EQ(bytes[146], item(ItemId::Stone) >> 8);
	CHECK_EQ(bytes[147], 7);                                                  // cursor count
	CHECK_EQ(bytes[148], 0);
}
