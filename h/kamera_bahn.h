#pragma once
// Kreisbahn der Kamera (target, distance, yaw, pitch; Camera::updateOrbit) aus Position und
// Zielpunkt neu bestimmen. Noetig nach jeder Aenderung von position/lookAt von aussen
// (Objektfenster, Ziehen in den Ansichten, Knoepfe -X..+Z): Camera::update() setzt nur target.
// Sonst rechnete das Mausrad (updateOrbit) mit alten Winkeln und die Kamera sprang.

#include "camera.h"
#include <algorithm>
#include <cmath>

inline void kameraBahnAbgleichen(Camera &c)
{
    c.target = c.lookAt;
    Vector3D d = c.position - c.target;
    c.distance = d.length();
    if (c.distance > 1e-12)
    {
        c.pitch = std::asin(std::clamp(d.y / c.distance, -1.0, 1.0));
        c.yaw = std::atan2(d.x, d.z);
    }
}
