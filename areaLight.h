#pragma once
// Flaechenlicht: leuchtendes Rechteck (Ecke 'position', Kanten edgeU und edgeV).
// Es leuchtet nur zu einer Seite: in Richtung edgeU x edgeV (z.B. U = +X, V = +Z -> nach unten).
// Im Editor ist die Position die Mitte des Rechtecks (gespeichert wird die Ecke).

#include "light.h"
#include <ostream>

class AreaLight : public light
{
public:
    Vector3D position;   // Ecke des Rechtecks
    double power = 50.0; // Gesamtleistung (Watt)

    // Rechteck-Kanten
    Vector3D edgeU;
    Vector3D edgeV;

    AreaLight(
        const Vector3D& pos,
        const Vector3D& u,
        const Vector3D& v,
        const Farbe& col,
        double power = 50.0);

    Vector3D mitte() const { return position + (edgeU + edgeV) * 0.5; }
    Vector3D normale() const { return edgeU.cross(edgeV).normalized(); } // Leuchtrichtung
    double flaeche() const { return edgeU.cross(edgeV).length(); }

    // ===== Rendering =====

    LightSample samplePoint() const override;

    Vector3D directionFrom(
        const Vector3D& point) const override;

    double distanceFrom(
        const Vector3D& point) const override;

    Farbe intensityAt(
        const Vector3D& point) const override;

    // ===== Editor =====

    void drawFlat(
        Wireframe& wf,
        DrawMode mode) const override;

    void drawWireframePixels(
        Wireframe& wf,
        DrawMode mode) const override;

    void intersect(
        const Ray& ray,
        Hit& hit) const override;

    void save(std::ostream& out) const override;

    Vector3D editorPosition() const override;

    void translate(const Vector3D &delta) override;

    // Mitte des Rechtecks (Markierung, Verschieben, Eingabe "Position")
    Vector3D getPosition() const override { return mitte(); }

    void setPosition(const Vector3D& p) override { position = p - (edgeU + edgeV) * 0.5; }

    bool isArea() const override;

    LightSample sample(const Vector3D& p) const override;

    Farbe getEmission() const override;

    int numberOfSamples(const Vector3D &hitPos) const override;
};
