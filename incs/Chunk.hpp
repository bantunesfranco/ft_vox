#ifndef CHUNK_HPP
#define CHUNK_HPP

#include "defines.hpp"
#include "Voxel.hpp"

// Define a chunk as a 1D vector of voxels
class Chunk {
public:
    struct ChunkRenderData {
        GLuint vao = 0;
        GLuint vbo = 0;
        GLuint ibo = 0;
        uint32_t indexCount = 0;
    } renderData;

    Chunk() = default;
    ~Chunk() = default;
    Chunk(const Chunk&);
    Chunk& operator=(const Chunk&);

    void setVoxel(const int x, const int y, const int z, const Voxel voxel) {
        if (static_cast<unsigned>(x) >= WIDTH || static_cast<unsigned>(y) >= HEIGHT || static_cast<unsigned>(z) >= DEPTH)
            return;
        voxels[x + y * WIDTH + z * WIDTH * HEIGHT] = voxel;
        isMeshDirty = true;
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
    static constexpr uint16_t HEIGHT = 128;
    static constexpr uint8_t DEPTH = 16;
    static constexpr uint32_t SIZE = WIDTH * HEIGHT * DEPTH;

    std::vector<Vertex> cachedVertices;
    std::vector<uint32_t> cachedIndices;
    bool isMeshDirty = true;
    std::atomic<bool> aoCalculated = false;
    glm::vec3 worldMax{};
    glm::vec3 worldMin{};

private:
    std::array<Voxel, SIZE> voxels{};
};

inline Chunk::Chunk(const Chunk& other)
    : renderData(other.renderData),
      cachedVertices(other.cachedVertices),
      cachedIndices(other.cachedIndices),
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
    cachedVertices = other.cachedVertices;
    cachedIndices = other.cachedIndices;
    isMeshDirty = other.isMeshDirty;
    aoCalculated.store(other.aoCalculated.load());
    worldMax = other.worldMax;
    worldMin = other.worldMin;
    voxels = other.voxels;

    return *this;
}

#endif // CHUNK_HPP
