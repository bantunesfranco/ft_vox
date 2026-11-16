// ============================================================================
//  NetClient.cpp : portable (no OS headers), like the rest of the transport
// ============================================================================

#include "NetClient.hpp"

#include <utility>

namespace net {

NetClient::NetClient(const size_t maxPacketSize)
	: _in(maxPacketSize), _maxPacket(maxPacketSize) {}

NetClient::~NetClient() {
	close();
}

bool NetClient::connect(const std::string& host, const uint16_t port, const std::string& playerName) {
	if (_state != State::Idle) { fail("connect called twice"); return false; }
	if (!_runtime.ok())        { fail(_runtime.error());       return false; }

	bool inProgress = false;
	std::string err;
	const SocketHandle s = platform::connectTcp(host, port, inProgress, err);
	if (s == platform::kInvalidSocket) { fail(err); return false; }
	_sock.reset(s);

	const TimePoint now = Clock::now();
	_connectStarted = now;
	_lastRecv       = now;
	_state          = inProgress ? State::Connecting : State::Connected;

	// Queued now, flushed as soon as the TCP connection is up.
	ByteWriter hello(PacketId::Hello);
	hello.writeU32(PROTOCOL_VERSION);
	send(hello.finish());
	ByteWriter login(PacketId::Login);
	login.writeString(playerName.substr(0, 32));
	send(login.finish());
	return true;
}

void NetClient::send(SharedBuffer frame) {
	if (_state != State::Connecting && _state != State::Connected) return;
	_out.push(std::move(frame));
	_lastSend = Clock::now();
}

void NetClient::poll() {
	const TimePoint now = Clock::now();

	if (_state == State::Connecting) {
		std::vector<PollItem>   items{{_sock.get(), 0, false, true}};
		std::vector<PollResult> results;
		if (pollSockets(items, results, 0) < 0) { fail("poll: " + platform::errorString(platform::lastError())); return; }

		if (!results.empty() && (results[0].writable || results[0].error)) {
			if (const int err = platform::socketError(_sock.get()); err != 0) {
				fail("connect: " + platform::errorString(err));
				return;
			}
			_state    = State::Connected;
			_lastRecv = now;
		} else if (now - _connectStarted > kConnectTimeout) {
			fail("connect timed out");      // also covers old WSAPoll missing a failed connect
			return;
		} else {
			return;
		}
	}
	if (_state != State::Connected) return;

	flush();
	if (_state != State::Connected) return;
	readAvailable();
	if (_state != State::Connected) return;

	if (now - _lastSend >= kKeepAliveEvery) {
		send(ByteWriter(PacketId::KeepAlive, 0).finish());
		flush();
	}
	if (now - _lastRecv >= kRecvTimeout)
		fail("server timed out");
}

bool NetClient::nextPacket(Packet& out) {
	if (_packets.empty()) return false;
	out = std::move(_packets.front());
	_packets.pop_front();
	return true;
}

void NetClient::close() {
	if (_state == State::Connected) {
		ByteWriter bye(PacketId::Disconnect, 1);
		bye.write(DisconnectReason::PeerClosed);
		_out.push(bye.finish());
		(void)_out.flush(_sock.get());   // best effort: never wait on a full socket
	}
	_sock.reset();
	if (_state != State::Closed) {
		_state = State::Closed;
		if (_error.empty()) _error = "disconnected";
	}
}

// ---- internals -----------------------------------------------------------------

void NetClient::fail(std::string why) {
	if (_state == State::Closed) return;
	_error = std::move(why);
	_state = State::Closed;
	_sock.reset();
}

void NetClient::flush() {
	if (_out.empty()) return;
	if (_out.flush(_sock.get()) == detail::FlushResult::Error)
		fail("send failed: " + platform::errorString(platform::lastError()));
	// WouldBlock: the rest goes out on a later poll()
}

void NetClient::readAvailable() {
	size_t budget = kReadBudget;
	while (budget > 0 && _state == State::Connected) {
		constexpr size_t kChunk = 64u * 1024;
		uint8_t* dst = _in.prepare(kChunk);
		if (!dst) { fail("receive buffer overflow"); return; }

		const std::ptrdiff_t n = platform::recvSome(_sock.get(), dst, kChunk);
		if (n > 0) {
			_in.commit(static_cast<size_t>(n));
			_lastRecv = Clock::now();
			budget -= std::min(budget, static_cast<size_t>(n));
			parseFrames();                 // keep the buffer small
			continue;
		}
		if (n == 0) { parseFrames(); fail("server closed the connection"); return; }

		const int err = platform::lastError();
		if (platform::isInterrupted(err)) continue;
		if (platform::isWouldBlock(err))  break;
		fail("receive failed: " + platform::errorString(err));
		return;
	}
}

void NetClient::parseFrames() {
	detail::PacketView pv;
	while (_state == State::Connected) {
		const detail::ParseResult r = detail::tryParse(_in, _maxPacket, pv);
		if (r == detail::ParseResult::NeedMore) return;
		if (r == detail::ParseResult::Error) { fail("protocol error from server"); return; }

		if (pv.id == PacketId::Disconnect) {
			ByteReader body(pv.payload, pv.payloadSize);
			const auto why = body.read<DisconnectReason>();
			fail(std::string("disconnected by server: ") + (body.ok() ? toString(why) : "no reason"));
			return;
		}
		if (pv.id != PacketId::KeepAlive)
			_packets.push_back({pv.id, Buffer(pv.payload, pv.payload + pv.payloadSize)});
		_in.consume(pv.frameSize);
	}
}

} // namespace net
