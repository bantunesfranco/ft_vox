#include "SelfTest.hpp"
#include "PredictedInventory.hpp"

namespace {

constexpr InventoryAction click(const std::uint8_t slot) {
	return {InventoryAction::Kind::Click, slot, false};
}

} // namespace

SELFTEST(PredictedInventory, server_state_replaces_view) {
	PredictedInventory predicted;
	CHECK_EQ(predicted.apply(click(0)), std::uint32_t{1});
	CHECK(predicted.view().cursorStack().item == ItemId::AmethystBlock);

	std::array<ItemStack, Inventory::SLOT_COUNT> slots{};
	slots[7] = {ItemId::Sand, 12};
	predicted.onServerState(Inventory::fromState(slots, {}, 2), 1);
	CHECK(predicted.view().cursorStack().empty());
	CHECK(predicted.view().slots()[0].empty());
	CHECK(predicted.view().slots()[7].item == ItemId::Sand);
	CHECK_EQ(predicted.view().slots()[7].count, 12);
	CHECK_EQ(predicted.view().selectedSlot(), 2);
}

SELFTEST(PredictedInventory, late_ack_keeps_pending) {
	PredictedInventory predicted;
	CHECK_EQ(predicted.apply(click(0)), std::uint32_t{1});   // pick up the amethyst
	CHECK_EQ(predicted.apply(click(5)), std::uint32_t{2});   // put it down

	Inventory server;                                        // the server saw only seq 1
	CHECK(applyAction(server, click(0)));
	predicted.onServerState(server, 1);

	CHECK(predicted.view().cursorStack().empty());
	CHECK(predicted.view().slots()[5].item == ItemId::AmethystBlock);
	CHECK_EQ(predicted.view().slots()[5].count, 64);
	CHECK(predicted.view().slots()[0].empty());
}

SELFTEST(PredictedInventory, server_overrides_cheat) {
	// The server's state, not the prediction, wins once every action is acknowledged.
	std::array<ItemStack, Inventory::SLOT_COUNT> cheat{};
	cheat[3] = {ItemId::Stone, 64};

	PredictedInventory predicted;
	predicted.onServerState(Inventory::fromState(cheat, {}, 0), 0);
	CHECK(predicted.view().slots()[3].item == ItemId::Stone);   // the view claims 64 stone

	const std::uint32_t seq = predicted.apply({InventoryAction::Kind::Select, 1, false});
	predicted.onServerState(Inventory(), seq);                  // the server has none
	for (const ItemStack& stack : predicted.view().slots())
		CHECK(stack.item != ItemId::Stone);
	CHECK_EQ(predicted.view().selectedSlot(), 0);
}
