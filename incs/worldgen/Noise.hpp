#pragma once
#ifndef WORLDGEN_NOISE_HPP
# define WORLDGEN_NOISE_HPP

// ============================================================================
//  worldgen::Noise : seeded gradient noise that gives the same bits on every
//  compiler, platform and SIMD level.
//
//  Only integer hashing, + - * / and std::floor: no sin/cos/exp/pow/sqrt
//  (libm results differ between platforms), and the build disables FMA
//  contraction for GCC/Clang (-ffp-contract=off; MSVC's /fp:precise already
//  does). Generated terrain is saved forever, so a world must come out the
//  same whichever build generates it.
// ============================================================================

#include <cstdint>
#include <vector>

namespace worldgen {

// Derives an independent seed for one use ("salt") of a world seed.
[[nodiscard]] std::uint64_t mixSeed(std::uint64_t seed, std::uint64_t salt) noexcept;

// Integer hash of a lattice point.
[[nodiscard]] std::uint32_t hash3(std::uint64_t seed, int x, int y, int z) noexcept;

// Gradient (Perlin-style) noise, quintic fade, values in [-1, 1].
class Noise {
public:
	explicit Noise(std::uint64_t seed) noexcept : _seed(seed) {}
	[[nodiscard]] float at(float x, float z) const noexcept;
	[[nodiscard]] float at(float x, float y, float z) const noexcept;

private:
	std::uint64_t _seed;
};

// Fractal sum of `count` independent Noise layers: each layer doubles the
// frequency and multiplies the amplitude by `persistence`. Normalised to [-1, 1].
class Octaves {
public:
	Octaves(std::uint64_t seed, int count, float frequency, float persistence = 0.5f);
	[[nodiscard]] float at(float x, float z) const noexcept;
	[[nodiscard]] float at(float x, float y, float z) const noexcept;

private:
	std::vector<Noise> _layers;
	float _frequency;
	float _persistence;
	float _normalise;
};

} // namespace worldgen

#endif /* WORLDGEN_NOISE_HPP */
