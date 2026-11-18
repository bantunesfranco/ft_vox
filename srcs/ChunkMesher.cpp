#include "ChunkMesher.hpp"

#include <algorithm>
#include <cassert>
#include <vector>

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

    // All air: no faces (the faces against it belong to the neighbours' blocks).
    const bool allAir = section.isUniform()
        ? section.uniformBlock() == AIR_BLOCK
        : std::ranges::all_of(*section.dense(), [](const BlockId block) { return block == AIR_BLOCK; });
    if (allAir)
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
    // Blocks drawn with their own model (torches), not as cube faces.
    std::array<bool, BLOCK_TYPE_COUNT> modelled{};
    for (std::size_t id = 0; id < BLOCK_TYPE_COUNT; ++id)
        modelled[id] = registry.get(static_cast<BlockId>(id)).model != BlockModel::Cube;
    std::vector<std::array<int, 3>> modelCells;
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
                if (interior && modelled[block])
                    modelCells.push_back({x, y, z});
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

    output.reserve(2048);
    transparentOutput.reserve(512);

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
                        if (modelled[block])
                            continue;                       // drawn below, with its model
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

    for (const auto& [x, y, z] : modelCells) {
        const BlockId block = blocks[cell(x, y, z)];
        const BlockDefinition& definition = registry.get(block);
        const std::uint8_t packed = light[cell(x, y, z)];
        const auto blockLight = static_cast<std::uint8_t>(std::max<int>(packed & 0x0F, definition.lightEmission));
        const auto cellLight = static_cast<std::uint8_t>((packed & 0xF0) | blockLight);
        const Textures textures{definition.textureSide, definition.textureTop, definition.textureBottom};
        if (definition.model == BlockModel::Torch) {
            // A 2 x 10 x 2 stick in the middle of the cell: four sides and a top.
            const auto corner = [](const glm::ivec3& v) {
                return std::array<std::uint8_t, 3>{static_cast<std::uint8_t>(v.x), static_cast<std::uint8_t>(v.y),
                                                   static_cast<std::uint8_t>(v.z)};
            };
            emitBox(output, x, y, z, corner(TORCH_BOX.min), corner(TORCH_BOX.max), SIDES | TOP, textures, cellLight);
        } else if (definition.model == BlockModel::Cactus) {
            // Minecraft's cactus: sides 1/16 inside the cell, full top and
            // bottom, none between two cacti or against an opaque block.
            emitBox(output, x, y, z, {1, 0, 0}, {15, 16, 16}, X_SIDES, textures, cellLight);
            emitBox(output, x, y, z, {0, 0, 1}, {16, 16, 15}, Z_SIDES, textures, cellLight);
            const BlockId above = blocks[cell(x, y + 1, z)], below = blocks[cell(x, y - 1, z)];
            unsigned ends = 0;
            if (above != block && !opaque[cell(x, y + 1, z)])
                ends |= TOP;
            if (below != block && !opaque[cell(x, y - 1, z)])
                ends |= BOTTOM;
            if (ends != 0)
                emitBox(output, x, y, z, {0, 0, 0}, {16, 16, 16}, ends, textures, cellLight);
        }
    }

    // The renderer's shared index buffer covers this many quads (see Mesh.hpp).
    assert(output.quadCount() + transparentOutput.quadCount() <= MAX_QUADS_PER_SECTION);
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

    /*
     * Every quad is drawn with the shared pattern 0 1 2, 2 3 0:
     *
     * 0 ---- 1
     * |    / |
     * |  /   |
     * 3 ---- 2
     *
     * That splits along the 0-2 diagonal. When the AO is brighter along
     * 0-2 the split must run along 1-3 instead (or the shading creases), so
     * the corners go out as 1 2 3 0: the pattern then draws (1 2 3), (3 0 1),
     * the same triangles and winding. Each vertex keeps its own corner number,
     * AO and light.
     */
    const std::size_t first = ao[0] + ao[2] > ao[1] + ao[3] ? 1 : 0;
    for (std::size_t slot = 0; slot < corners.size(); ++slot) {
        const std::size_t corner = (first + slot) % corners.size();
        const auto& position = corners[corner];
        emitVertex(
            mesh,
            position[0], position[1], position[2],
            normal, texture, ao[corner], static_cast<std::uint8_t>(corner),
            light[corner]
        );
    }
}

// Faces of a box inside cell (x, y, z), from `lo` to `hi` in 1/16 block, never
// merged with anything; corners in emitQuad's order (same winding).
void ChunkMesher::emitBox(MeshData& mesh, const int x, const int y, const int z,
                          const std::array<std::uint8_t, 3> lo, const std::array<std::uint8_t, 3> hi,
                          const unsigned faces, const Textures& textures, const std::uint8_t light) {
    using Corner = std::array<std::uint8_t, 3>;
    struct Face {
        unsigned flag;
        NormalDir normal;
        std::array<Corner, 4> corners;
        std::uint16_t texture;
    };
    const std::uint8_t x0 = lo[0], y0 = lo[1], z0 = lo[2], x1 = hi[0], y1 = hi[1], z1 = hi[2];
    const std::array<Face, 6> all = {{
        {POSITIVE_X, NormalDir::PositiveX, {{{x1, y0, z0}, {x1, y1, z0}, {x1, y1, z1}, {x1, y0, z1}}}, textures.side},
        {NEGATIVE_X, NormalDir::NegativeX, {{{x0, y0, z1}, {x0, y1, z1}, {x0, y1, z0}, {x0, y0, z0}}}, textures.side},
        {TOP,        NormalDir::PositiveY, {{{x0, y1, z1}, {x1, y1, z1}, {x1, y1, z0}, {x0, y1, z0}}}, textures.top},
        {BOTTOM,     NormalDir::NegativeY, {{{x0, y0, z0}, {x1, y0, z0}, {x1, y0, z1}, {x0, y0, z1}}}, textures.bottom},
        {POSITIVE_Z, NormalDir::PositiveZ, {{{x1, y0, z1}, {x1, y1, z1}, {x0, y1, z1}, {x0, y0, z1}}}, textures.side},
        {NEGATIVE_Z, NormalDir::NegativeZ, {{{x0, y0, z0}, {x0, y1, z0}, {x1, y1, z0}, {x1, y0, z0}}}, textures.side},
    }};
    const auto sky = static_cast<std::uint8_t>(light >> 4);
    const auto block = static_cast<std::uint8_t>(light & 0x0F);
    for (const Face& face : all) {
        if ((faces & face.flag) == 0)
            continue;
        for (std::size_t corner = 0; corner < face.corners.size(); ++corner) {
            const Corner& s = face.corners[corner];
            mesh.vertices.push_back(packVertex(
                static_cast<std::uint8_t>(x), static_cast<std::uint16_t>(y), static_cast<std::uint8_t>(z),
                face.normal, static_cast<std::uint8_t>(face.texture & 0x3F), 3, static_cast<std::uint8_t>(corner),
                sky, block, s[0], s[1], s[2]));
        }
    }
}
