#include "worldgen/Biomes.hpp"

#include "worldgen/TerrainGenerator.hpp"

namespace worldgen {

namespace {
constexpr BlockId block(const BlockType type) noexcept { return toBlockId(type); }
}

const char* biomeName(const Biome biome) noexcept {
	switch (biome) {
		case Biome::Ocean:       return "ocean";
		case Biome::Beach:       return "beach";
		case Biome::Plains:      return "plains";
		case Biome::Forest:      return "forest";
		case Biome::Desert:      return "desert";
		case Biome::SnowyPlains: return "snowy plains";
		case Biome::Taiga:       return "taiga";
		case Biome::Mountains:   return "mountains";
		case Biome::Badlands:    return "badlands";
		case Biome::Count:       break;
	}
	return "?";
}

Biome chooseBiome(const Climate& c, const float surfaceHeight, const float mountainFactor,
                  const float badlandsFactor) noexcept {
	if (surfaceHeight < static_cast<float>(SEA_LEVEL) - 3.0f)
		return Biome::Ocean;
	if (surfaceHeight < static_cast<float>(SEA_LEVEL) + 2.0f && mountainFactor < 0.3f && c.continentalness < -0.02f)
		return Biome::Beach;
	if (mountainFactor > 0.45f)
		return Biome::Mountains;
	if (badlandsFactor > 0.45f)
		return Biome::Badlands;
	if (c.temperature > 0.35f && c.humidity < 0.1f)
		return Biome::Desert;
	if (c.temperature < -0.4f)
		return c.humidity > 0.1f ? Biome::Taiga : Biome::SnowyPlains;
	if (c.temperature < -0.15f)
		return Biome::Taiga;
	if (c.humidity > 0.15f)
		return Biome::Forest;
	return Biome::Plains;
}

BlockId surfaceBlock(const Biome biome, const int depth, const int y, const bool underwater, const float variation) noexcept {
	const int soil = 3 + static_cast<int>(variation * 1.5f);   // 2..4 blocks of dirt / sand

	if (underwater) {                                            // sea, lake and river floors
		if (depth >= 3)
			return AIR_BLOCK;
		if (y < 40)
			return block(BlockType::Gravel);
		return variation > 0.55f ? block(BlockType::Clay) : block(BlockType::Sand);
	}

	switch (biome) {
		case Biome::Ocean:
		case Biome::Beach:
			if (depth < 4) return block(BlockType::Sand);
			if (depth < 6) return block(BlockType::Sandstone);
			return AIR_BLOCK;
		case Biome::Desert:
			if (depth < 4) return block(BlockType::Sand);
			if (depth < 9) return block(BlockType::Sandstone);
			return AIR_BLOCK;
		case Biome::Badlands:
			if (depth == 0) return block(BlockType::Sand);
			if (depth < 18) {
				// Layered cliffs: sandstone with clay and gravel bands.
				const int band = ((y % 11) + 11) % 11;
				if (band == 0) return block(BlockType::Clay);
				if (band == 6) return block(BlockType::Gravel);
				return block(BlockType::Sandstone);
			}
			return AIR_BLOCK;
		case Biome::Mountains:
			if (y > 150 + static_cast<int>(variation * 8.0f))
				return depth < 2 ? block(BlockType::Snow) : AIR_BLOCK;          // snow caps
			if (y > 112 + static_cast<int>(variation * 10.0f))
				return depth == 0 && variation > 0.6f ? block(BlockType::Gravel) : AIR_BLOCK;   // bare rock
			if (depth == 0) return block(BlockType::Grass);
			if (depth < soil) return block(BlockType::Dirt);
			return AIR_BLOCK;
		case Biome::SnowyPlains:
			if (depth == 0) return block(BlockType::Snow);
			if (depth < soil) return block(BlockType::Dirt);
			return AIR_BLOCK;
		case Biome::Plains:
		case Biome::Forest:
		case Biome::Taiga:
			if (depth == 0) return block(BlockType::Grass);
			if (depth < soil) return block(BlockType::Dirt);
			return AIR_BLOCK;
		case Biome::Count:
			break;
	}
	return AIR_BLOCK;
}

} // namespace worldgen
