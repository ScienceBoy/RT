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
    std::vector<Rect2D> rects; // in Einheiten von 'size' (Versalhoehe)
    double breite = 0.0;       // Breite des Zeichens, ebenfalls in 'size'
};

// 3D-Text aus extrudierten Rechtecken: 5x7-Pixelschrift mit Gross- und Kleinbuchstaben,
// Umlauten, ß, Ziffern und Satzzeichen; 'size' = Hoehe der Grossbuchstaben.
// Die Dreiecke gehoeren dem Objekt selbst (vorher landete jedes Rechteck als eigene Wand in 'scene').
class Text3D : public object
{
    friend class GpuAnsichten; // liest tris fuer die GPU-Ansichten
    friend class SceneSaver;   // speichert text, size, depth
    friend bool szeneAlsOBJExportieren(const std::string &, std::string &); // Export OBJ
    friend struct ZustandZugriff; // Rueckgaengig (bearbeitung.cpp)

public:
    Text3D(const std::string& text,
           const Vector3D& pos,
           double size,
           double depth,
           const Material& mat);

    const wchar_t* getTypeName() const override{return L"Text3D";}

    void setPosition(const Vector3D& pos);

    // Text aendern (baut die Buchstaben neu; der Name folgt dem Text)
    void setText(const std::string& neuerText);
    const std::string& getText() const { return text; }

    // Farbe fuer Anzeige und Raytracing (das Raytracing nutzt das Material jedes Dreiecks)
    void setFarbe(const Farbe& f);

    // ganzes Material (Objekt und alle Dreiecke), z.B. aus dem Reiter "Material"
    void setMaterial(const Material& m);

    int getTriangleCount() const override { return int(tris.size()); }

    void translate(const Vector3D &delta) override
    {
        position += delta;
        minBound += delta;
        maxBound += delta;
        boundingCenter += delta;
    }
    // Drehen (Grad um X, dann Y, dann Z wie Mesh::rotate) und Skalieren um die Mitte der Bounding-Box.
    // Beides landet in den Achsen; die Buchstaben werden damit neu aufgebaut.
    void rotate(const Vector3D &rotation) override;
    void scale(const Vector3D &scaleVec) override;

    // Achsen des Textes (Schreibrichtung, Hoehe, Tiefe), auch mit Laenge != 1 (Skalierung)
    void setAchsen(const Vector3D& x, const Vector3D& y, const Vector3D& z);
    const Vector3D& getAchseX() const { return achseX; }
    const Vector3D& getAchseY() const { return achseY; }
    const Vector3D& getAchseZ() const { return achseZ; }
    void drawFlat(Wireframe& wf, DrawMode mode) const;
    void drawWireframePixels(Wireframe& wf, DrawMode mode) const;

    void intersect(const Ray& ray, Hit& hit) const override;
    void getWorldAABB(Vector3D& minOut, Vector3D& maxOut) const;

private:
    std::string text;
    double size;
    double depth;

    std::vector<Dreieck> tris;

    Vector3D achseX{1, 0, 0};
    Vector3D achseY{0, 1, 0};
    Vector3D achseZ{0, 0, 1};

    std::unordered_map<int, Glyph> font; // Unicode -> Zeichen

    void buildFont();
    void buildText();
    void grenzenBerechnen();

    double addGlyphMesh(int c, const Vector3D& offset);
    void extrudeGlyph(const Glyph& g, const Vector3D& offset);

    Glyph makeGlyph(int c);
};

static std::vector<Vector3D> rect(double x0, double y0, double x1, double y1, double s);
