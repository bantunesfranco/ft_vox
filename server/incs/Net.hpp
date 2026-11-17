#pragma once
#ifndef NET_HPP
# define NET_HPP

// ============================================================================
//  Net.hpp : transport + protocol API for the game server
//
//  Layers (dependencies point downward only):
//
//      game logic        implements ITransportHandler, calls Transport::send
//      protocol          PacketId, ByteReader / ByteWriter, frame parsing
//      transport         Transport, Listener, Connection, buffers   (portable)
//      platform          Platform.hpp: sockets, poll, errors (Linux + Windows)
//
//  Threading: everything here is touched only by the main (net + game) thread,
//  except Transport::post(), which is thread-safe and is how worker threads
//  hand results back to the main thread.
// ============================================================================

#include <bit>
#include <chrono>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include "Platform.hpp"

// The wire format is little-endian. Integers are byte-swapped as needed
// (see net::wire below), so any host byte order works. Floats are sent as the
// IEEE 754 bit pattern, so the host must use IEEE 754 floats.
static_assert(std::numeric_limits<float>::is_iec559 && std::numeric_limits<double>::is_iec559,
              "wire format requires IEEE 754 floats");

namespace net {

// ============================================================================
//  Basic types
// ============================================================================

using ConnId       = uint32_t;                       // monotonic, never reused
constexpr ConnId INVALID_CONN = 0;

using Clock        = std::chrono::steady_clock;
using TimePoint    = Clock::time_point;
using Buffer       = std::vector<uint8_t>;
using SharedBuffer = std::shared_ptr<const Buffer>;  // shared between clients

// ============================================================================
//  Socket / SocketHandle: defined in Platform.hpp (int on Linux, SOCKET on Windows)
// ============================================================================

// ============================================================================
//  Configuration
// ============================================================================

struct Config {
	uint16_t port               = 25565;
	int      backlog            = 128;
	size_t   maxConnections     = 64;

	// tick loop
	unsigned tickRateHz         = 20;
	unsigned maxCatchUpTicks    = 3;       // beyond this, ticks are dropped

	// framing limits
	size_t   maxPacketSizeIn    = 1u << 20;   // client -> server
	size_t   maxPacketSizeOut   = 8u << 20;   // server -> client (chunk data)

	// timeouts
	std::chrono::milliseconds handshakeTimeout{5000};
	std::chrono::milliseconds loginTimeout{10000};
	std::chrono::milliseconds idleTimeout{30000};
	std::chrono::milliseconds closingTimeout{2000};

	// backpressure (bytes pending in a connection's send queue)
	size_t   lowWaterMark       = 256u << 10;   // chunk streaming allowed below this
	size_t   highWaterMark      = 4u << 20;     // disconnect "too slow" at/above this

	// per-event read budget so one flooding client cannot monopolize the loop
	size_t   readBudgetPerEvent = 256u << 10;

	// per-connection rate limit (token bucket)
	double   packetsPerSecond   = 200.0;
	double   bytesPerSecond     = 256.0 * 1024.0;
	double   burstSeconds       = 2.0;
};

// ============================================================================
//  Protocol: packet ids, disconnect reasons, wire constants
// ============================================================================

enum class PacketId : uint16_t {
	// client -> server
	Hello        = 0x0001,   // protocol version
	Login        = 0x0002,   // player name
	PlayerMove   = 0x0010,   // position / look intent
	BreakBlock   = 0x0020,   // intent, server validates
	PlaceBlock   = 0x0021,   // intent, server validates
	InventoryAction = 0x0030,   // seq + one inventory input, server applies it

