#include "wireframe_top.h"

// Blick von oben (-Y). Bildschirm: rechts = +X, oben = -Z.
// Vorher oben = +Z: die Ansicht war gespiegelt (rechts x oben zeigte in die Szene statt zum
// Betrachter), Drehungen um Y liefen dort scheinbar falsch herum, und vorne (+Z, wo die
// Front-Ansicht steht) lag oben statt unten.
AnsichtParameter WireframeTop::ansicht() const
{
    AnsichtParameter a;
    a.perspektivisch = false;
    a.auge = ansichtsMitte(sceneobjects.empty());
    a.rechts = Vector3D(1, 0, 0);
    a.oben = Vector3D(0, 0, -1);
    a.vorne = Vector3D(0, -1, 0);
    a.pixelProEinheit = scale;
    return a;
}

Vector3D WireframeTop::getDragPlaneNormal() const {
    return Vector3D(0, 1, 0); // Y-Achse
}
