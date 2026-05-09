#pragma once
#include <memory>
#include "object.h"
#include "vector3d.h"
#include "material.h"
#include "dreieck.h"

std::unique_ptr<object> createCubeMesh(
    const Vector3D& pos,
    double size,
    const Material& mat
);

std::unique_ptr<object> createWall(
    const Vector3D& a,
    const Vector3D& b,
    const Vector3D& c,
    const Vector3D& d,
    const Material& mat
);

static void makeTri(const Vector3D &A, const Vector3D &B, const Vector3D &C, const Material &mat, std::vector<Dreieck> &tris);
