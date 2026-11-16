#pragma once
#ifndef NET_CLIENT_HPP
# define NET_CLIENT_HPP

// ============================================================================
//  NetClient : the client end of the Transport protocol (one TCP connection)
//
//  Single threaded and non-blocking: call poll() once per frame. It finishes
//  the connect, flushes queued output, reads, and splits the stream into
//  packets that nextPacket() hands out.
//
//  Handled here, never seen by the caller:
//    - Hello{u32 version} + Login{string name}, queued by connect()
//    - KeepAlive (sent when idle, so the server's idle timeout never fires)
//    - Disconnect{u8 reason} from the server  -> state Closed, error() says why
//    - a silent server                         -> state Closed after kRecvTimeout
// ============================================================================

#include <chrono>
#include <deque>
#include <string>

#include "Net.hpp"

namespace net {

class NetClient {
public:
	enum class State : uint8_t {
		Idle,           // connect() not called yet
		Connecting,     // TCP connect in progress
		Connected,      // TCP up; Hello + Login sent (or queued)
		Closed,         // see error()
	};

	struct Packet {
		PacketId id{};
		Buffer   payload;
	};

	explicit NetClient(size_t maxPacketSize = (8u << 20) + 1024);
	~NetClient();
	NetClient(const NetClient&)            = delete;
	NetClient& operator=(const NetClient&) = delete;

	// Starts connecting and queues Hello + Login. Only resolving `host` can
	// block (briefly, for a DNS name). Returns false (state Closed, error()
	// filled) if it failed straight away.
	bool connect(const std::string& host, uint16_t port, const std::string& playerName);

	// Non-blocking: progress the connect, flush, read, parse. Once per frame.
	void poll();

	// The next received gameplay packet, oldest first. False when none is left.
	bool nextPacket(Packet& out);

	// Queue a frame (from ByteWriter::finish()). Sent on the next poll().
	// Ignored once Closed.
	void send(SharedBuffer frame);

	// Best-effort Disconnect packet, then close. Idempotent.
	void close();

	[[nodiscard]] State              state() const noexcept { return _state; }
	[[nodiscard]] const std::string& error() const noexcept { return _error; }

private:
	static constexpr std::chrono::seconds kConnectTimeout{8};
	static constexpr std::chrono::seconds kKeepAliveEvery{5};
	static constexpr std::chrono::seconds kRecvTimeout{30};
	static constexpr size_t               kReadBudget = 4u << 20;   // per poll()

	void fail(std::string why);
	void readAvailable();
	void parseFrames();
	void flush();

	// MUST stay the first member: WSACleanup() runs after the socket closes.
	platform::NetworkRuntime _runtime;

	Socket              _sock;
	detail::RecvBuffer  _in;
	detail::SendQueue   _out;
	std::deque<Packet>  _packets;
	size_t              _maxPacket;

	State       _state = State::Idle;
	std::string _error;
	TimePoint   _connectStarted{};
	TimePoint   _lastRecv{};
	TimePoint   _lastSend{};
};

} // namespace net

#endif /* NET_CLIENT_HPP */
