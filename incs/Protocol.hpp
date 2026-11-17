#pragma once
#ifndef PROTOCOL_HPP
# define PROTOCOL_HPP

// ============================================================================
//  Protocol.hpp : the game-level wire contract shared by GameServer and
//  RemoteSession. Framing, Hello / Login / KeepAlive / Disconnect live in
//  Net.hpp / NetClient.hpp. All integers little-endian.
//
//  client -> server
//    PlayerMove  {f32 x, f32 y, f32 z, f32 yaw, f32 pitch}   eye position
//    BreakBlock  {i32 x, i32 y, i32 z}                       block to remove
//    PlaceBlock  {i32 x, i32 y, i32 z, u16 block}            EMPTY cell to fill; `block` must
//                                                            be the server's selected stack's
//                                                            block, else refused (BlockUpdate +
//                                                            InventoryContents)
//    InventoryAction {u32 seq, u8 kind, u8 slot, u8 secondary}
//                                                            kind 0 Select / 1 Click / 2 Close;
//                                                            kind > 2, secondary > 1 or a wrong
//                                                            length is a protocol violation; an
//                                                            out-of-range slot is just rejected
//
//  server -> client
//    LoginOk     {u32 playerId, f32 x, f32 y, f32 z, f32 yaw, f32 pitch}
//                                                            spawn eye position and look
//    InventoryContents {u32 ackSeq, 149 bytes}               the player's inventory (see
//                                                            encodeInventory), after LoginOk, after
//                                                            every InventoryAction (ackSeq = its seq,
//                                                            accepted or not) and whenever a break
//                                                            or place changed it or was refused
//    ChunkData   {i32 cx, i32 cz, <column>}                  see ChunkCodec.hpp
//    UnloadChunk {i32 cx, i32 cz}
//    BlockUpdate {i32 x, i32 y, i32 z, u16 block}            authoritative; also
//                                                            the answer to a rejected edit
//    EntityMove  {u16 n, n * {u32 id, f32 x, f32 y, f32 z, f32 yaw, f32 pitch}}
//                                                            includes the receiver: skip your own id
//    PlayerJoin  {u32 id, string name}
//    PlayerLeave {u32 id}
// ============================================================================

#include <cstddef>
#include <cstdint>

namespace proto {

inline constexpr std::uint16_t DEFAULT_PORT   = 25565;
inline constexpr std::size_t   MAX_NAME_BYTES = 32;      // Transport::handlePacket enforces it too

// Chunks streamed around each player: World::CHUNK_LOAD_RADIUS, so a client
// meshes exactly what it would mesh in singleplayer.
inline constexpr int           VIEW_RADIUS    = 17;

// The server accepts edits up to this far from the eye (client reach is 6;
// the slack absorbs movement between the click and the server seeing it).
inline constexpr float         EDIT_REACH     = 8.0f;

} // namespace proto

#endif /* PROTOCOL_HPP */
