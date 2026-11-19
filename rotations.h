#pragma once
#include <cmath>
#include "vector3d.h"

inline Vector3D rotateX(const Vector3D& p, double a)
{
    double s = sin(a), c = cos(a);

    return Vector3D(
        p.x,
        p.y * c - p.z * s,
        p.y * s + p.z * c
    );
}

inline Vector3D rotateY(const Vector3D& p, double a)
{
    double s = sin(a), c = cos(a);

    return Vector3D(
        p.x * c + p.z * s,
        p.y,
        -p.x * s + p.z * c
    );
}

inline Vector3D rotateZ(const Vector3D& p, double a)
{
    double s = sin(a), c = cos(a);

    return Vector3D(
        p.x * c - p.y * s,
        p.x * s + p.y * c,
        p.z
    );
}