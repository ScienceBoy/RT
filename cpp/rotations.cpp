#include "rotations.h"
#include <numbers>

Vector3D rotateY(const Vector3D& v, double angle)
{
    double rad = angle * 3.14159265 / 180.0;

    double cosA = cos(rad);
    double sinA = sin(rad);

    return Vector3D(
        v.x * cosA + v.z * sinA,
        v.y,
        -v.x * sinA + v.z * cosA
    );
}