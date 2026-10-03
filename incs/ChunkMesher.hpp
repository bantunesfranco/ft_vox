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
    static void emitFace(
        MeshData& mesh,
        int x,
        int y,
        int z,
        NormalDir normal,
        std::uint16_t texture
    );

    static void emitVertex(
        MeshData &mesh,
        uint8_t x,
        uint8_t y,
        uint8_t z,
        NormalDir normal,
        uint16_t texture,
        uint8_t ao, uint8_t corner
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
