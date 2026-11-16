#include "BlockTextures.hpp"

#include <algorithm>

namespace BlockTextures {
namespace {

// Deterministic per-pixel randomness in [0, 1).
float rand01(const int x, const int y, const std::uint32_t salt) noexcept {
	std::uint32_t h = static_cast<std::uint32_t>(x) * 0x8DA6B343u ^ static_cast<std::uint32_t>(y) * 0xD8163841u ^ salt * 0xCB1AB31Fu;
	h ^= h >> 13;
	h *= 0x5BD1E995u;
	h ^= h >> 15;
	return static_cast<float>(h & 0xFFFFFF) / 16777216.0f;
}

struct Rgb { int r, g, b; };

std::uint32_t pack(const Rgb c, const int alpha = 255) noexcept {
	const auto clamp = [](const int v) { return static_cast<std::uint32_t>(std::clamp(v, 0, 255)); };
	return clamp(c.r) | (clamp(c.g) << 8) | (clamp(c.b) << 16) | (clamp(alpha) << 24);
}

Rgb shade(const Rgb c, const float f) noexcept {
	return {static_cast<int>(static_cast<float>(c.r) * f), static_cast<int>(static_cast<float>(c.g) * f),
	        static_cast<int>(static_cast<float>(c.b) * f)};
}

// Grainy fill: every pixel `base` brightened or darkened by up to `spread`.
Pixels noisy(const Rgb base, const float spread, const std::uint32_t salt) {
	Pixels p{};
	for (int y = 0; y < 16; ++y)
		for (int x = 0; x < 16; ++x)
			p[y * 16 + x] = pack(shade(base, 1.0f - spread + 2.0f * spread * rand01(x, y, salt)));
	return p;
}

// Bark: vertical grooves, a little noise.
Pixels logSide(const Rgb bark, const Rgb groove, const std::uint32_t salt) {
	Pixels p{};
	for (int x = 0; x < 16; ++x) {
		const bool isGroove = rand01(x, 0, salt) < 0.3f;
		for (int y = 0; y < 16; ++y) {
			const Rgb c = isGroove && rand01(x, y, salt + 1) < 0.85f ? groove : bark;
			p[y * 16 + x] = pack(shade(c, 0.9f + 0.2f * rand01(x, y, salt + 2)));
		}
	}
	return p;
}

// Birch: white bark with black horizontal marks.
Pixels birchSide() {
	Pixels p = noisy({216, 215, 210}, 0.06f, 31);
	for (int mark = 0; mark < 7; ++mark) {
		const int y = static_cast<int>(rand01(mark, 0, 32) * 16.0f);
		const int x0 = static_cast<int>(rand01(mark, 1, 32) * 13.0f);
		const int len = 2 + static_cast<int>(rand01(mark, 2, 32) * 4.0f);
		for (int x = x0; x < std::min(16, x0 + len); ++x)
			p[y * 16 + x] = pack({40, 40, 38});
	}
	return p;
}

// Log end: growth rings inside a bark border.
Pixels logTop(const Rgb light, const Rgb dark, const Rgb bark, const std::uint32_t salt) {
	Pixels p{};
	for (int y = 0; y < 16; ++y)
		for (int x = 0; x < 16; ++x) {
			const int ring = std::max(std::abs(2 * x - 15), std::abs(2 * y - 15)) / 2;   // 0 (centre) .. 7 (edge)
			const Rgb c = ring >= 7 ? bark : (ring % 2 == 0 ? light : dark);
			p[y * 16 + x] = pack(shade(c, 0.93f + 0.14f * rand01(x, y, salt)));
		}
	return p;
}

// Leaves: clumps of green with holes to see through.
Pixels leaves(const Rgb green, const std::uint32_t salt) {
	Pixels p{};
	for (int y = 0; y < 16; ++y)
		for (int x = 0; x < 16; ++x) {
			const float r = rand01(x, y, salt);
			if (r < 0.18f) {
				p[y * 16 + x] = 0;                         // fully transparent: discarded
				continue;
			}
			p[y * 16 + x] = pack(shade(green, r < 0.45f ? 0.72f : (r > 0.9f ? 1.18f : 0.95f)));
		}
	return p;
}

Pixels gravel() {
	Pixels p{};
	for (int y = 0; y < 16; ++y)
		for (int x = 0; x < 16; ++x) {
			// Pebbles: blocks of 2x2-ish cells with their own tone.
			const float pebble = rand01(x / 2 + (y % 4 < 2 ? 0 : 7), y / 2, 41);
			const int tone = 90 + static_cast<int>(pebble * 80.0f);
			const float grain = 0.9f + 0.2f * rand01(x, y, 42);
			p[y * 16 + x] = pack(shade({tone, tone - 4, tone - 8}, grain));
		}
	return p;
}

Pixels cactusSide() {
	Pixels p{};
	for (int y = 0; y < 16; ++y)
		for (int x = 0; x < 16; ++x) {
			Rgb c = (x == 0 || x == 15) ? Rgb{38, 74, 22} : (x % 4 == 2 ? Rgb{70, 120, 40} : Rgb{86, 140, 50});
			if (x % 4 == 2 && y % 5 == 1)
				c = {220, 220, 180};                       // spines
			p[y * 16 + x] = pack(shade(c, 0.94f + 0.12f * rand01(x, y, 51)));
		}
	return p;
}

Pixels cactusTop() {
	Pixels p{};
	for (int y = 0; y < 16; ++y)
		for (int x = 0; x < 16; ++x) {
			const int edge = std::min({x, y, 15 - x, 15 - y});
			const Rgb c = edge == 0 ? Rgb{38, 74, 22} : (edge < 3 ? Rgb{86, 140, 50} : Rgb{110, 160, 66});
			p[y * 16 + x] = pack(shade(c, 0.95f + 0.1f * rand01(x, y, 52)));
		}
	return p;
}

Pixels sandstone(const bool banded, const float brightness, const std::uint32_t salt) {
	Pixels p{};
	for (int y = 0; y < 16; ++y)
		for (int x = 0; x < 16; ++x) {
			float f = brightness * (0.95f + 0.1f * rand01(x, y, salt));
			if (banded && (y == 3 || y == 11))
				f *= 0.85f;
			if (banded && y >= 12)
				f *= 0.93f;
			p[y * 16 + x] = pack(shade({216, 203, 155}, f));
		}
	return p;
}

Pixels ice() {
	Pixels p{};
	for (int y = 0; y < 16; ++y)
		for (int x = 0; x < 16; ++x) {
			const bool streak = (x + y) % 9 == 0 || (x - y + 16) % 13 == 0;
			const Rgb c = streak ? Rgb{235, 245, 255} : Rgb{150, 190, 245};
			p[y * 16 + x] = pack(shade(c, 0.96f + 0.08f * rand01(x, y, 61)), 190);
		}
	return p;
}

} // namespace

std::vector<Pixels> generate() {
	std::vector<Pixels> out(generatedCount());
	const auto set = [&out](const Texture t, Pixels pixels) { out[static_cast<std::size_t>(t)] = pixels; };

	set(Texture::OakLogSide,      logSide({104, 82, 50}, {72, 55, 32}, 11));
	set(Texture::OakLogTop,       logTop({184, 148, 95}, {156, 122, 74}, {104, 82, 50}, 12));
	set(Texture::BirchLogSide,    birchSide());
	set(Texture::BirchLogTop,     logTop({214, 196, 140}, {190, 170, 116}, {216, 215, 210}, 13));
	set(Texture::SpruceLogSide,   logSide({60, 42, 24}, {42, 29, 16}, 14));
	set(Texture::SpruceLogTop,    logTop({150, 110, 66}, {120, 86, 50}, {60, 42, 24}, 15));
	set(Texture::OakLeaves,       leaves({72, 128, 40}, 21));
	set(Texture::BirchLeaves,     leaves({112, 152, 66}, 22));
	set(Texture::SpruceLeaves,    leaves({48, 88, 52}, 23));
	set(Texture::Gravel,          gravel());
	set(Texture::Clay,            noisy({158, 164, 176}, 0.05f, 43));
	set(Texture::CactusSide,      cactusSide());
	set(Texture::CactusTop,       cactusTop());
	set(Texture::SandstoneSide,   sandstone(true, 1.0f, 53));
	set(Texture::SandstoneTop,    sandstone(false, 1.04f, 54));
	set(Texture::SandstoneBottom, sandstone(false, 0.92f, 55));
	set(Texture::Ice,             ice());
	return out;
}

// A row of sprites in the empty bottom part of the 2048x2048 atlas, 8 px apart.
AtlasSpot atlasSpot(const std::uint16_t textureIndex) {
	const int i = textureIndex - FIRST_GENERATED;
	return {16 + (i % 64) * 24, 1856 + (i / 64) * 24};
}

} // namespace BlockTextures
