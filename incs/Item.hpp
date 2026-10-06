#ifndef ITEM_HPP
#define ITEM_HPP

#include <array>
#include <cstdint>

#include "Block.hpp"
#include "ItemId.hpp"

struct ItemDefinition {
	const char* name;
	BlockId placedBlock;
	std::uint8_t maxStack;
};

class ItemRegistry {
public:
	[[nodiscard]] static const ItemDefinition& get(ItemId item) noexcept;
	[[nodiscard]] static ItemId fromBlock(BlockId block) noexcept;
};

struct ItemStack {
	ItemId item{ItemId::Empty};
	std::uint16_t count{0};

	[[nodiscard]] bool empty() const noexcept { return count == 0 || item == ItemId::Empty; }
};

#endif
