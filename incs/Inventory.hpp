#ifndef INVENTORY_HPP
#define INVENTORY_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "Item.hpp"

class Inventory {
public:
	static constexpr std::size_t HOTBAR_SIZE = 9;
	static constexpr std::size_t SLOT_COUNT = 36;
	// u8 selected, SLOT_COUNT x (u16 item, u16 count), cursor (u16 item, u16 count); little-endian.
	static constexpr std::size_t ENCODED_SIZE = 1 + (SLOT_COUNT + 1) * 4;

	Inventory() noexcept;
	// Assigns without validating; decodeInventory validates before calling it.
	[[nodiscard]] static Inventory fromState(const std::array<ItemStack, SLOT_COUNT>& slots,
		ItemStack cursor, std::uint16_t selected) noexcept;

	[[nodiscard]] const std::array<ItemStack, SLOT_COUNT>& slots() const noexcept;
	[[nodiscard]] const ItemStack& cursorStack() const noexcept;
	[[nodiscard]] const ItemStack& selectedStack() const noexcept;
	[[nodiscard]] std::uint16_t selectedSlot() const noexcept;
	void selectHotbarSlot(std::size_t slot) noexcept;
	void cycleHotbar(int delta) noexcept;

	[[nodiscard]] bool canAdd(ItemId item, std::uint16_t count = 1) const noexcept;
	// Whether breaking `block` loses nothing: it drops no item, or its item fits.
	[[nodiscard]] bool canTakeDropOf(BlockId block) const noexcept;
	bool add(ItemId item, std::uint16_t count = 1) noexcept;
	bool consumeSelected(std::uint16_t count = 1) noexcept;
	void clickSlot(std::size_t slot, bool secondaryClick) noexcept;
	void returnCursorStack() noexcept;

private:
	std::array<ItemStack, SLOT_COUNT> _slots{};
	ItemStack _cursor{};
	std::uint16_t _selectedSlot{0};
};

// One player input that changes the inventory: the unit sent to the server and replayed by prediction.
struct InventoryAction {
	enum class Kind : std::uint8_t { Select = 0, Click = 1, Close = 2 };
	Kind kind{};
	std::uint8_t slot = 0;
	bool secondary = false;
};

// The single rule path for inventory actions. False, and no change, for an out-of-range slot or unknown kind.
bool applyAction(Inventory& inventory, const InventoryAction& action) noexcept;

// Appends Inventory::ENCODED_SIZE bytes.
void encodeInventory(const Inventory& inventory, std::vector<std::uint8_t>& out);
// Reads exactly ENCODED_SIZE bytes; nullopt on a wrong size or any inconsistent content.
[[nodiscard]] std::optional<Inventory> decodeInventory(std::span<const std::uint8_t> bytes);

#endif
