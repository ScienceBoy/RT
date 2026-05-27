#pragma once

#include "dreieck.h"
#include "object.h"
#include "stab.h"
#include <vector>
#include "material.h"
#include "bvh.h"
#include "BVHNode.h"
#include "stats.h"
#include <deque>


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

    //std::vector<object*> bvhObjects;

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

    //std::vector<Stab> stabs;       // Baumstruktur
    //std::vector<Dreieck> leaves;   // Blätter
    //std::vector<Dreieck> woodTriangles; // Baumstruktur aus Dreiecken
    std::vector<Dreieck> woodTriangles;
    std::vector<Dreieck> leaves;

    Vector3D position;

    Vector3D minBound;
    Vector3D maxBound;
    double boundingRadius;
    Vector3D boundingCenter;

    object* bvhRoot = nullptr;
    std::vector<object*> bvhObjects;

    void addBranchMesh(const Vector3D &start, const Vector3D &end, double radius, int segments, const Material &mat);
    void buildBVH();
    void translate(const Vector3D &delta) override;
    void rotate(const Vector3D &rotation);
    void scale(const Vector3D &scaleVec);
    void recomputeBounds();
    void safeDeleteBVH();
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

class TriangleObject  : public object {
public:
    const Dreieck* t;

    TriangleObject(const Dreieck* tri)
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

