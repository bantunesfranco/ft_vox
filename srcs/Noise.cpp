#include <cstdint>
#include <cmath>
#include "Noise.hpp"

PerlinNoise::PerlinNoise() {
	for (int i = 0; i < _PERM_SIZE; ++i) {
		_perm[i] = i;
	}

	// Shuffle permutation array
	for (int i = 0; i < _PERM_SIZE; ++i) {
		int j = rand() % _PERM_SIZE;
		std::swap(_perm[i], _perm[j]);
	}

	// Duplicate the permutation array to avoid buffer overflow
	for (int i = 0; i < _PERM_SIZE; ++i) {
		_perm[i + _PERM_SIZE] = _perm[i];
	}
}

PerlinNoise::PerlinNoise(uint32_t seed) {
	srand(seed);
	PerlinNoise();
}

double PerlinNoise::fade(double t) {
	return t * t * t * (t * (t * 6 - 15) + 10);
}

double PerlinNoise::lerp(double t, double a, double b) {
	return a + t * (b - a);
}

double PerlinNoise::grad(int hash, double x, double y, double z) {
	int h = hash & 15;
	double u = h < 8 ? x : y;
	double v = h < 4 ? y : h == 12 || h == 14 ? x : z;
	return ((h & 1) == 0 ? u : -u) + ((h & 2) == 0 ? v : -v);
}

double PerlinNoise::noise(double x, double y, double z) const {
	int X = (int)floor(x) & 255;
	int Y = (int)floor(y) & 255;
	int Z = (int)floor(z) & 255;

	x -= floor(x);
	y -= floor(y);
	z -= floor(z);

	double u = fade(x);
	double v = fade(y);
	double w = fade(z);

	int A = _perm[X] + Y;
	int AA = _perm[A] + Z;
	int AB = _perm[A + 1] + Z;
	int B = _perm[X + 1] + Y;
	int BA = _perm[B] + Z;
	int BB = _perm[B + 1] + Z;

	return lerp(w, lerp(v, lerp(u, grad(_perm[AA], x, y, z),
								   grad(_perm[BA], x - 1, y, z)),
						   lerp(u, grad(_perm[AB], x, y - 1, z),
								   grad(_perm[BB], x - 1, y - 1, z))),
				   lerp(v, lerp(u, grad(_perm[AA + 1], x, y, z - 1),
								   grad(_perm[BA + 1], x - 1, y, z - 1)),
						   lerp(u, grad(_perm[AB + 1], x, y - 1, z - 1),
								   grad(_perm[BB + 1], x - 1, y - 1, z - 1))));
}
