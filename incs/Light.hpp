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

#endif /* LIGHT_HPP */
