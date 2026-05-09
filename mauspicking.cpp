#include "mauspicking.h"
#include "mausray.h"
#include "ray.h"
#include "hit.h"

object* pickObject(
    int mouseX,
    int mouseY,
    int w,
    int h,
    Camera* camera,
    std::vector<object*>& objects)
{
    Ray Mausray = createRayFromMouse(mouseX, mouseY, w, h, camera);

    object* closest = nullptr;
    double minT = 1e9;

    for (auto obj : objects)
    {
        Hit hit;
        obj->intersect(Mausray, hit);

        if (hit.hit && hit.t < minT)
        {
            minT = hit.t;
            closest = obj;
        }
    }

    return closest;
}

bool intersect(const Ray& ray, const Vector3D& planePoint, const Vector3D& planeNormal, double& t)
{
    double denom = planeNormal * ray.direction;
    if(std::abs(denom) < 1e-6) return false; // parallel

    t = (planePoint - ray.origin) * planeNormal / denom;
    return t >= 0;
}