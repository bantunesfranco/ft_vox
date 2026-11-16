#include "worldgen/Noise.hpp"

#include <algorithm>
#include <cmath>

namespace worldgen {
namespace {

constexpr std::uint64_t splitmix(std::uint64_t z) noexcept {
	z += 0x9E3779B97F4A7C15ull;
	z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
	z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
	return z ^ (z >> 31);
}

constexpr float fade(const float t) noexcept {
	return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

constexpr float lerp(const float a, const float b, const float t) noexcept {
	return a + (b - a) * t;
}

// Dot product of the lattice point's gradient with the offset (dx, dz).
// 8 directions: the axes and the diagonals (unit length).
float grad2(const std::uint32_t hash, const float dx, const float dz) noexcept {
	constexpr float d = 0.70710678f;
	switch (hash & 7u) {
		case 0:  return  dx;
		case 1:  return -dx;
		case 2:  return  dz;
		case 3:  return -dz;
		case 4:  return ( dx + dz) * d;
		case 5:  return (-dx + dz) * d;
		case 6:  return ( dx - dz) * d;
		default: return (-dx - dz) * d;
	}
}

// 12 gradients: the edge midpoints of a cube (improved Perlin noise),
// 16 cases so the hash maps evenly.
float grad3(const std::uint32_t hash, const float x, const float y, const float z) noexcept {
	switch (hash & 15u) {
		case 0:  return  x + y;
		case 1:  return -x + y;
		case 2:  return  x - y;
		case 3:  return -x - y;
		case 4:  return  x + z;
		case 5:  return -x + z;
		case 6:  return  x - z;
		case 7:  return -x - z;
		case 8:  return  y + z;
		case 9:  return -y + z;
		case 10: return  y - z;
		case 11: return -y - z;
		case 12: return  x + y;
		case 13: return -y + z;
		case 14: return -x + y;
		default: return -y - z;
	}
}

int floorToInt(const float v) noexcept {
	return static_cast<int>(std::floor(v));
}

} // namespace

std::uint64_t mixSeed(const std::uint64_t seed, const std::uint64_t salt) noexcept {
	return splitmix(seed ^ splitmix(salt + 0x632BE59BD9B4E019ull));
}

std::uint32_t hash3(const std::uint64_t seed, const int x, const int y, const int z) noexcept {
	std::uint64_t h = seed;
	h = splitmix(h ^ (static_cast<std::uint64_t>(static_cast<std::uint32_t>(x)) * 0x9E3779B97F4A7C15ull));
	h = splitmix(h ^ (static_cast<std::uint64_t>(static_cast<std::uint32_t>(y)) * 0xC2B2AE3D27D4EB4Full));
	h = splitmix(h ^ (static_cast<std::uint64_t>(static_cast<std::uint32_t>(z)) * 0x165667B19E3779F9ull));
	return static_cast<std::uint32_t>(h >> 32);
}

float Noise::at(const float x, const float z) const noexcept {
	const int x0 = floorToInt(x);
	const int z0 = floorToInt(z);
	const float fx = x - static_cast<float>(x0);
	const float fz = z - static_cast<float>(z0);

	const float n00 = grad2(hash3(_seed, x0,     0, z0),     fx,        fz);
	const float n10 = grad2(hash3(_seed, x0 + 1, 0, z0),     fx - 1.0f, fz);
	const float n01 = grad2(hash3(_seed, x0,     0, z0 + 1), fx,        fz - 1.0f);
	const float n11 = grad2(hash3(_seed, x0 + 1, 0, z0 + 1), fx - 1.0f, fz - 1.0f);

	const float u = fade(fx);
	const float v = fade(fz);
	const float value = lerp(lerp(n00, n10, u), lerp(n01, n11, u), v);
	return std::clamp(value * 1.41421356f, -1.0f, 1.0f);   // 2D range is +-sqrt(1/2)
}

float Noise::at(const float x, const float y, const float z) const noexcept {
	const int x0 = floorToInt(x);
	const int y0 = floorToInt(y);
	const int z0 = floorToInt(z);
	const float fx = x - static_cast<float>(x0);
	const float fy = y - static_cast<float>(y0);
	const float fz = z - static_cast<float>(z0);

	const auto corner = [&](const int ix, const int iy, const int iz) {
		return grad3(hash3(_seed, x0 + ix, y0 + iy, z0 + iz),
		             fx - static_cast<float>(ix), fy - static_cast<float>(iy), fz - static_cast<float>(iz));
	};
	const float u = fade(fx);
	const float v = fade(fy);
	const float w = fade(fz);
	const float x00 = lerp(corner(0, 0, 0), corner(1, 0, 0), u);
	const float x10 = lerp(corner(0, 1, 0), corner(1, 1, 0), u);
	const float x01 = lerp(corner(0, 0, 1), corner(1, 0, 1), u);
	const float x11 = lerp(corner(0, 1, 1), corner(1, 1, 1), u);
	const float value = lerp(lerp(x00, x10, v), lerp(x01, x11, v), w);
	return std::clamp(value, -1.0f, 1.0f);
}

Octaves::Octaves(const std::uint64_t seed, const int count, const float frequency, const float persistence)
	: _frequency(frequency), _persistence(persistence) {
	float amplitude = 1.0f;
	float total = 0.0f;
	for (int i = 0; i < std::max(1, count); ++i) {
		_layers.emplace_back(mixSeed(seed, static_cast<std::uint64_t>(i)));
		total += amplitude;
		amplitude *= persistence;
	}
	_normalise = 1.0f / total;
}

float Octaves::at(const float x, const float z) const noexcept {
	float sum = 0.0f, amplitude = 1.0f, frequency = _frequency;
	for (const Noise& layer : _layers) {
		sum += layer.at(x * frequency, z * frequency) * amplitude;
		amplitude *= _persistence;
		frequency *= 2.0f;
	}
	return sum * _normalise;
}

float Octaves::at(const float x, const float y, const float z) const noexcept {
	float sum = 0.0f, amplitude = 1.0f, frequency = _frequency;
	for (const Noise& layer : _layers) {
		sum += layer.at(x * frequency, y * frequency, z * frequency) * amplitude;
		amplitude *= _persistence;
		frequency *= 2.0f;
	}
	return sum * _normalise;
}

} // namespace worldgen
