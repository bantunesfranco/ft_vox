#include "SelfTest.hpp"
#include "worldgen/Noise.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdio>

using worldgen::Noise;
using worldgen::Octaves;

// Recorded from the first passing run (see Noise.golden).
#define GOLDEN_0 0x3F0FFFBEu
#define GOLDEN_1 0xBE6992BAu
#define GOLDEN_2 0x3D01176Cu
#define GOLDEN_3 0x3D0CEB74u
#define GOLDEN_4 0x3C974BE6u

SELFTEST(Noise, deterministic) {
	const Noise a(1234), b(1234);
	int different = 0;
	for (int i = 0; i < 1000; ++i) {
		const float x = static_cast<float>(i) * 0.37f - 150.0f;
		const float y = static_cast<float>(i % 97) * 0.91f;
		const float z = static_cast<float>(i) * -0.53f + 77.0f;
		different += a.at(x, y, z) != b.at(x, y, z) ? 1 : 0;
		different += a.at(x, z) != b.at(x, z) ? 1 : 0;
	}
	CHECK_EQ(different, 0);
}

SELFTEST(Noise, seed_matters) {
	const Noise a(1), b(2);
	int same = 0;
	for (int i = 0; i < 200; ++i) {
		const float p = static_cast<float>(i) * 0.731f + 0.1f;
		same += a.at(p, -p) == b.at(p, -p) ? 1 : 0;
	}
	CHECK(same < 5);
}

SELFTEST(Noise, range) {
	const Noise noise(99);
	float lo = 1.0f, hi = -1.0f;
	bool inRange = true;
	for (int i = 0; i < 100000; ++i) {
		const float x = static_cast<float>(i % 317) * 0.173f;
		const float y = static_cast<float>(i / 317) * 0.211f;
		const float v3 = noise.at(x, y, x - y);
		const float v2 = noise.at(x, y);
		inRange = inRange && v3 >= -1.0f && v3 <= 1.0f && v2 >= -1.0f && v2 <= 1.0f;
		lo = std::min({lo, v3, v2});
		hi = std::max({hi, v3, v2});
	}
	CHECK(inRange);
	CHECK(lo < -0.5f);
	CHECK(hi > 0.5f);
}

SELFTEST(Noise, continuous) {
	const Noise noise(5);
	float worst = 0.0f;
	for (int i = 0; i < 2000; ++i) {
		const float x = static_cast<float>(i) * 0.0731f, y = static_cast<float>(i) * 0.0419f, z = static_cast<float>(i) * -0.0577f;
		worst = std::max(worst, std::abs(noise.at(x, y, z) - noise.at(x + 0.001f, y, z)));
		worst = std::max(worst, std::abs(noise.at(x, z) - noise.at(x, z + 0.001f)));
	}
	CHECK(worst < 0.01f);
}

SELFTEST(Noise, octaves_range_and_determinism) {
	const Octaves a(7, 4, 0.01f), b(7, 4, 0.01f);
	bool inRange = true, same = true;
	for (int i = 0; i < 5000; ++i) {
		const float x = static_cast<float>(i) * 3.7f, z = static_cast<float>(i) * -2.9f;
		const float v = a.at(x, z);
		inRange = inRange && v >= -1.0f && v <= 1.0f;
		same = same && v == b.at(x, z) && a.at(x, 10.0f, z) == b.at(x, 10.0f, z);
	}
	CHECK(inRange);
	CHECK(same);
}

// Bit-exact values from the first passing build: any other build (other
// compiler, flags, SIMD level) must produce exactly these.
SELFTEST(Noise, golden) {
	const Noise noise(1);
	const float values[] = {
		noise.at(0.5f, 0.25f),
		noise.at(-17.3f, 4.75f),
		noise.at(1.5f, -2.25f, 3.125f),
		noise.at(-1000.37f, 63.5f, 2048.9f),
		Octaves(42, 5, 0.013f).at(123.0f, -77.0f, 5000.0f),
	};
	const std::uint32_t expected[] = {GOLDEN_0, GOLDEN_1, GOLDEN_2, GOLDEN_3, GOLDEN_4};
	for (std::size_t i = 0; i < std::size(values); ++i) {
		const auto bits = std::bit_cast<std::uint32_t>(values[i]);
		if (bits != expected[i])
			std::printf("     golden[%zu] = 0x%08Xu (%.9g)\n", i, bits, static_cast<double>(values[i]));
		CHECK_EQ(bits, expected[i]);
	}
}
