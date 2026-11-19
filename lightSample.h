#pragma once

#include "vector3d.h"
#include "farbe.h"

struct LightSample
{
    Vector3D position; // Position des Samples auf der Lichtquelle
    Vector3D normal; // Normale auf der Lichtquelle am Sample-Punkt
    Vector3D directionToLight; // Normalisierte Richtung von Oberfläche zu Sample
    double distance; // Distanz zum Sample
    Farbe radiance; // Lichtintensität in Richtung Oberfläche
    double pdf; // Wahrscheinlichkeitsdichte des Samples
};
