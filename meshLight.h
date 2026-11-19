#pragma once
// Mesh-Licht: ein Dreiecksnetz (z.B. Wuerfel, Kugel aus OBJ) leuchtet auf seiner ganzen Oberflaeche.
// Beide Seiten jedes Dreiecks leuchten; bei geschlossenen Koerpern verdeckt die Vorderseite die
// Rueckseite (Schattentest), so zaehlt nur die Aussenseite.

#include "light.h"
#include "mesh.h"
#include <memory>
#include <ostream>
#include <vector>

class MeshLight : public light
{
public:
    std::unique_ptr<Mesh> mesh;
    double power;

    MeshLight(
        std::unique_ptr<Mesh> mesh,
        Farbe col,
        double power = 50.0);

    // Kopie (Duplizieren, Rueckgaengig): eigenes Netz mit denselben Dreiecken
    std::unique_ptr<MeshLight> kopie() const;

    double flaeche() const { return totalArea; }

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

    Vector3D editorPosition() const override;

    void translate(const Vector3D &delta) override;

    Vector3D getPosition() const override;

    void setPosition(const Vector3D& p) override;

    bool isArea() const override;

    LightSample sample(const Vector3D& p) const override;

    Farbe getEmission() const override;

    int numberOfSamples(const Vector3D &hitPos) const override;

private:

    std::vector<double> cdf;
    double totalArea = 0.0;
    Vector3D boxMin, boxMax; // Huellquader der Dreiecke (fuer intersect und Position)
    void flaechenBerechnen();
    bool intersectAABB(const Ray &ray) const;
};

bool intersectTriangle(const Ray &ray, const Dreieck &tri, Hit &hit);
