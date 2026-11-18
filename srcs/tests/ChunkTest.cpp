#include "SelfTest.hpp"
#include "Chunk.hpp"
#include "ChunkCodec.hpp"

namespace {

constexpr BlockId STONE = toBlockId(BlockType::Stone);
constexpr BlockId DIRT  = toBlockId(BlockType::Dirt);

// The codec output for `column`, without the frame header.
std::vector<std::uint8_t> encoded(const ChunkColumn& column) {
	net::ByteWriter writer(net::PacketId::ChunkData);
	ChunkCodec::encode(column, writer);
	const net::SharedBuffer frame = writer.finish();
	return {frame->begin() + static_cast<std::ptrdiff_t>(net::FRAME_HEADER_SIZE), frame->end()};
}

} // namespace

SELFTEST(Chunk, heap_bytes_dense_sections_only) {
	ChunkColumn column;
	CHECK_EQ(column.heapBytes(), std::size_t{0});
	column.set(0, 40, 0, STONE);
	CHECK_EQ(column.heapBytes(), static_cast<std::size_t>(4096 * 2));
}

SELFTEST(Chunk, uniform_section_writes) {
	ChunkSection section;
	CHECK(section.isUniform());
	CHECK_EQ(section.uniformBlock(), AIR_BLOCK);
	CHECK_EQ(section.heapBytes(), std::size_t{0});
	CHECK(section.dense() == nullptr);

	section.set(5, 6, 7, AIR_BLOCK);
	CHECK(section.isUniform());
	CHECK_EQ(section.heapBytes(), std::size_t{0});

	section.set(1, 2, 3, STONE);
	CHECK(!section.isUniform());
	CHECK(section.dense() != nullptr);
	CHECK_EQ(section.heapBytes(), static_cast<std::size_t>(4096 * 2));
	CHECK_EQ(section.get(1, 2, 3), STONE);
	CHECK_EQ(section.get(0, 0, 0), AIR_BLOCK);
	CHECK_EQ(section.get(15, 15, 15), AIR_BLOCK);
	CHECK_EQ(section.get(1, 3, 2), AIR_BLOCK);

	section.fill(DIRT);
	CHECK(section.isUniform());
	CHECK_EQ(section.uniformBlock(), DIRT);
	CHECK_EQ(section.heapBytes(), std::size_t{0});
	CHECK_EQ(section.get(1, 2, 3), DIRT);

	section.set(1, 2, 3, STONE);
	CHECK(!section.isUniform());
	section.compact();                     // still mixed: stays dense
	CHECK(!section.isUniform());
	section.set(1, 2, 3, DIRT);
	CHECK(!section.isUniform());           // writes never collapse on their own
	section.compact();
	CHECK(section.isUniform());
	CHECK_EQ(section.uniformBlock(), DIRT);
	CHECK_EQ(section.heapBytes(), std::size_t{0});
}

SELFTEST(Chunk, copy_is_deep) {
	ChunkColumn original({3, -4});
	original.set(1, 20, 2, STONE);         // section 1 dense, the rest uniform air
	ChunkColumn copy = original;
	copy.set(4, 21, 5, DIRT);
	copy.set(0, 100, 0, DIRT);             // a uniform section of the copy
	CHECK_EQ(original.get(4, 21, 5), AIR_BLOCK);
	CHECK_EQ(original.get(0, 100, 0), AIR_BLOCK);
	CHECK_EQ(original.get(1, 20, 2), STONE);
	CHECK_EQ(copy.get(1, 20, 2), STONE);
	CHECK_EQ(copy.get(4, 21, 5), DIRT);

	ChunkColumn assigned;
	assigned = original;
	assigned.set(1, 20, 2, DIRT);
	CHECK_EQ(original.get(1, 20, 2), STONE);
	CHECK(assigned.coord() == original.coord());
}

SELFTEST(Chunk, codec_uniform_roundtrip) {
	ChunkColumn column({7, 9});
	column.section(0).fill(STONE);         // uniform stone
	column.section(1).fill(DIRT);          // uniform dirt
	column.set(3, 40, 4, STONE);           // section 2 dense, mostly air
	column.set(0, 41, 0, DIRT);            // section 2 dense
	// sections 3.. stay uniform air

	// The same blocks with every section dense.
	ChunkColumn dense({7, 9});
	for (int s = 0; s < ChunkColumn::SECTION_COUNT; ++s) {
		const ChunkSection& from = column.section(s);
		auto& blocks = dense.section(s).makeDense();
		for (int i = 0; i < ChunkSection::BLOCK_COUNT; ++i)
			blocks[static_cast<std::size_t>(i)] = from.isUniform() ? from.uniformBlock()
			                                                       : (*from.dense())[static_cast<std::size_t>(i)];
		CHECK(!dense.section(s).isUniform());
	}

	const std::vector<std::uint8_t> bytes = encoded(column);
	CHECK(bytes == encoded(dense));

	ChunkColumn decoded({7, 9});
	net::ByteReader in(bytes.data(), bytes.size());
	CHECK(ChunkCodec::decode(in, decoded));
	CHECK(in.atEnd());
	for (int s = 0; s < ChunkColumn::SECTION_COUNT; ++s) {
		CHECK(decoded.section(s) == column.section(s));
		CHECK_EQ(decoded.section(s).isUniform(), column.section(s).isUniform());
	}
	CHECK_EQ(decoded.section(0).uniformBlock(), STONE);
	CHECK_EQ(decoded.section(1).uniformBlock(), DIRT);
	CHECK_EQ(decoded.section(5).uniformBlock(), AIR_BLOCK);
	CHECK_EQ(decoded.get(3, 40, 4), STONE);
}

SELFTEST(Chunk, section_equality_ignores_storage) {
	ChunkSection uniform;
	uniform.fill(STONE);
	ChunkSection dense;
	dense.makeDense().fill(STONE);
	CHECK(uniform == dense);
	CHECK(dense == uniform);
	dense.set(0, 0, 0, DIRT);
	CHECK(!(uniform == dense));
	CHECK(!(dense == uniform));
	ChunkSection other;
	other.set(0, 0, 0, DIRT);
	CHECK(!(other == dense));
	other.fill(STONE);
	other.set(0, 0, 0, DIRT);
	CHECK(other == dense);
}
