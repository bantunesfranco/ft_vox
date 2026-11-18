#include "WorldStore.hpp"

#include "LittleEndian.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cerrno>
#include <chrono>
#include <fstream>
#include <random>
#include <system_error>

#if defined(_WIN32)
#  include <windows.h>
#else
#  include <fcntl.h>
#  include <sys/file.h>
#  include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace {

constexpr std::array<char, 4> LEVEL_MAGIC{'F', 'V', 'L', 'V'};
constexpr std::uint32_t LEVEL_VERSION = 2;          // 2: time of day appended
constexpr const char* LEVEL_FILE = "level.bin";
constexpr const char* LOCK_FILE = "session.lock";
constexpr std::size_t MAX_FOLDER_CHARS = 32;

std::int64_t nowMillis() {
	return std::chrono::duration_cast<std::chrono::milliseconds>(
		std::chrono::system_clock::now().time_since_epoch()).count();
}

std::string_view trim(std::string_view s) {
	const auto isSpace = [](const char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
	while (!s.empty() && isSpace(s.front())) s.remove_prefix(1);
	while (!s.empty() && isSpace(s.back()))  s.remove_suffix(1);
	return s;
}

std::uint64_t fnv1a(const std::string_view text) {
	std::uint64_t h = 14695981039346656037ull;
	for (const char c : text) {
		h ^= static_cast<unsigned char>(c);
		h *= 1099511628211ull;
	}
	return h;
}

// ---- level.bin ------------------------------------------------------------------

bool writeLevel(const WorldInfo& world, std::string& error) {
	std::string data(LEVEL_MAGIC.begin(), LEVEL_MAGIC.end());
	le::put(data, LEVEL_VERSION);
	le::put(data, world.seed);
	le::put(data, world.lastPlayed);
	const std::size_t nameBytes = std::min<std::size_t>(world.name.size(), 0xFFFF);
	le::put(data, static_cast<std::uint16_t>(nameBytes));
	data.append(world.name, 0, nameBytes);
	le::put(data, world.timeOfDay);

	// Write a temp file, then replace: a crash never leaves a half-written level.bin.
	const fs::path tmp = world.folder / "level.bin.tmp";
	std::error_code ec;
	{
		// close() is where a full disk shows: check it, never rename a short file over level.bin.
		std::ofstream file(tmp, std::ios::binary | std::ios::trunc);
		file.write(data.data(), static_cast<std::streamsize>(data.size()));
		file.close();
		if (!file) {
			error = "cannot write " + tmp.string();
			fs::remove(tmp, ec);
			return false;
		}
	}
	fs::rename(tmp, world.folder / LEVEL_FILE, ec);
	if (ec) {
		error = "cannot write level.bin: " + ec.message();
		return false;
	}
	return true;
}

bool readLevel(const fs::path& folder, WorldInfo& world) {
	std::ifstream file(folder / LEVEL_FILE, std::ios::binary);
	if (!file)
		return false;
	const std::string data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
	if (data.size() < LEVEL_MAGIC.size() || !std::equal(LEVEL_MAGIC.begin(), LEVEL_MAGIC.end(), data.begin()))
		return false;

	std::size_t pos = LEVEL_MAGIC.size();
	std::uint32_t version = 0;
	std::uint16_t nameBytes = 0;
	if (!le::get(data, pos, version) || (version != 1 && version != LEVEL_VERSION)
	    || !le::get(data, pos, world.seed) || !le::get(data, pos, world.lastPlayed)
	    || !le::get(data, pos, nameBytes) || data.size() - pos < nameBytes)
		return false;
	world.name.assign(data, pos, nameBytes);
	pos += nameBytes;
	world.timeOfDay = TimeOfDay::NEW_WORLD;           // version 1 had no clock
	if (version >= 2 && !le::get(data, pos, world.timeOfDay))
		return false;
	world.timeOfDay %= TimeOfDay::TICKS_PER_DAY;
	return pos == data.size();
}

// ---- folder names ------------------------------------------------------------------

bool isReservedName(std::string upper) {
	std::ranges::transform(upper, upper.begin(), [](const unsigned char c) { return static_cast<char>(std::toupper(c)); });
	static const std::array<std::string_view, 4> plain{"CON", "PRN", "AUX", "NUL"};
	if (std::ranges::find(plain, upper) != plain.end())
		return true;
	return upper.size() == 4 && (upper.starts_with("COM") || upper.starts_with("LPT"))
	    && upper[3] >= '1' && upper[3] <= '9';
}

// A folder name that is valid on every OS and stays inside `savesDir`.
std::string folderNameFor(const std::string_view name) {
	std::string out;
	for (const char c : name) {
		const auto u = static_cast<unsigned char>(c);
		const bool keep = (u >= 'a' && u <= 'z') || (u >= 'A' && u <= 'Z') || (u >= '0' && u <= '9')
		               || u == ' ' || u == '_' || u == '-';
		out.push_back(keep ? c : '_');
		if (out.size() >= MAX_FOLDER_CHARS)
			break;
	}
	out = std::string(trim(out));
	if (out.empty() || isReservedName(out))
		out = "World";
	return out;
}

fs::path uniqueFolder(const fs::path& savesDir, const std::string& base) {
	fs::path candidate = savesDir / base;
	for (int n = 2; fs::exists(candidate); ++n)
		candidate = savesDir / (base + " (" + std::to_string(n) + ")");
	return candidate;
}

} // namespace

// ============================================================================
//  Seeds
// ============================================================================

