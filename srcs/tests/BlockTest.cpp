#include "SelfTest.hpp"
#include "Block.hpp"
#include "BlockTextures.hpp"
#include "Item.hpp"

#include <set>

namespace {
const BlockType NEW_BLOCKS[] = {
	BlockType::OakLog, BlockType::BirchLog, BlockType::SpruceLog,
	BlockType::OakLeaves, BlockType::BirchLeaves, BlockType::SpruceLeaves,
	BlockType::Gravel, BlockType::Clay, BlockType::Cactus, BlockType::Sandstone, BlockType::Ice,
};
}

SELFTEST(Blocks, ids_never_move) {
	// Saved worlds store these numbers: existing ids must never change.
	CHECK_EQ(toBlockId(BlockType::Bedrock), BlockId{8});
	CHECK_EQ(toBlockId(BlockType::OakLog), BlockId{9});
	CHECK_EQ(toBlockId(BlockType::Ice), BlockId{19});
	CHECK_EQ(toBlockId(BlockType::Lava), BlockId{20});
	CHECK_EQ(toBlockId(BlockType::Glowstone), BlockId{21});
	CHECK_EQ(toBlockId(BlockType::Torch), BlockId{22});
	CHECK_EQ(BLOCK_TYPE_COUNT, std::size_t{23});
}

SELFTEST(Blocks, new_blocks_registered) {
	const BlockRegistry registry;
	std::set<std::uint16_t> textures;
	for (const BlockType type : NEW_BLOCKS) {
		const BlockDefinition& def = registry.get(type);
		CHECK(def.textureSide >= BlockTextures::FIRST_ADDED);   // after the original nine
		CHECK(def.textureSide < BlockTextures::TEXTURE_COUNT);
		textures.insert(def.textureSide);
	}
	CHECK(textures.size() >= 10);                            // nearly every block looks different
	for (const BlockType log : {BlockType::OakLog, BlockType::BirchLog, BlockType::SpruceLog})
		CHECK(registry.get(log).textureTop != registry.get(log).textureSide);
}

SELFTEST(Blocks, solidity) {
	const BlockRegistry registry;
	CHECK(registry.get(BlockType::OakLeaves).solid);
	CHECK(!registry.get(BlockType::OakLeaves).opaque);
	CHECK(!registry.get(BlockType::OakLeaves).transparent);  // cut-out, drawn in the opaque pass
	CHECK(registry.get(BlockType::Ice).solid);
	CHECK(registry.get(BlockType::Ice).transparent);
	CHECK(!registry.get(BlockType::Water).solid);
	CHECK(!registry.get(BlockType::Air).solid);
	CHECK(registry.get(BlockType::Stone).solid && registry.get(BlockType::Stone).opaque);
	CHECK(registry.get(BlockType::Cactus).solid);
}

SELFTEST(Blocks, items_map_back) {
	for (const BlockType type : {BlockType::OakLog, BlockType::BirchLog, BlockType::SpruceLog,
	                             BlockType::Gravel, BlockType::Clay, BlockType::Cactus, BlockType::Sandstone,
	                             BlockType::Glowstone, BlockType::Torch}) {
		const ItemId item = ItemRegistry::fromBlock(toBlockId(type));
		CHECK(item != ItemId::Empty);
		CHECK_EQ(ItemRegistry::get(item).placedBlock, toBlockId(type));
	}
	// Leaves and ice drop nothing.
	CHECK(ItemRegistry::fromBlock(toBlockId(BlockType::OakLeaves)) == ItemId::Empty);
	CHECK(ItemRegistry::fromBlock(toBlockId(BlockType::Ice)) == ItemId::Empty);
}

SELFTEST(Blocks, textures_are_atlas_sprites) {
	// Every texture is a cell of Minecraft's block atlas: 48 px apart, inside
	// the 2048 x 2048 image, each used once.
	std::set<std::pair<int, int>> cells;
	for (std::uint16_t i = 0; i < BlockTextures::TEXTURE_COUNT; ++i) {
		const BlockTextures::Sprite& s = BlockTextures::sprite(i);
		CHECK(s.x % 16 == 0 && s.y % 16 == 0);
		CHECK(s.x >= 0 && s.x + BlockTextures::CELL <= 2048);
		CHECK(s.y >= 0 && s.y + BlockTextures::CELL <= 2048);
		cells.insert({s.x, s.y});
	}
	CHECK_EQ(cells.size(), std::size_t{BlockTextures::TEXTURE_COUNT});

	// Leaves are grey in the atlas and get a green tint; logs keep their colours.
	using BlockTextures::Texture;
	const BlockRegistry registry;
	for (const BlockType leaves : {BlockType::OakLeaves, BlockType::BirchLeaves, BlockType::SpruceLeaves}) {
		const std::uint32_t tint = BlockTextures::sprite(registry.get(leaves).textureSide).tint;
		CHECK(tint != BlockTextures::NO_TINT);
		CHECK(((tint >> 8) & 0xFFu) > (tint >> 16) && ((tint >> 8) & 0xFFu) > (tint & 0xFFu));   // green
	}
	CHECK_EQ(BlockTextures::sprite(registry.get(BlockType::OakLog).textureSide).tint, BlockTextures::NO_TINT);
	// The original nine keep the coordinates they always had (dirt, stone).
	CHECK(BlockTextures::sprite(1).x == 1168 && BlockTextures::sprite(1).y == 720);
	CHECK(BlockTextures::sprite(3).x == 1680 && BlockTextures::sprite(3).y == 1312);
}

