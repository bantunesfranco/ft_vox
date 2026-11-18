#ifndef CHUNK_HPP
#define CHUNK_HPP

#include <cstdint>
#include <cstddef>
#include <algorithm>
#include <array>
#include <cassert>
#include <functional>
#include <memory>

#include "Block.hpp"


struct ChunkCoord {
    std::int32_t x = 0;
    std::int32_t z = 0;

    constexpr bool operator==(const ChunkCoord&) const noexcept = default;
};

template<>
struct std::hash<ChunkCoord> {
    std::size_t operator()(const ChunkCoord& coord) const noexcept
    {
        std::uint64_t x = static_cast<std::uint32_t>(coord.x);

        std::uint64_t z = static_cast<std::uint32_t>(coord.z);

        /* 64-bit hash combining */
        std::uint64_t h = x * 0x9E3779B185EBCA87ULL;

        h ^= z + 0x9E3779B97F4A7C15ULL + (h << 6) + (h >> 2);

        return static_cast<std::size_t>(h);
    }
};


class ChunkSection {
public:
    static constexpr int SIZE = 16;
    static constexpr int BLOCK_COUNT = SIZE * SIZE * SIZE;

    using Blocks = std::array<BlockId, BLOCK_COUNT>;

    ChunkSection() noexcept = default;

    // Copies are deep; moves hand the dense array over.
    ChunkSection(const ChunkSection& other);
    ChunkSection& operator=(const ChunkSection& other);
    ChunkSection(ChunkSection&&) noexcept = default;
    ChunkSection& operator=(ChunkSection&&) noexcept = default;

    [[nodiscard]]
    BlockId get(int x, int y, int z) const noexcept;

    // A uniform section turns dense on its first differing write.
    void set(int x, int y, int z, BlockId block) noexcept;

    [[nodiscard]]
    BlockId getUnchecked(int x, int y, int z) const noexcept {
        return _dense ? (*_dense)[static_cast<std::size_t>(index(x, y, z))] : _uniform;
    }

    // Copies the `count` blocks of row (y, z) from x on into `out`.
    void copyRow(const int x, const int y, const int z, const int count, BlockId* out) const noexcept {
        assert(x >= 0 && count >= 0 && x + count <= SIZE);
        if (_dense)
            std::copy_n(_dense->data() + index(x, y, z), count, out);
        else
            std::fill_n(out, count, _uniform);
    }

    void setUnchecked(int x, int y, int z, BlockId block) noexcept {
        if (!_dense) {
            if (block == _uniform)
                return;
            allocateDense();
        }
        (*_dense)[static_cast<std::size_t>(index(x, y, z))] = block;
    }

    // Makes the section uniform `block`, freeing the dense array.
    void fill(BlockId block) noexcept;

    // Collapses the section to uniform if every block is the same.
    void compact() noexcept;

    [[nodiscard]]
    bool isUniform() const noexcept { return !_dense; }

    // The id of every block while uniform (meaningless while dense).
    [[nodiscard]]
    BlockId uniformBlock() const noexcept { return _uniform; }

    // The blocks, or nullptr while uniform.
    [[nodiscard]]
    const Blocks* dense() const noexcept { return _dense.get(); }

    // The dense array (allocated, filled with the uniform id, if needed), for
    // bulk writers; call compact() when done.
    Blocks& makeDense();

    // Bytes held on the heap (the dense array, if any).
    [[nodiscard]]
    std::size_t heapBytes() const noexcept;

    // Same blocks, whatever the storage.
    friend bool operator==(const ChunkSection& a, const ChunkSection& b) noexcept;

private:
    /*
     * Layout:
     *
     * x + (z << 4) + (y << 8)
     *
     * This means:
     *
     * x changes fastest
     * z changes next
     * y changes slowest
     *
     * 4096 contiguous uint16_t values, or none while every block is _uniform.
     */
    BlockId _uniform = AIR_BLOCK;
    std::unique_ptr<Blocks> _dense;

