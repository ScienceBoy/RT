#include "ray.h"

Ray::Ray() : origin(0,0,0), direction(0,0,1) {}
Ray::Ray(const Vector3D& s, const Vector3D& r)
    : origin(s), direction(r.normalized())
{
    invDirection = Vector3D(
        (direction.x != 0.0) ? 1.0 / direction.x : 1e30,
        (direction.y != 0.0) ? 1.0 / direction.y : 1e30,
        (direction.z != 0.0) ? 1.0 / direction.z : 1e30
    );
    for (int i = 0; i < 3; i++)
    {
        sign[i] = (direction[i] < 0);
    }
}

Vector3D Ray::punkt(double t) const {
    return origin + direction * t;
}

