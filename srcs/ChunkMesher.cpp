#include "ChunkMesher.hpp"

#include <algorithm>

void ChunkMesher::build(
    const ChunkSection& section,
    const int sectionY,
    const ChunkNeighborhood& neighborhood,
    const BlockRegistry& registry,
    MeshData& output,
    MeshData& transparentOutput
) {
    output.clear();
    transparentOutput.clear();

    if (std::all_of(section.blocks().begin(), section.blocks().end(), [](const BlockId block) {
        return block == AIR_BLOCK;
    }))
        return;

    constexpr int size = ChunkSection::SIZE;
    constexpr int empty = -1;
    constexpr std::uint8_t nonUniformAo = 0xFF;
    constexpr std::array<NormalDir, 6> directions = {
        NormalDir::PositiveX, NormalDir::NegativeX,
        NormalDir::PositiveY, NormalDir::NegativeY,
        NormalDir::PositiveZ, NormalDir::NegativeZ
    };
    constexpr std::array<std::array<int, 2>, 3> faceAxes = {{{1, 2}, {0, 2}, {0, 1}}};
    constexpr std::array<std::array<std::array<int, 2>, 4>, 6> cornerUv = {{
        {{{0, 0}, {1, 0}, {1, 1}, {0, 1}}},
        {{{0, 1}, {1, 1}, {1, 0}, {0, 0}}},
        {{{0, 1}, {1, 1}, {1, 0}, {0, 0}}},
        {{{0, 0}, {1, 0}, {1, 1}, {0, 1}}},
        {{{1, 0}, {1, 1}, {0, 1}, {0, 0}}},
        {{{0, 0}, {0, 1}, {1, 1}, {1, 0}}}
    }};
    std::array<int, size * size> mask{};
    std::array<std::uint8_t, size * size> maskAo{};
    std::array<std::array<std::uint8_t, 4>, size * size> maskCornerAo{};

    output.reserve(2048, 3072);
    transparentOutput.reserve(512, 768);

    for (int axis = 0; axis < 3; ++axis) {
        const int uAxis = faceAxes[axis][0];
        const int vAxis = faceAxes[axis][1];

        for (int directionIndex = axis * 2; directionIndex < axis * 2 + 2; ++directionIndex) {
            const NormalDir direction = directions[directionIndex];
            const int sign = directionIndex % 2 == 0 ? 1 : -1;

            for (int slice = 0; slice < size; ++slice) {
                mask.fill(empty);
                maskAo.fill(nonUniformAo);

                for (int v = 0; v < size; ++v) {
                    for (int u = 0; u < size; ++u) {
                        std::array<int, 3> position{};
                        position[axis] = slice;
                        position[uAxis] = u;
                        position[vAxis] = v;

                        const BlockId block = section.getUnchecked(position[0], position[1], position[2]);
                        if (block == AIR_BLOCK)
                            continue;

                        const BlockDefinition& definition = registry.get(block);
                        std::array<int, 3> neighborPosition = position;
                        neighborPosition[axis] += sign;
                        const BlockId neighbor = neighborhood.getBlock(
                            neighborPosition[0],
                            neighborPosition[1] + sectionY * size,
                            neighborPosition[2]
                        );

                        if ((definition.transparent && neighbor == block) || registry.isOpaque(neighbor))
                            continue;

                        const std::uint16_t texture = axis == 1
                            ? (sign > 0 ? definition.textureTop : definition.textureBottom)
                            : definition.textureSide;
                        std::array<std::uint8_t, 4> faceAo{};
                        for (std::size_t corner = 0; corner < faceAo.size(); ++corner) {
                            std::array<int, 3> sideOne = position;
                            std::array<int, 3> sideTwo = position;
                            std::array<int, 3> diagonal = position;
                            sideOne[axis] += sign;
                            sideTwo[axis] += sign;
                            diagonal[axis] += sign;

                            const int uSign = cornerUv[directionIndex][corner][0] == 0 ? -1 : 1;
                            const int vSign = cornerUv[directionIndex][corner][1] == 0 ? -1 : 1;
                            sideOne[uAxis] += uSign;
                            sideTwo[vAxis] += vSign;
                            diagonal[uAxis] += uSign;
                            diagonal[vAxis] += vSign;

                            const int worldY = sectionY * size;
                            const bool sideOneOpaque = registry.isOpaque(neighborhood.getBlock(
                                sideOne[0], sideOne[1] + worldY, sideOne[2]));
                            const bool sideTwoOpaque = registry.isOpaque(neighborhood.getBlock(
                                sideTwo[0], sideTwo[1] + worldY, sideTwo[2]));
                            const bool diagonalOpaque = registry.isOpaque(neighborhood.getBlock(
                                diagonal[0], diagonal[1] + worldY, diagonal[2]));
                            faceAo[corner] = sideOneOpaque && sideTwoOpaque
                                ? 0
                                : static_cast<std::uint8_t>(3 - static_cast<int>(sideOneOpaque)
                                    - static_cast<int>(sideTwoOpaque) - static_cast<int>(diagonalOpaque));
                        }

                        const bool uniformAo = std::all_of(faceAo.begin() + 1, faceAo.end(),
                            [&faceAo](const std::uint8_t value) { return value == faceAo[0]; });
                        const std::size_t maskIndex = static_cast<std::size_t>(v * size + u);
                        mask[maskIndex] = static_cast<int>(texture)
                            | (definition.transparent ? 0x40 : 0);
                        maskAo[maskIndex] = uniformAo ? faceAo[0] : nonUniformAo;
                        maskCornerAo[maskIndex] = faceAo;
                    }
                }

                for (int v = 0; v < size; ++v) {
                    for (int u = 0; u < size;) {
                        const std::size_t faceIndex = static_cast<std::size_t>(v * size + u);
                        const int key = mask[faceIndex];
                        if (key == empty) {
                            ++u;
                            continue;
                        }

                        int width = 1;
                        const std::uint8_t aoKey = maskAo[faceIndex];
                        const auto& faceAo = maskCornerAo[faceIndex];
                        while (aoKey != nonUniformAo && u + width < size
                            && mask[v * size + u + width] == key
                            && maskAo[v * size + u + width] == aoKey)
                            ++width;

                        int height = 1;
                        bool canExtend = aoKey != nonUniformAo;
                        while (v + height < size && canExtend) {
                            for (int offset = 0; offset < width; ++offset) {
                                const std::size_t candidateIndex = static_cast<std::size_t>((v + height) * size + u + offset);
                                if (mask[candidateIndex] != key || maskAo[candidateIndex] != aoKey) {
                                    canExtend = false;
                                    break;
                                }
                            }
                            if (canExtend)
                                ++height;
                        }

                        for (int row = 0; row < height; ++row)
                            for (int column = 0; column < width; ++column)
                                mask[(v + row) * size + u + column] = empty;

                        std::array<int, 3> minimum{};
                        std::array<int, 3> maximum{};
                        minimum[axis] = slice + (sign > 0 ? 1 : 0);
                        maximum[axis] = minimum[axis];
                        minimum[uAxis] = u;
                        maximum[uAxis] = u + width;
                        minimum[vAxis] = v;
                        maximum[vAxis] = v + height;

                        MeshData& target = (key & 0x40) != 0 ? transparentOutput : output;
                        emitQuad(
                            target,
                            minimum[0], minimum[1], minimum[2],
                            maximum[0], maximum[1], maximum[2],
                            direction,
                            static_cast<std::uint16_t>(key & 0x3F),
                            faceAo
                        );
                        u += width;
                    }
                }
            }
        }
    }
}

