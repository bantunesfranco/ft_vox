#include "Item.hpp"

namespace {
constexpr std::array<ItemDefinition, static_cast<std::size_t>(ItemId::Count)> DEFINITIONS{{
	{"Empty", AIR_BLOCK, 0},
	{"Grass Block", toBlockId(BlockType::Grass), 64},
	{"Dirt", toBlockId(BlockType::Dirt), 64},
	{"Stone", toBlockId(BlockType::Stone), 64},
	{"Sand", toBlockId(BlockType::Sand), 64},
	{"Amethyst Block", toBlockId(BlockType::Amethyst), 64},
	{"Snow", toBlockId(BlockType::Snow), 64},
}};
}

const ItemDefinition& ItemRegistry::get(const ItemId item) noexcept {
	const std::size_t index = static_cast<std::size_t>(item);
	return DEFINITIONS[index < DEFINITIONS.size() ? index : 0];
}

ItemId ItemRegistry::fromBlock(const BlockId block) noexcept {
	switch (toBlockType(block)) {
		case BlockType::Grass: return ItemId::GrassBlock;
		case BlockType::Dirt: return ItemId::Dirt;
		case BlockType::Stone: return ItemId::Stone;
		case BlockType::Sand: return ItemId::Sand;
		case BlockType::Amethyst: return ItemId::AmethystBlock;
		case BlockType::Snow: return ItemId::Snow;
		default: return ItemId::Empty;
	}
}
