#include "worldgen/Structures.hpp"

#include <algorithm>
#include <cstdlib>

#include "worldgen/TerrainGenerator.hpp"

namespace worldgen {
namespace {

constexpr BlockId block(const BlockType t) noexcept { return toBlockId(t); }

// ---- tune here: structures per 1000 positions -----------------------------------------

constexpr std::uint32_t MAX_DENSITY = 14;   // highest of the densities below (cheap pre-filter)

std::uint32_t densityPerMille(const Biome biome) noexcept {
	switch (biome) {
		case Biome::Forest:      return 14;
		case Biome::Taiga:       return 10;
		case Biome::Plains:      return 1;
		case Biome::SnowyPlains: return 3;
		case Biome::Desert:      return 5;
		case Biome::Mountains:   return 2;
		case Biome::Ocean:
		case Biome::Beach:
		case Biome::Badlands:
		case Biome::Count:       return 0;
	}
	return 0;
}

constexpr int MAX_SLOPE = 2;   // most a neighbour's ground may differ, in blocks
constexpr int SPACING = 2;     // no two structure bases within this many blocks (Chebyshev)

// Whether another candidate within SPACING beats the one at (x, z): the lowest
// (hash, x, z) wins. Hashes only, so every column decides the same way.
bool crowded(const std::uint64_t seed, const int x, const int z, const std::uint32_t h) noexcept {
	for (int dz = -SPACING; dz <= SPACING; ++dz)
		for (int dx = -SPACING; dx <= SPACING; ++dx) {
			if (dx == 0 && dz == 0)
				continue;
			const std::uint32_t other = hash3(seed, x + dx, 0, z + dz);
			if (other % 1000u >= MAX_DENSITY)
				continue;                                  // not a candidate
			if (other < h || (other == h && (dx < 0 || (dx == 0 && dz < 0))))
				return true;
		}
	return false;
}

enum class Fill { Air, AirOrLeaves };

// Writes the parts of one structure that fall inside the column.
class Writer {
public:
	Writer(ChunkColumn& column, const int baseX, const int baseZ) noexcept
		: _column(column), _baseX(baseX), _baseZ(baseZ) {}

	void put(const int wx, const int y, const int wz, const BlockId id, const Fill fill) const {
		const int x = wx - _baseX, z = wz - _baseZ;
		if (x < 0 || x >= ChunkSection::SIZE || z < 0 || z >= ChunkSection::SIZE)
			return;
		if (y < ChunkColumn::MIN_WORLD_Y || y >= ChunkColumn::MAX_WORLD_Y)
			return;
		const int sy = ChunkColumn::toStorageY(y);
		const BlockId current = _column.getUnchecked(x, sy, z);
		const bool leaves = current == block(BlockType::OakLeaves) || current == block(BlockType::BirchLeaves)
		                 || current == block(BlockType::SpruceLeaves);
		if (current == AIR_BLOCK || (fill == Fill::AirOrLeaves && leaves))
			_column.setUnchecked(x, sy, z, id);
	}

	[[nodiscard]] BlockId at(const int wx, const int y, const int wz) const {
		return _column.getUnchecked(wx - _baseX, ChunkColumn::toStorageY(y), wz - _baseZ);
	}

