#pragma once
#include "vector3d.h"
#include "farbe.h"

struct SmokeBox
{
    Vector3D min;
    Vector3D max;

    Farbe color;

    double densityMultiplier;

    double seed;

    SmokeBox(
        const Vector3D& min,
        const Vector3D& max,
        const Farbe& c = Farbe(1,1,1),
        double d = 1.0,
        double s = 0.0)
        : min(min),
          max(max),
          color(c),
          densityMultiplier(d),
          seed(s)
    {
    }
};