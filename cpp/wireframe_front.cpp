#include "wireframe_front.h"

// Blick von vorne (+Z nach -Z). Bildschirm: rechts = +X, oben = +Y (wie bisher).
AnsichtParameter WireframeFront::ansicht() const
{
    AnsichtParameter a;
    a.perspektivisch = false;
    a.auge = ansichtsMitte(sceneobjects.empty());
    a.rechts = Vector3D(1, 0, 0);
    a.oben = Vector3D(0, 1, 0);
    a.vorne = Vector3D(0, 0, -1);
    a.pixelProEinheit = scale;
    return a;
}

Vector3D WireframeFront::getDragPlaneNormal() const {
    return Vector3D(0, 0, 1); // Z-Achse
}
