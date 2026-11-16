#include "RegionStore.hpp"

#include <array>
#include <cstring>
#include <fstream>
#include <limits>
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

// The table of a region file, or false when the file is absent or damaged.
bool readHeader(std::fstream& file, std::vector<char>& header) {
	header.assign(HEADER_SIZE, 0);
	file.seekg(0);
	if (!file.read(header.data(), static_cast<std::streamsize>(HEADER_SIZE)))
		return false;
	return std::memcmp(header.data(), MAGIC.data(), MAGIC.size()) == 0
	    && getU32(header.data() + MAGIC.size()) == VERSION;
}

bool writeFreshFile(const fs::path& path) {
	std::vector<char> header(HEADER_SIZE, 0);
	std::memcpy(header.data(), MAGIC.data(), MAGIC.size());
	putU32(header.data() + MAGIC.size(), VERSION);
	std::ofstream file(path, std::ios::binary | std::ios::trunc);
	return static_cast<bool>(file.write(header.data(), static_cast<std::streamsize>(header.size())));
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

bool RegionStore::open(Region& region, const ChunkCoord& regionCoord, const bool create) {
	if (region.valid)
		return true;

	const fs::path path = fileFor(regionCoord);
	region.file.close();
	region.file.clear();
	region.file.open(path, std::ios::binary | std::ios::in | std::ios::out);
	if (region.file && readHeader(region.file, region.header)) {
		region.valid = true;
		return true;
	}
	if (!create)
		return false;                       // absent or damaged: loads find nothing

	region.file.close();
	region.file.clear();
	std::error_code ec;
	fs::create_directories(_dir, ec);
	if (!writeFreshFile(path))              // start a missing or damaged file again
		return false;
	region.file.open(path, std::ios::binary | std::ios::in | std::ios::out);
	region.valid = region.file && readHeader(region.file, region.header);
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

	std::fstream& file = region.file;
	file.clear();
	file.seekp(0, std::ios::end);
	const auto offset = static_cast<std::uint64_t>(file.tellp());
	if (offset + length > std::numeric_limits<std::uint32_t>::max())
		return false;                       // a region file never grows past 4 GiB
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
