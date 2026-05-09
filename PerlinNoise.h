#include <cmath>
#include <cstdlib>
#include <numeric>
#include <vector>
#include <algorithm>
#include <random>

class PerlinNoise {
private:
    std::vector<int> p;

public:
    PerlinNoise(unsigned int seed = 1337)
    {
        p.resize(256);
        std::iota(p.begin(), p.end(), 0);

        std::mt19937 engine(seed);
        std::shuffle(p.begin(), p.end(), engine);

        p.insert(p.end(), p.begin(), p.end());
    }

    double fade(double t) const {
        return t * t * t * (t * (t * 6 - 15) + 10);
    }

    double lerp(double a, double b, double t) const {
        return a + t * (b - a);
    }

    double grad(int hash, double x, double y) const {
        int h = hash & 3;
        double u = h < 2 ? x : y;
        double v = h < 2 ? y : x;
        return ((h & 1) ? -u : u) + ((h & 2) ? -2.0 * v : 2.0 * v);
    }

    double noise(double x, double y) const {
        int X = (int)floor(x) & 255;
        int Y = (int)floor(y) & 255;

        x -= floor(x);
        y -= floor(y);

        double u = fade(x);
        double v = fade(y);

        int A = p[X] + Y;
        int B = p[X + 1] + Y;

        return lerp(
            lerp(grad(p[A], x, y),
                 grad(p[B], x - 1, y), u),
            lerp(grad(p[A + 1], x, y - 1),
                 grad(p[B + 1], x - 1, y - 1), u),
            v);
    }
};

double fractalNoise(const PerlinNoise &perlin, double x, double z, int octaves);
