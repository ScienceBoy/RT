#pragma once

#include "light.h"
#include <ostream>

class PointLight : public light
{
public:
    Vector3D position;
    double power = 100000.0; // Watt

    PointLight();
    PointLight(
        const Vector3D& pos,
        const Farbe& col,
        double power);

    Vector3D samplePoint() const override;

    Vector3D directionFrom(
        const Vector3D& p) const override;

    double distanceFrom(
        const Vector3D& p) const override;

    Farbe intensityAt(
        const Vector3D& p) const override;

    void drawFlat(
        Wireframe& wf,
        DrawMode mode) const override;

    void drawWireframePixels(
        Wireframe& wf,
        DrawMode mode) const override;

    void intersect(
        const Ray& ray,
        Hit& hit) const override;

    void translate(const Vector3D &delta);

    void save(std::ostream& out) const override;

    Vector3D editorPosition() const;

    Vector3D getPosition() const override { return position; }
    
    void setPosition(const Vector3D& p) override { position = p; }
    
    LightSample sample(const Vector3D &p) const override;
    
    Farbe getEmission() const override;

    int numberOfSamples(const Vector3D &hitPos) const override {return 1;};
};