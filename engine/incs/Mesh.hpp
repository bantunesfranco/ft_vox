//
// Created by bruno on 30/09/2026.
//

#ifndef MESH_HPP
#define MESH_HPP

#include <cstdint>
#include <vector>

using PackedVertex = std::uint32_t;

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
 * Section vertices can exist at coordinate 16 because a block at
 * local coordinate 15 occupies [15, 16].
 *
 * Therefore X/Y/Z each need 5 bits.
 *
 * Layout:
 *
 * bits  0 -  4 : X       5 bits
 * bits  5 -  9 : Y       5 bits
 * bits 10 - 14 : Z       5 bits
 * bits 15 - 17 : Normal  3 bits
 * bits 18 - 23 : Texture 6 bits
 * bits 24 - 25 : AO      2 bits
 * bits 26 - 31 : corner
 */
constexpr PackedVertex packVertex(
    std::uint8_t x,
    std::uint8_t y,
    std::uint8_t z,
    NormalDir normal,
    std::uint8_t texture,
    std::uint8_t ao,
    std::uint8_t corner
) noexcept
{
    return
        (static_cast<PackedVertex>(x) & 0x1Fu) |
        ((static_cast<PackedVertex>(y) & 0x1Fu) << 5u) |
        ((static_cast<PackedVertex>(z) & 0x1Fu) << 10u) |
        ((static_cast<PackedVertex>(normal) & 0x07u) << 15u) |
        ((static_cast<PackedVertex>(texture) & 0x3Fu) << 18u) |
        ((static_cast<PackedVertex>(ao) & 0x03u) << 24u) |
        ((static_cast<PackedVertex>(corner) & 0x03u) << 26u);
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
