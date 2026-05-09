#pragma once
#include "object.h"
#include "material.h"
#include "ray.h"
#include "hit.h"
#include "TextureManager.h"

class Kugel : public object
{
public:
    double radius;

    // Konstruktoren
    Kugel(); // Standard: Mitte 0,0,0, Radius 1
    Kugel(const Vector3D& pos, double r, const Material& mat);

    // Schnitt mit Ray
    void intersect(const Ray& ray, Hit& hit) const override;

    void drawFlat(Wireframe &wf, DrawMode mode) const override;
    void drawWireframePixels(Wireframe& wf, DrawMode mode = DrawMode::NORMAL) const override;
    void updateBounds();
    virtual void getWorldAABB(Vector3D& min, Vector3D& max) const;
};

double randomDouble(double min, double max);

Vector3D randomInSphere(double radius);

Vector3D randomOnUnitSphere();

void generateCloudRecursive(std::vector<Kugel> &kugeln, const Vector3D &center, double radius, int depth, int maxDepth, const Material &mat);

std::vector<Kugel> generateWolkeAusKugeln(const Vector3D &center, double startRadius, int maxDepth, const Material &mat);
