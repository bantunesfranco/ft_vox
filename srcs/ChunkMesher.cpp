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
    constexpr std::uint16_t nonUniformLight = 0xFFFF;
    std::array<std::uint16_t, size * size> maskLight{};
    std::array<std::array<std::uint8_t, 4>, size * size> maskCornerLight{};

    /*
     * The section plus a one-block border, copied once into flat arrays.
     * Every query below (the face neighbor, the AO corners) is at most one
     * block outside the section, so after this no lookup has to find which
     * column it falls in. Coordinates are section-local, -1..16.
     */
    constexpr int padded = size + 2;
    thread_local std::array<BlockId, padded * padded * padded> blocks{};
    thread_local std::array<std::uint8_t, padded * padded * padded> opaque{};
    thread_local std::array<std::uint8_t, padded * padded * padded> light{};   // (sky << 4) | block
    const auto cell = [](const int x, const int y, const int z) noexcept {
        return static_cast<std::size_t>((x + 1) + (z + 1) * padded + (y + 1) * padded * padded);
    };
    const int baseY = sectionY * size;
    // Buried: the section and every border cell sharing a face with it are
    // opaque, so no face can be visible (deep stone). Border edges and
    // corners only matter for AO of visible faces, so they are not checked.
    bool buried = true;
    for (int y = -1; y <= size; ++y) {
        for (int z = -1; z <= size; ++z) {
            const bool interiorRow = y >= 0 && y < size && z >= 0 && z < size;
            for (int x = -1; x <= size; ++x) {
                const bool interior = interiorRow && x >= 0 && x < size;
                const BlockId block = interior
                    ? section.getUnchecked(x, y, z)
                    : neighborhood.getBlock(x, y + baseY, z);
                const bool isOpaque = registry.isOpaque(block);
                blocks[cell(x, y, z)] = block;
                opaque[cell(x, y, z)] = isOpaque ? 1 : 0;
                light[cell(x, y, z)] = neighborhood.getLight(x, y + baseY, z);

                const int outside = (x < 0 || x >= size) + (y < 0 || y >= size) + (z < 0 || z >= size);
                if (!isOpaque && outside <= 1)
                    buried = false;
            }
        }
    }
    if (buried)
        return;
    const auto blockAt  = [&](const std::array<int, 3>& p) { return blocks[cell(p[0], p[1], p[2])]; };
    const auto opaqueAt = [&](const std::array<int, 3>& p) { return opaque[cell(p[0], p[1], p[2])] != 0; };
    const auto lightAt  = [&](const std::array<int, 3>& p) { return light[cell(p[0], p[1], p[2])]; };

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

                        const BlockId block = blockAt(position);
                        if (block == AIR_BLOCK)
                            continue;

                        std::array<int, 3> neighborPosition = position;
                        neighborPosition[axis] += sign;
                        if (opaqueAt(neighborPosition))
                            continue;                       // hidden face: the common case
                        const BlockDefinition& definition = registry.get(block);
                        if (definition.transparent && blockAt(neighborPosition) == block)
                            continue;

                        const std::uint16_t texture = axis == 1
                            ? (sign > 0 ? definition.textureTop : definition.textureBottom)
                            : definition.textureSide;
                        std::array<std::uint8_t, 4> faceAo{};
                        std::array<std::uint8_t, 4> faceLight{};
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

                            const bool sideOneOpaque = opaqueAt(sideOne);
                            const bool sideTwoOpaque = opaqueAt(sideTwo);
                            const bool diagonalOpaque = opaqueAt(diagonal);
                            faceAo[corner] = sideOneOpaque && sideTwoOpaque
                                ? 0
                                : static_cast<std::uint8_t>(3 - static_cast<int>(sideOneOpaque)
                                    - static_cast<int>(sideTwoOpaque) - static_cast<int>(diagonalOpaque));

                            // Smooth light: the average of the open cells among the
                            // four in front of this corner (as Minecraft does).
                            int skySum = 0, blockSum = 0, samples = 0;
                            const auto add = [&](const std::array<int, 3>& p) {
                                const std::uint8_t packed = lightAt(p);
                                skySum += packed >> 4;
                                blockSum += packed & 0x0F;
                                ++samples;
                            };
                            add(neighborPosition);
                            if (!sideOneOpaque)
                                add(sideOne);
                            if (!sideTwoOpaque)
                                add(sideTwo);
                            if (!diagonalOpaque && !(sideOneOpaque && sideTwoOpaque))
                                add(diagonal);
                            const int blockLight = std::max(blockSum / samples, static_cast<int>(definition.lightEmission));
                            faceLight[corner] = static_cast<std::uint8_t>(((skySum / samples) << 4) | blockLight);
                        }

                        const bool uniformAo = std::all_of(faceAo.begin() + 1, faceAo.end(),
                            [&faceAo](const std::uint8_t value) { return value == faceAo[0]; });
                        const std::size_t maskIndex = static_cast<std::size_t>(v * size + u);
                        mask[maskIndex] = static_cast<int>(texture)
                            | (definition.transparent ? 0x40 : 0);
                        maskAo[maskIndex] = uniformAo ? faceAo[0] : nonUniformAo;
                        maskCornerAo[maskIndex] = faceAo;
                        const bool uniformLight = std::all_of(faceLight.begin() + 1, faceLight.end(),
                            [&faceLight](const std::uint8_t value) { return value == faceLight[0]; });
                        maskLight[maskIndex] = uniformLight ? faceLight[0] : nonUniformLight;
                        maskCornerLight[maskIndex] = faceLight;
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

                        // Faces merge only with the same texture, AO and light,
                        // each uniform across the face.
                        int width = 1;
                        const std::uint8_t aoKey = maskAo[faceIndex];
                        const std::uint16_t lightKey = maskLight[faceIndex];
                        const auto& faceAo = maskCornerAo[faceIndex];
                        const auto& faceLight = maskCornerLight[faceIndex];
                        const bool mergeable = aoKey != nonUniformAo && lightKey != nonUniformLight;
                        while (mergeable && u + width < size
                            && mask[v * size + u + width] == key
                            && maskAo[v * size + u + width] == aoKey
                            && maskLight[v * size + u + width] == lightKey)
                            ++width;

                        int height = 1;
                        bool canExtend = mergeable;
                        while (v + height < size && canExtend) {
                            for (int offset = 0; offset < width; ++offset) {
                                const std::size_t candidateIndex = static_cast<std::size_t>((v + height) * size + u + offset);
                                if (mask[candidateIndex] != key || maskAo[candidateIndex] != aoKey
                                    || maskLight[candidateIndex] != lightKey) {
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
                            faceAo,
                            faceLight
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
    std::uint8_t corner,
    std::uint8_t light
) {
    /*
     * Texture IDs are currently limited to 6 bits by the packed
     * vertex format.
     */
    const auto textureIndex = static_cast<std::uint8_t>(texture & 0x3F);

    mesh.vertices.push_back(packVertex(x, y, z, normal, textureIndex, ao, corner,
        static_cast<std::uint8_t>(light >> 4), static_cast<std::uint8_t>(light & 0x0F)));
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
    const std::array<std::uint8_t, 4>& ao,
    const std::array<std::uint8_t, 4>& light
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
            normal, texture, ao[corner], static_cast<std::uint8_t>(corner),
            light[corner]
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
