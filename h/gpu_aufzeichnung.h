#pragma once
#include "vector3d.h"
#include "farbe.h"
#include <algorithm>
#include <cstdint>
#include <vector>

// Ein Vertex fuer die GPU: Position (relativ zum Ursprung der Aufzeichnung) + Farbe als R8G8B8A8_UNORM
struct GpuVertex
{
    float x, y, z;
    uint32_t farbe;
};

// true:  Farben exakt wie bisher in den Ansichten. toCOLORREF() liefert Rot im untersten Byte,
//        uploadFrameToGpu() liest es aber als Blau -> Rot und Blau sind vertauscht
//        (z.B. rotes Fadenkreuz erscheint blau, X-Achse rot statt blau).
// false: echte Materialfarben, wie im Raytracing-Fenster
static constexpr bool ROT_BLAU_WIE_BISHER = false;

// Farbe wie bisher ueber Farbe::toCOLORREF() umrechnen (gleiche Rundung wie im CPU-Bild)
inline uint32_t farbeZuRgba(const Farbe &f, uint8_t alpha = 255)
{
    uint32_t pixel = f.toCOLORREF();

    uint32_t r = (pixel >> 16) & 0xFF;
    uint32_t g = (pixel >> 8) & 0xFF;
    uint32_t b = pixel & 0xFF;

    if (!ROT_BLAU_WIE_BISHER)
        std::swap(r, b);

    return r | (g << 8) | (b << 16) | (uint32_t(alpha) << 24);
}

// Sammelt die Geometrie, die die Objekte ueber Wireframe::project/fillTriangle/drawLine "zeichnen".
// project() liefert dabei statt Bildschirmkoordinaten einen Index auf den Weltpunkt zurueck,
// fillTriangle()/drawLine() setzen daraus Dreiecke und Linien in Weltkoordinaten zusammen.
class GpuAufzeichnung
{
public:
    Vector3D ursprung;   // gespeicherte Positionen sind relativ dazu (float-Genauigkeit)
    double zWert = -1.0; // z-Rueckgabe von project(); Camera::drawFlat fuellt das Frustum nur bei zWert < 0

    std::vector<GpuVertex> dreiecke;
    std::vector<GpuVertex> linien;
    double maxAbstand2 = 0.0; // groesster quadrierter Abstand eines Punkts zum Ursprung

    void leeren()
    {
        dreiecke.clear();
        linien.clear();
        maxAbstand2 = 0.0;
        neuesObjekt();
    }

    // Vor jedem drawFlat()/drawWireframePixels() aufrufen
    void neuesObjekt() { zaehler = 0; }

    int punkt(const Vector3D &p)
    {
        punkte[zaehler % RING] = p;
        return zaehler++;
    }

    void dreieck(int a, int b, int c, const Farbe &f)
    {
        if (!gueltig(a) || !gueltig(b) || !gueltig(c))
            return;

        uint32_t farbe = farbeZuRgba(f);
        dreiecke.push_back(vertex(a, farbe));
        dreiecke.push_back(vertex(b, farbe));
        dreiecke.push_back(vertex(c, farbe));
    }

    void linie(int a, int b, const Farbe &f)
    {
        if (!gueltig(a) || !gueltig(b))
            return;

        uint32_t farbe = farbeZuRgba(f);
        linien.push_back(vertex(a, farbe));
        linien.push_back(vertex(b, farbe));
    }

private:
    // Die Objekte verwenden projizierte Punkte nur kurz nach project() wieder,
    // daher reicht ein Ringpuffer statt einer Liste aller Punkte (Meshes haben Millionen davon)
    static constexpr int RING = 4096;
    Vector3D punkte[RING];
    int zaehler = 0;

    bool gueltig(int i) const { return i >= 0 && i < zaehler && zaehler - i <= RING; }

    GpuVertex vertex(int i, uint32_t farbe)
    {
        Vector3D p = punkte[i % RING] - ursprung;
        maxAbstand2 = std::max(maxAbstand2, p * p);

        return GpuVertex{float(p.x), float(p.y), float(p.z), farbe};
    }
};