	// server -> client
	LoginOk      = 0x0102,   // player id, spawn
	EntityMove   = 0x0110,   // batched positions of other players
	PlayerJoin   = 0x0111,   // another player appeared (id, name)
	PlayerLeave  = 0x0112,   // another player left (id)
	BlockUpdate  = 0x0120,   // authoritative result
	ChunkDelta   = 0x0121,   // batched per-chunk changes
	ChunkData    = 0x0130,   // encoded chunk
	UnloadChunk  = 0x0131,
	TimeUpdate   = 0x0140,   // u32 time of day (ticks); clients ignore it if unknown
	InventoryContents = 0x0150,   // the authoritative inventory + last action seq applied

	// both directions
	KeepAlive    = 0x0F00,
	Disconnect   = 0x0FFF,   // reason, then close
};

enum class DisconnectReason : uint8_t {
	PeerClosed,          // orderly close (recv == 0)
	IoError,             // fatal socket error
	ProtocolViolation,   // bad length, malformed payload, wrong-state packet
	Timeout,             // state or idle timeout
	TooSlow,             // send queue hit the high-water mark
	RateLimited,         // packet / byte flood
	ServerFull,
	ServerShutdown,
	KickedByGame,        // requested through Transport::disconnect()
	NameTaken,           // a player with that name is already playing
	VersionMismatch,     // Hello carried another PROTOCOL_VERSION
};

const char* toString(DisconnectReason r);

// Frame layout (all integers little-endian):
//
//   [ u32 length ][ u16 packetId ][ payload ... ]
//
//   `length` counts packetId + payload, so it is always >= 2.
constexpr size_t FRAME_LENGTH_SIZE = sizeof(uint32_t);
constexpr size_t FRAME_ID_SIZE     = sizeof(uint16_t);
constexpr size_t FRAME_HEADER_SIZE = FRAME_LENGTH_SIZE + FRAME_ID_SIZE;
constexpr uint32_t PROTOCOL_VERSION = 2;

// ============================================================================
//  net::wire : host <-> little-endian conversion for fixed-size values
//
//  toWire(v)   : any 1/2/4/8-byte arithmetic value -> its little-endian bytes,
//                held in an unsigned integer of the same size
//  fromWire<T> : the inverse
//
//  Signed integers and floats go through memcpy into an unsigned integer of
//  the same size, so the bit pattern is preserved and only the byte order is
//  changed. Uses std::endian (<bit>), so no <endian.h>; on a little-endian
//  host the conversion compiles to nothing.
// ============================================================================

namespace wire {

template <size_t N>
struct UIntOfSize { static_assert(N == 0, "wire: only 1, 2, 4 and 8 byte types are supported"); };
template <> struct UIntOfSize<1> { using type = uint8_t;  };
template <> struct UIntOfSize<2> { using type = uint16_t; };
template <> struct UIntOfSize<4> { using type = uint32_t; };
template <> struct UIntOfSize<8> { using type = uint64_t; };

// --- concepts: which types may go on the wire -----------------------------------

template <typename T>
concept SizeOk = sizeof(T) == 1 || sizeof(T) == 2 || sizeof(T) == 4 || sizeof(T) == 8;

// integers (not bool) and floating point types of 1/2/4/8 bytes
template <typename T>
concept Number = SizeOk<T> &&
                 ((std::integral<T> && !std::same_as<T, bool>) || std::floating_point<T>);

// bool is sent as exactly one byte: 0 or 1
template <typename T>
concept Boolean = std::same_as<T, bool>;

// enums are sent as their underlying integer type
template <typename T>
concept Enum = std::is_enum_v<T> && SizeOk<T>;

// anything write<T> / read<T> accept
template <typename T>
concept Scalar = Number<T> || Boolean<T> || Enum<T>;

template <typename T>
using UInt = typename UIntOfSize<sizeof(T)>::type;

static_assert(std::endian::native == std::endian::little ||
              std::endian::native == std::endian::big,
              "mixed-endian hosts are not supported");

// Reverses the bytes of an unsigned integer. std::byteswap is C++23; below
// that, a shift loop (compilers turn it into a single bswap instruction).
template <std::unsigned_integral U>
constexpr U byteSwap(U v) {
#if defined(__cpp_lib_byteswap) && __cpp_lib_byteswap >= 202110L
	return std::byteswap(v);
#else
	if constexpr (sizeof(U) == 1) {
		return v;
	} else {
		U r = 0;
		for (size_t i = 0; i < sizeof(U); ++i) {
			r = static_cast<U>((r << 8) | (v & 0xFF));
			v = static_cast<U>(v >> 8);
		}
		return r;
	}
#endif
}

// host -> little-endian. A no-op on little-endian hosts.
template <std::unsigned_integral U>
constexpr U hostToLe(U v) {
	if constexpr (std::endian::native == std::endian::little) return v;
	else                                                      return byteSwap(v);
}

// little-endian -> host. The same operation (a swap is its own inverse).
template <std::unsigned_integral U>
constexpr U leToHost(U v) { return hostToLe(v); }

template <Number T>
UInt<T> toWire(T v) {
	UInt<T> u;
	std::memcpy(&u, &v, sizeof(u));
	return hostToLe(u);
}

template <Number T>
T fromWire(UInt<T> u) {
	u = leToHost(u);
	T v;
	std::memcpy(&v, &u, sizeof(v));
	return v;
}

} // namespace wire

// ============================================================================
//  ByteWriter : builds one outgoing frame
//
//      ByteWriter w(PacketId::BlockUpdate);
//      w.write<int32_t>(x).write<int32_t>(y).write<int32_t>(z).write<uint16_t>(blockId);
//      transport.send(id, w.finish());
//
//  write<T> accepts arithmetic types and enums. Prefer fixed-width types
//  (uint32_t, not int / long / size_t) so the wire size never depends on the
//  platform. Strings and blobs have their own functions (length prefixes).
//  finish() patches the length prefix and returns a shareable buffer.
// ============================================================================

class ByteWriter {
public:
	explicit ByteWriter(PacketId id, size_t reserve = 64);

