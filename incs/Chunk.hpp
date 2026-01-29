#ifndef CHUNK_HPP
#define CHUNK_HPP

#include "defines.hpp"
#include "Voxel.hpp"

#include <atomic>

// Define a chunk as a 1D vector of voxels
class Chunk {
public:
    struct RenderBatch {
        GLuint vao = 0;
        GLuint vbo = 0;
        GLuint ibo = 0;
        uint32_t indexCount = 0;
    };

    struct ChunkRenderData {
        RenderBatch opaque;
        RenderBatch transparent;
    } renderData;

    Chunk() = default;
    ~Chunk() = default;
    Chunk(const Chunk&);
    Chunk& operator=(const Chunk&);

    void markMeshDirty() { isMeshDirty = true; }

    void setVoxel(const int x, const int y, const int z, const Voxel voxel) {
        if (static_cast<unsigned>(x) >= WIDTH || static_cast<unsigned>(y) >= HEIGHT || static_cast<unsigned>(z) >= DEPTH)
            return;
        voxels[x + y * WIDTH + z * WIDTH * HEIGHT] = voxel;
        isMeshDirty = true;
    }

    void setVoxelSilent(const int x, const int y, const int z, const Voxel voxel) {
        if (static_cast<unsigned>(x) >= WIDTH || static_cast<unsigned>(y) >= HEIGHT || static_cast<unsigned>(z) >= DEPTH)
            return;
        voxels[x + y * WIDTH + z * WIDTH * HEIGHT] = voxel;
    }

    [[nodiscard]] Voxel getVoxel(const int x, const int y, const int z) const {
        if (static_cast<unsigned>(x) >= WIDTH || static_cast<unsigned>(y) >= HEIGHT || static_cast<unsigned>(z) >= DEPTH)
            return 0;
        return voxels[x + y * WIDTH + z * WIDTH * HEIGHT];
    }

    [[nodiscard]] bool isBlockActive(const int x, const int y, const int z) const {
        return isActive(getVoxel(x, y, z));
    }

    [[nodiscard]] auto 		getVoxels() const { return voxels; }

    // Define the dimensions of a chunk
    static constexpr uint8_t WIDTH = 16;
    static constexpr uint16_t HEIGHT = 256;
    static constexpr uint8_t DEPTH = 16;
    static constexpr uint32_t SIZE = WIDTH * HEIGHT * DEPTH;

    std::vector<Vertex>   cachedOpaqueVertices;
    std::vector<Vertex>   cachedTransparentVertices;
    std::vector<uint32_t> cachedOpaqueIndices;
    std::vector<uint32_t> cachedTransparentIndices;

    bool isMeshDirty = false;
    std::atomic<bool> aoCalculated = false;
    glm::vec3 worldMax{};
    glm::vec3 worldMin{};

private:
    std::array<Voxel, SIZE> voxels{};
};

inline Chunk::Chunk(const Chunk& other)
    : renderData(other.renderData),
      cachedOpaqueVertices(other.cachedOpaqueVertices),
      cachedTransparentVertices(other.cachedTransparentVertices),
      cachedOpaqueIndices(other.cachedOpaqueIndices),
      cachedTransparentIndices(other.cachedTransparentIndices),
      isMeshDirty(other.isMeshDirty),
      aoCalculated(other.aoCalculated.load()),
      worldMax(other.worldMax),
      worldMin(other.worldMin),
      voxels(other.voxels)
{
}

inline Chunk& Chunk::operator=(const Chunk& other)
{
    if (this == &other)
        return *this;

    renderData = other.renderData;
    cachedOpaqueVertices = other.cachedOpaqueVertices;
    cachedTransparentVertices = other.cachedTransparentVertices;
    cachedOpaqueIndices = other.cachedOpaqueIndices;
    cachedTransparentIndices = other.cachedTransparentIndices;
    isMeshDirty = other.isMeshDirty;
    aoCalculated.store(other.aoCalculated.load());
    worldMax = other.worldMax;
    worldMin = other.worldMin;
    voxels = other.voxels;

    return *this;
}

#endif // CHUNK_HPP
