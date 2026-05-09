#pragma once

#include "object.h"
#include "dreieck.h"
#include "drawmode.h"
#include <vector>

class Cube : public object
{
public:
    Cube();
    //Cube(const Vector3D& center, double size, const Farbe& f);
    Cube(const Vector3D& center, double size, const Material& mat);

    double halfSize;
    void intersect(const Ray& ray, Hit& hit) const override;
    void drawFlat(Wireframe &wf, DrawMode mode) const override;
    void drawWireframePixels(Wireframe &wf, DrawMode mode = DrawMode::NORMAL) const override;

private:
    std::vector<Dreieck> tris;
};