	// --- generic fixed-size write ---------------------------------------------
	//   Constrained by net::wire concepts: Number, Boolean, Enum.
	//   Anything else (std::string, structs, long double, ...) does not compile.
	template <wire::Number T>
	ByteWriter& write(T v) {
		const auto le = wire::toWire(v);                            // little-endian bytes
		return writeBytes(reinterpret_cast<const uint8_t*>(&le), sizeof(le));
	}

	template <wire::Boolean T>
	ByteWriter& write(T v) {
		return write<uint8_t>(v ? 1 : 0);                           // exactly 0 or 1 on the wire
	}

	template <wire::Enum T>
	ByteWriter& write(T v) {
		return write(static_cast<std::underlying_type_t<T>>(v));
	}

	// --- named shortcuts (thin wrappers over write<T>) --------------------------
	ByteWriter& writeU8  (uint8_t  v) { return write(v); }
	ByteWriter& writeU16 (uint16_t v) { return write(v); }
	ByteWriter& writeU32 (uint32_t v) { return write(v); }
	ByteWriter& writeU64 (uint64_t v) { return write(v); }
	ByteWriter& writeI32 (int32_t  v) { return write(v); }
	ByteWriter& writeF32 (float    v) { return write(v); }
	ByteWriter& writeF64 (double   v) { return write(v); }
	ByteWriter& writeBool(bool     v) { return write(v); }

	// --- variable-size data -----------------------------------------------------
	ByteWriter& writeString(const std::string& s);                   // u16 length + bytes
	ByteWriter& writeBlob(const uint8_t* data, size_t n);            // u32 length + bytes
	ByteWriter& writeBytes(const uint8_t* data, size_t n);           // raw, no prefix

