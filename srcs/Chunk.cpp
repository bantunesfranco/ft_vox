#include "Chunk.hpp"
#include "Light.hpp"

#include <algorithm>

BlockId ChunkSection::get(int x, int y, int z) const noexcept {
    assert(x >= 0 && x < SIZE);
    assert(y >= 0 && y < SIZE);
    assert(z >= 0 && z < SIZE);

    return getUnchecked(x, y, z);
}

void ChunkSection::set(int x, int y, int z, BlockId block) noexcept {
    assert(x >= 0 && x < SIZE);
    assert(y >= 0 && y < SIZE);
    assert(z >= 0 && z < SIZE);

    setUnchecked(x, y, z, block);
}

void ChunkSection::fill(BlockId block) noexcept {
    _dense.reset();
    _uniform = block;
}

ChunkSection::ChunkSection(const ChunkSection& other)
    : _uniform(other._uniform),
      _dense(other._dense ? std::make_unique<Blocks>(*other._dense) : nullptr) {}

ChunkSection& ChunkSection::operator=(const ChunkSection& other) {
    if (this != &other) {
        _uniform = other._uniform;
        if (!other._dense)
            _dense.reset();
        else if (_dense)
            *_dense = *other._dense;
        else
            _dense = std::make_unique<Blocks>(*other._dense);
    }
    return *this;
}

void ChunkSection::allocateDense() noexcept {
    _dense = std::make_unique_for_overwrite<Blocks>();
    _dense->fill(_uniform);
}

ChunkSection::Blocks& ChunkSection::makeDense() {
    if (!_dense)
        allocateDense();
    return *_dense;
}

void ChunkSection::compact() noexcept {
    if (!_dense)
        return;
    const BlockId first = (*_dense)[0];
    if (std::ranges::all_of(*_dense, [first](const BlockId b) { return b == first; }))
        fill(first);
}

std::size_t ChunkSection::heapBytes() const noexcept {
    return _dense ? sizeof(Blocks) : 0;
}

bool operator==(const ChunkSection& a, const ChunkSection& b) noexcept {
    if (!a._dense && !b._dense)
        return a._uniform == b._uniform;
    if (a._dense && b._dense)
        return *a._dense == *b._dense;
    const ChunkSection& uniform = a._dense ? b : a;
    const ChunkSection& dense = a._dense ? a : b;
    return std::ranges::all_of(*dense._dense, [&uniform](const BlockId x) { return x == uniform._uniform; });
}


ChunkColumn::ChunkColumn(ChunkCoord coord) noexcept : _coord(coord) {}

void ChunkColumn::compact() noexcept {
    for (ChunkSection& section : _sections)
        section.compact();
}

std::size_t ChunkColumn::heapBytes() const noexcept {
    std::size_t bytes = 0;
    for (const ChunkSection& section : _sections)
        bytes += section.heapBytes();
    return bytes;
}

const ChunkCoord& ChunkColumn::coord() const noexcept {
    return _coord;
}

ChunkSection& ChunkColumn::section(int sectionY) noexcept {
    assert(sectionY >= 0);
    assert(sectionY < SECTION_COUNT);

    return _sections[sectionY];
}


const ChunkSection& ChunkColumn::section(int sectionY) const noexcept {
    assert(sectionY >= 0);
    assert(sectionY < SECTION_COUNT);

    return _sections[sectionY];
}


BlockId ChunkColumn::get(int x, int y, int z) const noexcept {
    assert(x >= 0 && x < 16);
    assert(y >= 0 && y < HEIGHT);
    assert(z >= 0 && z < 16);

    return getUnchecked(x, y, z);
}


void ChunkColumn::set(int x, int y, int z, BlockId block) noexcept {
    assert(x >= 0 && x < 16);
    assert(y >= 0 && y < HEIGHT);
    assert(z >= 0 && z < 16);

    setUnchecked(x, y, z, block);
}


BlockId ChunkColumn::getUnchecked(int x, int y, int z) const noexcept {
    const int sectionY = y >> 4;
    const int localY = y & 15;

    return _sections[sectionY].getUnchecked(x, localY, z);
}


void ChunkColumn::setUnchecked(int x, int y, int z, BlockId block) noexcept {
    const int sectionY = y >> 4;
    const int localY = y & 15;

    _sections[sectionY].setUnchecked(x, localY, z, block);
}


ChunkNeighborhood::ChunkNeighborhood(const ChunkColumn* center) noexcept {
    setCenter(center);
}


void ChunkNeighborhood::setCenter(const ChunkColumn* center) noexcept {
    _columns.fill(nullptr);

    _columns[index(0, 0)] = center;
}


void ChunkNeighborhood::setNeighbor(int dx, int dz, const ChunkColumn* chunk) noexcept {
    if (dx < -1 || dx > 1) return;
    if (dz < -1 || dz > 1) return;

    _columns[index(dx, dz)] = chunk;
}


const ChunkColumn* ChunkNeighborhood::getColumn(int dx, int dz) const noexcept {
    if (dx < -1 || dx > 1) return nullptr;
    if (dz < -1 || dz > 1) return nullptr;

    return _columns[index(dx, dz)];
}


