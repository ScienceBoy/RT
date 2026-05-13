#pragma once
#include "vector3d.h"
#include "vector2d.h"
#include "object.h"
#include "ray.h"
#include "hit.h"
#include "material.h"
#include "drawmode.h"


class Dreieck : public object
{
public:
    Vector3D a, b, c;       // lokale Eckpunkte
    Vector3D edge1, edge2;  // Kanten
    Vector3D normale, geomNormale;       // interpolierte und geometrische Normale
    Vector3D nA, nB, nC;    // Vertex-Normalen
    Vector2D uvA, uvB, uvC;

    // Konstruktoren
    Dreieck();
    Dreieck(const Vector3D& A, const Vector3D& B, const Vector3D& C, const Material& mat);

    // Schnitt mit Ray
    void intersect(const Ray& ray, Hit& hit) const override;

    void drawFlat(Wireframe &wf, DrawMode mode) const override;
    void drawWireframePixels(Wireframe& wf, DrawMode mode = DrawMode::NORMAL) const override;

    // Optional: Dreieck relativ zu position verschieben
    void setPosition(const Vector3D& pos);

    virtual void getWorldAABB(Vector3D& min, Vector3D& max) const;

    int getTriangleCount() const override { return 1; }
};