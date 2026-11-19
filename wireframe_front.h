#pragma once
#include "wireframe.h"
#include "vector3d.h"

class WireframeFront : public Wireframe
{
public:
    using Wireframe::Wireframe;
    AnsichtParameter ansicht() const override;
    Vector3D getDragPlaneNormal() const;
};
