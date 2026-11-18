#include "SelfTest.hpp"
#include "TimeOfDay.hpp"
#include "WorldStore.hpp"

#include <fstream>

namespace fs = std::filesystem;

namespace {
std::uint64_t fnv1a(std::string_view text) {
	std::uint64_t h = 14695981039346656037ull;
	for (const char c : text) {
		h ^= static_cast<unsigned char>(c);
		h *= 1099511628211ull;
	}
	return h;
}
}

SELFTEST(WorldStore, seed_empty_uses_random) {
	CHECK_EQ(parseSeed("", 7), 7ull);
	CHECK_EQ(parseSeed("   ", 9), 9ull);
}

SELFTEST(WorldStore, seed_number) {
	CHECK_EQ(parseSeed("123", 0), 123ull);
	CHECK_EQ(parseSeed(" 42 ", 0), 42ull);
	CHECK_EQ(parseSeed("-1", 0), ~0ull);
	CHECK_EQ(parseSeed("-0", 5), 0ull);
}

SELFTEST(WorldStore, seed_text_stable) {
	CHECK_EQ(parseSeed("potato", 0), parseSeed("potato", 1));
	CHECK(parseSeed("potato", 0) != parseSeed("potato2", 0));
	CHECK_EQ(parseSeed("potato", 0), fnv1a("potato"));
}

SELFTEST(WorldStore, seed_out_of_range_is_text) {
	CHECK_EQ(parseSeed("99999999999999999999", 0), fnv1a("99999999999999999999"));
	CHECK_EQ(parseSeed("12a", 0), fnv1a("12a"));
}

SELFTEST(WorldStore, create_list_delete) {
	const selftest::TempDir dir;
	std::string error;
	auto a = createWorld(dir.path(), "A", "1", error);
	auto b = createWorld(dir.path(), "B", "", error);
	CHECK(a.has_value());
	CHECK(b.has_value());
	if (!a || !b)
		return;
	CHECK_EQ(a->seed, 1ull);
	CHECK(fs::exists(a->folder / "level.bin"));

	CHECK(markPlayed(*a, error));
	const auto worlds = listWorlds(dir.path());
	CHECK_EQ(worlds.size(), std::size_t{2});
	if (worlds.size() == 2) {
		CHECK_EQ(worlds[0].name, std::string("A"));   // just played
		CHECK_EQ(worlds[0].seed, 1ull);
	}

	CHECK(deleteWorld(*b, error));
	CHECK(!fs::exists(b->folder));
	CHECK(fs::exists(a->folder));
	CHECK_EQ(listWorlds(dir.path()).size(), std::size_t{1});
}

SELFTEST(WorldStore, same_name_gets_unique_folder) {
	const selftest::TempDir dir;
	std::string error;
	auto first = createWorld(dir.path(), "Test", "", error);
	auto second = createWorld(dir.path(), "Test", "", error);
	CHECK(first && second);
	if (first && second) {
		CHECK(first->folder != second->folder);
		CHECK_EQ(second->name, std::string("Test"));   // display name kept
	}
}

SELFTEST(WorldStore, bad_names_stay_inside) {
	const selftest::TempDir dir;
	const std::string names[] = {"", "CON", "a/b", "..", "   ", std::string(200, 'x'), "w\xC3\xB6rld", "nul", "COM1"};
	for (const std::string& name : names) {
		std::string error;
		const auto world = createWorld(dir.path(), name, "", error);
		CHECK(world.has_value());
		if (!world)
			continue;
		CHECK_EQ(world->folder.parent_path(), dir.path());
		CHECK(fs::is_directory(world->folder));
		const std::string folderName = world->folder.filename().string();
		CHECK(folderName != "." && folderName != ".." && !folderName.empty());
		CHECK(folderName.size() <= 40);
	}
	CHECK_EQ(listWorlds(dir.path()).size(), std::size(names));
}

SELFTEST(WorldStore, damaged_level_listed_unreadable) {
	const selftest::TempDir dir;
	std::string error;
	const auto world = createWorld(dir.path(), "Broken", "5", error);
	CHECK(world.has_value());
	if (!world)
		return;
	std::ofstream(world->folder / "level.bin", std::ios::binary | std::ios::trunc) << "abc";
	const auto worlds = listWorlds(dir.path());
	CHECK_EQ(worlds.size(), std::size_t{1});
	if (!worlds.empty())
		CHECK(!worlds[0].readable);
}

