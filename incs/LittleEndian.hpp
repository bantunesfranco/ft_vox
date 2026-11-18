#ifndef LITTLE_ENDIAN_HPP
#define LITTLE_ENDIAN_HPP

// Little-endian integer read/write on byte strings, for the save-file formats.

#include <cstddef>
#include <string>
#include <type_traits>

namespace le {

template <typename T>
void put(std::string& out, T value) {
	auto u = static_cast<std::make_unsigned_t<T>>(value);
	for (std::size_t i = 0; i < sizeof(T); ++i) {
		out.push_back(static_cast<char>(u & 0xFF));
		u = static_cast<decltype(u)>(u >> 8);
	}
}

// Reads at `pos` and advances it; false (nothing read) when fewer than sizeof(T) bytes remain.
template <typename T>
bool get(const std::string& in, std::size_t& pos, T& value) {
	if (pos > in.size() || in.size() - pos < sizeof(T))
		return false;
	std::make_unsigned_t<T> u = 0;
	for (std::size_t i = 0; i < sizeof(T); ++i)
		u = static_cast<decltype(u)>(u | (static_cast<decltype(u)>(static_cast<unsigned char>(in[pos + i])) << (8 * i)));
	pos += sizeof(T);
	value = static_cast<T>(u);
	return true;
}

} // namespace le

#endif
