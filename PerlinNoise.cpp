#include "PerlinNoise.h"

double fractalNoise(const PerlinNoise& perlin, double x, double z, int octaves)
{
    double value = 0.0;
    double amplitude = 1.0;
    double frequency = 1.0;
    double maxValue = 0.0;

    for (int i = 0; i < octaves; i++)
    {
        value += perlin.noise(x * frequency, z * frequency) * amplitude;

        maxValue += amplitude;
        amplitude *= 0.5;
        frequency *= 2.0;
    }

    return value / maxValue; // normalize to [-1..1]
}