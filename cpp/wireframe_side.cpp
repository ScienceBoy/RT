#include "wireframe_side.h"

// Blick von der Seite (+X nach -X). Bildschirm: rechts = -Z, oben = +Y (wie bisher).
AnsichtParameter WireframeSide::ansicht() const
{
    AnsichtParameter a;
    a.perspektivisch = false;
    a.auge = ansichtsMitte(sceneobjects.empty());
    a.rechts = Vector3D(0, 0, -1);
    a.oben = Vector3D(0, 1, 0);
    a.vorne = Vector3D(-1, 0, 0);
    a.pixelProEinheit = scale;
    return a;
}

Vector3D WireframeSide::getDragPlaneNormal() const {
    return Vector3D(1, 0, 0); // X-Achse
}
