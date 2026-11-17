#ifndef Block_HPP
#define Block_HPP

#include <array>
#include <cstddef>
#include <cstdint>

enum class BlockType : std::uint8_t {
    Air = 0,
    Grass,
    Dirt,
    Stone,
    Sand,
    Amethyst,
    Water,
    Snow,
    Bedrock,
    // Ids are saved in worlds: only ever append.
    OakLog,
    BirchLog,
    SpruceLog,
    OakLeaves,
    BirchLeaves,
    SpruceLeaves,
    Gravel,
    Clay,
    Cactus,
    Sandstone,
    Ice,
    Lava,
    Glowstone,
    Torch,

    Count
};


using BlockId = std::uint16_t;

constexpr BlockId toBlockId(BlockType type) noexcept {
    return static_cast<BlockId>(type);
}

constexpr BlockType toBlockType(BlockId id) noexcept {
    return static_cast<BlockType>(id);
}

constexpr BlockId AIR_BLOCK = toBlockId(BlockType::Air);
constexpr BlockId WATER_BLOCK = toBlockId(BlockType::Water);
constexpr BlockId LAVA_BLOCK = toBlockId(BlockType::Lava);

// How a block is drawn: a full cube, or a torch (a thin stick).
enum class BlockModel : std::uint8_t { Cube, Torch };

constexpr std::size_t BLOCK_TYPE_COUNT = static_cast<std::size_t>(BlockType::Count);


struct BlockDefinition {
    bool opaque = true;        // hides the faces of its neighbours (and casts AO)
    bool transparent = false;  // drawn in the blended pass (water, ice)
    bool solid = true;         // the player collides with it

    // Texture-array layer indices.
    //
    // These are NOT OpenGL texture handles.
    std::uint16_t textureTop = 0;
    std::uint16_t textureBottom = 0;
    std::uint16_t textureSide = 0;

    // Fog while the camera's eye is inside this block: the world fades into
    // `fogColor` (0xRRGGBB) over `fogDistance` blocks; 0 = no fog.
    // Water: blue over 96 blocks (lava, once added: red over ~1.5).
    std::uint32_t fogColor = 0;
    float fogDistance = 0.0f;

    // Light (0..15): what the block gives off, and how many extra levels light
    // loses passing through it (water and leaves dim it; opaque blocks stop it).
    std::uint8_t lightEmission = 0;
    std::uint8_t lightFilter = 0;

    BlockModel model = BlockModel::Cube;
};


class BlockRegistry {
public:
    BlockRegistry() noexcept;

    [[nodiscard]]
    const BlockDefinition& get(BlockId id) const noexcept;

    [[nodiscard]]
    const BlockDefinition& get(BlockType type) const noexcept;

    [[nodiscard]]
    bool isOpaque(BlockId id) const noexcept;

    [[nodiscard]]
    std::uint16_t getTexture(BlockId id, std::uint8_t face ) const noexcept;

private:
    std::array<BlockDefinition, BLOCK_TYPE_COUNT> _definitions{};
};

// Whether `block` may be placed on top of `below`: a torch needs a solid,
// opaque block under it; everything else can go anywhere.
[[nodiscard]] bool placementAllowed(const BlockRegistry& registry, BlockId block, BlockId below) noexcept;

#endif
