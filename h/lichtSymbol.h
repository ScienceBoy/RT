#pragma once
// Richtungslicht: Position des Symbols in den Ansichten.
// Ein Richtungslicht hat keine echte Position (es ist unendlich weit weg); das Symbol steht nur
// zur Anzeige irgendwo in der Szene. Verschieben bewegt das Symbol, die Richtung bleibt.
// Gespeichert in DirectionalLight::editorPos (das liefert auch getPosition()).

#include "vector3d.h"

class DirectionalLight;

Vector3D richtungslichtSymbol(const DirectionalLight *l);
void richtungslichtSymbolSetzen(DirectionalLight *l, const Vector3D &position);
