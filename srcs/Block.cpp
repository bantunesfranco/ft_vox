#include "Block.hpp"

BlockRegistry::BlockRegistry() noexcept {
    /*
     * Texture indices correspond to layers in the texture array.
     *
     * Change these numbers to match your actual texture-array order.
     *
     * Example:
     *   0 = grass
     *   1 = dirt
     *   2 = stone
     *   3 = sand
     *   4 = amethyst
     */

    // Air
    _definitions[toBlockId(BlockType::Air)] = {
        .opaque = false,
        .transparent = true,
        .textureTop = 0,
        .textureBottom = 0,
        .textureSide = 0
    };

    // Grass
    _definitions[toBlockId(BlockType::Grass)] = {
        .opaque = true,
        .transparent = false,
        .textureTop = 0,
        .textureBottom = 1,
        .textureSide = 2
    };

    // Dirt
    _definitions[toBlockId(BlockType::Dirt)] = {
        .opaque = true,
        .transparent = false,
        .textureTop = 1,
        .textureBottom = 1,
        .textureSide = 1
    };

    // Stone
    _definitions[toBlockId(BlockType::Stone)] = {
        .opaque = true,
        .transparent = false,
        .textureTop = 2,
        .textureBottom = 2,
        .textureSide = 2
    };

    // Sand
    _definitions[toBlockId(BlockType::Sand)] = {
        .opaque = true,
        .transparent = false,
        .textureTop = 3,
        .textureBottom = 3,
        .textureSide = 3
    };

    // Amethyst
    _definitions[toBlockId(BlockType::Amethyst)] = {
        .opaque = true,
        .transparent = false,
        .textureTop = 4,
        .textureBottom = 4,
        .textureSide = 4
    };
}


const BlockDefinition& BlockRegistry::get(BlockId id) const noexcept {
    /*
     * IDs should always come from BlockType.
     *
     * We still guard against an invalid ID so malformed world data
     * cannot cause an out-of-bounds access.
     */
    if (id >= BLOCK_TYPE_COUNT)
        return _definitions[0];

    return _definitions[id];
}


const BlockDefinition& BlockRegistry::get(BlockType type) const noexcept {
    return get(toBlockId(type));
}


bool BlockRegistry::isAir(BlockId id) const noexcept {
    return id == AIR_BLOCK;
}


bool BlockRegistry::isOpaque(BlockId id) const noexcept {
    return get(id).opaque;
}


std::uint16_t BlockRegistry::getTexture(BlockId id, std::uint8_t face) const noexcept {
    const BlockDefinition& block = get(id);

    /*
     * Face convention:
     *
     * 0 = +X
     * 1 = -X
     * 2 = +Y
     * 3 = -Y
     * 4 = +Z
     * 5 = -Z
     */

    switch (face)
    {
        case 2: return block.textureTop;
        case 3: return block.textureBottom;
        default: return block.textureSide;
    }
}