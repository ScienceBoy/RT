#pragma once
#include "vector3d.h"

class Camera; // Forward declaration

class Ray {
public:
    Vector3D origin;
    Vector3D direction;

    Vector3D invDirection;
    bool sign[3];

    Ray();
    Ray(const Vector3D& s, const Vector3D& r);

    Vector3D punkt(double t) const;

    static Ray makeRay(int x, int y);
};