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
