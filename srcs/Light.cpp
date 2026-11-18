#include "Light.hpp"

#include <algorithm>
#include <cstring>
#include <memory>
#include <vector>

namespace {

constexpr int WIDTH = 3 * ChunkSection::SIZE;      // the window, in blocks (48)
constexpr int SECTIONS = ChunkColumn::SECTION_COUNT;

struct Props {
	bool opaque = false;
	std::uint8_t filter = 0;
	std::uint8_t emission = 0;
};

// Work arrays reused by each worker thread. Cells are stored per block column,
// y fastest: index = (z * WIDTH + x) * top + y, for y < top; everything above
// `top` is open sky with no block light.
struct Scratch {
	std::vector<std::uint8_t> sky;
	std::vector<std::uint8_t> block;
	std::vector<std::uint32_t> queue;
	std::array<int, WIDTH * WIDTH> litFrom{};       // lowest y lit straight from the sky
	std::array<int, WIDTH * WIDTH> limit{};         // y below which neighbours may need light from here
};
thread_local Scratch scratch;

// 4096 copies of `id`: what a uniform section reads as, so the lighting loops
// index every section the same way. Built once, read-only after.
const BlockId* uniformBlocks(const BlockId id) noexcept {
	static const auto table = [] {
		auto t = std::make_unique<std::array<ChunkSection::Blocks, BLOCK_TYPE_COUNT>>();
		for (std::size_t b = 0; b < BLOCK_TYPE_COUNT; ++b)
			(*t)[b].fill(static_cast<BlockId>(b));
		return t;
	}();
	return (*table)[id].data();
}

class Lighter {
public:
	Lighter(const ColumnWindow& window, const BlockRegistry& registry) {
		for (std::size_t id = 0; id < BLOCK_TYPE_COUNT; ++id) {
			const BlockDefinition& def = registry.get(static_cast<BlockId>(id));
			_props[id] = {def.opaque, def.lightFilter, def.lightEmission};
			if (def.lightEmission > 0)
				_firstEmitter = std::min(_firstEmitter, static_cast<BlockId>(id));
		}
		// Per section: all air? any light block? Both from one pass over the ids
		// as 64-bit words (four ids each), a loop compilers vectorise: adding
		// 0x8000 - firstEmitter to each 16-bit lane sets the lane's top bit
		// exactly when its id >= firstEmitter (ids are far below 0x8000, so
		// no carry crosses lanes). Every light block has an id >= firstEmitter;
		// sections with one get scanned for the real emitters.
		const std::uint64_t lanes = 0x0001000100010001ull;
		const std::uint64_t bias = (0x8000ull - _firstEmitter) * lanes;
		const std::uint64_t topBits = 0x8000ull * lanes;
		int highest = -1;
		for (std::size_t c = 0; c < 9; ++c)
			for (int s = 0; s < SECTIONS; ++s) {
				const ChunkSection& section = window[c]->section(s);
				std::uint64_t any = 0, emitters = 0;
				if (section.isUniform()) {               // the same answers from the one id
					const BlockId id = section.uniformBlock();
					_blocks[c][static_cast<std::size_t>(s)] = uniformBlocks(id);
					any = id;
					emitters = id >= _firstEmitter ? 1 : 0;
				} else {
					const auto& blocks = *section.dense();
					_blocks[c][static_cast<std::size_t>(s)] = blocks.data();
					for (std::size_t i = 0; i < blocks.size(); i += 4) {
						std::uint64_t word;
						std::memcpy(&word, blocks.data() + i, sizeof(word));
						any |= word;
						emitters |= (word + bias) & topBits;
					}
				}
				_empty[c][static_cast<std::size_t>(s)] = any == 0;
				_hasEmitter[c][static_cast<std::size_t>(s)] = emitters != 0;
				_anyEmitter = _anyEmitter || emitters != 0;
				if (any != 0)
					highest = std::max(highest, s);
			}
		// Light reaches 15 above the highest block: one more section.
		_top = std::min(ChunkColumn::HEIGHT, (highest + 2) * ChunkSection::SIZE);
	}

	void run() {
		const std::size_t cells = static_cast<std::size_t>(WIDTH * WIDTH) * static_cast<std::size_t>(_top);
		scratch.sky.assign(cells, 0);
		scratch.block.assign(_anyEmitter ? cells : 0, 0);        // no light block nearby: all 0
		if (_top > 0) {
			skyFromAbove();
			seedSkySpread();
			spread(scratch.sky);
			if (_anyEmitter) {
				seedEmitters();
				spread(scratch.block);
			}
		}
	}

	[[nodiscard]] int top() const noexcept { return _top; }

	// The cells of block column (x, z) from y (contiguous, `count` of them).
	void copyColumn(const int x, const int z, const int y, const int count, std::uint8_t* out, const int stride) const noexcept {
		const std::size_t at = index(x, z, y);
		for (int i = 0; i < count; ++i)
			out[i * stride] = static_cast<std::uint8_t>((scratch.sky[at + static_cast<std::size_t>(i)] << 4)
			                                          | (_anyEmitter ? scratch.block[at + static_cast<std::size_t>(i)] : 0));
	}

