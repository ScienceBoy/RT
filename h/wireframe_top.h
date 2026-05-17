#pragma once
#include "wireframe.h"   // Basisklasse
#include "vector3d.h"    // Für Vector3D

class WireframeTop : public Wireframe {
public:
    WireframeTop(Fenster& f) : Wireframe(f) {}
    void project(const Vector3D& p, int& x, int& y, double& z) override;

    Ray createRay(int x, int y);
    Vector3D getDragPlaneNormal() const;

};

