#include "Inventory.hpp"

#include <algorithm>

Inventory::Inventory() noexcept {
	_slots[0] = {ItemId::AmethystBlock, 64};
}

const std::array<ItemStack, Inventory::SLOT_COUNT>& Inventory::slots() const noexcept {
	return _slots;
}

const ItemStack& Inventory::cursorStack() const noexcept {
	return _cursor;
}

const ItemStack& Inventory::selectedStack() const noexcept {
	return _slots[_selectedSlot];
}

std::uint16_t Inventory::selectedSlot() const noexcept {
	return _selectedSlot;
}

void Inventory::selectHotbarSlot(const std::size_t slot) noexcept {
	if (slot < HOTBAR_SIZE)
		_selectedSlot = static_cast<std::uint16_t>(slot);
}

void Inventory::cycleHotbar(const int delta) noexcept {
	const int size = static_cast<int>(HOTBAR_SIZE);
	const int next = (static_cast<int>(_selectedSlot) + delta % size + size) % size;
	_selectedSlot = static_cast<std::uint16_t>(next);
}

bool Inventory::canAdd(const ItemId item, const std::uint16_t count) const noexcept {
	if (item == ItemId::Empty || count == 0)
		return count == 0;

	const std::uint16_t limit = ItemRegistry::get(item).maxStack;
	std::uint32_t capacity = 0;
	for (const ItemStack& stack : _slots) {
		if (stack.empty())
			capacity += limit;
		else if (stack.item == item && stack.count < limit)
			capacity += limit - stack.count;
		if (capacity >= count)
			return true;
	}
	return false;
}

bool Inventory::canTakeDropOf(const BlockId block) const noexcept {
	const ItemId drop = ItemRegistry::fromBlock(block);
	return drop == ItemId::Empty || canAdd(drop);     // leaves and ice drop nothing
}

bool Inventory::add(const ItemId item, const std::uint16_t count) noexcept {
	if (!canAdd(item, count))
		return false;
	if (count == 0)
		return true;

	const std::uint16_t limit = ItemRegistry::get(item).maxStack;
	std::uint16_t remaining = count;
	for (ItemStack& stack : _slots) {
		if (!stack.empty() && stack.item == item && stack.count < limit) {
			const std::uint16_t moved = std::min<std::uint16_t>(remaining, limit - stack.count);
			stack.count += moved;
			remaining -= moved;
			if (remaining == 0)
				return true;
		}
	}
	for (ItemStack& stack : _slots) {
		if (stack.empty()) {
			const std::uint16_t moved = std::min(remaining, limit);
			stack = {item, moved};
			remaining -= moved;
			if (remaining == 0)
				return true;
		}
	}
	return true;
}

bool Inventory::consumeSelected(const std::uint16_t count) noexcept {
	ItemStack& stack = _slots[_selectedSlot];
	if (stack.empty() || stack.count < count)
		return false;
	stack.count -= count;
	if (stack.count == 0)
		stack = {};
	return true;
}

void Inventory::clickSlot(const std::size_t slotIndex, const bool secondaryClick) noexcept {
	if (slotIndex >= SLOT_COUNT)
		return;
	ItemStack& slot = _slots[slotIndex];
	if (secondaryClick) {
		if (_cursor.empty()) {
			if (slot.empty())
				return;
			const std::uint16_t taken = static_cast<std::uint16_t>((slot.count + 1) / 2);
			_cursor = {slot.item, taken};
			slot.count -= taken;
			if (slot.count == 0)
				slot = {};
		} else if (slot.empty()) {
			slot = {_cursor.item, 1};
			if (--_cursor.count == 0)
				_cursor = {};
		} else if (slot.item == _cursor.item
			&& slot.count < ItemRegistry::get(slot.item).maxStack) {
			++slot.count;
			if (--_cursor.count == 0)
				_cursor = {};
		}
		return;
	}

	if (_cursor.empty()) {
		_cursor = slot;
		slot = {};
	} else if (slot.empty()) {
		slot = _cursor;
		_cursor = {};
	} else if (slot.item == _cursor.item) {
		const std::uint16_t limit = ItemRegistry::get(slot.item).maxStack;
		const std::uint16_t moved = std::min<std::uint16_t>(_cursor.count, limit - slot.count);
		slot.count += moved;
		_cursor.count -= moved;
		if (_cursor.count == 0)
			_cursor = {};
	} else {
		std::swap(slot, _cursor);
	}
}

void Inventory::returnCursorStack() noexcept {
	if (_cursor.empty())
		return;
	const ItemStack held = _cursor;
	if (add(held.item, held.count))
		_cursor = {};
}