	[[nodiscard]] bool inside(const int wx, const int wz) const noexcept {
		return wx >= _baseX && wx < _baseX + ChunkSection::SIZE && wz >= _baseZ && wz < _baseZ + ChunkSection::SIZE;
	}

private:
	ChunkColumn& _column;
	int _baseX;
	int _baseZ;
};

// Round canopy (oak, birch): two wide layers, then two narrow ones.
void roundTree(const Writer& w, const std::uint64_t seed, const int x, const int ground, const int z,
               const int height, const BlockId log, const BlockId leaves) {
	const int top = ground + height;
	for (int y = top - 2; y <= top + 1; ++y) {
		const int radius = y <= top - 1 ? 2 : 1;
		for (int dz = -radius; dz <= radius; ++dz)
			for (int dx = -radius; dx <= radius; ++dx) {
				const bool corner = std::abs(dx) == radius && std::abs(dz) == radius;
				// Wide layers lose some corners, narrow ones all of them (a plus on top).
				if (corner && (radius == 1 || (hash3(seed, x + dx, y, z + dz) & 1u) != 0u))
					continue;
				w.put(x + dx, y, z + dz, leaves, Fill::Air);
			}
	}
	for (int y = ground + 1; y <= top; ++y)
		w.put(x, y, z, log, Fill::AirOrLeaves);
}

// Conical canopy (spruce): rings alternating radius 2 and 1, narrowing to a tip.
void spruce(const Writer& w, const int x, const int ground, const int z, const int height) {
	const BlockId log = block(BlockType::SpruceLog), leaves = block(BlockType::SpruceLeaves);
	const int top = ground + height;
	w.put(x, top + 1, z, leaves, Fill::Air);
	for (int y = top; y >= ground + 3; --y) {
		const int fromTop = top - y;
		const int radius = fromTop < 2 || fromTop % 2 == 1 ? 1 : 2;
		for (int dz = -radius; dz <= radius; ++dz)
			for (int dx = -radius; dx <= radius; ++dx)
				if (!(std::abs(dx) == radius && std::abs(dz) == radius))     // no corners
					w.put(x + dx, y, z + dz, leaves, Fill::Air);
	}
	for (int y = ground + 1; y <= top; ++y)
		w.put(x, y, z, log, Fill::AirOrLeaves);
}

void boulder(const Writer& w, const int x, const int ground, const int z, const int radius) {
	const int r2 = radius * radius + 1;
	for (int dy = -1; dy <= radius; ++dy)
		for (int dz = -radius; dz <= radius; ++dz)
			for (int dx = -radius; dx <= radius; ++dx)
				if (dx * dx + dy * dy + dz * dz <= r2)
					w.put(x + dx, ground + 1 + dy, z + dz, block(BlockType::Stone), Fill::Air);
}

} // namespace

void placeStructures(const TerrainGenerator& gen, const std::uint64_t seed, ChunkColumn& column) {
	const int baseX = column.coord().x * ChunkSection::SIZE;
	const int baseZ = column.coord().z * ChunkSection::SIZE;
	const Writer writer(column, baseX, baseZ);
	const CaveCarver& caves = gen.caves();

	for (int z = baseZ - STRUCTURE_MARGIN; z < baseZ + ChunkSection::SIZE + STRUCTURE_MARGIN; ++z)
		for (int x = baseX - STRUCTURE_MARGIN; x < baseX + ChunkSection::SIZE + STRUCTURE_MARGIN; ++x) {
			const std::uint32_t h = hash3(seed, x, 0, z);
			const std::uint32_t roll = h % 1000u;
			if (roll >= MAX_DENSITY)
				continue;                                  // most positions: no noise evaluated
			if (crowded(seed, x, z, h))
				continue;                                  // a neighbour wins this spot
			const Biome biome = gen.biomeAt(x, z);
			if (roll >= densityPerMille(biome))
				continue;
			// Every test below uses (seed, coordinates) only, so the neighbouring
			// column reaches the same decision for the parts it places.
			const int ground = gen.surfaceHeight(x, z);
			if (ground < SEA_LEVEL)
				continue;                                  // under water
			if (caves.ravineStrength(x, z) > 0.0f || caves.entrance(x, z))
				continue;                                  // the ground may be carved away
			const bool steep = std::abs(gen.surfaceHeight(x + 1, z) - ground) > MAX_SLOPE
			                || std::abs(gen.surfaceHeight(x - 1, z) - ground) > MAX_SLOPE
			                || std::abs(gen.surfaceHeight(x, z + 1) - ground) > MAX_SLOPE
			                || std::abs(gen.surfaceHeight(x, z - 1) - ground) > MAX_SLOPE;
			if (steep)
				continue;

			const std::uint32_t variant = h / 1000u;
			switch (biome) {
				case Biome::Forest:
				case Biome::Plains:
					if (variant % 4u == 0u)
						roundTree(writer, seed, x, ground, z, 5 + static_cast<int>(variant / 4u % 3u),
						          block(BlockType::BirchLog), block(BlockType::BirchLeaves));
					else
						roundTree(writer, seed, x, ground, z, 4 + static_cast<int>(variant / 4u % 3u),
						          block(BlockType::OakLog), block(BlockType::OakLeaves));
					break;
				case Biome::Taiga:
				case Biome::SnowyPlains:
					spruce(writer, x, ground, z, 6 + static_cast<int>(variant % 4u));
					break;
				case Biome::Desert:
					// One block wide: it lies entirely in the base's column, so the
					// ground block itself can be checked.
					if (writer.inside(x, z) && writer.at(x, ground, z) == block(BlockType::Sand))
						for (int y = 1; y <= 1 + static_cast<int>(variant % 3u); ++y)
							writer.put(x, ground + y, z, block(BlockType::Cactus), Fill::Air);
					break;
				case Biome::Mountains:
					boulder(writer, x, ground, z, 1 + static_cast<int>(variant % 2u));
					break;
				case Biome::Ocean:
				case Biome::Beach:
				case Biome::Badlands:
				case Biome::Count:
					break;
			}
		}
}

} // namespace worldgen
