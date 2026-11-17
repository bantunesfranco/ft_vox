#include "BlockTextures.hpp"

#include <array>

namespace BlockTextures {
namespace {

// Minecraft's default foliage colours (the leaf sprites are grey).
constexpr std::uint32_t OAK_FOLIAGE    = 0x48B518;
constexpr std::uint32_t BIRCH_FOLIAGE  = 0x80A755;
constexpr std::uint32_t SPRUCE_FOLIAGE = 0x619961;

constexpr std::array<Sprite, TEXTURE_COUNT> SPRITES{{
	// The original nine (indices 0..8).
	{1408,  528, NO_TINT},          // grass_block_top (tinted in the shader)
	{1168,  720, NO_TINT},          // dirt
	{1408,  384, NO_TINT},          // grass_block_side
	{1680, 1312, NO_TINT},          // stone
	{1440, 1216, NO_TINT},          // sand
	{ 512,  288, NO_TINT},          // amethyst_block
	{ 480, 1504, NO_TINT},          // water_still (tinted in the shader)
	{ 672, 1312, NO_TINT},          // snow
	{ 704,  240, NO_TINT},          // bedrock
	// Added blocks, in `Texture` order.
	{1888,  768, NO_TINT},          // oak_log
	{1888,  816, NO_TINT},          // oak_log_top
	{ 800,  432, NO_TINT},          // birch_log
	{ 848,    0, NO_TINT},          // birch_log_top
	{1440, 1312, NO_TINT},          // spruce_log
	{1488, 1312, NO_TINT},          // spruce_log_top
	{1888,  720, OAK_FOLIAGE},      // oak_leaves
	{ 800,  384, BIRCH_FOLIAGE},    // birch_leaves
	{1392, 1312, SPRUCE_FOLIAGE},   // spruce_leaves
	{1408,  576, NO_TINT},          // gravel
	{ 768,  752, NO_TINT},          // clay
	{ 432,  608, NO_TINT},          // cactus_side
	{ 480,  608, NO_TINT},          // cactus_top
	{1488, 1216, NO_TINT},          // sandstone
	{1584, 1216, NO_TINT},          // sandstone_top
	{1536, 1216, NO_TINT},          // sandstone_bottom
	{1552,   48, NO_TINT},          // ice
	{1600,  624, NO_TINT},          // lava_still
	{1408,  144, NO_TINT},          // glowstone
	{1680, 1408, NO_TINT},          // torch
}};

} // namespace

const Sprite& sprite(const std::uint16_t textureIndex) noexcept {
	return SPRITES[textureIndex < TEXTURE_COUNT ? textureIndex : 0];
}

} // namespace BlockTextures
