// ============================================================================
//  Sockets.cpp : Listener and SendQueue
//
//  100% portable: no OS headers, no #ifdef. Everything platform specific is
//  reached through the platform:: functions in Platform.hpp.
// ============================================================================

#include "Net.hpp"


namespace net::detail {

// ============================================================================
//  Listener
// ============================================================================

bool Listener::open(const uint16_t port, const int backlog, std::string& error) {
	uint16_t bound = 0;
	const SocketHandle s = platform::listenTcp(port, backlog, bound, error);
	if (s == platform::kInvalidSocket) return false;
	_sock.reset(s);
	_port = bound;
	return true;
}

AcceptStatus Listener::acceptOne(Socket& out, PeerInfo& peer) const {
	SocketHandle h = platform::kInvalidSocket;
	const AcceptStatus st = platform::acceptTcp(_sock.get(), h, peer.address, peer.port);
	if (st == AcceptStatus::Accepted) out.reset(h);
	return st;
}

// ============================================================================
//  SendQueue
// ============================================================================

void SendQueue::push(SharedBuffer buf) {
	if (!buf || buf->empty()) return;            // empty frames would make a zero-length slice
	_pending += buf->size();
	_q.push_back(OutChunk{std::move(buf), 0});
}

FlushResult SendQueue::flush(const SocketHandle s) {
	while (!_q.empty()) {
		// Gather the front of the queue into one scatter/gather send.
		platform::IoSlice slices[platform::kMaxIoSlices];
		int    count   = 0;
		size_t offered = 0;
		for (auto it = _q.begin(); it != _q.end() && count < platform::kMaxIoSlices; ++it, ++count) {
			const size_t remaining = it->data->size() - it->offset;
			slices[count] = { it->data->data() + it->offset, remaining };
			offered += remaining;
		}

		const std::ptrdiff_t n = platform::sendv(s, slices, count);
		if (n < 0) {
			const int err = platform::lastError();   // read it before anything else runs
			if (platform::isWouldBlock(err))  return FlushResult::WouldBlock;
			if (platform::isInterrupted(err)) continue;
			return FlushResult::Error;
		}
		if (n == 0) return FlushResult::WouldBlock;  // defensive: nothing was accepted

		// Account for what the kernel took: pop finished chunks, advance the partial one.
		auto left = static_cast<size_t>(n);
		_pending -= left;
		while (left > 0) {
			auto&[data, offset] = _q.front();
			const size_t remaining = data->size() - offset;
			if (left >= remaining) { left -= remaining; _q.pop_front(); }
			else                   { offset += left;  left = 0; }
		}

		// A short write on a stream socket means its buffer is full. Another
		// send() would only return WouldBlock, so skip that wasted call.
		if (static_cast<size_t>(n) < offered) return FlushResult::WouldBlock;
	}
	return FlushResult::Drained;
}

} // namespace net::detail
