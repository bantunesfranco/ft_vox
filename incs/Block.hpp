#ifndef Block_HPP
#define Block_HPP

#include <array>
#include <cstdint>

#include "glad/glad.h"

enum class BlockType : std::uint8_t {
    Air = 0,
    Grass,
    Dirt,
    Stone,
    Sand,
    Amethyst,
    Water,
    Snow,

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

constexpr std::size_t BLOCK_TYPE_COUNT = static_cast<std::size_t>(BlockType::Count);


struct BlockDefinition {
    bool opaque = true;
    bool transparent = false;

    // Texture-array layer indices.
    //
    // These are NOT OpenGL texture handles.
    std::uint16_t textureTop = 0;
    std::uint16_t textureBottom = 0;
    std::uint16_t textureSide = 0;
};


class BlockRegistry {
public:
    BlockRegistry() noexcept;

    [[nodiscard]]
    const BlockDefinition& get(BlockId id) const noexcept;

    [[nodiscard]]
    const BlockDefinition& get(BlockType type) const noexcept;

    [[nodiscard]]
    bool isAir(BlockId id) const noexcept;

    [[nodiscard]]
    bool isOpaque(BlockId id) const noexcept;

    [[nodiscard]]
    std::uint16_t getTexture(BlockId id, std::uint8_t face ) const noexcept;

private:
    std::array<BlockDefinition, BLOCK_TYPE_COUNT> _definitions{};
};


class BlockTextureArray {
public:
    void load();

    [[nodiscard]]
    GLuint handle() const noexcept;

private:
    GLuint _texture = 0;
};

#endif
