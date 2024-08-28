#ifndef NOISE_HPP
#define NOISE_HPP

#include <cstdint>
#include <utility>

typedef enum block_type {
	AIR,
	GRASS,
	DIRT,
	STONE,
	WATER
} block_type;


class PerlinNoise {
	public:
		PerlinNoise();
		PerlinNoise(uint32_t seed);
		
		double noise(double x, double y, double z) const;

	private:
		static constexpr int _PERM_SIZE = 256;
		uint8_t _perm[_PERM_SIZE * 2];

		static double fade(double t);
		static double lerp(double t, double a, double b);
		static double grad(int hash, double x, double y, double z);
};


#endif