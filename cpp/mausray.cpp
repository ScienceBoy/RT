#include "ray.h"
#include "camera.h"
#include <cmath>
#include <numbers>

/*Ray createRayFromMouse(int x, int y, int w, int h, Camera* camera)
{
    double ndcX = (2.0 * x) / w - 1.0;
    double ndcY = 1.0 - (2.0 * y) / h;

    double fovRad = camera->fov * 3.14159265 / 180.0;
    double scale = tan(fovRad / 2.0);

    Vector3D rayDirCamera(
        ndcX * scale,
        ndcY * scale,
        -1.0
    );

    Vector3D dir =
        camera->u * rayDirCamera.x +
        camera->v * rayDirCamera.y +
        camera->w * rayDirCamera.z;

    dir.normalize();

    return { camera->position, dir };
}*/

Ray createRayFromMouse(int mouseX, int mouseY, int screenW, int screenH, Camera* camera) {
    double zoom = 0.7; // wie in deinem ersten Code
    double aspect = double(screenW) / double(screenH);

    // Normalisierte Device Coordinates [-1, 1]
    double ndcX = (2.0 * mouseX / screenW - 1.0) / zoom;
    double ndcY = (1.0 - 2.0 * mouseY / screenH) / zoom;

    // Berücksichtige FOV
    double fovRad = camera->fov * 3.14159265 / 180.0;
    double scale = tan(fovRad / 2.0);

    double px = ndcX * scale * aspect;
    double py = ndcY * scale;
    double pz = -1.0; // Kamera blickt in -z Richtung

    // Ray in Weltkoordinaten
    Vector3D dir = (camera->u * px + camera->v * py + camera->w * pz).normalized();

    return Ray{camera->position, dir};
}