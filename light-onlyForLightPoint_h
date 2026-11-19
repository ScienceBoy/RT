#pragma once
#include "vector3d.h"
#include "ray.h"
#include "material.h"
#include "hit.h"
#include "farbe.h"
#include "wireframe.h"
#include "drawmode.h"
#include <vector>

// Vorwärtsdeklaration
class Wireframe;


class light
{
public:
    Vector3D position;  // Weltposition des Lichts
    Farbe farbe;

    light();
    light(const Vector3D& pos, const Farbe& col);

    void drawFlat(Wireframe &wf, DrawMode mode) const;
    void drawWireframePixels(Wireframe& wf, DrawMode mode) const;
    void intersect(const Ray& ray, Hit& hit) const;

    // Für Drag-&-Drop
    void setPosition(const Vector3D& pos) { position = pos; }
};