SELFTEST(WorldStore, open_or_create) {
	const selftest::TempDir dir;
	std::string error;
	const auto created = openOrCreateWorld(dir.path() / "server", 77, error);
	CHECK(created && created->seed == 77);
	const auto reopened = openOrCreateWorld(dir.path() / "server", 99, error);
	CHECK(reopened && reopened->seed == 77);   // existing seed wins
}

SELFTEST(WorldStore, lock_is_exclusive) {
	const selftest::TempDir dir;
	std::string error;
	auto first = WorldLock::acquire(dir.path(), error);
	CHECK(first != nullptr);
	std::string secondError;
	auto second = WorldLock::acquire(dir.path(), secondError);
	CHECK(second == nullptr);
	CHECK(!secondError.empty());
	first.reset();
	auto third = WorldLock::acquire(dir.path(), error);
	CHECK(third != nullptr);
}

SELFTEST(WorldStore, time_saved) {
	const selftest::TempDir dir;
	std::string error;
	auto world = createWorld(dir.path(), "Clock", "3", error);
	CHECK(world.has_value());
	if (!world)
		return;
	CHECK_EQ(world->timeOfDay, TimeOfDay::NEW_WORLD);                // new worlds start in the morning
	world->timeOfDay = 15000;
	CHECK(markPlayed(*world, error));
	const auto worlds = listWorlds(dir.path());
	CHECK(worlds.size() == 1 && worlds[0].readable && worlds[0].timeOfDay == 15000u);
}

SELFTEST(WorldStore, v1_level_loads) {
	// A level.bin written before the time of day existed (version 1).
	const selftest::TempDir dir;
	const fs::path folder = dir.path() / "old";
	fs::create_directories(folder);
	std::string bytes = "FVLV";
	const auto put = [&bytes](const std::uint64_t v, const int n) {
		for (int i = 0; i < n; ++i)
			bytes.push_back(static_cast<char>((v >> (8 * i)) & 0xFF));
	};
	put(1, 4);                  // version
	put(42, 8);                 // seed
	put(1000, 8);               // last played
	put(3, 2);                  // name length
	bytes += "Old";
	std::ofstream(folder / "level.bin", std::ios::binary) << bytes;
	const auto worlds = listWorlds(dir.path());
	CHECK(worlds.size() == 1 && worlds[0].readable);
	if (!worlds.empty()) {
		CHECK_EQ(worlds[0].seed, 42ull);
		CHECK_EQ(worlds[0].name, std::string("Old"));
		CHECK_EQ(worlds[0].timeOfDay, TimeOfDay::NEW_WORLD);
	}
}

SELFTEST(WorldStore, delete_refuses_locked_world) {
	const selftest::TempDir dir;
	std::string error;
	const auto world = createWorld(dir.path(), "Busy", "4", error);
	CHECK(world.has_value());
	if (!world)
		return;
	fs::create_directories(world->folder / "region");
	std::ofstream(world->folder / "region" / "r.0.0.bin", std::ios::binary) << "data";
	auto lock = WorldLock::acquire(world->folder, error);
	CHECK(lock != nullptr);

	std::string deleteError;
	CHECK(!deleteWorld(*world, deleteError));                    // another process plays it
	CHECK(!deleteError.empty());
	CHECK(fs::exists(world->folder / "level.bin"));              // nothing removed
	CHECK(fs::exists(world->folder / "region" / "r.0.0.bin"));

	lock.reset();
	CHECK(deleteWorld(*world, error));
	CHECK(!fs::exists(world->folder));
}

SELFTEST(WorldStore, lock_error_names_the_cause) {
	const selftest::TempDir dir;
	std::string error;
	const auto lock = WorldLock::acquire(dir.path() / "missing", error);   // no such folder
	CHECK(lock == nullptr);
	CHECK(error.find("already open") == std::string::npos);
}

SELFTEST(WorldStore, non_ascii_folder_listed) {
	// A folder made by hand with characters outside the ANSI code page.
	const selftest::TempDir dir;
	const std::u8string name = u8"\u4e16\u754c \u00e9";
	fs::create_directories(dir.path() / fs::path(name));
	const auto worlds = listWorlds(dir.path());       // must not throw
	CHECK_EQ(worlds.size(), std::size_t{1});
	if (!worlds.empty()) {
		CHECK(!worlds[0].readable);
		CHECK(worlds[0].name == std::string(name.begin(), name.end()));   // UTF-8
		CHECK_EQ(pathText(worlds[0].folder.filename()), worlds[0].name);
	}
}
