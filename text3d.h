#pragma once
#include "object.h"
#include "dreieck.h"
#include "ray.h"
#include "hit.h"
#include "material.h"
#include <vector>
#include <string>

class Text3D : public object
{
public:
    std::string text;
    double size;
    std::vector<Dreieck> tris;

    Text3D(const std::string& t, const Vector3D& pos, double s, const Material& mat);

    // Setzt Weltposition
    void setPosition(const Vector3D& pos);

    // Intersection für Picking
    void intersect(const Ray& ray, Hit& hit) const override;

    void drawFlat(Wireframe &wf, DrawMode mode) const override;
    void drawWireframePixels(Wireframe& wf, DrawMode mode = DrawMode::NORMAL) const override;

    virtual void getWorldAABB(Vector3D& min, Vector3D& max) const;

private:
    void buildText();
    void addDigit(char d, const Vector3D& offset);
    void addLine(const Vector3D& a, const Vector3D& b);
};