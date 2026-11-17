#include "Block.hpp"
#include "BlockTextures.hpp"

BlockRegistry::BlockRegistry() noexcept {
    /*
     * Texture indices select sprite rectangles in the block atlas.
     *
     * Change these numbers to match your sprite-rectangle table.
     *
     * Example:
     *   0 = grass top
     *   1 = dirt
     *   2 = grass side
     *   3 = stone
     *   4 = sand
     *   5 = amethyst
     *   6 = water
     *   7 = snow
     */

    // Air
    _definitions[toBlockId(BlockType::Air)] = {
        .opaque = false,
        .transparent = true,
        .solid = false,
        .textureTop = 0,
        .textureBottom = 0,
        .textureSide = 0,
        .replaceable = true
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
        .textureTop = 3,
        .textureBottom = 3,
        .textureSide = 3
    };

    // Sand
    _definitions[toBlockId(BlockType::Sand)] = {
        .opaque = true,
        .transparent = false,
        .textureTop = 4,
        .textureBottom = 4,
        .textureSide = 4
    };

    // Amethyst
    _definitions[toBlockId(BlockType::Amethyst)] = {
        .opaque = true,
        .transparent = false,
        .textureTop = 5,
        .textureBottom = 5,
        .textureSide = 5
    };

    _definitions[toBlockId(BlockType::Water)] = {
        .opaque = false,
        .transparent = true,
        .solid = false,
        .textureTop = 6,
        .textureBottom = 6,
        .textureSide = 6,
        .fogColor = 0x1A4AB4,         // deep blue
        .fogDistance = 96.0f,
        .lightFilter = 1,
        .replaceable = true
    };

    _definitions[toBlockId(BlockType::Snow)] = {
        .opaque = true,
        .transparent = false,
        .textureTop = 7,
        .textureBottom = 7,
        .textureSide = 7
    };

    _definitions[toBlockId(BlockType::Bedrock)] = {
        .opaque = true,
        .transparent = false,
        .textureTop = 8,
        .textureBottom = 8,
        .textureSide = 8
    };

    // Generated textures (BlockTextures), not from the atlas image.
    using BlockTextures::Texture;
    using BlockTextures::index;
    const auto cube = [this](const BlockType type, const Texture top, const Texture bottom, const Texture side) {
        _definitions[toBlockId(type)] = {
            .opaque = true, .transparent = false, .solid = true,
            .textureTop = index(top), .textureBottom = index(bottom), .textureSide = index(side)
        };
    };
    cube(BlockType::OakLog,    Texture::OakLogTop,    Texture::OakLogTop,    Texture::OakLogSide);
    cube(BlockType::BirchLog,  Texture::BirchLogTop,  Texture::BirchLogTop,  Texture::BirchLogSide);
    cube(BlockType::SpruceLog, Texture::SpruceLogTop, Texture::SpruceLogTop, Texture::SpruceLogSide);
    cube(BlockType::Gravel,    Texture::Gravel,       Texture::Gravel,       Texture::Gravel);
    cube(BlockType::Clay,      Texture::Clay,         Texture::Clay,         Texture::Clay);
    cube(BlockType::Sandstone, Texture::SandstoneTop, Texture::SandstoneBottom, Texture::SandstoneSide);

    // Leaves: solid, but see-through holes (alpha cut-out in the opaque pass),
    // so they hide nothing behind them.
    const auto leaves = [this](const BlockType type, const Texture texture) {
        _definitions[toBlockId(type)] = {
            .opaque = false, .transparent = false, .solid = true,
            .textureTop = index(texture), .textureBottom = index(texture), .textureSide = index(texture),
            .lightFilter = 1
        };
    };
    leaves(BlockType::OakLeaves,    Texture::OakLeaves);
    leaves(BlockType::BirchLeaves,  Texture::BirchLeaves);
    leaves(BlockType::SpruceLeaves, Texture::SpruceLeaves);

    // Ice: solid, blended like water.
    _definitions[toBlockId(BlockType::Ice)] = {
        .opaque = false, .transparent = true, .solid = true,
        .textureTop = index(Texture::Ice), .textureBottom = index(Texture::Ice), .textureSide = index(Texture::Ice)
    };

    // Lava: not walked on, but drawn like a solid cube (not see-through);
    // glows, and tints the view red when the eye is inside it.
    _definitions[LAVA_BLOCK] = {
        .opaque = true, .transparent = false, .solid = false,
        .textureTop = index(Texture::LavaStill), .textureBottom = index(Texture::LavaStill),
        .textureSide = index(Texture::LavaStill),
        .fogColor = 0xC8400A, .fogDistance = 1.5f,
        .lightEmission = 15,
        .replaceable = true
    };
    _definitions[toBlockId(BlockType::Glowstone)] = {
        .opaque = true, .transparent = false, .solid = true,
        .textureTop = index(Texture::Glowstone), .textureBottom = index(Texture::Glowstone),
        .textureSide = index(Texture::Glowstone),
        .lightEmission = 15
    };
    // Cactus: sides 1/16 inside the cell (Minecraft's shape), so it hides
    // nothing; full top and bottom.
    _definitions[toBlockId(BlockType::Cactus)] = {
        .opaque = false, .transparent = false, .solid = true,
        .textureTop = index(Texture::CactusTop), .textureBottom = index(Texture::CactusTop),
        .textureSide = index(Texture::CactusSide),
        .model = BlockModel::Cactus,
        .collisionInset = 1
    };
    // Torch: a thin stick, walked through, lets light pass.
    _definitions[toBlockId(BlockType::Torch)] = {
        .opaque = false, .transparent = false, .solid = false,
        .textureTop = index(Texture::Torch), .textureBottom = index(Texture::Torch),
        .textureSide = index(Texture::Torch),
        .lightEmission = 14,
        .model = BlockModel::Torch
    };
}

BlockBox outlineBox(const BlockDefinition& definition) noexcept {
    if (definition.model == BlockModel::Torch)
        return TORCH_BOX;
    const int inset = definition.collisionInset;
    return {{inset, 0, inset}, {16 - inset, 16, 16 - inset}};
}

bool placementAllowed(const BlockRegistry& registry, const BlockId block, const BlockId below) noexcept {
    if (registry.get(block).model != BlockModel::Torch)
        return true;
    const BlockDefinition& ground = registry.get(below);
    return ground.solid && ground.opaque;
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
