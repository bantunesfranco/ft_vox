#include "ChunkMesher.hpp"

constexpr std::uint8_t DEFAULT_AO = 3;

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

    /*
     * Worst-case:
     *
     * 4096 blocks
     * 6 faces
     * 4 vertices
     * 6 indices
     *
     * Reserve enough memory so the normal case doesn't constantly
     * reallocate.
     *
     * This is intentionally conservative. Greedy meshing will make
     * this dramatically smaller later.
     */
    output.reserve(4096 * 6 * 4,4096 * 6 * 6);

    for (int y = 0; y < ChunkSection::SIZE; ++y)
    {
        for (int z = 0; z < ChunkSection::SIZE; ++z)
        {
            for (int x = 0; x < ChunkSection::SIZE; ++x)
            {
                const BlockId block = section.getUnchecked(x, y, z);
                const int columnY = sectionY * ChunkSection::SIZE + y;

                /* Air doesn't generate geometry */
                if (block == AIR_BLOCK)
                    continue;

                const BlockDefinition& definition = registry.get(block);
                MeshData& target = definition.transparent ? transparentOutput : output;
                const auto shouldRenderFace = [&](const int neighborX, const int neighborY, const int neighborZ) {
                    const BlockId neighbor = neighborhood.getBlock(neighborX, neighborY, neighborZ);
                    if (definition.transparent && neighbor == block)
                        return false;
                    return !registry.isOpaque(neighbor);
                };

                // +X
                if (shouldRenderFace(x + 1, columnY, z))
                    emitFace(target, x, y, z, NormalDir::PositiveX, definition.textureSide);

                // -X
                if (shouldRenderFace(x - 1, columnY, z))
                    emitFace(target, x, y, z, NormalDir::NegativeX, definition.textureSide);

                // +Y
                if (shouldRenderFace(x, columnY + 1, z))
                    emitFace(target, x, y, z, NormalDir::PositiveY, definition.textureTop);

                // -Y
                if (shouldRenderFace(x, columnY - 1, z))
                    emitFace(target, x, y, z, NormalDir::NegativeY, definition.textureBottom);

                // +Z
                if (shouldRenderFace(x, columnY, z + 1))
                    emitFace(target, x, y, z, NormalDir::PositiveZ, definition.textureSide);

                // -Z
                if (shouldRenderFace(x, columnY, z - 1))
                    emitFace(target, x, y, z, NormalDir::NegativeZ, definition.textureSide);
            }
        }
    }
}

void ChunkMesher::emitVertex(
    MeshData& mesh,
    std::uint8_t x,
    std::uint8_t y,
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

void ChunkMesher::emitFace(
    MeshData& mesh,
    int x,
    int y,
    int z,
    NormalDir normal,
    std::uint16_t texture
) {
    const auto base = static_cast<std::uint32_t>(mesh.vertices.size());

    const auto x0 = static_cast<std::uint8_t>(x);
    const auto y0 = static_cast<std::uint8_t>(y);
    const auto z0 = static_cast<std::uint8_t>(z);
    const auto x1 = static_cast<std::uint8_t>(x + 1);
    const auto y1 = static_cast<std::uint8_t>(y + 1);
    const auto z1 = static_cast<std::uint8_t>(z + 1);

    /*
     * Four corners are emitted in winding order.
     *
     * The exact winding here assumes the renderer uses the same
     * front-face convention as the rest of the engine.
     */

    switch (normal) {
        case NormalDir::PositiveX: {
            emitVertex(mesh, x1, y0, z0, normal, texture, DEFAULT_AO, 0);
            emitVertex(mesh, x1, y1, z0, normal, texture, DEFAULT_AO, 1);
            emitVertex(mesh, x1, y1, z1, normal, texture, DEFAULT_AO, 2);
            emitVertex(mesh, x1, y0, z1, normal, texture, DEFAULT_AO, 3);

            break;
        }

        case NormalDir::NegativeX: {
            emitVertex(mesh, x0, y0, z1, normal, texture, DEFAULT_AO, 0);
            emitVertex(mesh, x0, y1, z1, normal, texture, DEFAULT_AO, 1);
            emitVertex(mesh, x0, y1, z0, normal, texture, DEFAULT_AO, 2);
            emitVertex(mesh, x0, y0, z0, normal, texture, DEFAULT_AO, 3);

            break;
        }

        case NormalDir::PositiveY: {
            emitVertex(mesh, x0, y1, z1, normal, texture, DEFAULT_AO, 0);
            emitVertex(mesh, x1, y1, z1, normal, texture, DEFAULT_AO, 1);
            emitVertex(mesh, x1, y1, z0, normal, texture, DEFAULT_AO, 2);
            emitVertex(mesh, x0, y1, z0, normal, texture, DEFAULT_AO, 3);

            break;
        }

        case NormalDir::NegativeY: {
            emitVertex(mesh, x0, y0, z0, normal, texture, DEFAULT_AO, 0);
            emitVertex(mesh, x0, y0, z1, normal, texture, DEFAULT_AO, 1);
            emitVertex(mesh, x1, y0, z1, normal, texture, DEFAULT_AO, 2);
            emitVertex(mesh, x1, y0, z0, normal, texture, DEFAULT_AO, 3);

            break;
        }

        case NormalDir::PositiveZ: {
            emitVertex(mesh, x1, y0, z1, normal, texture, DEFAULT_AO, 0);
            emitVertex(mesh, x1, y1, z1, normal, texture, DEFAULT_AO, 1);
            emitVertex(mesh, x0, y1, z1, normal, texture, DEFAULT_AO, 2);
            emitVertex(mesh, x0, y0, z1, normal, texture, DEFAULT_AO, 3);

            break;
        }

        case NormalDir::NegativeZ: {
            emitVertex(mesh, x0, y0, z0, normal, texture, DEFAULT_AO, 0);
            emitVertex(mesh, x0, y1, z0, normal, texture, DEFAULT_AO, 1);
            emitVertex(mesh, x1, y1, z0, normal, texture, DEFAULT_AO, 2);
            emitVertex(mesh, x1, y0, z0, normal, texture, DEFAULT_AO, 3);

            break;
        }
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
    mesh.indices.push_back(base + 0);
    mesh.indices.push_back(base + 1);
    mesh.indices.push_back(base + 2);

    mesh.indices.push_back(base + 2);
    mesh.indices.push_back(base + 3);
    mesh.indices.push_back(base + 0);
}