BlockId ChunkNeighborhood::getBlock(int x, int y, int z) const noexcept {
    /*
     * Outside the vertical world.
     */
    if (y < 0 || y >= ChunkColumn::HEIGHT)
        return AIR_BLOCK;

    /*
     * Determine which chunk column contains the requested block.
     */
    int dx = 0;
    int dz = 0;

    int localX = x;
    int localZ = z;

    if (localX < 0)
    {
        localX += ChunkSection::SIZE;
        dx = -1;
    }
    else if (localX >= ChunkSection::SIZE)
    {
        localX -= ChunkSection::SIZE;
        dx = 1;
    }

    if (localZ < 0)
    {
        localZ += ChunkSection::SIZE;
        dz = -1;
    }
    else if (localZ >= ChunkSection::SIZE)
    {
        localZ -= ChunkSection::SIZE;
        dz = 1;
    }

    const ChunkColumn* column = getColumn(dx, dz);

    /*
     * A missing neighboring chunk is NOT automatically treated as
     * a solid block.
     *
     * For the initial streaming implementation we treat it as AIR,
     * which means the boundary face is emitted.
     *
     * Later ChunkManager can avoid meshing a chunk until its required
     * neighbors exist.
     */
    if (column == nullptr)
        return AIR_BLOCK;

    return column->getUnchecked(localX, y, localZ);
}

void ChunkNeighborhood::copyBlocks(int x0, const int x1, const int y, const int z, BlockId* out) const noexcept {
    assert(x0 >= -ChunkSection::SIZE && x0 <= x1 && x1 <= 2 * ChunkSection::SIZE);
    assert(z >= -ChunkSection::SIZE && z < 2 * ChunkSection::SIZE);
    if (y < 0 || y >= ChunkColumn::HEIGHT) {
        std::fill_n(out, x1 - x0, AIR_BLOCK);
        return;
    }
    const int dz = z < 0 ? -1 : (z >= ChunkSection::SIZE ? 1 : 0);
    while (x0 < x1) {
        const int dx = x0 < 0 ? -1 : (x0 >= ChunkSection::SIZE ? 1 : 0);
        const int end = std::min(x1, (dx + 1) * ChunkSection::SIZE);
        const ChunkColumn* column = _columns[static_cast<std::size_t>(index(dx, dz))];
        if (column == nullptr)
            std::fill_n(out, end - x0, AIR_BLOCK);   // as getBlock: a missing column reads as air
        else
            column->section(y >> 4).copyRow(x0 - dx * ChunkSection::SIZE, y & 15, z - dz * ChunkSection::SIZE,
                                            end - x0, out);
        out += end - x0;
        x0 = end;
    }
}

void ChunkNeighborhood::copyLight(int x0, const int x1, const int y, const int z, std::uint8_t* out) const noexcept {
    assert(x0 >= -ChunkSection::SIZE && x0 <= x1 && x1 <= 2 * ChunkSection::SIZE);
    assert(z >= -ChunkSection::SIZE && z < 2 * ChunkSection::SIZE);
    if (y < 0 || y >= ChunkColumn::HEIGHT) {
        std::fill_n(out, x1 - x0, y < 0 ? std::uint8_t{0} : ColumnLight::FULL_SKY);
        return;
    }
    const int dz = z < 0 ? -1 : (z >= ChunkSection::SIZE ? 1 : 0);
    while (x0 < x1) {
        const int dx = x0 < 0 ? -1 : (x0 >= ChunkSection::SIZE ? 1 : 0);
        const int end = std::min(x1, (dx + 1) * ChunkSection::SIZE);
        const ColumnLight* light = _lights[static_cast<std::size_t>(index(dx, dz))];
        if (light == nullptr)
            std::fill_n(out, end - x0, ColumnLight::FULL_SKY);
        else
            light->copyRow(x0 - dx * ChunkSection::SIZE, y, z - dz * ChunkSection::SIZE, end - x0, out);
        out += end - x0;
        x0 = end;
    }
}

void ChunkNeighborhood::setLight(const int dx, const int dz, const ColumnLight* light) noexcept {
    if (dx < -1 || dx > 1 || dz < -1 || dz > 1)
        return;
    _lights[static_cast<std::size_t>(index(dx, dz))] = light;
}

std::uint8_t ChunkNeighborhood::getLight(const int x, const int y, const int z) const noexcept {
    if (y >= ChunkColumn::HEIGHT)
        return ColumnLight::FULL_SKY;
    if (y < 0)
        return 0;
    const int dx = x < 0 ? -1 : (x >= ChunkSection::SIZE ? 1 : 0);
    const int dz = z < 0 ? -1 : (z >= ChunkSection::SIZE ? 1 : 0);
    const ColumnLight* light = _lights[static_cast<std::size_t>(index(dx, dz))];
    if (!light)
        return ColumnLight::FULL_SKY;
    return light->at(x - dx * ChunkSection::SIZE, y, z - dz * ChunkSection::SIZE);
}
