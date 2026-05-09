#include "wireframe_front.h"

void WireframeFront::project(const Vector3D& p, int& x, int& y, double& z) 
{
    Vector3D center;
    center.x = (sceneMin.x + sceneMax.x) / 2.0;
    center.y = (sceneMin.y + sceneMax.y) / 2.0;
    center.z = (sceneMin.z + sceneMax.z) / 2.0;

    double sizeX = sceneMax.x - sceneMin.x;
    double sizeY = sceneMax.y - sceneMin.y;

    double neededScaleX = (w * 0.45) / sizeX;
    double neededScaleY = (h * 0.45) / sizeY;
    // scale = std::min(neededScaleX, neededScaleY);

    x = int(w/2 + (p.x - center.x) * scale);
    y = int(h/2 - (p.y - center.y) * scale);

    z = p.z;
}

Ray WireframeFront::createRay(int x, int y)
{
    Vector3D center;
    center.x = (sceneMin.x + sceneMax.x) / 2.0;
    center.y = (sceneMin.y + sceneMax.y) / 2.0;
    center.z = (sceneMin.z + sceneMax.z) / 2.0;

    double worldX = (x - w/2) / scale + center.x;
    double worldY = -(y - h/2) / scale + center.y;

    double startZ = sceneMax.z + 10.0; 
    Vector3D origin = Vector3D(worldX, worldY, startZ);
    Vector3D dir    = Vector3D(0.001, 0.001, -1);

    return Ray(origin, dir.normalized());
}

Vector3D WireframeFront::getDragPlaneNormal() const {
    return Vector3D(0, 0, 1); // Z-Achse
}