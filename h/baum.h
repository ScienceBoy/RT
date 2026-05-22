#pragma once

#include "scene.h"
#include "vector3d.h"
#include "farbe.h"
#include "material.h"
#include "mesh.h"
#include "stab.h"
#include <cmath>
#include <vector>
#include <memory>

class baum
{
public:
    std::vector<std::unique_ptr<object>> generateTree(
        Vector3D pos,
        Vector3D richtung,
        double laenge,
        double radius,
        int tiefe,
        double wkeitWeiteresGlied = 0.7);
    
    double randomDouble(double min, double max);
    std::unique_ptr<object> Blatt(
        const Vector3D& pos,
        double size,
        const Material& mat);
};

