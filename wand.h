#pragma once
#include "object.h"
#include "dreieck.h"
#include "ray.h"
#include "hit.h"
#include "material.h"

class Wand : public object
{
public:
    Dreieck t1, t2;

    Wand();
    Wand(const Vector3D& a, const Vector3D& b, const Vector3D& c, const Vector3D& d, const Material& mat);

    void setPosition(const Vector3D& pos);

    void intersect(const Ray& ray, Hit& hit) const override;
    void drawFlat(Wireframe &wf, DrawMode mode) const override;
    void drawWireframePixels(Wireframe& wf, DrawMode mode = DrawMode::NORMAL) const override;

    virtual void getWorldAABB(Vector3D& min, Vector3D& max) const;
};