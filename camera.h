#pragma once
#include "vector3d.h"
#include "ray.h"
#include "wireframe.h"
#include "fenster.h"
#include "material.h"

class Camera
{
public:
    Vector3D position;
    Vector3D lookAt;
    Vector3D up;

    Vector3D u, v, w; // Kamera-Basis
    double fov;
    double aspectRatio;
    double halfHeight, halfWidth;

    double yaw = 0;
    double pitch = 0;

    Camera(const Vector3D& pos, const Vector3D& lookAt, double fovDeg, double aspect, const Vector3D& upVec = Vector3D(0,1,0));

    Ray makeRay(double x, double y, Fenster& f) const;

    void drawFlat(Wireframe &wf, DrawMode mode) const;
    void drawWireframePixels(Wireframe& wf, DrawMode mode = DrawMode::NORMAL) const;

    void update();          // Basisvektoren neu berechnen
    void updateDirection(); // yaw/pitch → Blickrichtung
    void setPosition(const Vector3D& pos);
};