	[[nodiscard]] std::uint8_t packedAt(const int x, const int z, const int y) const noexcept {
		if (y >= _top)
			return ColumnLight::FULL_SKY;
		const std::size_t i = index(x, z, y);
		return static_cast<std::uint8_t>((scratch.sky[i] << 4) | (_anyEmitter ? scratch.block[i] : 0));
	}

private:
	[[nodiscard]] std::size_t index(const int x, const int z, const int y) const noexcept {
		return static_cast<std::size_t>((z * WIDTH + x) * _top + y);
	}

	[[nodiscard]] static std::uint32_t pack(const int x, const int z, const int y) noexcept {
		return static_cast<std::uint32_t>(x | (z << 6) | (y << 12));
	}

	[[nodiscard]] BlockId blockAt(const int x, const int z, const int y) const noexcept {
		const auto c = static_cast<std::size_t>((z >> 4) * 3 + (x >> 4));
		return _blocks[c][static_cast<std::size_t>(y >> 4)][(x & 15) + ((z & 15) << 4) + ((y & 15) << 8)];
	}

	[[nodiscard]] bool sectionEmpty(const int x, const int z, const int s) const noexcept {
		return _empty[static_cast<std::size_t>((z >> 4) * 3 + (x >> 4))][static_cast<std::size_t>(s)];
	}

	// Straight down each block column: 15 until the first opaque block,
	// minus the filter of every water / leaves block passed.
	void skyFromAbove() {
		for (int z = 0; z < WIDTH; ++z)
			for (int x = 0; x < WIDTH; ++x) {
				const std::size_t column = index(x, z, 0);
				int level = 15;
				int y = _top - 1;
				int firstFilter = -1;
				while (y >= 0) {
					const int s = y >> 4;
					if (sectionEmpty(x, z, s)) {                 // all air: the whole slice at once
						const int bottom = s * ChunkSection::SIZE;
						std::fill(scratch.sky.begin() + static_cast<std::ptrdiff_t>(column + static_cast<std::size_t>(bottom)),
						          scratch.sky.begin() + static_cast<std::ptrdiff_t>(column + static_cast<std::size_t>(y) + 1),
						          static_cast<std::uint8_t>(level));
						y = bottom - 1;
						continue;
					}
					const Props& p = _props[blockAt(x, z, y)];
					if (p.opaque)
						break;
					if (p.filter > 0) {
						level -= p.filter;
						if (firstFilter < 0)
							firstFilter = y;
						if (level <= 0)
							break;
					}
					scratch.sky[column + static_cast<std::size_t>(y)] = static_cast<std::uint8_t>(level);
					--y;
				}
				const auto cell = static_cast<std::size_t>(z * WIDTH + x);
				scratch.litFrom[cell] = y + 1;
				scratch.limit[cell] = std::max(y + 1, firstFilter + 1);
			}
	}

	// Sideways spreading only starts where a neighbouring block column is lit
	// less from above at the same height: under overhangs, beside cliffs, at
	// the edges of water and leaves.
	void seedSkySpread() {
		scratch.queue.clear();
		static constexpr int DX[4] = {1, -1, 0, 0};
		static constexpr int DZ[4] = {0, 0, 1, -1};
		for (int z = 0; z < WIDTH; ++z)
			for (int x = 0; x < WIDTH; ++x) {
				const int from = scratch.litFrom[static_cast<std::size_t>(z * WIDTH + x)];
				for (int d = 0; d < 4; ++d) {
					const int nx = x + DX[d], nz = z + DZ[d];
					if (nx < 0 || nx >= WIDTH || nz < 0 || nz >= WIDTH)
						continue;
					const int to = std::min(scratch.limit[static_cast<std::size_t>(nz * WIDTH + nx)], _top);
					for (int y = from; y < to; ++y) {
						// Only where this neighbour is open and darker than this cell can give.
						const std::uint8_t here = scratch.sky[index(x, z, y)];
						if (scratch.sky[index(nx, nz, y)] + 1 < here && !_props[blockAt(nx, nz, y)].opaque)
							scratch.queue.push_back(pack(x, z, y));
					}
				}
			}
	}

	void seedEmitters() {
		scratch.queue.clear();
		for (int cz = 0; cz < 3; ++cz)
			for (int cx = 0; cx < 3; ++cx)
				for (int s = 0; s < SECTIONS && s * ChunkSection::SIZE < _top; ++s) {
					const auto c = static_cast<std::size_t>(cz * 3 + cx);
					if (!_hasEmitter[c][static_cast<std::size_t>(s)])
						continue;                                // no light block in this section
					const BlockId* blocks = _blocks[c][static_cast<std::size_t>(s)];
					for (int i = 0; i < ChunkSection::BLOCK_COUNT; ++i) {
						const std::uint8_t emission = _props[blocks[i]].emission;
						if (emission == 0)
							continue;
						const int x = cx * 16 + (i & 15), z = cz * 16 + ((i >> 4) & 15), y = s * 16 + (i >> 8);
						const std::size_t at = index(x, z, y);
						if (emission > scratch.block[at]) {
							scratch.block[at] = emission;
							scratch.queue.push_back(pack(x, z, y));
						}
					}
				}
	}

