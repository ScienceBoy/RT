#pragma once

#include "light.h"
#include <ostream>
#include <mesh.h>

/*struct MeshSample {
    Vector3D position;
    Vector3D direction;
    double distance;
    Farbe emission;
    double areaPDF;
    Vector3D normal; 
};*/

class MeshLight : public light
{
public:
    std::unique_ptr<Mesh> mesh;
    Farbe color;
    double power;

    MeshLight(
        std::unique_ptr<Mesh> mesh, 
        Farbe col, 
        double power = 50.0);

    // ===== Rendering =====

    const Dreieck& randomTriangle() const;

    Vector3D samplePointOnTriangle(const Dreieck& t) const;

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

    Vector3D editorPosition() const;

    void translate(const Vector3D &delta);

    Vector3D getPosition() const override { return mesh->boundingCenter; }
    
    void setPosition(const Vector3D& p) override { mesh->translate(mesh->boundingCenter - p);}
        
    bool isArea() const override;

    LightSample sample(const Vector3D& p) const override;
    
    Farbe getEmission() const override;
    
    int numberOfSamples(const Vector3D &hitPos) const override;

private:

    std::vector<double> cdf;
    double totalArea = 0.0;
    bool intersectWithUmhuellende(const Ray &ray) const;
    bool intersectAABB(const Ray &ray) const;
};

bool intersectTriangle(const Ray &ray, const Dreieck &tri, Hit &hit);
