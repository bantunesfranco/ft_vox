/* ************************************************************************** */
/*                                                                            */
/*                                                        ::::::::            */
/*   main.cpp                                           :+:    :+:            */
/*                                                     +:+                    */
/*   By: bfranco <bfranco@student.codam.nl>           +#+                     */
/*                                                   +#+                      */
/*   Created: 2024/04/21 14:36:49 by bfranco       #+#    #+#                 */
/*   Updated: 2024/09/08 23:12:11 by bfranco       ########   odam.nl         */
/*                                                                            */
/* ************************************************************************** */

#include <algorithm>
#include <charconv>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <thread>

#include "App.hpp"
#include "GameServer.hpp"
#include "SelfTest.hpp"
#include "WorldStore.hpp"
#include "worldgen/TerrainGenerator.hpp"
#include <string_view>

namespace {

GameServer* g_server = nullptr;

void onSignal(int) {
	if (g_server)
		g_server->stop();     // one atomic store: async-signal-safe
}

int serverUsage(const char* exe) {
	std::cerr << "usage: " << exe << " --server [port] [--world <folder>] [--seed <seed>]\n";
	return EXIT_FAILURE;
}

// ft_vox --server [port] [--world <folder>] [--seed <seed>]
// Headless dedicated server, no window. Ctrl+C stops it (after saving).
int runDedicatedServer(const int argc, char** argv) {
	ServerConfig config;
	config.workerThreads = std::max(2u, std::thread::hardware_concurrency()) - 1;   // 0 = unknown
	for (int i = 2; i < argc; ++i) {
		const std::string_view arg = argv[i];
		if (arg == "--world" && i + 1 < argc) {
			config.worldFolder = argv[++i];
		} else if (arg == "--seed" && i + 1 < argc) {
			config.seed = parseSeed(argv[++i], randomSeed());
		} else if (i == 2 && !arg.starts_with("--")) {
			unsigned port = 0;
			const char* end = arg.data() + arg.size();
			const auto [ptr, ec] = std::from_chars(arg.data(), end, port);
			if (ec != std::errc{} || ptr != end || port == 0 || port > 65535)
				return serverUsage(argv[0]);
			config.port = static_cast<std::uint16_t>(port);
		} else {
			return serverUsage(argv[0]);
		}
	}

	GameServer server(config);
	std::string error;
	if (!server.start(error)) {
		std::cerr << "cannot start server: " << error << '\n';
		return EXIT_FAILURE;
	}
	g_server = &server;
	std::signal(SIGINT, onSignal);
	std::signal(SIGTERM, onSignal);
#if defined(SIGBREAK)
	std::signal(SIGBREAK, onSignal);   // Windows Ctrl+Break / CTRL_BREAK_EVENT
#endif
	server.run();
	g_server = nullptr;
	return EXIT_SUCCESS;
}

// ft_vox --selftest-hash <seed> <cx> <cz>: generates one column the way the
// game and the server do and prints a hash of its blocks (FNV-1a 64 over every
// section's block ids as little-endian u16, absent sections as zeros), so
// tests/e2e_server.py can check that both generate the same world.
int runColumnHash(const int argc, char** argv) {
	if (argc != 5) {
		std::cerr << "usage: " << argv[0] << " --selftest-hash <seed> <cx> <cz>\n";
		return EXIT_FAILURE;
	}
	const std::uint64_t seed = parseSeed(argv[2], 0);
	const ChunkCoord coord{std::atoi(argv[3]), std::atoi(argv[4])};
	ChunkColumn column(coord);
	worldgen::TerrainGenerator(seed).generate(column);

	std::uint64_t hash = 14695981039346656037ull;
	const auto mix = [&hash](const std::uint8_t byte) { hash = (hash ^ byte) * 1099511628211ull; };
	for (int s = 0; s < ChunkColumn::SECTION_COUNT; ++s)
		for (const BlockId block : column.section(s).blocks()) {
			mix(static_cast<std::uint8_t>(block & 0xFFu));
			mix(static_cast<std::uint8_t>(block >> 8));
		}
	std::printf("%016llx\n", static_cast<unsigned long long>(hash));
	return EXIT_SUCCESS;
}

} // namespace

int main(const int argc, char** argv)
{
	if (argc >= 2 && std::strcmp(argv[1], "--server") == 0)
		return runDedicatedServer(argc, argv);
	if (argc >= 2 && std::strcmp(argv[1], "--selftest-hash") == 0)
		return runColumnHash(argc, argv);
	if (argc >= 2 && std::strcmp(argv[1], "--selftest") == 0)
		return runSelfTests(argc, argv);

	std::map<settings_t, bool> settings = {{VOX_FULLSCREEN, false}, {VOX_RESIZE, true}, {VOX_DECORATED, true}};

	const auto app = std::make_unique<App>(1440, 900, "PotatoCraft", settings);
	if (argc >= 2 && std::strcmp(argv[1], "--bench") == 0)
		app->enableBenchmark(argc >= 3 ? std::atof(argv[2]) : 30.0,   // --bench [seconds [x z [ticks]]]
			argc >= 5 ? glm::vec2(std::atof(argv[3]), std::atof(argv[4])) : glm::vec2(8.0f, 8.0f),
			argc >= 6 ? static_cast<std::uint32_t>(std::atoi(argv[5])) : 6000u);
	app->run();
	app->terminate();

	return (EXIT_SUCCESS);
}