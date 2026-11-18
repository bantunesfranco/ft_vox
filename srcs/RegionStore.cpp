#include "RegionStore.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <utility>
#include <vector>

#include "ChunkCodec.hpp"

namespace fs = std::filesystem;

namespace {

constexpr std::array<char, 4> MAGIC{'F', 'V', 'R', 'G'};
constexpr std::uint32_t VERSION = 1;
constexpr std::size_t ENTRIES = RegionStore::REGION_SIZE * RegionStore::REGION_SIZE;
constexpr std::size_t ENTRY_SIZE = 8;                                   // u32 offset, u32 length
constexpr std::size_t HEADER_SIZE = MAGIC.size() + 4 + ENTRIES * ENTRY_SIZE;

int floorDiv(const int value, const int divisor) noexcept {
	const int q = value / divisor;
	const int r = value % divisor;
	return (r != 0 && ((r < 0) != (divisor < 0))) ? q - 1 : q;
}

ChunkCoord regionOf(const ChunkCoord& c) noexcept {
	return {floorDiv(c.x, RegionStore::REGION_SIZE), floorDiv(c.z, RegionStore::REGION_SIZE)};
}

std::size_t entryIndex(const ChunkCoord& c) noexcept {
	const ChunkCoord r = regionOf(c);
	return static_cast<std::size_t>((c.x - r.x * RegionStore::REGION_SIZE)
	                              + (c.z - r.z * RegionStore::REGION_SIZE) * RegionStore::REGION_SIZE);
}

void putU32(char* out, const std::uint32_t v) {
	for (int i = 0; i < 4; ++i)
		out[i] = static_cast<char>((v >> (8 * i)) & 0xFF);
}

std::uint32_t getU32(const char* in) {
	std::uint32_t v = 0;
	for (int i = 0; i < 4; ++i)
		v |= static_cast<std::uint32_t>(static_cast<unsigned char>(in[i])) << (8 * i);
	return v;
}

enum class Header { Ok, Damaged, Newer };

// The table of an open region file.
Header readHeader(std::fstream& file, std::vector<char>& header) {
	header.assign(HEADER_SIZE, 0);
	file.clear();
	file.seekg(0);
	if (!file.read(header.data(), static_cast<std::streamsize>(HEADER_SIZE))
	    || std::memcmp(header.data(), MAGIC.data(), MAGIC.size()) != 0)
		return Header::Damaged;
	const std::uint32_t version = getU32(header.data() + MAGIC.size());
	if (version > VERSION)
		return Header::Newer;
	return version == VERSION ? Header::Ok : Header::Damaged;
}

bool writeFreshFile(const fs::path& path) {
	std::vector<char> header(HEADER_SIZE, 0);
	std::memcpy(header.data(), MAGIC.data(), MAGIC.size());
	putU32(header.data() + MAGIC.size(), VERSION);
	std::ofstream file(path, std::ios::binary | std::ios::trunc);
	file.write(header.data(), static_cast<std::streamsize>(header.size()));
	file.close();
	return static_cast<bool>(file);
}

// Renames a damaged file to <file>.bad (replacing an older one), so starting
// it again never destroys the only copy. False when it could not be moved.
bool keepAside(const fs::path& file) {
	fs::path bad = file;
	bad += ".bad";
	std::error_code ec;
	fs::rename(file, bad, ec);
	if (ec) {
		std::cerr << "region file " << file.filename().string() << " is damaged and could not be kept as "
		          << bad.filename().string() << ": " << ec.message() << '\n';
		return false;
	}
	std::cerr << "region file " << file.filename().string() << " is damaged: kept as "
	          << bad.filename().string() << ", started again\n";
	return true;
}

// Where a blob of `length` bytes can go without touching any blob the table
// points at: the first gap that fits, else past the last one.
std::uint64_t freeOffset(const std::vector<char>& header, const std::size_t length) {
	std::vector<std::pair<std::uint64_t, std::uint64_t>> used;   // {offset, end}
	used.reserve(ENTRIES);
	for (std::size_t i = 0; i < ENTRIES; ++i) {
		const char* entry = header.data() + MAGIC.size() + 4 + i * ENTRY_SIZE;
		const std::uint32_t offset = getU32(entry);
		const std::uint32_t size = getU32(entry + 4);
		if (size != 0 && offset >= HEADER_SIZE)
			used.emplace_back(offset, std::uint64_t{offset} + size);
	}
	std::ranges::sort(used);
	std::uint64_t cursor = HEADER_SIZE;
	for (const auto& [offset, end] : used) {
		if (offset >= cursor + length)
			return cursor;
		cursor = std::max(cursor, end);
	}
	return cursor;
}

} // namespace

RegionStore::RegionStore(fs::path regionDir) : _dir(std::move(regionDir)) {}

RegionStore::Region& RegionStore::regionFor(const ChunkCoord& regionCoord) {
	std::lock_guard lock(_regionsMutex);
	auto& region = _regions[regionCoord];
	if (!region)
		region = std::make_unique<Region>();
	return *region;
}

fs::path RegionStore::fileFor(const ChunkCoord& r) const {
	return _dir / ("r." + std::to_string(r.x) + "." + std::to_string(r.z) + ".bin");
}

