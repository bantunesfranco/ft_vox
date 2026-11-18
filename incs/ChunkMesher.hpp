#ifndef CHUNK_MESHER_HPP
#define CHUNK_MESHER_HPP

#include "Block.hpp"
#include "Chunk.hpp"
#include "Mesh.hpp"
#include "Renderer.hpp"
#include "glad/glad.h"

class ChunkMesher {
public:
    static void build(
        const ChunkSection& section,
        int sectionY,
        const ChunkNeighborhood& neighborhood,
        const BlockRegistry& registry,
        MeshData& output,
        MeshData& transparentOutput
    );

private:
    static void emitQuad(
        MeshData& mesh,
        int minX,
        int minY,
        int minZ,
        int maxX,
        int maxY,
        int maxZ,
        NormalDir normal,
        std::uint16_t texture,
        const std::array<std::uint8_t, 4>& ao,
        const std::array<std::uint8_t, 4>& light      // (sky << 4) | block per corner
    );

    // Faces of emitBox, as flags.
    static constexpr unsigned POSITIVE_X = 1u, NEGATIVE_X = 2u, TOP = 4u, BOTTOM = 8u, POSITIVE_Z = 16u, NEGATIVE_Z = 32u;
    static constexpr unsigned X_SIDES = POSITIVE_X | NEGATIVE_X, Z_SIDES = POSITIVE_Z | NEGATIVE_Z;
    static constexpr unsigned SIDES = X_SIDES | Z_SIDES;
    struct Textures {
        std::uint16_t side;
        std::uint16_t top;
        std::uint16_t bottom;
    };
    static void emitBox(MeshData& mesh, int x, int y, int z, std::array<std::uint8_t, 3> lo,
                        std::array<std::uint8_t, 3> hi, unsigned faces, const Textures& textures, std::uint8_t light);

    static void emitVertex(
        MeshData &mesh,
        uint8_t x,
        uint16_t y,
        uint8_t z,
        NormalDir normal,
        uint16_t texture,
        uint8_t ao, uint8_t corner,
        uint8_t light
    );
};


struct ChunkRenderState
{
    ChunkCoord coord{};
    std::uint8_t sectionY = 0;

    glm::vec3 worldPosition{0.0f};
    glm::vec3 worldMin{0.0f};
    glm::vec3 worldMax{0.0f};

    MeshData mesh{};
    GpuMesh gpuMesh{};
    MeshData transparentMesh{};
    GpuMesh transparentGpuMesh{};
};

#endif // CHUNK_MESHER_HPP
