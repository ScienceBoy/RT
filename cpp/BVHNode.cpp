#include "BVHNode.h"
#include <algorithm>

// Konstruktor
BVHNode::BVHNode(object* l, object* r)
    : object(Material())
{
    this->left = l;
    this->right = r;

    Vector3D minL, maxL, minR, maxR;

    l->getWorldAABB(minL, maxL);
    r->getWorldAABB(minR, maxR);

    minBound.x = std::min(minL.x, minR.x);
    minBound.y = std::min(minL.y, minR.y);
    minBound.z = std::min(minL.z, minR.z);

    maxBound.x = std::max(maxL.x, maxR.x);
    maxBound.y = std::max(maxL.y, maxR.y);
    maxBound.z = std::max(maxL.z, maxR.z);

    boundingCenter = (minBound + maxBound) * 0.5;
}

// Robuster AABB Test
/*bool BVHNode::intersectAABB(const Ray& ray, double tMax) const
{
    double tmin = 0.0;

    for (int i = 0; i < 3; i++)
    {
        double invD = ray.invDirection[i];

        double t0 = (minBound[i] - ray.origin[i]) * invD;
        double t1 = (maxBound[i] - ray.origin[i]) * invD;

        double tmin_i = std::min(t0, t1);
        double tmax_i = std::max(t0, t1);

        tmin = std::max(tmin, tmin_i);
        tMax = std::min(tMax, tmax_i);

        if (tMax <= tmin)
            return false;
    }

    return true;
}*/

bool BVHNode::intersectAABB(const Ray& ray, double tMax) const
{
    double tmin = ( (ray.sign[0] ? maxBound.x : minBound.x) - ray.origin.x ) * ray.invDirection.x;
    double tmax = ( (ray.sign[0] ? minBound.x : maxBound.x) - ray.origin.x ) * ray.invDirection.x;

    double tymin = ( (ray.sign[1] ? maxBound.y : minBound.y) - ray.origin.y ) * ray.invDirection.y;
    double tymax = ( (ray.sign[1] ? minBound.y : maxBound.y) - ray.origin.y ) * ray.invDirection.y;

    if ( (tmin > tymax) || (tymin > tmax) )
        return false;

    if (tymin > tmin) tmin = tymin;
    if (tymax < tmax) tmax = tymax;

    double tzmin = ( (ray.sign[2] ? maxBound.z : minBound.z) - ray.origin.z ) * ray.invDirection.z;
    double tzmax = ( (ray.sign[2] ? minBound.z : maxBound.z) - ray.origin.z ) * ray.invDirection.z;

    if ( (tmin > tzmax) || (tzmin > tmax) )
        return false;

    return true;
}

// Traversal
/*void BVHNode::intersect(const Ray& ray, Hit& hit) const
{
    if (!intersectAABB(ray, hit.t))
        return;

    bool hitSomething = false;

    if (left)
        left->intersect(ray, hit);

    if (right)
        right->intersect(ray, hit);

    return;
}*/

void BVHNode::intersect(const Ray& ray, Hit& hit) const
{
    if (!intersectAABB(ray, hit.t))  // check, ob dieser Knoten des BVH-Baums vom Ray getroffen wird
        return;

    object* first = left;
    object* second = right;

    Vector3D minL, maxL, minR, maxR;

    if (left && right)
    {
        /*left->getWorldAABB(minL, maxL);
        right->getWorldAABB(minR, maxR);

        Vector3D centerL = (minL + maxL) * 0.5;
        Vector3D centerR = (minR + maxR) * 0.5;*/

        Vector3D centerL = left->boundingCenter;
        Vector3D centerR = right->boundingCenter;

        double distL = (centerL - ray.origin).lengthSquared();
        double distR = (centerR - ray.origin).lengthSquared();

        if (distR < distL)
            std::swap(first, second);
    }

    // den näheren Ast zuerst testen
    if (first)
        first->intersect(ray, hit);

    if (second)
        second->intersect(ray, hit);
}

void BVHNode::drawWireframePixels(Wireframe& wf, DrawMode mode) const 
{
    if (left) left->drawWireframePixels(wf, mode);
    if (right) right->drawWireframePixels(wf, mode);
}

void BVHNode::drawFlat(Wireframe& wf, DrawMode mode) const 
{
    if (left) left->drawFlat(wf, mode);
    if (right) right->drawFlat(wf, mode);
}