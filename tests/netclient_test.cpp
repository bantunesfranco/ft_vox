// Drives net::NetClient against a running GameServer on 127.0.0.1:<port>.
#include "NetClient.hpp"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>

using namespace net;

#define CHECK(c, msg) do { if (!(c)) { std::printf("FAIL %s\n", msg); std::exit(1); } std::printf("PASS %s\n", msg); } while (0)

int main(int argc, char** argv) {
	if (argc < 2) {
		std::printf("usage: %s <port>\n", argv[0]);
		return 2;
	}
	const auto port = static_cast<uint16_t>(std::atoi(argv[1]));

	{   // refused connection -> Closed with an error, never hangs
		NetClient c;
		c.connect("127.0.0.1", 1, "x");
		auto t0 = Clock::now();
		while (c.state() != NetClient::State::Closed && Clock::now() - t0 < std::chrono::seconds(10)) {
			c.poll();
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}
		std::printf("     error: %s\n", c.error().c_str());
		CHECK(c.state() == NetClient::State::Closed && !c.error().empty(), "connect to closed port fails cleanly");
	}

	NetClient c;
	CHECK(c.connect("localhost", port, "carol"), "connect started (name resolution)");
	bool loginOk = false;
	int chunks = 0;
	auto t0 = Clock::now();
	while (Clock::now() - t0 < std::chrono::seconds(8)) {
		c.poll();
		NetClient::Packet p;
		while (c.nextPacket(p)) {
			if (p.id == PacketId::LoginOk) loginOk = true;
			if (p.id == PacketId::ChunkData) ++chunks;
		}
		if (c.state() == NetClient::State::Closed) break;
		std::this_thread::sleep_for(std::chrono::milliseconds(16));
	}
	CHECK(c.state() == NetClient::State::Connected, "still connected after 8 s");
	CHECK(loginOk, "LoginOk received through NetClient");
	std::printf("     %d columns received\n", chunks);
	CHECK(chunks > 1000, "full view radius streamed through NetClient");

	// Stay idle past the server's keep-alive period: NetClient keep-alives must hold the line.
	t0 = Clock::now();
	while (Clock::now() - t0 < std::chrono::seconds(12)) {
		c.poll();
		NetClient::Packet p;
		while (c.nextPacket(p)) {}
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
	}
	CHECK(c.state() == NetClient::State::Connected, "idle 12 s, still connected (keep-alive both ways)");
	c.close();
	CHECK(c.state() == NetClient::State::Closed, "close()");
	std::printf("ALL PASSED\n");
}