    void allocateDense() noexcept;

    [[nodiscard]]
    static constexpr int index(int x, int y, int z) noexcept {
        return x + (z << 4) + (y << 8);
    }
};


class ChunkColumn {
public:
    static constexpr int MIN_WORLD_Y = -64;
    static constexpr int MAX_WORLD_Y = 256;
    static constexpr int SECTION_COUNT = (MAX_WORLD_Y - MIN_WORLD_Y) / ChunkSection::SIZE;
    static constexpr int HEIGHT = SECTION_COUNT * ChunkSection::SIZE;

    [[nodiscard]] static constexpr int toStorageY(const int worldY) noexcept {
        return worldY - MIN_WORLD_Y;
    }

    [[nodiscard]] static constexpr int toWorldY(const int storageY) noexcept {
        return storageY + MIN_WORLD_Y;
    }

    ChunkColumn() noexcept = default;

    explicit ChunkColumn(ChunkCoord coord) noexcept;

    [[nodiscard]]
    const ChunkCoord& coord() const noexcept;

    [[nodiscard]]
    ChunkSection& section(int sectionY) noexcept;

    [[nodiscard]]
    const ChunkSection& section(int sectionY) const noexcept;

    [[nodiscard]]
    BlockId get(int x, int y, int z) const noexcept;

    void set(int x, int y, int z, BlockId block) noexcept;

    [[nodiscard]]
    BlockId getUnchecked(int x, int y, int z) const noexcept;

    void setUnchecked(int x, int y, int z, BlockId block) noexcept;

    // Collapses every section whose blocks are all the same.
    void compact() noexcept;

    // Bytes held by block storage (the dense sections).
    [[nodiscard]]
    std::size_t heapBytes() const noexcept;

private:
    ChunkCoord _coord{};

    std::array<ChunkSection, SECTION_COUNT> _sections{};
};


class ColumnLight;

class ChunkNeighborhood {
public:
    ChunkNeighborhood() noexcept = default;

    explicit ChunkNeighborhood(const ChunkColumn* center) noexcept;

    void setCenter(const ChunkColumn* center) noexcept;

    void setNeighbor(int dx, int dz, const ChunkColumn* chunk) noexcept;

    [[nodiscard]]
    const ChunkColumn* getColumn(int dx, int dz) const noexcept;

    [[nodiscard]]
    BlockId getBlock(int x, int y, int z) const noexcept;

    // getBlock for x0 <= x < x1 of row (y, z) into out[0 .. x1 - x0), and
    // getLight the same way: whole rows, one column lookup per column.
    void copyBlocks(int x0, int x1, int y, int z, BlockId* out) const noexcept;
    void copyLight(int x0, int x1, int y, int z, std::uint8_t* out) const noexcept;

    // Light of the 3x3 columns (null: not computed yet, reads as full sky).
    void setLight(int dx, int dz, const ColumnLight* light) noexcept;

    // Packed (sky << 4) | block at centre-relative x, z (-16..31), storage y;
    // above the world full sky, below it dark.
    [[nodiscard]]
    std::uint8_t getLight(int x, int y, int z) const noexcept;

private:
    /*
     * 3x3 neighborhood:
     *
     *       -X       0       +X
     *
     * -Z    [0]     [1]      [2]
     *  0    [3]     [4]      [5]
     * +Z    [6]     [7]      [8]
     *
     * We only currently need the center and four direct neighbors,
     * but keeping the full 3x3 layout makes future AO/greedy meshing
     * neighborhood queries easier.
     */
    std::array<const ChunkColumn*, 9> _columns{};
    std::array<const ColumnLight*, 9> _lights{};

    [[nodiscard]]
    static constexpr int index(int dx, int dz) noexcept {
        return (dz + 1) * 3 + (dx + 1);
    }
};

#endif // CHUNK_HPP
