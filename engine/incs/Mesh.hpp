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
 * Chunk vertices can exist at Y=256 because the world column is
 * 256 blocks tall. X/Z remain section-sized and can reach 16.
 *
 * Layout:
 *
 * bits  0 -  4 : X       5 bits
 * bits  5 - 13 : Y       9 bits
 * bits 14 - 18 : Z       5 bits
 * bits 19 - 21 : Normal  3 bits
 * bits 22 - 27 : Texture 6 bits
 * bits 28 - 29 : AO      2 bits
 * bits 30 - 31 : corner  2 bits
 */
constexpr PackedVertex packVertex(
    std::uint8_t x,
    std::uint16_t y,
    std::uint8_t z,
    NormalDir normal,
    std::uint8_t texture,
    std::uint8_t ao,
    std::uint8_t corner
) noexcept
{
    return
        (static_cast<PackedVertex>(x) & 0x1Fu) |
        ((static_cast<PackedVertex>(y) & 0x1FFu) << 5u) |
        ((static_cast<PackedVertex>(z) & 0x1Fu) << 14u) |
        ((static_cast<PackedVertex>(normal) & 0x07u) << 19u) |
        ((static_cast<PackedVertex>(texture) & 0x3Fu) << 22u) |
        ((static_cast<PackedVertex>(ao) & 0x03u) << 28u) |
        ((static_cast<PackedVertex>(corner) & 0x03u) << 30u);
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
