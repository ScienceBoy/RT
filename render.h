#pragma once

#include "ray.h"
#include "hit.h"
#include "fenster.h"
#include <atomic>

Hit ClosestPointOfIntersection(const Ray& ray);

Farbe trace(const Ray &ray, int depth, double current_ior);

Vector3D reflect(const Vector3D &D, const Vector3D &N);

Vector3D refract(const Vector3D & I, const Vector3D & N, double n1, double n2);

double fresnel(const Vector3D &I, const Vector3D &N, double n1, double n2);

Farbe flatShading(const Farbe& objectFarbe, const Vector3D& normale,  const Vector3D& punkt);

void render(Fenster &f, int xStart, int xEnd, int yStart, int yEnd, std::atomic<int> &linesDone, std::atomic<bool> &stopRendering);

Farbe flatShadingAndshadow(const Farbe &objectFarbe, const Vector3D &normale, const Vector3D &punkt);

Farbe blinnPhong(const Material & mat, const Vector3D & normale, const Vector3D & punkt, const Hit h, const Vector3D & viewPos);

Farbe nebel(Hit h, Farbe farbe);

struct VolumeResult {
    Farbe L;   // inscattered light
    double T;  // transmittance after volume
};

double noise(const Vector3D &p);

// Interpolation 
inline double smooth(double t)
{
    return t * t * (3.0 - 2.0 * t); // smoothstep
}

inline double lerp(double a, double b, double t)
{
    return a + t * (b - a);
}

// Pseudo‑Zufallsfunktion
inline double rand3(int x, int y, int z)
{
    int n = x * 15731 + y * 789221 + z * 1376312589;
    n = (n << 13) ^ n;
    return 1.0 - ((n * (n * n * 15731 + 789221) + 1376312589)
                 & 0x7fffffff) / 1073741824.0;
}

inline double clamp(double x, double a, double b)
{
    return x < a ? a : (x > b ? b : x);
}

inline double smoothstep(double edge0, double edge1, double x)
{
    // Normalisieren auf [0,1]
    x = (x - edge0) / (edge1 - edge0);
    x = clamp(x, 0.0, 1.0);

    // kubische S-Kurve
    return x * x * (3.0 - 2.0 * x);
}

double fbm(const Vector3D &p);

Farbe rauch(Ray ray, Farbe farbe);

Farbe rauchInBox(Ray ray, double t_entry, double t_exit, Farbe smokeColor);

bool intersectSmokeBox(const Ray &ray, double &tmin, double &tmax);

double computeShadowOfSmoke(Vector3D pos, Vector3D lightDir);

double sampleDensityOfSmoke(Vector3D p);

double phaseHG(double cosTheta, double g);

double getTime();

double sampleDensityCloud(Vector3D p);

double computeShadowOfCloud(Vector3D pos, Vector3D lightDir);

Farbe wolke(Ray ray, Farbe farbe);

Farbe wolkeInBox(Ray ray, double t_entry, double t_exit, Farbe cloudColor);

bool intersectCloudBox(const Ray &ray, double &tmin, double &tmax);

void generateTerrain(int gridResolution, double sizeXZ, double heightScale, int octaves);