	[[nodiscard]]
	size_t       size() const { return _buf.size(); }
	SharedBuffer finish();                                           // call once

private:
	Buffer _buf;
	bool   _finished = false;
};

// ============================================================================
//  ByteReader : bounds-checked reader over one packet payload
//
//  Never throws, never reads out of range. After any failed read ok() is
//  false (and stays false), and every further read returns a zero value.
//  Check ok() once after parsing a packet and treat failure as a protocol
//  violation.
//
//  read<T> accepts arithmetic types and enums. An enum read from the network
//  is NOT range-checked: validate it before you use it.
// ============================================================================

class ByteReader {
public:
	ByteReader(const uint8_t* data, const size_t size) : _p(data), _end(data + size) {}

	// --- generic fixed-size read ------------------------------------------------
	//   Constrained by net::wire concepts: Number, Boolean, Enum.
	template <wire::Number T>
	T read() {
		wire::UInt<T> raw{};
		take(&raw, sizeof(raw));                                    // raw stays 0 on failure
		return wire::fromWire<T>(raw);                              // little-endian -> host
	}

	template <wire::Boolean T>
	T read() {
		return read<uint8_t>() != 0;                                // never load a raw bool from the network
	}

	template <wire::Enum T>
	T read() {
		return static_cast<T>(read<std::underlying_type_t<T>>());
	}

	// --- named shortcuts (thin wrappers over read<T>) ---------------------------
	uint8_t  readU8  () { return read<uint8_t>();  }
	uint16_t readU16 () { return read<uint16_t>(); }
	uint32_t readU32 () { return read<uint32_t>(); }
	uint64_t readU64 () { return read<uint64_t>(); }
	int32_t  readI32 () { return read<int32_t>();  }
	float    readF32 () { return read<float>();    }
	double   readF64 () { return read<double>();   }
	bool     readBool() { return read<bool>();     }

	// --- variable-size data -----------------------------------------------------
	std::string readString(size_t maxLen);                           // u16 length + bytes, <= maxLen
	bool        readBlob(Buffer& out, size_t maxLen);                // u32 length + bytes, <= maxLen
	bool        readBytes(uint8_t* out, size_t n) { return take(out, n); }   // raw, no prefix

	[[nodiscard]] bool   ok()        const { return _ok; }
	[[nodiscard]] size_t remaining() const { return static_cast<size_t>(_end - _p); }
	[[nodiscard]] bool   atEnd()     const { return _p == _end; }                  // use to reject trailing junk


private:
	bool take(void* dst, size_t n);

	const uint8_t* _p;
	const uint8_t* _end;
	bool           _ok = true;
};

// ============================================================================
//  Handler interface: the ONLY thing game logic sees of the transport
// ============================================================================

struct PeerInfo {
	std::string address;     // "1.2.3.4"
	uint16_t    port = 0;
};

class ITransportHandler {
public:
	virtual ~ITransportHandler() = default;

	// A connection finished the TCP accept. Still in Handshake state.
	// Return false to reject it (it is closed with ServerFull).
	virtual bool onConnect(ConnId id, const PeerInfo& peer) = 0;

	// The client completed Hello + Login. `name` is already length-validated.
	// Return std::nullopt to accept, or the reason the connection is closed with.
	virtual std::optional<DisconnectReason> onLogin(ConnId id, const std::string& name) = 0;

	// A gameplay packet arrived in Play state. Only record intent here;
	// simulate in onTick(). If body.ok() is false after return, the transport
	// disconnects the client.
	virtual void onPacket(ConnId id, PacketId type, ByteReader& body) = 0;

	// Called exactly once per connection that received onConnect(),
	// before the Connection object is destroyed. send() on `id` is a no-op now.
	virtual void onDisconnect(ConnId id, DisconnectReason why) = 0;

	// Fixed-rate tick (Config::tickRateHz). `tick` increases by 1 per call.
	virtual void onTick(uint64_t tick) = 0;
};

// ============================================================================
//  Transport : public facade
// ============================================================================

class Transport {
public:
	Transport(const Config& cfg, ITransportHandler& handler);
	~Transport();
	Transport(const Transport&)            = delete;
	Transport& operator=(const Transport&) = delete;

	// --- lifecycle ---------------------------------------------------------

