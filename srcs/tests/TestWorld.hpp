#pragma once
#ifndef TEST_WORLD_HPP
# define TEST_WORLD_HPP

// Hand-built 3x3 columns around (0, 0) for light and mesher tests: all air
// with a stone floor up to `floorTop` (storage y). Blocks are set in
// centre-relative coordinates, x and z in -16..31.

#include <array>
#include <memory>

#include "Block.hpp"
#include "Chunk.hpp"
#include "Light.hpp"

struct TestWorld {
	std::array<std::unique_ptr<ChunkColumn>, 9> columns;
	BlockRegistry registry;

	explicit TestWorld(const int floorTop) {
		for (int dz = -1; dz <= 1; ++dz)
			for (int dx = -1; dx <= 1; ++dx)
				columns[static_cast<std::size_t>((dz + 1) * 3 + (dx + 1))] = std::make_unique<ChunkColumn>(ChunkCoord{dx, dz});
		for (int z = -16; z < 32; ++z)
			for (int x = -16; x < 32; ++x)
				for (int y = 0; y <= floorTop; ++y)
					set(x, y, z, toBlockId(BlockType::Stone));
	}

	void set(const int x, const int y, const int z, const BlockId block) {
		const int cx = (x + 16) / 16, cz = (z + 16) / 16;
		columns[static_cast<std::size_t>(cz * 3 + cx)]->setUnchecked((x + 16) % 16, y, (z + 16) % 16, block);
	}

	// Stone all around a hollow [x0, x1] x [y0, y1] x [z0, z1].
	void hollowBox(const int x0, const int y0, const int z0, const int x1, const int y1, const int z1) {
		for (int z = z0 - 1; z <= z1 + 1; ++z)
			for (int y = y0 - 1; y <= y1 + 1; ++y)
				for (int x = x0 - 1; x <= x1 + 1; ++x) {
					const bool inside = x >= x0 && x <= x1 && y >= y0 && y <= y1 && z >= z0 && z <= z1;
					set(x, y, z, inside ? AIR_BLOCK : toBlockId(BlockType::Stone));
				}
	}

	[[nodiscard]] ColumnWindow window() const {
		ColumnWindow w{};
		for (std::size_t i = 0; i < 9; ++i)
			w[i] = columns[i].get();
		return w;
	}

	[[nodiscard]] std::shared_ptr<const ColumnLight> light() const { return computeLight(window(), registry); }

	[[nodiscard]] const ChunkColumn& centre() const { return *columns[4]; }
};

#endif /* TEST_WORLD_HPP */
