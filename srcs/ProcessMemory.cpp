#include "ProcessMemory.hpp"

#if defined(_WIN32)
# include <windows.h>
# include <psapi.h>

namespace {
bool counters(PROCESS_MEMORY_COUNTERS& out) noexcept {
	out = {};
	out.cb = sizeof out;
	return GetProcessMemoryInfo(GetCurrentProcess(), &out, sizeof out) != 0;
}
}

std::size_t ProcessMemory::currentBytes() noexcept {
	PROCESS_MEMORY_COUNTERS c;
	return counters(c) ? c.WorkingSetSize : 0;
}

std::size_t ProcessMemory::peakBytes() noexcept {
	PROCESS_MEMORY_COUNTERS c;
	return counters(c) ? c.PeakWorkingSetSize : 0;
}

#elif defined(__linux__)
# include <cstdio>
# include <cstring>

namespace {
// A "VmRSS:   1234 kB" line of /proc/self/status, in bytes.
std::size_t statusKilobytes(const char* key) noexcept {
	std::FILE* f = std::fopen("/proc/self/status", "r");
	if (!f)
		return 0;
	char line[256];
	std::size_t bytes = 0;
	const std::size_t keyLength = std::strlen(key);
	while (std::fgets(line, sizeof line, f)) {
		if (std::strncmp(line, key, keyLength) == 0) {
			unsigned long long kb = 0;
			if (std::sscanf(line + keyLength, " %llu", &kb) == 1)
				bytes = static_cast<std::size_t>(kb) * 1024;
			break;
		}
	}
	std::fclose(f);
	return bytes;
}
}

std::size_t ProcessMemory::currentBytes() noexcept { return statusKilobytes("VmRSS:"); }
std::size_t ProcessMemory::peakBytes() noexcept { return statusKilobytes("VmHWM:"); }

#else   // macOS and others: the peak from getrusage, the current one is not exposed here
# include <sys/resource.h>

std::size_t ProcessMemory::currentBytes() noexcept { return peakBytes(); }

std::size_t ProcessMemory::peakBytes() noexcept {
	rusage usage{};
	if (getrusage(RUSAGE_SELF, &usage) != 0)
		return 0;
# if defined(__APPLE__)
	return static_cast<std::size_t>(usage.ru_maxrss);          // bytes
# else
	return static_cast<std::size_t>(usage.ru_maxrss) * 1024;   // kilobytes
# endif
}

#endif
