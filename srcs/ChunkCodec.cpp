#include "ChunkCodec.hpp"

#include <algorithm>

static_assert(ChunkColumn::SECTION_COUNT <= 32, "sectionMask is a u32");
static_assert(ChunkSection::BLOCK_COUNT <= 0xFFFF, "run lengths and counts are u16");

namespace ChunkCodec {

void encode(const ChunkColumn& column, net::ByteWriter& out) {
	std::uint32_t mask = 0;
	for (int s = 0; s < ChunkColumn::SECTION_COUNT; ++s) {
		const auto& blocks = column.section(s).blocks();
		if (std::ranges::any_of(blocks, [](const BlockId b) { return b != AIR_BLOCK; }))
			mask |= std::uint32_t{1} << s;
	}
	out.writeU32(mask);

	std::vector<std::uint16_t> runs;          // (length, block) pairs, flattened
	runs.reserve(512);
	for (int s = 0; s < ChunkColumn::SECTION_COUNT; ++s) {
		if (!(mask & (std::uint32_t{1} << s)))
			continue;
		const auto& blocks = column.section(s).blocks();

		runs.clear();
		std::size_t i = 0;
		while (i < blocks.size()) {
			const BlockId block = blocks[i];
			std::size_t j = i + 1;
			while (j < blocks.size() && blocks[j] == block)
				++j;
			runs.push_back(static_cast<std::uint16_t>(j - i));
			runs.push_back(block);
			i = j;
		}
		out.writeU16(static_cast<std::uint16_t>(runs.size() / 2));
		for (const std::uint16_t v : runs)
			out.writeU16(v);
	}
}

bool decode(net::ByteReader& in, ChunkColumn& column) {
	const std::uint32_t mask = in.readU32();
	if (!in.ok() || (mask >> ChunkColumn::SECTION_COUNT) != 0)
		return false;

	for (int s = 0; s < ChunkColumn::SECTION_COUNT; ++s) {
		if (!(mask & (std::uint32_t{1} << s)))
			continue;                         // stays air

		auto& blocks = column.section(s).blocks();
		const std::uint16_t runCount = in.readU16();
		if (!in.ok() || runCount == 0 || runCount > ChunkSection::BLOCK_COUNT
		    || in.remaining() < std::size_t{runCount} * 4)
			return false;

		std::size_t filled = 0;
		for (std::uint16_t r = 0; r < runCount; ++r) {
			const std::uint16_t length = in.readU16();
			const std::uint16_t block  = in.readU16();
			if (length == 0 || block >= BLOCK_TYPE_COUNT || filled + length > blocks.size())
				return false;
			std::fill_n(blocks.begin() + static_cast<std::ptrdiff_t>(filled), length, block);
			filled += length;
		}
		if (filled != blocks.size())
			return false;
	}
	return in.ok();
}

} // namespace ChunkCodec
