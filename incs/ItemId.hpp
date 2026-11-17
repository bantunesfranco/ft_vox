#ifndef ITEM_ID_HPP
#define ITEM_ID_HPP

#include <cstdint>

enum class ItemId : std::uint8_t {
	Empty,
	GrassBlock,
	Dirt,
	Stone,
	Sand,
	AmethystBlock,
	Snow,
	OakLog,
	BirchLog,
	SpruceLog,
	Gravel,
	Clay,
	Cactus,
	Sandstone,
	Glowstone,
	Torch,
	Count,
};

#endif