std::uint64_t parseSeed(const std::string_view text, const std::uint64_t randomSeedValue) {
	const std::string_view trimmed = trim(text);
	if (trimmed.empty())
		return randomSeedValue;

	long long number = 0;
	const char* end = trimmed.data() + trimmed.size();
	const auto [ptr, ec] = std::from_chars(trimmed.data(), end, number);
	if (ec == std::errc{} && ptr == end)
		return static_cast<std::uint64_t>(number);
	return fnv1a(trimmed);
}

std::uint64_t randomSeed() {
	std::random_device device;
	const std::uint64_t high = device();
	const std::uint64_t low = device();
	return (high << 32) ^ low ^ static_cast<std::uint64_t>(nowMillis());
}

// ============================================================================
//  Worlds
// ============================================================================

std::vector<WorldInfo> listWorlds(const fs::path& savesDir) {
	std::vector<WorldInfo> worlds;
	std::error_code ec;
	for (const fs::directory_entry& entry : fs::directory_iterator(savesDir, ec)) {
		if (!entry.is_directory(ec))
			continue;
		WorldInfo world;
		world.folder = entry.path();
		if (!readLevel(world.folder, world)) {
			world = {};
			world.folder = entry.path();
			world.name = entry.path().filename().string();
			world.readable = false;
		}
		worlds.push_back(std::move(world));
	}
	std::ranges::sort(worlds, [](const WorldInfo& a, const WorldInfo& b) {
		if (a.lastPlayed != b.lastPlayed)
			return a.lastPlayed > b.lastPlayed;
		return a.folder < b.folder;
	});
	return worlds;
}

std::optional<WorldInfo> createWorld(const fs::path& savesDir, const std::string_view name,
                                     const std::string_view seedText, std::string& error) {
	std::error_code ec;
	fs::create_directories(savesDir, ec);

	WorldInfo world;
	world.name = std::string(trim(name));
	if (world.name.empty())
		world.name = "New World";
	world.folder = uniqueFolder(savesDir, folderNameFor(world.name));
	world.seed = parseSeed(seedText, randomSeed());
	world.lastPlayed = nowMillis();

	if (!fs::create_directories(world.folder, ec) || ec) {
		error = "cannot create " + world.folder.string() + ": " + ec.message();
		return std::nullopt;
	}
	if (!writeLevel(world, error)) {
		fs::remove_all(world.folder, ec);
		return std::nullopt;
	}
	return world;
}

std::optional<WorldInfo> openOrCreateWorld(const fs::path& folder, const std::uint64_t seedIfNew, std::string& error) {
	WorldInfo world;
	world.folder = folder;
	if (readLevel(folder, world))
		return world;
	if (fs::exists(folder / LEVEL_FILE)) {
		error = (folder / LEVEL_FILE).string() + " is damaged";
		return std::nullopt;
	}

	std::error_code ec;
	fs::create_directories(folder, ec);
	if (ec) {
		error = "cannot create " + folder.string() + ": " + ec.message();
		return std::nullopt;
	}
	world.name = folder.filename().string();
	world.seed = seedIfNew;
	world.lastPlayed = nowMillis();
	if (!writeLevel(world, error))
		return std::nullopt;
	return world;
}

bool markPlayed(WorldInfo& world, std::string& error) {
	world.lastPlayed = nowMillis();
	return writeLevel(world, error);
}

bool deleteWorld(const WorldInfo& world, std::string& error) {
	std::error_code ec;
	if (world.folder.empty() || !fs::is_directory(world.folder, ec)) {
		error = "not a world folder: " + world.folder.string();
		return false;
	}
	// Never delete a world another game or server is playing: take its lock first,
	// remove everything but the lock file, then release it and remove the folder.
	auto lock = WorldLock::acquire(world.folder, error);
	if (!lock)
		return false;
	std::vector<fs::path> contents;
	for (const fs::directory_entry& entry : fs::directory_iterator(world.folder, ec))
		if (entry.path().filename() != LOCK_FILE)
			contents.push_back(entry.path());
	for (const fs::path& path : contents) {
		if (ec)
			break;
		fs::remove_all(path, ec);
	}
	lock.reset();
	if (!ec)
		fs::remove_all(world.folder, ec);
	if (ec) {
		error = "cannot delete " + world.folder.string() + ": " + ec.message();
		return false;
	}
	return true;
}

// ============================================================================
//  WorldLock
// ============================================================================

std::unique_ptr<WorldLock> WorldLock::acquire(const fs::path& folder, std::string& error) {
	const fs::path path = folder / LOCK_FILE;
	std::unique_ptr<WorldLock> lock(new WorldLock());
#if defined(_WIN32)
	// Share mode 0: any other open of the file fails until we close it.
	const HANDLE handle = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
	                                  OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (handle == INVALID_HANDLE_VALUE) {
		const DWORD code = GetLastError();
		if (code == ERROR_SHARING_VIOLATION)
			error = "the world is already open in another game or server";
		else
			error = "cannot open " + path.string() + ": " + std::system_category().message(static_cast<int>(code));
		return nullptr;
	}
	lock->_handle = handle;
#else
	const int fd = ::open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0644);
	if (fd < 0) {
		error = "cannot open " + path.string() + ": " + std::generic_category().message(errno);
		return nullptr;
	}
	if (::flock(fd, LOCK_EX | LOCK_NB) != 0) {
		::close(fd);
		error = "the world is already open in another game or server";
		return nullptr;
	}
	lock->_fd = fd;
#endif
	return lock;
}

WorldLock::~WorldLock() {
#if defined(_WIN32)
	if (_handle)
		CloseHandle(static_cast<HANDLE>(_handle));
#else
	if (_fd >= 0)
		::close(_fd);
#endif
}
