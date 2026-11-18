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
	{"Oak Log", toBlockId(BlockType::OakLog), 64},
	{"Birch Log", toBlockId(BlockType::BirchLog), 64},
	{"Spruce Log", toBlockId(BlockType::SpruceLog), 64},
	{"Gravel", toBlockId(BlockType::Gravel), 64},
	{"Clay", toBlockId(BlockType::Clay), 64},
	{"Cactus", toBlockId(BlockType::Cactus), 64},
	{"Sandstone", toBlockId(BlockType::Sandstone), 64},
	{"Glowstone", toBlockId(BlockType::Glowstone), 64},
	{"Torch", toBlockId(BlockType::Torch), 64},
}};
}

const ItemDefinition& ItemRegistry::get(const ItemId item) noexcept {
	const auto index = static_cast<std::size_t>(item);
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
		case BlockType::OakLog: return ItemId::OakLog;
		case BlockType::BirchLog: return ItemId::BirchLog;
		case BlockType::SpruceLog: return ItemId::SpruceLog;
		case BlockType::Gravel: return ItemId::Gravel;
		case BlockType::Clay: return ItemId::Clay;
		case BlockType::Cactus: return ItemId::Cactus;
		case BlockType::Sandstone: return ItemId::Sandstone;
		case BlockType::Glowstone: return ItemId::Glowstone;
		case BlockType::Torch: return ItemId::Torch;
		default: return ItemId::Empty;
	}
}
