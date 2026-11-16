#pragma once
#ifndef CHUNK_CODEC_HPP
# define CHUNK_CODEC_HPP

// ============================================================================
//  ChunkCodec : ChunkColumn <-> bytes, shared by GameServer and RemoteSession
//
//  Layout (after the ChunkData header {i32 cx, i32 cz}):
//
//    u32 sectionMask                      bit i set = section i present
//                                         (absent = all air)
//    per present section, lowest first:
//      u16 runCount                       1..4096
//      runCount * {u16 length, u16 block} run-length encoding of
//                                         ChunkSection::blocks(), whose order
//                                         is x + (z << 4) + (y << 8)
// ============================================================================

#include "Chunk.hpp"
#include "Net.hpp"

namespace ChunkCodec {

// Appends the encoded column to `out`.
void encode(const ChunkColumn& column, net::ByteWriter& out);

// Fills `column` (whose sections must be all air, as freshly constructed)
// from `in`. Returns false on malformed input (runs that do not add up to a
// section, unknown block ids, truncated data).
[[nodiscard]] bool decode(net::ByteReader& in, ChunkColumn& column);

} // namespace ChunkCodec

#endif /* CHUNK_CODEC_HPP */
