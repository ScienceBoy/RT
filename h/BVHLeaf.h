#pragma once

#include "object.h"
#include <vector>

class BVHLeaf : public object {
public:
    std::vector<object*> primitives;

    BVHLeaf(const std::vector<object*>& prims)
        : object(Material()), primitives(prims) {}

    virtual ~BVHLeaf() {
        // primitives werden vom BVH-Baum-Destruktor verwaltet; 
        // falls Ownership anders ist, passe hier an.
    }

    void getWorldAABB(Vector3D& min, Vector3D& max) const override {
        if (primitives.empty()) {
            min = Vector3D(0,0,0);
            max = Vector3D(0,0,0);
            return;
        }
        Vector3D mi, ma;
        primitives[0]->getWorldAABB(min, max);
        for (size_t i = 1; i < primitives.size(); ++i) {
            primitives[i]->getWorldAABB(mi, ma);
            min.x = std::min(min.x, mi.x);
            min.y = std::min(min.y, mi.y);
            min.z = std::min(min.z, mi.z);
            max.x = std::max(max.x, ma.x);
            max.y = std::max(max.y, ma.y);
            max.z = std::max(max.z, ma.z);
        }
    }

    void intersect(const Ray& ray, Hit& hit) const override {
        for (auto* p : primitives) {
            p->intersect(ray, hit);
        }
    }

    void drawWireframePixels(Wireframe& wf, DrawMode mode) const override {
        for (auto* p : primitives) p->drawWireframePixels(wf, mode);
    }

    void drawFlat(Wireframe& wf, DrawMode mode) const override {
        for (auto* p : primitives) p->drawFlat(wf, mode);
    }
};
