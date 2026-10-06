#ifndef INVENTORY_HPP
#define INVENTORY_HPP

#include <array>
#include <cstddef>
#include <cstdint>

#include "Item.hpp"

class Inventory {
public:
	static constexpr std::size_t HOTBAR_SIZE = 9;
	static constexpr std::size_t SLOT_COUNT = 36;

	Inventory() noexcept;

	[[nodiscard]] const std::array<ItemStack, SLOT_COUNT>& slots() const noexcept;
	[[nodiscard]] const ItemStack& cursorStack() const noexcept;
	[[nodiscard]] const ItemStack& selectedStack() const noexcept;
	[[nodiscard]] std::uint16_t selectedSlot() const noexcept;
	void selectHotbarSlot(std::size_t slot) noexcept;
	void cycleHotbar(int delta) noexcept;

	[[nodiscard]] bool canAdd(ItemId item, std::uint16_t count = 1) const noexcept;
	bool add(ItemId item, std::uint16_t count = 1) noexcept;
	bool consumeSelected(std::uint16_t count = 1) noexcept;
	void clickSlot(std::size_t slot, bool secondaryClick) noexcept;
	void returnCursorStack() noexcept;

private:
	std::array<ItemStack, SLOT_COUNT> _slots{};
	ItemStack _cursor{};
	std::uint16_t _selectedSlot{0};
};

#endif
