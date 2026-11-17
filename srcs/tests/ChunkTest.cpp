#include "SelfTest.hpp"
#include "Chunk.hpp"

SELFTEST(Chunk, heap_bytes_full) {
	const ChunkColumn column;
	CHECK_EQ(column.heapBytes(), static_cast<std::size_t>(20 * 4096 * 2));
}
