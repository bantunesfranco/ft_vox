#include "SelfTest.hpp"

#include <chrono>
#include <cstdio>
#include <exception>
#include <string_view>
#include <vector>

namespace selftest {
namespace {

struct Test {
	std::string name;   // "Suite.name"
	TestFn      fn;
};

std::vector<Test>& registry() {
	static std::vector<Test> tests;   // function-local: safe from static init order
	return tests;
}

std::vector<std::string>* currentFailures = nullptr;

} // namespace

Registrar::Registrar(const char* suite, const char* name, const TestFn fn) {
	registry().push_back({std::string(suite) + "." + name, fn});
}

void fail(const char* file, const int line, const std::string& message) {
	const std::string where = std::string(file) + ":" + std::to_string(line) + ": " + message;
	if (currentFailures)
		currentFailures->push_back(where);
	else
		std::fprintf(stderr, "CHECK outside a test: %s\n", where.c_str());
}

} // namespace selftest

int runSelfTests(const int argc, char** argv) {
	const std::string_view filter = argc >= 3 ? argv[2] : "";
	int passed = 0;
	int failed = 0;

	for (const auto& [name, fn] : selftest::registry()) {
		if (!filter.empty() && name.find(filter) == std::string::npos)
			continue;

		std::vector<std::string> failures;
		selftest::currentFailures = &failures;
		const auto start = std::chrono::steady_clock::now();
		try {
			fn();
		} catch (const std::exception& e) {
			failures.push_back(std::string("exception: ") + e.what());
		} catch (...) {
			failures.push_back("unknown exception");
		}
		selftest::currentFailures = nullptr;
		const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();

		if (failures.empty()) {
			++passed;
			std::printf("PASS %s (%.0f ms)\n", name.c_str(), ms);
		} else {
			++failed;
			std::printf("FAIL %s\n", name.c_str());
			for (const std::string& f : failures)
				std::printf("     %s\n", f.c_str());
		}
		std::fflush(stdout);
	}

	std::printf("\n%d passed, %d failed\n", passed, failed);
	return failed == 0 && passed > 0 ? 0 : 1;
}
