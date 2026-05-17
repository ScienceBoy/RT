#pragma once

#include "light.h"
#include <ostream>

class SphereLight : public light
{
public:

    Vector3D center;
    double radius;
    double power = 50.0; // Watt
    mutable Vector3D lastSamplePoint;
    mutable bool hasSample = false;

    SphereLight(
        const Vector3D& c,
        double r,
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

    Vector3D getPosition() const override { return center; };
    
    void setPosition(const Vector3D& p) override { center = p; };

    LightSample sample(const Vector3D& p) const override;

    Farbe getEmission() const override;

    int numberOfSamples(const Vector3D &hitPos) const override;
};