SELFTEST(Blocks, fog_inside_liquids) {
	// With the eye inside water the world fades into blue a few blocks away;
	// ordinary blocks have no fog.
	const BlockRegistry registry;
	const BlockDefinition& water = registry.get(WATER_BLOCK);
	CHECK(water.fogDistance >= 32.0f && water.fogDistance <= 128.0f);   // murky, not blind
	CHECK((water.fogColor & 0xFFu) > (water.fogColor >> 16));          // blue over red
	for (const BlockType type : {BlockType::Air, BlockType::Stone, BlockType::Ice}) {
		CHECK_EQ(registry.get(type).fogDistance, 0.0f);
		CHECK_EQ(registry.get(type).fogColor, 0u);
	}
}

SELFTEST(Blocks, light_blocks) {
	const BlockRegistry r;
	CHECK_EQ(r.get(BlockType::Glowstone).lightEmission, std::uint8_t{15});
	CHECK_EQ(r.get(BlockType::Lava).lightEmission, std::uint8_t{15});
	CHECK_EQ(r.get(BlockType::Torch).lightEmission, std::uint8_t{14});
	CHECK_EQ(r.get(BlockType::Stone).lightEmission, std::uint8_t{0});
	CHECK_EQ(r.get(BlockType::Water).lightFilter, std::uint8_t{1});
	CHECK_EQ(r.get(BlockType::OakLeaves).lightFilter, std::uint8_t{1});
	CHECK_EQ(r.get(BlockType::Air).lightFilter, std::uint8_t{0});

	const BlockDefinition& lava = r.get(BlockType::Lava);
	CHECK(!lava.solid);
	CHECK_EQ(lava.fogColor, 0xC8400Au);
	CHECK_EQ(lava.fogDistance, 1.5f);
	CHECK(ItemRegistry::fromBlock(LAVA_BLOCK) == ItemId::Empty);

	const BlockDefinition& torch = r.get(BlockType::Torch);
	CHECK(!torch.solid && !torch.opaque);
	CHECK(torch.model == BlockModel::Torch);
	CHECK(r.get(BlockType::Glowstone).opaque && r.get(BlockType::Glowstone).solid);
	CHECK(r.get(BlockType::Stone).model == BlockModel::Cube);
}

SELFTEST(Blocks, torch_needs_solid_ground) {
	const BlockRegistry r;
	const BlockId torch = toBlockId(BlockType::Torch);
	CHECK(placementAllowed(r, torch, toBlockId(BlockType::Stone)));
	CHECK(placementAllowed(r, torch, toBlockId(BlockType::Glowstone)));
	for (const BlockType below : {BlockType::Air, BlockType::Water, BlockType::Lava, BlockType::OakLeaves,
	                              BlockType::Ice, BlockType::Torch})
		CHECK(!placementAllowed(r, torch, toBlockId(below)));
	CHECK(placementAllowed(r, toBlockId(BlockType::Stone), AIR_BLOCK));
}

SELFTEST(Blocks, sprite_pixels_inside_cells) {
	// The 16 x 16 picture sits 16 px into its 48 x 48 atlas cell (the rest is
	// padding): icons and the world must read it there, not at the cell origin.
	for (std::uint16_t i = 0; i < BlockTextures::TEXTURE_COUNT; ++i) {
		const BlockTextures::Sprite& cell = BlockTextures::sprite(i);
		const BlockTextures::PixelRect px = BlockTextures::pixels(i);
		CHECK_EQ(px.x, cell.x + 16);
		CHECK_EQ(px.y, cell.y + 16);
		CHECK(px.w == 16 && px.h == 16);
		CHECK(px.x + px.w <= cell.x + BlockTextures::CELL && px.y + px.h <= cell.y + BlockTextures::CELL);
	}
}
