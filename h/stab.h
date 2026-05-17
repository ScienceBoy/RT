#pragma once
#include "object.h"
#include "vector3d.h"
#include "ray.h"
#include "hit.h"

class Stab : public object
{
public:
    Vector3D p1;     // Startpunkt
    Vector3D p2;     // Endpunkt
    double radius;

    Stab();
    Stab(const Vector3D& a, const Vector3D& b, double r, const Material& mat);

    void intersect(const Ray& ray, Hit& hit) const override;

    void drawFlat(Wireframe& wf, DrawMode mode) const override;
    void drawWireframePixels(Wireframe& wf, DrawMode mode) const override;

    virtual void getWorldAABB(Vector3D& min, Vector3D& max) const;
};