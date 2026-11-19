#pragma once

#include "object.h"
#include "vector3d.h"
#include "ray.h"
#include "hit.h"

class BVHNode : public object
{
public:
    object* left = nullptr;
    object* right = nullptr;

    const wchar_t* getTypeName() const override{return L"BVHNode";}

    Vector3D minBound;
    Vector3D maxBound;

    BVHNode(object* l, object* r);

    // Nur Unterknoten gehoeren dem Baum. Die Blaetter (Dreiecke usw.) gehoeren dem Mesh/Tree;
    // sie zu loeschen hat beim Leeren der Szene den Heap beschaedigt.
    virtual ~BVHNode()
    {
        if (dynamic_cast<BVHNode *>(left))
            delete left;
        if (dynamic_cast<BVHNode *>(right))
            delete right;
    }

    void getWorldAABB(Vector3D& min, Vector3D& max) const override
    {
        min = minBound;
        max = maxBound;
    }

    bool intersectAABB(const Ray& ray, double tMax) const;
    void translate(const Vector3D &delta) override {};
    void rotate(const Vector3D &delta) override {};
    void scale(const Vector3D &delta) override {};
    void intersect(const Ray& ray, Hit& hit) const override;
    void drawWireframePixels(Wireframe &wf, DrawMode mode) const override;
    void drawFlat(Wireframe &wf, DrawMode mode) const override;
};
