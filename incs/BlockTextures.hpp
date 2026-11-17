#pragma once
#ifndef BLOCK_TEXTURES_HPP
# define BLOCK_TEXTURES_HPP

// ============================================================================
//  BlockTextures : where every block texture is in textures/minecraft_atlas.png
//
//  The atlas is a dump of Minecraft's block atlas. Coordinates come from its
//  index (minecraft_textures_atlas_blocks.png.txt): each is a 48 x 48 cell
//  holding the 16 x 16 sprite with its edges repeated around it, which keeps
//  mipmaps from bleeding between sprites. App::loadTextures turns a cell into
//  the shader's texture rect the same way for every block.
//
//  Texture indices 0..8 are the original blocks (grass top .. bedrock), set in
//  that order; FIRST_ADDED.. follow `Texture`.
// ============================================================================

#include <cstdint>

namespace BlockTextures {

constexpr std::uint16_t FIRST_ADDED = 9;
constexpr int CELL = 48;                         // atlas cell size, sprite included
constexpr std::uint32_t NO_TINT = 0xFFFFFF;

enum class Texture : std::uint16_t {
	OakLogSide, OakLogTop, BirchLogSide, BirchLogTop, SpruceLogSide, SpruceLogTop,
	OakLeaves, BirchLeaves, SpruceLeaves,
	Gravel, Clay, CactusSide, CactusTop,
	SandstoneSide, SandstoneTop, SandstoneBottom, Ice,
	LavaStill, Glowstone, Torch,
	Count
};

constexpr std::uint16_t index(const Texture t) noexcept {
	return static_cast<std::uint16_t>(FIRST_ADDED + static_cast<std::uint16_t>(t));
}

constexpr std::uint16_t TEXTURE_COUNT = FIRST_ADDED + static_cast<std::uint16_t>(Texture::Count);

struct Sprite {
	int x;                   // atlas cell, as in Minecraft's atlas index
	int y;
	std::uint32_t tint;      // 0xRRGGBB multiplied into the pixels once at load (leaves are grey)
};

// Texture index 0 .. TEXTURE_COUNT - 1.
[[nodiscard]] const Sprite& sprite(std::uint16_t textureIndex) noexcept;

// Where the 16 x 16 picture itself is in the atlas image: PADDING pixels into
// its cell (the rest of the cell repeats its edges). Icons and the world
// both read it from here.
constexpr int PADDING = 16;
struct PixelRect { int x; int y; int w; int h; };
[[nodiscard]] PixelRect pixels(std::uint16_t textureIndex) noexcept;

// The grass top is grey in the atlas; this is the green it is drawn with.
constexpr float GRASS_TINT[3] = {0.65f, 0.99f, 0.45f};

} // namespace BlockTextures

#endif /* BLOCK_TEXTURES_HPP */