	// Initializes the OS socket runtime (WSAStartup on Windows) and opens the
	// listener. Returns false and fills `error`
	// on failure (bind in use, etc.).
	bool start(std::string& error);

	// Enters the event loop. Returns after stop() or a termination signal.
	void run();

	// Request shutdown. Safe from any thread and from signal handlers (it is a
	// single atomic store). run() notices within one tick period, because the
	// poll timeout never exceeds the time to the next tick.
	void stop();

	// --- sending (main thread) ---------------------------------------------

	// Queue a frame. The buffer is shared, never copied.
	// No-op if `id` is unknown / already closing.
	void send(ConnId id, SharedBuffer frame);

	// Same frame to many connections (serialize once, share the buffer).
	void broadcast(const std::vector<ConnId>& ids, const SharedBuffer& frame);

	// Flush queued output, then close. Sends a Disconnect packet first.
	// onDisconnect() fires with `why`.
	void disconnect(ConnId id, DisconnectReason why);

	// --- queries (main thread) ---------------------------------------------

	bool   peerInfo(ConnId id, PeerInfo& out) const;

	[[nodiscard]]
	bool   isConnected(ConnId id) const;
	[[nodiscard]]
	size_t pendingBytes(ConnId id) const;      // for chunk throttling (lowWaterMark)
	[[nodiscard]]
	size_t connectionCount() const;

	// --- worker -> main thread bridge (THREAD-SAFE) -------------------------

	// Queues `fn` to run on the main thread. It runs on the next loop pass, at
	// most one tick period later (the loop is not woken early: queued data is
	// only flushed on the tick anyway). Workers use this to deliver encoded
	// chunks. Never capture raw Connection pointers; capture a ConnId and
	// re-check isConnected() when `fn` runs.
	void post(std::function<void()> fn);

private:
	struct Impl;
	std::unique_ptr<Impl> _impl;
};

// ============================================================================
//  detail : transport internals (exposed so they can be unit tested)
// ============================================================================

namespace detail {

// ---- Event sources: poll() tags --------------------------------------------------
//
// pollSockets() (Platform.hpp) hands back the 64-bit tag we registered. It
// encodes WHAT became ready; clients are referenced by ConnId, never by
// pointer, so a connection destroyed mid-batch cannot cause a use-after-free.

enum class SourceKind : uint8_t { Listener, Client };

struct EventTag {
	SourceKind kind;
	ConnId     id;      // meaningful for Client only

	[[nodiscard]] uint64_t pack() const { return (static_cast<uint64_t>(kind) << 32) | id; }
	static EventTag unpack(uint64_t v) {
		return { static_cast<SourceKind>(v >> 32), static_cast<ConnId>(v & 0xFFFFFFFFu) };
	}
};

// ---- Listener ----------------------------------------------------------------

class Listener {
public:
	// Non-blocking TCP listener on 0.0.0.0:port (port 0 = let the OS pick).
	bool     open(uint16_t port, int backlog, std::string& error);
	[[nodiscard]] SocketHandle handle() const { return _sock.get(); }
	[[nodiscard]] uint16_t port()   const { return _port; }          // the real port (useful with port 0)

	// Accepts ONE pending connection. Call in a loop until it returns
	// WouldBlock; Retry means "call again", OutOfDescriptors means back off.
	// The accepted socket is already non-blocking, no-inherit and TCP_NODELAY.
	AcceptStatus acceptOne(Socket& out, PeerInfo& peer) const;

private:
	Socket   _sock;
	uint16_t _port = 0;
};

// ---- RecvBuffer : contiguous receive buffer holding partial frames ------------

class RecvBuffer {
public:
	explicit RecvBuffer(const size_t maxSize) : _max(maxSize) {}

	// Returns a writable region of at least `n` bytes, or nullptr if growing
	// would exceed the cap (caller treats that as a protocol violation).
	uint8_t*       prepare(size_t n);
	void           commit(const size_t n)        { _size += n; }

