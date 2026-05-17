#pragma once
#include "vector3d.h"
#include "ray.h"
#include "material.h"
#include "hit.h"
#include "farbe.h"
#include "wireframe.h"
#include "drawmode.h"
#include <vector>
#include <ostream>
#include <lightSample.h>

// Vorwärtsdeklaration
class Wireframe;

class light
{
public:
    Farbe farbe;

    virtual ~light() = default;

    //double intensity = 100000.0;

    // ===== Rendering =====
    
    virtual bool isArea() const { return false; }

    virtual LightSample sample(const Vector3D& p) const = 0;

    virtual Vector3D samplePoint() const = 0;

    virtual Vector3D directionFrom(const Vector3D& p) const = 0;

    virtual double distanceFrom(const Vector3D& p) const = 0;

    virtual Farbe intensityAt(const Vector3D& p) const = 0;
    
    virtual bool isEmissive() const { return true; }

    virtual Farbe getEmission() const = 0;

    virtual int numberOfSamples(const Vector3D& hitPos) const = 0;

    // ===== Editor =====

    virtual void drawFlat(Wireframe& wf, DrawMode mode) const = 0;

    virtual void drawWireframePixels(Wireframe& wf, DrawMode mode) const = 0;

    virtual void intersect(const Ray& ray, Hit& hit) const = 0;

    virtual void save(std::ostream& out) const = 0;

    virtual Vector3D editorPosition() const = 0;

    virtual void translate(const Vector3D& delta) = 0;

    virtual Vector3D getPosition() const = 0;
    
    virtual void setPosition(const Vector3D& p) = 0;
};