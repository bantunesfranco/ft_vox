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
	CHECK_EQ(BLOCK_TYPE_COUNT, std::size_t{20});
}

SELFTEST(Blocks, new_blocks_registered) {
	const BlockRegistry registry;
	std::set<std::uint16_t> textures;
	for (const BlockType type : NEW_BLOCKS) {
		const BlockDefinition& def = registry.get(type);
		CHECK(def.textureSide >= 9);                         // generated textures, after the atlas ones
		CHECK(def.textureSide < BlockTextures::FIRST_GENERATED + BlockTextures::generatedCount());
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
	                             BlockType::Gravel, BlockType::Clay, BlockType::Cactus, BlockType::Sandstone}) {
		const ItemId item = ItemRegistry::fromBlock(toBlockId(type));
		CHECK(item != ItemId::Empty);
		CHECK_EQ(ItemRegistry::get(item).placedBlock, toBlockId(type));
	}
	// Leaves and ice drop nothing.
	CHECK(ItemRegistry::fromBlock(toBlockId(BlockType::OakLeaves)) == ItemId::Empty);
	CHECK(ItemRegistry::fromBlock(toBlockId(BlockType::Ice)) == ItemId::Empty);
}

SELFTEST(Blocks, generated_textures) {
	const auto first = BlockTextures::generate();
	const auto second = BlockTextures::generate();
	CHECK_EQ(first.size(), BlockTextures::generatedCount());
	CHECK(first == second);                                   // deterministic
	const BlockRegistry registry;
	const auto& leaves = first[registry.get(BlockType::OakLeaves).textureSide - BlockTextures::FIRST_GENERATED];
	int holes = 0;
	for (const std::uint32_t pixel : leaves)
		holes += (pixel >> 24) == 0 ? 1 : 0;
	CHECK(holes > 10 && holes < 128);                        // see-through, but mostly leaf
	const auto& gravel = first[registry.get(BlockType::Gravel).textureSide - BlockTextures::FIRST_GENERATED];
	for (const std::uint32_t pixel : gravel)
		CHECK_EQ(pixel >> 24, 255u);                          // opaque blocks have no holes
}