void ChunkMesher::emitVertex(
    MeshData& mesh,
    std::uint8_t x,
    std::uint16_t y,
    std::uint8_t z,
    NormalDir normal,
    std::uint16_t texture,
    std::uint8_t ao,
    std::uint8_t corner
) {
    /*
     * Texture IDs are currently limited to 6 bits by the packed
     * vertex format.
     */
    const auto textureIndex = static_cast<std::uint8_t>(texture & 0x3F);

    mesh.vertices.push_back(packVertex(x, y, z, normal, textureIndex, ao, corner));
}

void ChunkMesher::emitQuad(
    MeshData& mesh,
    const int minX,
    const int minY,
    const int minZ,
    const int maxX,
    const int maxY,
    const int maxZ,
    NormalDir normal,
    std::uint16_t texture,
    const std::array<std::uint8_t, 4>& ao
) {
    const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
    using VertexPosition = std::array<std::uint8_t, 3>;
    const auto vertex = [](const int x, const int y, const int z) {
        return VertexPosition{
            static_cast<std::uint8_t>(x),
            static_cast<std::uint8_t>(y),
            static_cast<std::uint8_t>(z)
        };
    };
    std::array<VertexPosition, 4> corners{};

    switch (normal) {
        case NormalDir::PositiveX:
            corners = {vertex(maxX, minY, minZ), vertex(maxX, maxY, minZ), vertex(maxX, maxY, maxZ), vertex(maxX, minY, maxZ)};
            break;
        case NormalDir::NegativeX:
            corners = {vertex(minX, minY, maxZ), vertex(minX, maxY, maxZ), vertex(minX, maxY, minZ), vertex(minX, minY, minZ)};
            break;
        case NormalDir::PositiveY:
            corners = {vertex(minX, maxY, maxZ), vertex(maxX, maxY, maxZ), vertex(maxX, maxY, minZ), vertex(minX, maxY, minZ)};
            break;
        case NormalDir::NegativeY:
            corners = {vertex(minX, minY, minZ), vertex(maxX, minY, minZ), vertex(maxX, minY, maxZ), vertex(minX, minY, maxZ)};
            break;
        case NormalDir::PositiveZ:
            corners = {vertex(maxX, minY, maxZ), vertex(maxX, maxY, maxZ), vertex(minX, maxY, maxZ), vertex(minX, minY, maxZ)};
            break;
        case NormalDir::NegativeZ:
            corners = {vertex(minX, minY, minZ), vertex(minX, maxY, minZ), vertex(maxX, maxY, minZ), vertex(maxX, minY, minZ)};
            break;
    }

    for (std::size_t corner = 0; corner < corners.size(); ++corner) {
        const auto& position = corners[corner];
        emitVertex(
            mesh,
            position[0], position[1], position[2],
            normal, texture, ao[corner], static_cast<std::uint8_t>(corner)
        );
    }

    /*
     * Two triangles:
     *
     * 0 ---- 1
     * |    / |
     * |  /   |
     * 3 ---- 2
     *
     * Winding:
     *
     * 0 1 2
     * 2 3 0
     */
    if (ao[0] + ao[2] > ao[1] + ao[3]) {
        mesh.indices.push_back(base + 0);
        mesh.indices.push_back(base + 1);
        mesh.indices.push_back(base + 3);
        mesh.indices.push_back(base + 1);
        mesh.indices.push_back(base + 2);
        mesh.indices.push_back(base + 3);
    } else {
        mesh.indices.push_back(base + 0);
        mesh.indices.push_back(base + 1);
        mesh.indices.push_back(base + 2);
        mesh.indices.push_back(base + 2);
        mesh.indices.push_back(base + 3);
        mesh.indices.push_back(base + 0);
    }
}
