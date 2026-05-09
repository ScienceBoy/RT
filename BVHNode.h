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

    Vector3D minBound;
    Vector3D maxBound;

    BVHNode(object* l, object* r);

    virtual ~BVHNode()
    {
        delete left;
        delete right;
    }

    void getWorldAABB(Vector3D& min, Vector3D& max) const override
    {
        min = minBound;
        max = maxBound;
    }

    bool intersectAABB(const Ray& ray, double tMax) const;

    void intersect(const Ray& ray, Hit& hit) const override;
    void drawWireframePixels(Wireframe &wf, DrawMode mode) const override;
    void drawFlat(Wireframe &wf, DrawMode mode) const override;
};
