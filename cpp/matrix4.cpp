#include "matrix4.h"
#include <cmath>

// --------------------------------------------------
// Constructor
// --------------------------------------------------
Matrix4::Matrix4()
{
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
            m[i][j] = 0.0;
}

// --------------------------------------------------
Matrix4 Matrix4::identity()
{
    Matrix4 r;
    for (int i = 0; i < 4; i++)
        r.m[i][i] = 1.0;
    return r;
}

// --------------------------------------------------
Matrix4 Matrix4::translate(const Vector3D& t)
{
    Matrix4 r = identity();
    r.m[0][3] = t.x;
    r.m[1][3] = t.y;
    r.m[2][3] = t.z;
    return r;
}

// --------------------------------------------------
Matrix4 Matrix4::scale(double sx, double sy, double sz)
{
    Matrix4 r = identity();
    r.m[0][0] = sx;
    r.m[1][1] = sy;
    r.m[2][2] = sz;
    return r;
}

// --------------------------------------------------
Vector3D Matrix4::transformPoint(const Vector3D& v) const
{
    Vector3D r;

    r.x = m[0][0]*v.x + m[0][1]*v.y + m[0][2]*v.z + m[0][3];
    r.y = m[1][0]*v.x + m[1][1]*v.y + m[1][2]*v.z + m[1][3];
    r.z = m[2][0]*v.x + m[2][1]*v.y + m[2][2]*v.z + m[2][3];

    return r;
}

// --------------------------------------------------
Vector3D Matrix4::transformVector(const Vector3D& v) const
{
    Vector3D r;

    r.x = m[0][0]*v.x + m[0][1]*v.y + m[0][2]*v.z;
    r.y = m[1][0]*v.x + m[1][1]*v.y + m[1][2]*v.z;
    r.z = m[2][0]*v.x + m[2][1]*v.y + m[2][2]*v.z;

    return r;
}

// --------------------------------------------------
Matrix4 Matrix4::operator*(const Matrix4& b) const
{
    Matrix4 r;

    for (int i = 0; i < 4; i++)
    {
        for (int j = 0; j < 4; j++)
        {
            r.m[i][j] = 0.0;
            for (int k = 0; k < 4; k++)
                r.m[i][j] += m[i][k] * b.m[k][j];
        }
    }

    return r;
}

Matrix4 Matrix4::inverse() const
{
    Matrix4 r = Matrix4::identity();

    // inverse scale
    r.m[0][0] = 1.0 / m[0][0];
    r.m[1][1] = 1.0 / m[1][1];
    r.m[2][2] = 1.0 / m[2][2];

    // inverse translation
    r.m[0][3] = -m[0][3];
    r.m[1][3] = -m[1][3];
    r.m[2][3] = -m[2][3];

    return r;
}