#pragma once
#ifndef LIGHT_HPP
# define LIGHT_HPP

// ============================================================================
//  Light : Minecraft-style light levels for one column
//
//  Every cell has two levels, 0..15:
//    sky   15 under open sky, straight down until an opaque block (water and
//          leaves dim it by their lightFilter), then spreading sideways and
//          down, -1 per block;
//    block from emitters (BlockDefinition::lightEmission), -1 per block.
//  Opaque blocks stop both; filtering blocks cost lightFilter extra levels.
//
//  A column's light is computed from its 3x3 window of columns: light travels
//  at most 15 blocks, the window reaches 16 past each side, so the result
//  depends only on the blocks near the column, never on load order.
// ============================================================================

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

#include "Block.hpp"
#include "Chunk.hpp"

class ColumnLight {
public:
	// Packed (sky << 4) | block; 0 <= x, z < 16, 0 <= storageY < HEIGHT.
	[[nodiscard]] std::uint8_t at(int x, int storageY, int z) const noexcept;
	// at() for the `count` cells of row (storageY, z) from x on, into `out`.
	void copyRow(int x, int storageY, int z, int count, std::uint8_t* out) const noexcept;
	[[nodiscard]] static std::uint8_t sky(const std::uint8_t packed) noexcept { return static_cast<std::uint8_t>(packed >> 4); }
	[[nodiscard]] static std::uint8_t block(const std::uint8_t packed) noexcept { return static_cast<std::uint8_t>(packed & 0x0F); }

	// Memory held by sections that are not uniform.
	[[nodiscard]] std::size_t heapBytes() const noexcept;

	static constexpr std::uint8_t FULL_SKY = 0xF0;

private:
	friend std::shared_ptr<const ColumnLight> computeLight(const std::array<const ChunkColumn*, 9>&, const BlockRegistry&);
	friend std::uint16_t lightChangeMask(const ColumnLight*, const ColumnLight&) noexcept;

	struct Section {
		std::uint8_t uniform = FULL_SKY;                                        // when `values` is null
		std::unique_ptr<std::array<std::uint8_t, ChunkSection::BLOCK_COUNT>> values;   // x + (z << 4) + (y << 8)
	};
	std::array<Section, ChunkColumn::SECTION_COUNT> _sections;
};

// The 3x3 columns around the one being lit: index (dz + 1) * 3 + (dx + 1),
// all nine present.
using ColumnWindow = std::array<const ChunkColumn*, 9>;

[[nodiscard]] std::shared_ptr<const ColumnLight> computeLight(const ColumnWindow& window, const BlockRegistry& registry);

// How bright a cell with these levels looks, 0.05 (level 0) .. 1 (level 15):
// the fragment shader's curve, max(sky * daylight, block) through
// v / (4 - 3v) with a 5 % floor (without its fixed 0.625 scale).
[[nodiscard]] float lightBrightness(int sky, int block, float daylight) noexcept;

// Which meshes show a cell whose light differs between `before` and `after`
// (a column's light, recomputed): bit (dz + 1) * 3 + (dx + 1) for the column
// at (dx, dz) from it. A mesh reads its own column's light and the one-cell
// border of its neighbours', so a change at local x = 0 also concerns the
// west neighbour, one on the vertical edge x = 0, z = 0 the north-west one.
// Null `before` (no light yet): all nine.
[[nodiscard]] std::uint16_t lightChangeMask(const ColumnLight* before, const ColumnLight& after) noexcept;

#endif /* LIGHT_HPP */
