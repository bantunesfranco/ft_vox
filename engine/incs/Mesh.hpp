//
// Created by bruno on 30/09/2026.
//

#ifndef MESH_HPP
#define MESH_HPP

#include <cstdint>
#include <vector>

// Two 32-bit words per vertex (see packVertex).
struct PackedVertex {
    std::uint32_t a;
    std::uint32_t b;
};

enum class NormalDir : std::uint8_t
{
    PositiveX = 0,
    NegativeX = 1,

    PositiveY = 2,
    NegativeY = 3,

    PositiveZ = 4,
    NegativeZ = 5
};

/*
 * Word a:
 *   bits  0 -  4 : X       5 bits   (block corner, 0..16)
 *   bits  5 - 13 : Y       9 bits
 *   bits 14 - 18 : Z       5 bits
 *   bits 19 - 21 : Normal  3 bits
 *   bits 22 - 27 : Texture 6 bits
 *   bits 28 - 29 : AO      2 bits
 *   bits 30 - 31 : corner  2 bits
 * Word b:
 *   bits  0 -  3 : sky light    0..15
 *   bits  4 -  7 : block light  0..15
 *   bits  8 - 12 : sub-x   (each in 1/16 block, added to the position:
 *   bits 13 - 17 : sub-y    used by torches, 0 for cubes)
 *   bits 18 - 22 : sub-z
 */
constexpr PackedVertex packVertex(
    std::uint8_t x,
    std::uint16_t y,
    std::uint8_t z,
    NormalDir normal,
    std::uint8_t texture,
    std::uint8_t ao,
    std::uint8_t corner,
    std::uint8_t sky = 15,
    std::uint8_t block = 0,
    std::uint8_t subX = 0,
    std::uint8_t subY = 0,
    std::uint8_t subZ = 0
) noexcept
{
    return PackedVertex{
        (static_cast<std::uint32_t>(x) & 0x1Fu) |
        ((static_cast<std::uint32_t>(y) & 0x1FFu) << 5u) |
        ((static_cast<std::uint32_t>(z) & 0x1Fu) << 14u) |
        ((static_cast<std::uint32_t>(normal) & 0x07u) << 19u) |
        ((static_cast<std::uint32_t>(texture) & 0x3Fu) << 22u) |
        ((static_cast<std::uint32_t>(ao) & 0x03u) << 28u) |
        ((static_cast<std::uint32_t>(corner) & 0x03u) << 30u),
        (static_cast<std::uint32_t>(sky) & 0x0Fu) |
        ((static_cast<std::uint32_t>(block) & 0x0Fu) << 4u) |
        ((static_cast<std::uint32_t>(subX) & 0x1Fu) << 8u) |
        ((static_cast<std::uint32_t>(subY) & 0x1Fu) << 13u) |
        ((static_cast<std::uint32_t>(subZ) & 0x1Fu) << 18u)
    };
}


struct MeshData
{
    std::vector<PackedVertex> vertices;
    std::vector<std::uint32_t> indices;

    void clear() noexcept {
        vertices.clear();
        indices.clear();
    }

    void reserve(std::size_t vertexCount, std::size_t indexCount) {
        vertices.reserve(vertexCount);
        indices.reserve(indexCount);
    }

    [[nodiscard]]
    bool empty() const noexcept {
        return vertices.empty();
    }
};


#endif // MESH_HPP
