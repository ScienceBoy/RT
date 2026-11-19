#include "ray.h"
#include "camera.h"
#include <cmath>

/*Ray createRayFromMouse(int x, int y, int w, int h, Camera* camera)
{
    double ndcX = (2.0 * x) / w - 1.0;
    double ndcY = 1.0 - (2.0 * y) / h;

    double fovRad = camera->fov * M_PI / 180.0;
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
    double zoom = 0.7;
    double aspect = double(screenW) / double(screenH);

    double ndcX = (2.0 * mouseX / screenW - 1.0) / zoom;
    double ndcY = (1.0 - 2.0 * mouseY / screenH) / zoom;

    double fovRad = camera->fov * M_PI / 180.0;
    double tanHalfFov = std::tan(fovRad * 0.5);

    double px = ndcX * tanHalfFov * aspect;
    double py = ndcY * tanHalfFov;
    double pz = -1.0;

    Vector3D dir = (camera->u * px + camera->v * py + camera->w * pz).normalized();
    return Ray{camera->position, dir};
}