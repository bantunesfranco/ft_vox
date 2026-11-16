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

namespace {

GameServer* g_server = nullptr;

void onSignal(int) {
	if (g_server)
		g_server->stop();     // one atomic store: async-signal-safe
}

// ft_vox --server [port]   headless dedicated server, no window. Ctrl+C stops it.
int runDedicatedServer(const int argc, char** argv) {
	ServerConfig config;
	config.workerThreads = std::max(2u, std::thread::hardware_concurrency()) - 1;   // 0 = unknown
	if (argc >= 3) {
		unsigned port = 0;
		const char* end = argv[2] + std::strlen(argv[2]);
		const auto [ptr, ec] = std::from_chars(argv[2], end, port);
		if (ec != std::errc{} || ptr != end || port == 0 || port > 65535) {
			std::cerr << "usage: " << argv[0] << " --server [port]\n";
			return EXIT_FAILURE;
		}
		config.port = static_cast<std::uint16_t>(port);
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
		app->enableBenchmark(argc >= 3 ? std::atof(argv[2]) : 30.0);
	app->run();
	app->terminate();

	return (EXIT_SUCCESS);
}