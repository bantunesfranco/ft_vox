#include "PlayerStore.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <span>
#include <vector>

#include "LittleEndian.hpp"
#include "PlayerCollision.hpp"

namespace fs = std::filesystem;

namespace {

constexpr std::array<char, 4> PLAYER_MAGIC{'F', 'V', 'P', 'L'};
constexpr std::uint32_t PLAYER_VERSION = 1;

void putFloat(std::string& out, const float value) {
	le::put(out, std::bit_cast<std::uint32_t>(value));
}

bool getFloat(const std::string& in, std::size_t& pos, float& value) {
	std::uint32_t bits = 0;
	if (!le::get(in, pos, bits))
		return false;
	value = std::bit_cast<float>(bits);
	return true;
}

// Renames a damaged file to <file>.bad (replacing an older one), so the next
// save cannot overwrite the only copy.
void keepAside(const fs::path& file, const char* reason) {
	fs::path bad = file;
	bad += ".bad";
	std::error_code ec;
	fs::rename(file, bad, ec);
	std::cerr << "player file " << file.string() << " ignored (" << reason << "): ";
	if (ec)
		std::cerr << "could not keep it as " << bad.string() << ": " << ec.message() << '\n';
	else
		std::cerr << "kept as " << bad.string() << '\n';
}

} // namespace

fs::path playerFile(const fs::path& worldFolder, const std::string_view name) {
	static constexpr char DIGITS[] = "0123456789abcdef";
	std::string stem;
	for (const char c : name) {
		const auto byte = static_cast<unsigned char>(c);
		stem.push_back(DIGITS[byte >> 4]);
		stem.push_back(DIGITS[byte & 0xF]);
	}
	return worldFolder / "players" / (stem + ".bin");
}

fs::path singleplayerFile(const fs::path& worldFolder) {
	return worldFolder / "player.bin";
}

bool savePlayer(const fs::path& file, PlayerRecord record, std::string& error) {
	record.inventory.returnCursorStack();

	std::vector<std::uint8_t> inventoryBytes;
	encodeInventory(record.inventory, inventoryBytes);

	std::string data(PLAYER_MAGIC.begin(), PLAYER_MAGIC.end());
	le::put(data, PLAYER_VERSION);
	putFloat(data, record.eye.x);
	putFloat(data, record.eye.y);
	putFloat(data, record.eye.z);
	putFloat(data, record.yaw);
	putFloat(data, record.pitch);
	data.append(inventoryBytes.begin(), inventoryBytes.end());

	std::error_code ec;
	if (file.has_parent_path()) {
		fs::create_directories(file.parent_path(), ec);
		if (ec) {
			error = "cannot create " + file.parent_path().string() + ": " + ec.message();
			return false;
		}
	}

	// Write a temp file, then replace: a crash never leaves a half-written player file.
	fs::path tmp = file;
	tmp += ".tmp";
	{
		std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
		if (!out.write(data.data(), static_cast<std::streamsize>(data.size())) || !out.flush()) {
			error = "cannot write " + tmp.string();
			return false;
		}
	}
	fs::rename(tmp, file, ec);
	if (ec) {
		error = "cannot write " + file.string() + ": " + ec.message();
		return false;
	}
	return true;
}

namespace {

// The record in a player file's bytes, or nullopt and why not.
std::optional<PlayerRecord> parsePlayer(const std::string& data, const char*& reason) {
	if (data.size() < PLAYER_MAGIC.size() || !std::equal(PLAYER_MAGIC.begin(), PLAYER_MAGIC.end(), data.begin())) {
		reason = "bad magic";
		return std::nullopt;
	}
	std::size_t pos = PLAYER_MAGIC.size();
	std::uint32_t version = 0;
	if (!le::get(data, pos, version) || version != PLAYER_VERSION) {
		reason = "unknown version";
		return std::nullopt;
	}

	PlayerRecord record;
	if (!getFloat(data, pos, record.eye.x) || !getFloat(data, pos, record.eye.y) || !getFloat(data, pos, record.eye.z)
	    || !getFloat(data, pos, record.yaw) || !getFloat(data, pos, record.pitch)) {
		reason = "truncated";
		return std::nullopt;
	}
	if (!PlayerCollision::plausibleEye(record.eye) || !std::isfinite(record.yaw) || !std::isfinite(record.pitch)) {
		reason = "position outside the world or non-finite look";
		return std::nullopt;
	}
	record.pitch = std::clamp(record.pitch, -90.0f, 90.0f);

	const std::size_t rest = data.size() - pos;
	if (rest != Inventory::ENCODED_SIZE) {
		reason = rest < Inventory::ENCODED_SIZE ? "truncated" : "trailing bytes";
		return std::nullopt;
	}
	const auto* bytes = reinterpret_cast<const std::uint8_t*>(data.data()) + pos;
	std::optional<Inventory> inventory = decodeInventory(std::span<const std::uint8_t>(bytes, rest));
	if (!inventory) {
		reason = "invalid inventory";
		return std::nullopt;
	}
	record.inventory = *inventory;
	return record;
}

} // namespace

std::optional<PlayerRecord> loadPlayer(const fs::path& file) {
	std::error_code ec;
	if (!fs::exists(file, ec))
		return std::nullopt;                              // a new player, silently

	const char* reason = "cannot be opened";              // e.g. locked
	{
		std::ifstream in(file, std::ios::binary);         // closed before the rename below
		if (in) {
			const std::string data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
			if (std::optional<PlayerRecord> record = parsePlayer(data, reason))
				return record;
		}
	}
	keepAside(file, reason);
	return std::nullopt;
}
