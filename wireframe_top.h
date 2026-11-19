#pragma once
#include "wireframe.h" // Basisklasse
#include "vector3d.h"  // Für Vector3D

class WireframeTop : public Wireframe
{
public:
    WireframeTop(Fenster &f) : Wireframe(f) {}
    AnsichtParameter ansicht() const override;

    Vector3D getDragPlaneNormal() const;
};
