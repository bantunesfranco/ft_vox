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
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <thread>

#include "App.hpp"
#include "GameServer.hpp"
#include "SelfTest.hpp"
#include "WorldStore.hpp"
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

} // namespace

int main(const int argc, char** argv)
{
	if (argc >= 2 && std::strcmp(argv[1], "--server") == 0)
		return runDedicatedServer(argc, argv);
	if (argc >= 2 && std::strcmp(argv[1], "--selftest") == 0)
		return runSelfTests(argc, argv);

	std::map<settings_t, bool> settings = {{VOX_FULLSCREEN, false}, {VOX_RESIZE, true}, {VOX_DECORATED, true}};

	const auto app = std::make_unique<App>(1440, 900, "PotatoCraft", settings);
	if (argc >= 2 && std::strcmp(argv[1], "--bench") == 0)
		app->enableBenchmark(argc >= 3 ? std::atof(argv[2]) : 30.0,   // --bench [seconds [x z]]
			argc >= 5 ? glm::vec2(std::atof(argv[3]), std::atof(argv[4])) : glm::vec2(8.0f, 8.0f));
	app->run();
	app->terminate();

	return (EXIT_SUCCESS);
}