	[[nodiscard]] const uint8_t* data() const            { return _buf.data() + _head; }
	[[nodiscard]] size_t         size() const            { return _size; }
	void           consume(size_t n);      // drops n bytes from the front

private:
	Buffer _buf;
	size_t _head = 0;       // start of unread data
	size_t _size = 0;       // unread bytes
	size_t _max;
};

// ---- Frame parser ------------------------------------------------------------

enum class ParseResult { NeedMore, Packet, Error };

struct PacketView {
	PacketId       id{};
	const uint8_t* payload = nullptr;   // points into RecvBuffer, valid until consume()
	size_t         payloadSize = 0;
	size_t         frameSize = 0;       // header + payload; pass to RecvBuffer::consume()
};

// Validates `length` (>= 2 and <= maxPacketSize) BEFORE waiting for the body,
// so a hostile length never causes an allocation. Does not consume.
ParseResult tryParse(const RecvBuffer& in, size_t maxPacketSize, PacketView& out);

// ---- SendQueue : outgoing frames with partial-write tracking -------------------

struct OutChunk {
	SharedBuffer data;
	size_t       offset = 0;     // bytes of `data` already sent
};

enum class FlushResult { Drained, WouldBlock, Error };

class SendQueue {
public:
	[[nodiscard]] size_t pending() const { return _pending; }
	[[nodiscard]] bool   empty()   const { return _q.empty(); }
	void   push(SharedBuffer buf);

	// Gathered send (sendmsg / WSASend) over the first few chunks. Loops until the
	// queue is empty (Drained), the socket buffer is full (WouldBlock) or a
	// fatal error. The caller asks poll for writability on WouldBlock and stops
	// asking once it returns Drained.
	FlushResult flush(SocketHandle s);

private:
	std::deque<OutChunk> _q;
	size_t               _pending = 0;
};

// ---- Rate limiting -------------------------------------------------------------

class RateLimit {
public:
	RateLimit() = default;
	RateLimit(double packetsPerSec, double bytesPerSec, double burstSec);

	// Returns false when the budget is exhausted (caller disconnects).
	bool allow(size_t bytes, TimePoint now);

private:
	double    _pktRate = 0, _byteRate = 0;
	double    _pktCap  = 0, _byteCap  = 0;
	double    _pktTokens = 0, _byteTokens = 0;
	TimePoint _last{};
};

// ---- Connection -----------------------------------------------------------------

enum class ConnState : uint8_t { Handshake, Login, Play, Closing };

// Which packets are valid in which state (checked before dispatch).
bool packetAllowedIn(ConnState s, PacketId id);

class Connection {
public:
	Connection(ConnId id, Socket sock, PeerInfo  peer, const Config& cfg, TimePoint now);

	// Not copyable / movable: owned through unique_ptr in the Impl map.
	Connection(const Connection&)            = delete;
	Connection& operator=(const Connection&) = delete;

	[[nodiscard]] ConnId      id()      const { return _id; }
	[[nodiscard]] SocketHandle handle() const { return _sock.get(); }
	[[nodiscard]] const PeerInfo& peer() const { return _peer; }

	ConnState   state     = ConnState::Handshake;
	RecvBuffer  in;
	SendQueue   out;
	RateLimit   limiter;
	TimePoint   connectedAt;
	TimePoint   lastActivity;
	TimePoint   stateSince;
	bool        wantWrite = false;           // asking poll for writability?
	bool        closing   = false;           // flush remaining output, then close
	DisconnectReason closeReason = DisconnectReason::PeerClosed;

	// Timeout for the current state (Handshake / Login / Play idle / Closing).
	[[nodiscard]] TimePoint   deadline(const Config& cfg) const;

private:
	ConnId   _id;
	Socket   _sock;
	PeerInfo _peer;
};

} // namespace detail
} // namespace net

#endif /* NET_HPP */