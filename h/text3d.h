#pragma once
#include "object.h"
#include "dreieck.h"
#include <unordered_map>

struct Rect2D
{
    double x0, y0;
    double x1, y1;
};

struct Glyph
{
    std::vector<Rect2D> rects;
};

class Text3D : public object
{
public:
    Text3D(const std::string& text,
           const Vector3D& pos,
           double size,
           double depth,
           const Material& mat);

    void setPosition(const Vector3D& pos);

    void drawFlat(Wireframe& wf, DrawMode mode) const;
    void drawWireframePixels(Wireframe& wf, DrawMode mode) const;

    void intersect(const Ray& ray, Hit& hit) const override;
    void getWorldAABB(Vector3D& minOut, Vector3D& maxOut) const;

private:
    std::string text;
    double size;
    double depth;

    std::vector<Dreieck> tris;

    std::unordered_map<char, Glyph> font;

    void buildFont();
    void buildText();

    void addGlyphMesh(char c, const Vector3D& offset);
    void extrudeGlyph(const Glyph& g, const Vector3D& offset);

    Glyph makeGlyph(char c, double s);
};

static std::vector<Vector3D> rect(double x0, double y0, double x1, double y1, double s);
