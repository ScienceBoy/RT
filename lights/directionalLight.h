#pragma once

#include "light.h"
#include <ostream>

class DirectionalLight : public light
{
public:
    Vector3D direction;
    double irradiance = 1.0; // W/m² (konstant!)
    Vector3D editorPos;   // fake position für UI

    DirectionalLight(
        const Vector3D& dir,
        const Farbe& col,
        double irradiance);

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

    void save(std::ostream& out) const override;

    Vector3D editorPosition() const;
    
    void translate(const Vector3D &);

    Vector3D getPosition() const override { return editorPos; };
    
    void setPosition(const Vector3D& p) override { editorPos = p; };
    
    LightSample sample(const Vector3D &p) const override;
    
    Farbe getEmission() const override;

    int numberOfSamples(const Vector3D &hitPos) const override {return 1;};

};