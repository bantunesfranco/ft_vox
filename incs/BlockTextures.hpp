#pragma once
#ifndef BLOCK_TEXTURES_HPP
# define BLOCK_TEXTURES_HPP

// ============================================================================
//  BlockTextures : 16x16 pixel-art textures for blocks the atlas image does
//  not contain (logs, leaves, gravel, ...). Generated deterministically at
//  startup and copied into the unused bottom part of the atlas texture, so
//  the renderer treats them like any other atlas sprite.
//
//  Texture indices 0..8 are sprites of minecraft_atlas.png (App::loadTextures);
//  FIRST_GENERATED.. are these, in the order of `Texture`.
// ============================================================================

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace BlockTextures {

constexpr std::uint16_t FIRST_GENERATED = 9;

enum class Texture : std::uint16_t {
	OakLogSide, OakLogTop, BirchLogSide, BirchLogTop, SpruceLogSide, SpruceLogTop,
	OakLeaves, BirchLeaves, SpruceLeaves,
	Gravel, Clay, CactusSide, CactusTop,
	SandstoneSide, SandstoneTop, SandstoneBottom, Ice,
	Count
};

constexpr std::uint16_t index(const Texture t) noexcept {
	return static_cast<std::uint16_t>(FIRST_GENERATED + static_cast<std::uint16_t>(t));
}

constexpr std::size_t generatedCount() noexcept { return static_cast<std::size_t>(Texture::Count); }

using Pixels = std::array<std::uint32_t, 16 * 16>;   // 0xAABBGGRR (RGBA bytes in memory), row 0 = top

// Every generated texture, in `Texture` order.
std::vector<Pixels> generate();

// Where a generated texture lives in the atlas image (top-left pixel).
struct AtlasSpot { int x; int y; };
AtlasSpot atlasSpot(std::uint16_t textureIndex);

} // namespace BlockTextures

#endif /* BLOCK_TEXTURES_HPP */
