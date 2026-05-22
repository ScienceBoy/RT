#pragma once

#include "dreieck.h"
#include "object.h"
#include "stab.h"
#include <vector>
#include "material.h"
#include "bvh.h"
#include "BVHNode.h"

class Tree : public object
{
public:
    Tree(const Vector3D& pos,
         const Vector3D& richtung,
         double laenge,
         double radiusHolz,
         int tiefe,
         double wkeitWeiteresGlied);

    ~Tree();

    void intersect(const Ray& ray, Hit& hit) const override;
    void drawFlat(Wireframe& wf, DrawMode mode) const override;
    void drawWireframePixels(Wireframe& wf, DrawMode mode) const override;

    void setPosition(const Vector3D &pos);

private:
    void build(const Vector3D& pos,
               const Vector3D& dir,
               double len,
               double radiusHolz,
               int depth,
               double wkeitWeiteresGlied);

    void updateBounds();

    void addLeaf(const Vector3D &pos, const Vector3D &dir);

    bool intersectAABB(const Ray &ray, double tMax) const;

    double randRange(double a, double b);

    int getTriangleCount() const override;

    Vector3D randomInSphere();

    std::vector<Stab> stabs;       // Baumstruktur
    std::vector<Dreieck> leaves;   // Blätter

    Vector3D position;

    Vector3D minBound;
    Vector3D maxBound;
    double boundingRadius;
    Vector3D boundingCenter;

    
    object* bvhRoot = nullptr;
    std::vector<object*> bvhObjects;

};

class StabObject : public object {
public:
    const Stab* s;

    StabObject(const Stab* stab)
        : object(stab->getMaterial()), s(stab)
    {
        Vector3D minB = s->getMinBound();
        Vector3D maxB = s->getMaxBound();
        boundingCenter = (minB + maxB) * 0.5;
    }

    void intersect(const Ray& ray, Hit& hit) const override {
        s->intersect(ray, hit);
    }

    void getWorldAABB(Vector3D& min, Vector3D& max) const override {
        min = s->getMinBound();
        max = s->getMaxBound();
    }

    void drawFlat(Wireframe& wf, DrawMode mode) const override {
        s->drawFlat(wf, mode);
    }

    void drawWireframePixels(Wireframe& wf, DrawMode mode) const override {
        s->drawWireframePixels(wf, mode);
    }
};


class LeafObject : public object {
public:
    const Dreieck* t;

    LeafObject(const Dreieck* tri)
        : object(tri->getMaterial()), t(tri)
    {
        Vector3D minB = t->getMinBound();
        Vector3D maxB = t->getMaxBound();
        boundingCenter = (minB + maxB) * 0.5;
    }

    void intersect(const Ray& ray, Hit& hit) const override {
        t->intersect(ray, hit);
    }

    void getWorldAABB(Vector3D& min, Vector3D& max) const override {
        min = t->getMinBound();
        max = t->getMaxBound();
    }

    void drawFlat(Wireframe& wf, DrawMode mode) const override {
        t->drawFlat(wf, mode);
    }

    void drawWireframePixels(Wireframe& wf, DrawMode mode) const override {
        t->drawWireframePixels(wf, mode);
    }

};

