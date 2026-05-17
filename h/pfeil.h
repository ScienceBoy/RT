#pragma once
#include "object.h"
#include "dreieck.h"
#include "ray.h"
#include "hit.h"
#include "material.h"
#include <vector>

class Pfeil : public object
{
public:
    // Lokale Dreiecke für Schaft und Spitze
    std::vector<Dreieck> tris;

    // Konstruktor
    Pfeil(const Vector3D& start, const Vector3D& end, double thickness, const Material& mat);

    // Objekte verschiebbar
    void setPosition(const Vector3D& pos);

    // Intersection und Wireframe
    void intersect(const Ray& ray, Hit& hit) const override;
    void drawFlat(Wireframe &wf, DrawMode mode) const override;
    void drawWireframePixels(Wireframe &wf, DrawMode mode = DrawMode::NORMAL) const override;

    virtual void getWorldAABB(Vector3D& min, Vector3D& max) const;

private:
    void addQuad(const Vector3D& a, const Vector3D& b, const Vector3D& c, const Vector3D& d, const Material& mat);
    void addTriangle(const Vector3D& a, const Vector3D& b, const Vector3D& c, const Material& mat);
};