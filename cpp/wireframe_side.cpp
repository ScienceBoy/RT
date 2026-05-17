#include "wireframe_side.h"

void WireframeSide::project(const Vector3D& p, int& x, int& y, double& z) {
    Vector3D center;
    center.x = (sceneMin.x + sceneMax.x) / 2.0;
    center.y = (sceneMin.y + sceneMax.y) / 2.0;
    center.z = (sceneMin.z + sceneMax.z) / 2.0;

    double sizeX = sceneMax.x - sceneMin.x;
    double sizeY = sceneMax.y - sceneMin.y;
    double sizeZ = sceneMax.z - sceneMin.z;

    double neededScaleZ = (w * 0.45) / sizeZ;
    double neededScaleY = (h * 0.45) / sizeY;
    //scale = std::min(neededScaleZ, neededScaleY);
    
    x = w/2 - (p.z - center.z) * scale;
    y = h/2 - (p.y - center.y) * scale;

    z = p.z;
}

Ray WireframeSide::createRay(int x, int y)
{
    Vector3D center;
    center.x = (sceneMin.x + sceneMax.x) / 2.0;
    center.y = (sceneMin.y + sceneMax.y) / 2.0;
    center.z = (sceneMin.z + sceneMax.z) / 2.0;

    double worldZ = -(x - w/2) / scale + center.z;
    double worldY = -(y - h/2) / scale + center.y;

    double startX = sceneMax.x + 10.0; 
    Vector3D origin = Vector3D(startX, worldY, worldZ);
    Vector3D dir    = Vector3D(-1, 0.001, 0.001);

    return Ray(origin, dir.normalized());
}

Vector3D WireframeSide::getDragPlaneNormal() const {
    return Vector3D(1, 0, 0); // X-Achse
}