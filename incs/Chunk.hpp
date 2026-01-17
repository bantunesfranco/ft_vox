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


    Chunk() {}
    ~Chunk() = default;
    Chunk(const Chunk&) = default;
    Chunk& operator=(const Chunk&) = default;

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
    glm::vec3 worldMax{};
    glm::vec3 worldMin{};

private:
    std::array<Voxel, SIZE> voxels{};
};

#endif // CHUNK_HPP