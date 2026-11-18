#include "worldgen/Caves.hpp"

#include <algorithm>
#include <cmath>

#include "Chunk.hpp"
#include "worldgen/Spline.hpp"

namespace worldgen {
namespace {

// ---- tune here -----------------------------------------------------------------------

constexpr float RAVINE_BAND     = 0.06f;   // half width of the ridge band (noise units)
constexpr float TUNNEL_Y_SQUASH = 0.7f;    // < 1: tunnels run more horizontally

} // namespace

CaveCarver::CaveCarver(const std::uint64_t seed)
	: _cheese(mixSeed(seed, 1), 3, 1.0f / 64.0f),
	  _tunnelA(mixSeed(seed, 2), 1, 1.0f / 56.0f),
	  _tunnelB(mixSeed(seed, 3), 1, 1.0f / 56.0f),
	  _ravine(mixSeed(seed, 4), 2, 1.0f / 160.0f),
	  _ravineRegion(mixSeed(seed, 5), 2, 1.0f / 500.0f),
	  _entrance(mixSeed(seed, 6), 1, 1.0f / 80.0f) {}

CaveCarver::Sample CaveCarver::sample(const int x, const int y, const int z) const noexcept {
	const float fx = static_cast<float>(x), fy = static_cast<float>(y), fz = static_cast<float>(z);
	return {
		_cheese.at(fx, fy * 1.6f, fz),
		_tunnelA.at(fx, fy * TUNNEL_Y_SQUASH, fz),
		_tunnelB.at(fx + 1000.0f, fy * TUNNEL_Y_SQUASH, fz - 1000.0f),
	};
}

float CaveCarver::ravineStrength(const int x, const int z) const noexcept {
	const float fx = static_cast<float>(x), fz = static_cast<float>(z);
	const float region = smoothstep(0.25f, 0.4f, _ravineRegion.at(fx, fz));
	if (region <= 0.0f)
		return 0.0f;
	const float band = 1.0f - std::abs(_ravine.at(fx, fz)) / RAVINE_BAND;
	return band > 0.0f ? band * region : 0.0f;
}

bool CaveCarver::entrance(const int x, const int z) const noexcept {
	return _entrance.at(static_cast<float>(x), static_cast<float>(z)) > 0.5f;
}

} // namespace worldgen
