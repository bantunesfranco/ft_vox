// Portable loopback test of the platform layer (runs on Linux and on Windows).
#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <winsock2.h>
#  include <ws2tcpip.h>
#else
#  include <arpa/inet.h>
#  include <netinet/in.h>
#  include <sys/socket.h>
#endif
#include "Net.hpp"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>
using namespace net;
using Clk = std::chrono::steady_clock;

#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); std::fflush(stdout); std::exit(1);} } while (0)

static uint8_t pat(size_t i) { return static_cast<uint8_t>(i * 31 + 7); }

int main() {
	platform::NetworkRuntime rt;
	CHECK(rt.ok());

	// 1. listener on an ephemeral port
	detail::Listener lis; std::string err;
	CHECK(lis.open(0, 16, err));
	CHECK(lis.port() != 0);

	// 2. poll with only a listener and nothing pending -> times out (0), takes about the timeout
	std::vector<PollItem> items{{lis.handle(), 1, true, false}};
	std::vector<PollResult> res;
	auto t0 = Clk::now();
	CHECK(pollSockets(items, res, 60) == 0 && res.empty());
	CHECK(Clk::now() - t0 >= std::chrono::milliseconds(40));
	// 2b. accept with nothing pending
	{ Socket s; PeerInfo p; CHECK(lis.acceptOne(s, p) == platform::AcceptStatus::WouldBlock); }
	// 2c. empty poll set just sleeps
	{ std::vector<PollItem> none; t0 = Clk::now(); CHECK(pollSockets(none, res, 30) == 0); CHECK(Clk::now() - t0 >= std::chrono::milliseconds(20)); }

	// 3. a client connects (blocking socket, plain BSD API)
	using Native = decltype(::socket(0, 0, 0));
	const auto client = static_cast<SocketHandle>(::socket(AF_INET, SOCK_STREAM, 0));
	CHECK(client != platform::kInvalidSocket);
	sockaddr_in a{}; a.sin_family = AF_INET; a.sin_port = htons(lis.port());
	CHECK(::inet_pton(AF_INET, "127.0.0.1", &a.sin_addr) == 1);
	CHECK(::connect(static_cast<Native>(client), reinterpret_cast<sockaddr*>(&a), sizeof(a)) == 0);

	// 4. poll reports the listener readable; accept it; a second accept would block
	CHECK(pollSockets(items, res, 1000) == 1 && res[0].readable && res[0].tag == 1);
	Socket srv; PeerInfo peer;
	CHECK(lis.acceptOne(srv, peer) == platform::AcceptStatus::Accepted);
	CHECK(peer.address == "127.0.0.1" && peer.port != 0);
	{ Socket s2; PeerInfo p2; CHECK(lis.acceptOne(s2, p2) == platform::AcceptStatus::WouldBlock); }

	// 5. accepted socket is non-blocking: recv with no data -> would block
	{ char b[8]; auto n = platform::recvSome(srv.get(), b, sizeof(b));
	  CHECK(n < 0 && platform::isWouldBlock(platform::lastError())); }

	// 6. fill the pipe until the OS pushes back. The client is not reading, so some flush
	//    must eventually return WouldBlock. We do NOT assume how much the OS can buffer:
	//    Linux may take a few MB, a large SO_SNDBUF takes far more, and Windows accepts one
	//    big send whole. So push small chunks and flush after each, until it blocks.
	constexpr size_t kChunk = 256 * 1000 + 13, kMaxTotal = size_t{1} << 30;   // give up at 1 GiB
	detail::SendQueue q;
	size_t gen = 0, pushed = 0;
	bool sawWouldBlock = false;
	q.push(nullptr);                               // ignored
	q.push(std::make_shared<Buffer>());            // empty, ignored
	CHECK(q.pending() == 0 && q.empty());
	while (pushed < kMaxTotal) {
		auto buf = std::make_shared<Buffer>(kChunk);
		for (auto& b : *buf) b = pat(gen++);
		q.push(std::move(buf));
		pushed += kChunk;
		const auto r = q.flush(srv.get());
		if (r == detail::FlushResult::Error) { std::printf("flush returned Error (%s)\n", platform::errorString(platform::lastError()).c_str()); }
		CHECK(r != detail::FlushResult::Error);
		if (r == detail::FlushResult::WouldBlock) { sawWouldBlock = true; break; }
		CHECK(q.empty() && q.pending() == 0);      // Drained must really mean empty
	}
	if (!sawWouldBlock) std::printf("the OS accepted %zu bytes without ever blocking\n", pushed);
	CHECK(sawWouldBlock);

	std::printf("WouldBlock after %zu bytes pushed, %zu still queued\n", pushed, q.pending());

	// 7. now the client drains everything on another thread; the server keeps flushing
	//    whenever poll says the socket is writable
	const size_t kTotal = pushed;
	size_t got = 0; bool ordered = true;
	std::thread reader([&] {
		std::vector<uint8_t> buf(64 * 1024);
		while (got < kTotal) {
			const auto n = platform::recvSome(client, buf.data(), buf.size());
			if (n <= 0) break;
			for (ptrdiff_t i = 0; i < n; ++i) if (buf[i] != pat(got + i)) ordered = false;
			got += static_cast<size_t>(n);
		}
	});
	items = {{srv.get(), 2, false, true}};
	int writableWakeups = 0;
	auto deadline = Clk::now() + std::chrono::seconds(30);
	while (!q.empty()) {
		CHECK(Clk::now() < deadline);
		int n = pollSockets(items, res, 1000);
		CHECK(n >= 0);
		if (n == 1 && res[0].writable) {
			++writableWakeups;
			const auto r = q.flush(srv.get());
			CHECK(r != detail::FlushResult::Error);
		}
	}
	reader.join();
	CHECK(got == kTotal && ordered && q.pending() == 0);
	std::printf("drained after %d writable wakeups; %zu bytes received in order\n", writableWakeups, got);
	CHECK(writableWakeups >= 1);

	// 8. client closes -> server sees readable and recv()==0
	platform::closeSocket(client);
	items = {{srv.get(), 3, true, false}};
	CHECK(pollSockets(items, res, 2000) == 1 && (res[0].readable || res[0].error));
	char b[8];
	CHECK(platform::recvSome(srv.get(), b, sizeof(b)) == 0);

	// 9. errors have readable text
	CHECK(!platform::errorString(platform::lastError()).empty());

	std::puts("ALL OK");
}