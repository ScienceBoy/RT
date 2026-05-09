#pragma once
#include "wireframe.h"   
#include "vector3d.h"

class WireframeSide : public Wireframe {
public:
    using Wireframe::Wireframe;
    void project(const Vector3D& p, int& x, int& y, double& z) override;
    Ray createRay(int x, int y);
    Vector3D getDragPlaneNormal() const;

};