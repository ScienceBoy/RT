#include "wireframe_top.h"

void WireframeTop::project(const Vector3D& p, int& x, int& y, double& z) {
    Vector3D center;
    center.x = (sceneMin.x + sceneMax.x) / 2.0;
    center.y = (sceneMin.y + sceneMax.y) / 2.0;
    center.z = (sceneMin.z + sceneMax.z) / 2.0;

    double sizeX = sceneMax.x - sceneMin.x;
    double sizeY = sceneMax.y - sceneMin.y;
    double sizeZ = sceneMax.z - sceneMin.z;

    double neededScaleX = (w * 0.45) / sizeX;
    double neededScaleZ = (h * 0.45) / sizeZ;
    //scale = std::min(neededScaleX, neededScaleZ);

    x = w/2 + (p.x - center.x) * scale;
    y = h/2 - (p.z - center.z) * scale;

    z = p.z;
}

Ray WireframeTop::createRay(int x, int y)
{
    Vector3D center;
    center.x = (sceneMin.x + sceneMax.x) / 2.0;
    center.y = (sceneMin.y + sceneMax.y) / 2.0;
    center.z = (sceneMin.z + sceneMax.z) / 2.0;

    //  inverse projection
    double worldX = (x - w/2) / scale + center.x;
    double worldZ = -(y - h/2) / scale + center.z;

    double startY = sceneMax.y + 10.0; 
    Vector3D origin = Vector3D(worldX, startY, worldZ);
    Vector3D dir    = Vector3D(0.001, -1, 0.001);

    return Ray(origin, dir.normalized());
}

Vector3D WireframeTop::getDragPlaneNormal() const {
    return Vector3D(0, 1, 0); // Y-Achse
}