#pragma once

#include "BVHNode.h"
#include <vector>
#include <algorithm>

// Hilfsfunktion: Welt-AABB berechnen
inline void computeBounds(object* obj, Vector3D& minB, Vector3D& maxB)
{
    obj->getWorldAABB(minB, maxB);
}

// BVH Build
inline object* buildBVH(std::vector<object*>& objects)
{
    if (objects.empty())
        return nullptr;

    if (objects.size() == 1)
        return objects[0];

    // Gesamt-Bounding Box berechnen
    Vector3D minB, maxB;
    computeBounds(objects[0], minB, maxB);

    for (auto obj : objects)
    {
        Vector3D mi, ma;
        computeBounds(obj, mi, ma);

        minB.x = std::min(minB.x, mi.x);
        minB.y = std::min(minB.y, mi.y);
        minB.z = std::min(minB.z, mi.z);

        maxB.x = std::max(maxB.x, ma.x);
        maxB.y = std::max(maxB.y, ma.y);
        maxB.z = std::max(maxB.z, ma.z);
    }

    // Beste Split-Achse wählen
    Vector3D extent = maxB - minB;

    int axis = 0;
    if (extent.y > extent.x) axis = 1;
    if (extent.z > extent[axis]) axis = 2;

    // Sortieren entlang Achse
    /*std::sort(objects.begin(), objects.end(),
        [axis](object* a, object* b)
        {
            return a->boundingCenter[axis] < b->boundingCenter[axis];
        });*/

    /*std::sort(objects.begin(), objects.end(),
        [axis](object* a, object* b)
        {
            Vector3D minA, maxA, minB, maxB;

            a->getWorldAABB(minA, maxA);
            b->getWorldAABB(minB, maxB);

            double centerA = (minA[axis] + maxA[axis]) * 0.5;
            double centerB = (minB[axis] + maxB[axis]) * 0.5;

            return centerA < centerB;
        });*/

    int mid = objects.size() / 2;

    std::nth_element(objects.begin(), objects.begin() + mid, objects.end(),
        [axis](object* a, object* b)
        {
/*            Vector3D minA, maxA, minB, maxB;

            a->getWorldAABB(minA, maxA);
            b->getWorldAABB(minB, maxB);

            double centerA = (minA[axis] + maxA[axis]) * 0.5;
            double centerB = (minB[axis] + maxB[axis]) * 0.5;

            return centerA < centerB;
*/
            return a->boundingCenter[axis] < b->boundingCenter[axis];
        });

    std::vector<object*> left(objects.begin(), objects.begin() + mid);
    std::vector<object*> right(objects.begin() + mid, objects.end());

    object* leftNode = buildBVH(left);
    object* rightNode = buildBVH(right);

    return new BVHNode(leftNode, rightNode);
}