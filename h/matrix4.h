#pragma once

#include "vector3d.h"

class Matrix4
{
public:
    double m[4][4];

    Matrix4();

    static Matrix4 identity();

    static Matrix4 translate(const Vector3D& t);
    static Matrix4 scale(double sx, double sy, double sz);

    Vector3D transformPoint(const Vector3D& v) const;
    Vector3D transformVector(const Vector3D& v) const;

    Matrix4 operator*(const Matrix4& rhs) const;

    Matrix4 inverse() const;
};