void RegionStore::touch(Region& region) {
	std::lock_guard lock(_regionsMutex);
	if (region.listed) {
		_open.splice(_open.begin(), _open, region.lru);
	} else {
		_open.push_front(&region);
		region.lru = _open.begin();
		region.listed = true;
	}
	// Close the least recently used files. A region another thread is using
	// is skipped (try_lock: never wait for a region mutex while holding ours).
	auto it = _open.end();
	while (_open.size() > MAX_OPEN_FILES && it != _open.begin()) {
		--it;
		Region* victim = *it;
		if (victim == &region)
			continue;
		std::unique_lock victimLock(victim->mutex, std::try_to_lock);
		if (!victimLock)
			continue;
		victim->file.close();
		victim->valid = false;
		victim->header.clear();
		victim->header.shrink_to_fit();
		victim->listed = false;
		it = _open.erase(it);
	}
}

bool RegionStore::open(Region& region, const ChunkCoord& regionCoord, const bool create) {
	if (region.valid) {
		touch(region);
		return true;
	}
	if (region.newer)
		return false;

	const fs::path path = fileFor(regionCoord);
	region.file.close();
	region.file.clear();
	std::error_code ec;
	if (fs::exists(path, ec)) {
		region.file.open(path, std::ios::binary | std::ios::in | std::ios::out);
		if (!region.file)
			return false;                   // there but not openable now: never start it again
		switch (readHeader(region.file, region.header)) {
			case Header::Ok:
				region.valid = true;
				touch(region);
				return true;
			case Header::Newer:
				region.file.close();
				region.newer = true;
				std::cerr << "region file " << path.filename().string()
				          << " is from a newer version of the game: left untouched\n";
				return false;
			case Header::Damaged:
				region.file.close();
				if (!create || !keepAside(path))
					return false;           // loads find nothing
				break;
		}
	} else if (!create) {
		return false;
	}

	region.file.clear();
	fs::create_directories(_dir, ec);
	if (!writeFreshFile(path))              // start a missing or damaged file
		return false;
	region.file.open(path, std::ios::binary | std::ios::in | std::ios::out);
	region.valid = region.file && readHeader(region.file, region.header) == Header::Ok;
	if (region.valid)
		touch(region);
	return region.valid;
}

std::unique_ptr<ChunkColumn> RegionStore::load(const ChunkCoord& coord) {
	const ChunkCoord r = regionOf(coord);
	Region& region = regionFor(r);
	std::lock_guard lock(region.mutex);
	if (!open(region, r, false))
		return nullptr;

	const char* entry = region.header.data() + MAGIC.size() + 4 + entryIndex(coord) * ENTRY_SIZE;
	const std::uint32_t offset = getU32(entry);
	const std::uint32_t length = getU32(entry + 4);
	if (length == 0 || offset < HEADER_SIZE)
		return nullptr;

	std::fstream& file = region.file;
	file.clear();
	file.seekg(0, std::ios::end);
	const auto fileSize = static_cast<std::uint64_t>(file.tellg());
	if (std::uint64_t{offset} + length > fileSize)
		return nullptr;

	std::vector<std::uint8_t> blob(length);
	file.seekg(offset);
	if (!file.read(reinterpret_cast<char*>(blob.data()), static_cast<std::streamsize>(length))) {
		file.clear();
		return nullptr;
	}

	net::ByteReader in(blob.data(), blob.size());
	const ChunkCoord stored{in.readI32(), in.readI32()};
	if (!in.ok() || stored != coord)
		return nullptr;
	auto column = std::make_unique<ChunkColumn>(coord);
	if (!ChunkCodec::decode(in, *column) || !in.atEnd())
		return nullptr;
	return column;
}

bool RegionStore::save(const ChunkColumn& column) {
	// Encode outside the lock: it is the expensive part.
	net::ByteWriter writer(net::PacketId::ChunkData, 16u * 1024);
	writer.writeI32(column.coord().x).writeI32(column.coord().z);
	ChunkCodec::encode(column, writer);
	const net::SharedBuffer frame = writer.finish();
	const char* blob = reinterpret_cast<const char*>(frame->data()) + net::FRAME_HEADER_SIZE;
	const std::size_t length = frame->size() - net::FRAME_HEADER_SIZE;

	const ChunkCoord r = regionOf(column.coord());
	Region& region = regionFor(r);
	std::lock_guard lock(region.mutex);
	if (!open(region, r, true))
		return false;

	// Space no table entry points at (the column's current blob included):
	// a crash while writing it only damages dead space.
	const std::uint64_t offset = freeOffset(region.header, length);
	if (offset + length > std::numeric_limits<std::uint32_t>::max())
		return false;                       // a region file never grows past 4 GiB
	std::fstream& file = region.file;
	file.clear();
	file.seekp(static_cast<std::streamoff>(offset));
	if (!file.write(blob, static_cast<std::streamsize>(length)) || !file.flush()) {
		region.valid = false;               // reopen next time
		return false;
	}

	// Only now point the table at the new blob.
	char* entry = region.header.data() + MAGIC.size() + 4 + entryIndex(column.coord()) * ENTRY_SIZE;
	putU32(entry, static_cast<std::uint32_t>(offset));
	putU32(entry + 4, static_cast<std::uint32_t>(length));
	file.seekp(entry - region.header.data());
	if (!file.write(entry, ENTRY_SIZE) || !file.flush()) {
		region.valid = false;
		return false;
	}
	return true;
}
