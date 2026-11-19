#pragma once
#include "object.h"
#include "vector3d.h"
#include "ray.h"
#include "hit.h"
#include "material.h"

class Stab : public object
{
public:
    Vector3D p1;     // Startpunkt
    Vector3D p2;     // Endpunkt
    double radius;

    const wchar_t* getTypeName() const override{return L"Stab";}

    Stab();
    Stab(const Vector3D& a, const Vector3D& b, double r, const Material& mat);

    void intersect(const Ray& ray, Hit& hit) const override;
    void translate(const Vector3D &delta) override
    {
        position += delta;
        minBound += delta;
        maxBound += delta;
        boundingCenter += delta;
    }
    // Drehen (Grad um X, dann Y, dann Z wie Mesh::rotate) und Skalieren um die Mitte 'position'
    void rotate(const Vector3D &rotation) override;
    void scale(const Vector3D &scaleVec) override;
    void drawFlat(Wireframe& wf, DrawMode mode) const override;
    void drawWireframePixels(Wireframe& wf, DrawMode mode) const override;

    virtual void getWorldAABB(Vector3D& min, Vector3D& max) const;

    const Material& getMaterial() const
    {
        return mat;
    }

    Vector3D getMinBound() const { return minBound; }
    Vector3D getMaxBound() const { return maxBound; }

private:
    void grenzenBerechnen();
};