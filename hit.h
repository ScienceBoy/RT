#pragma once
#include "farbe.h"
#include "vector3d.h"
#include "material.h"

class object;

struct Hit
{
    bool hit = false;

    double t = 1e9;      // Abstand entlang des Rays
    double u = 0;      // baryzentrische Koordinate
    double v = 0;

    Farbe farbe;       // Farbe des Objekts
    Vector3D position;
    Vector3D normale;
    const object* obj = nullptr; // welches object
    const Material* material;
    bool frontFace;
};