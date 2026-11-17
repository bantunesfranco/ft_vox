#include "Net.hpp"

#include <array>
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <utility>

namespace net {

// ============================================================================
//  DisconnectReason
// ============================================================================

const char* toString(const DisconnectReason r) {
    switch (r) {
        case DisconnectReason::PeerClosed:        return "peer closed connection";
        case DisconnectReason::IoError:           return "socket error";
        case DisconnectReason::ProtocolViolation: return "protocol violation";
        case DisconnectReason::Timeout:           return "timed out";
        case DisconnectReason::TooSlow:           return "send queue full";
        case DisconnectReason::RateLimited:       return "rate limited";
        case DisconnectReason::ServerFull:        return "server is full";
        case DisconnectReason::ServerShutdown:    return "server shutting down";
        case DisconnectReason::KickedByGame:      return "kicked by game";
    }
    return "unknown";   // no `default:` above, so -Wswitch flags new enumerators
}

// ============================================================================
//  ByteWriter  (write<T> and the named shortcuts live in the header)
// ============================================================================

ByteWriter::ByteWriter(PacketId id, const size_t reserve) {
    _buf.reserve(FRAME_HEADER_SIZE + reserve);   // capacity only, size stays 0
    write<uint32_t>(0);                          // length placeholder, patched in finish()
    write(id);                                   // enum -> underlying uint16_t
}

ByteWriter& ByteWriter::writeBytes(const uint8_t* data, const size_t n) {
    if (_finished) throw std::logic_error("ByteWriter: write after finish");
    if (n) _buf.insert(_buf.end(), data, data + n);
    return *this;
}

ByteWriter& ByteWriter::writeString(const std::string& s) {
    if (s.size() > std::numeric_limits<uint16_t>::max())
        throw std::length_error("ByteWriter: string too long");
    write<uint16_t>(static_cast<uint16_t>(s.size()));
    return writeBytes(reinterpret_cast<const uint8_t*>(s.data()), s.size());
}

ByteWriter& ByteWriter::writeBlob(const uint8_t* data, const size_t n) {
    if (n > std::numeric_limits<uint32_t>::max())
        throw std::length_error("ByteWriter: blob too long");
    write<uint32_t>(static_cast<uint32_t>(n));
    return writeBytes(data, n);
}

SharedBuffer ByteWriter::finish() {
    if (_finished) throw std::logic_error("ByteWriter: finish called twice");

    // `length` counts packetId + payload, i.e. everything after the length field.
    const uint32_t length = wire::toWire(static_cast<uint32_t>(_buf.size() - FRAME_LENGTH_SIZE));
    std::memcpy(_buf.data(), &length, sizeof(length));

    _finished = true;
    return std::make_shared<const Buffer>(std::move(_buf));   // move, don't copy
}

// ============================================================================
//  ByteReader  (read<T> and the named shortcuts live in the header)
// ============================================================================

// Sticky failure: once _ok is false it stays false, and every later read
// returns a zero value.
bool ByteReader::take(void* dst, const size_t n) {
    if (!_ok || n > remaining()) {   // compare against remaining(): `_p + n` could overflow
        _ok = false;
        return false;
    }
    if (n) std::memcpy(dst, _p, n);
    _p += n;
    return true;
}

// Mirrors ByteWriter::writeString: u16 length prefix + bytes.
std::string ByteReader::readString(const size_t maxLen) {
    const size_t len = read<uint16_t>();
    if (!_ok) return {};
    if (len > maxLen || len > remaining()) { _ok = false; return {}; }
    std::string s(reinterpret_cast<const char*>(_p), len);
    _p += len;
    return s;
}

// Mirrors ByteWriter::writeBlob: u32 length prefix + bytes.
bool ByteReader::readBlob(Buffer& out, const size_t maxLen) {
    const size_t len = read<uint32_t>();
    if (!_ok) return false;
    if (len > maxLen || len > remaining()) { _ok = false; return false; }
    out.assign(_p, _p + len);
    _p += len;
    return true;
}

namespace detail {

// ============================================================================
//  RecvBuffer
// ============================================================================

// Unread bytes may hold one maximum-size frame plus the start of the next
// ones; anything beyond that slack is a peer that never lets us parse.
uint8_t* RecvBuffer::prepare(const size_t n) {
    constexpr size_t kSlack = 1u << 20;
    if (n > kSlack || _size + n > _max + FRAME_HEADER_SIZE + kSlack)
        return nullptr;

    // Move unread data to the front when the tail has no room for `n` bytes.
    if (_head + _size + n > _buf.size() && _head > 0) {
        if (_size) std::memmove(_buf.data(), _buf.data() + _head, _size);
        _head = 0;
    }
    if (_head + _size + n > _buf.size())
        _buf.resize(std::max(_head + _size + n, _buf.size() * 2));   // geometric growth
    return _buf.data() + _head + _size;
}

void RecvBuffer::consume(size_t n) {
    n = std::min(n, _size);
    _head += n;
    _size -= n;
    if (_size == 0) _head = 0;                   // cheap reset: next prepare() starts at the front
}

// ============================================================================
//  Frame parsing
// ============================================================================

ParseResult tryParse(const RecvBuffer& in, const size_t maxPacketSize, PacketView& out) {
    if (in.size() < FRAME_HEADER_SIZE) return ParseResult::NeedMore;

    ByteReader r(in.data(), FRAME_HEADER_SIZE);
    const uint32_t length = r.readU32();
    if (length < FRAME_ID_SIZE || length > maxPacketSize) return ParseResult::Error;

    out.id = static_cast<PacketId>(r.readU16());
    out.payload =  in.data() + FRAME_HEADER_SIZE;
    out.payloadSize = length - FRAME_ID_SIZE;
    out.frameSize = FRAME_HEADER_SIZE + out.payloadSize;

    if (!r.ok() || r.remaining() != 0) return ParseResult::Error;

    if (in.size() < out.frameSize) return ParseResult::NeedMore;

    return ParseResult::Packet;
}

// Token bucket: each bucket starts full (`burstSec` seconds' worth), refills
// continuously at its rate, and every packet takes 1 packet token and
// `bytes` byte tokens.
RateLimit::RateLimit(const double packetsPerSec, const double bytesPerSec, const double burstSec)
    : _pktRate(packetsPerSec),
      _byteRate(bytesPerSec),
      _pktCap(packetsPerSec * burstSec),
      _byteCap(bytesPerSec * burstSec),
      _pktTokens(_pktCap),
      _byteTokens(_byteCap),
      _last(Clock::now()) {}

// Returns false when the budget is exhausted (caller disconnects).
bool RateLimit::allow(const size_t bytes, const TimePoint now) {
    const double elapsed = std::chrono::duration<double>(now - _last).count();
    _last = now;
    if (elapsed > 0.0) {
        _pktTokens  = std::min(_pktCap,  _pktTokens  + elapsed * _pktRate);
        _byteTokens = std::min(_byteCap, _byteTokens + elapsed * _byteRate);
    }

    const auto cost = static_cast<double>(bytes);
    if (_pktTokens < 1.0 || _byteTokens < cost)
        return false;
    _pktTokens  -= 1.0;
    _byteTokens -= cost;
    return true;
}

bool packetAllowedIn(const ConnState s, PacketId id) {
    // Both directions / universal control packets allowed anywhere except when closed
    if (s != ConnState::Closing && (id == PacketId::KeepAlive || id == PacketId::Disconnect)) {
        return true;
    }

    switch (s) {
        case ConnState::Handshake: return id == PacketId::Hello;
        case ConnState::Login:     return id == PacketId::Login;

        case ConnState::Play: {
            // Comprehensive list of all valid incoming client payloads
            constexpr std::array inboundPlayPackets = {
                PacketId::PlayerMove,
                PacketId::BreakBlock,
                PacketId::PlaceBlock,
                PacketId::InventoryAction
            };
            return std::ranges::find(inboundPlayPackets, id) != inboundPlayPackets.end();
        }

        case ConnState::Closing: {
            return false;
        }
    }
    return false;
}


Connection::Connection(const ConnId id, Socket sock, PeerInfo peer, const Config& cfg, const TimePoint now):
    in(cfg.maxPacketSizeIn),
    limiter(cfg.packetsPerSecond, cfg.bytesPerSecond, cfg.burstSeconds),
    connectedAt(now), lastActivity(now), stateSince(now),
    _id(id), _sock(std::move(sock)), _peer(std::move(peer)) {}


// Timeout for the current state (Handshake / Login / Play idle / Closing).
TimePoint   Connection::deadline(const Config& cfg) const {
    switch (state) {
        // Anchored to state change (fixed authorization window)
        case ConnState::Handshake:  return stateSince + cfg.handshakeTimeout;
        case ConnState::Login:      return stateSince + cfg.loginTimeout;
        case ConnState::Closing:    return stateSince + cfg.closingTimeout;

        // Anchored to activity (slides forward on every valid client action)
        case ConnState::Play:       return lastActivity + cfg.idleTimeout;
    }
    return Clock::now();   // defensive: so -Wswitch flags new enumerators
}


} // namespace detail


} // namespace net