	// Breadth-first: each cell passes level - 1 - filter to non-opaque neighbours.
	void spread(std::vector<std::uint8_t>& level) {
		for (std::size_t head = 0; head < scratch.queue.size(); ++head) {
			const std::uint32_t packed = scratch.queue[head];
			const int x = static_cast<int>(packed & 63u), z = static_cast<int>((packed >> 6) & 63u);
			const int y = static_cast<int>(packed >> 12);
			const int here = level[index(x, z, y)];
			if (here <= 1)
				continue;
			const auto visit = [&](const int nx, const int nz, const int ny) {
				const Props& p = _props[blockAt(nx, nz, ny)];
				if (p.opaque)
					return;
				const int next = here - 1 - p.filter;
				const std::size_t n = index(nx, nz, ny);
				if (next > level[n]) {
					level[n] = static_cast<std::uint8_t>(next);
					scratch.queue.push_back(pack(nx, nz, ny));
				}
			};
			if (x + 1 < WIDTH) visit(x + 1, z, y);
			if (x > 0)         visit(x - 1, z, y);
			if (z + 1 < WIDTH) visit(x, z + 1, y);
			if (z > 0)         visit(x, z - 1, y);
			if (y + 1 < _top)  visit(x, z, y + 1);
			if (y > 0)         visit(x, z, y - 1);
		}
	}

	std::array<Props, BLOCK_TYPE_COUNT> _props{};
	BlockId _firstEmitter = static_cast<BlockId>(BLOCK_TYPE_COUNT);
	std::array<std::array<const BlockId*, SECTIONS>, 9> _blocks{};
	std::array<std::array<bool, SECTIONS>, 9> _empty{};
	std::array<std::array<bool, SECTIONS>, 9> _hasEmitter{};
	bool _anyEmitter = false;
	int _top = 0;
};

} // namespace

std::uint8_t ColumnLight::at(const int x, const int storageY, const int z) const noexcept {
	const Section& s = _sections[static_cast<std::size_t>(storageY / ChunkSection::SIZE)];
	if (!s.values)
		return s.uniform;
	return (*s.values)[static_cast<std::size_t>(x + (z << 4) + ((storageY % ChunkSection::SIZE) << 8))];
}

void ColumnLight::copyRow(const int x, const int storageY, const int z, const int count, std::uint8_t* out) const noexcept {
	assert(x >= 0 && count >= 0 && x + count <= ChunkSection::SIZE);
	const Section& s = _sections[static_cast<std::size_t>(storageY / ChunkSection::SIZE)];
	if (!s.values)
		std::fill_n(out, count, s.uniform);
	else
		std::copy_n(s.values->data() + x + (z << 4) + ((storageY % ChunkSection::SIZE) << 8), count, out);
}

std::size_t ColumnLight::heapBytes() const noexcept {
	std::size_t bytes = 0;
	for (const Section& s : _sections)
		bytes += s.values ? sizeof(*s.values) : 0;
	return bytes;
}

std::shared_ptr<const ColumnLight> computeLight(const ColumnWindow& window, const BlockRegistry& registry) {
	Lighter lighter(window, registry);
	auto light = std::make_shared<ColumnLight>();
	lighter.run();

	// Keep the centre column; a section with one value everywhere stores just that.
	constexpr int CENTRE = ChunkSection::SIZE;
	for (int s = 0; s < SECTIONS; ++s) {
		ColumnLight::Section& section = light->_sections[static_cast<std::size_t>(s)];
		const int base = s * ChunkSection::SIZE;
		if (base >= lighter.top()) {
			section.uniform = ColumnLight::FULL_SKY;
			continue;
		}
		const std::uint8_t first = lighter.packedAt(CENTRE, CENTRE, base);
		bool uniform = true;
		for (int z = 0; z < ChunkSection::SIZE && uniform; ++z)
			for (int x = 0; x < ChunkSection::SIZE && uniform; ++x)
				for (int y = 0; y < ChunkSection::SIZE && uniform; ++y)
					uniform = lighter.packedAt(CENTRE + x, CENTRE + z, base + y) == first;
		if (uniform) {
			section.uniform = first;
			continue;
		}
		auto values = std::make_unique<std::array<std::uint8_t, ChunkSection::BLOCK_COUNT>>();
		for (int z = 0; z < ChunkSection::SIZE; ++z)
			for (int x = 0; x < ChunkSection::SIZE; ++x)          // y is the stride-256 axis of a section
				lighter.copyColumn(CENTRE + x, CENTRE + z, base, ChunkSection::SIZE,
				                   values->data() + x + (z << 4), ChunkSection::SIZE * ChunkSection::SIZE);
		section.values = std::move(values);
	}
	return light;
}
