#pragma once

#include "light.h"
#include <ostream>

class AreaLight : public light
{
public:

    Vector3D position;
    double power = 50.0;  // Gesamtleistung (Watt)

    // Rechteck-Kanten
    Vector3D edgeU;
    Vector3D edgeV;

    AreaLight(
        const Vector3D& pos,
        const Vector3D& u,
        const Vector3D& v,
        const Farbe& col,
        double power = 50.0);

    // ===== Rendering =====

    Vector3D samplePoint() const override;

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

    Vector3D editorPosition() const;

    void translate(const Vector3D &delta);

    Vector3D getPosition() const override { return position; }
    
    void setPosition(const Vector3D& p) override { position = p; }
        
    bool isArea() const override;

    LightSample sample(const Vector3D& p) const override;
    
    Farbe getEmission() const override;
    
    int numberOfSamples(const Vector3D &hitPos) const override;
};
