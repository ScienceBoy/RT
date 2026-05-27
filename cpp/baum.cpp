#include "baum.h"
#include <vector>
#include <memory>
#include <cmath>
#include <cstdlib>

std::vector<std::unique_ptr<object>> baum::generateTree(
    Vector3D pos,
    Vector3D richtung,
    double laenge,
    double radius,
    int tiefe,
    double wkeitWeiteresGlied)
{
    std::vector<std::unique_ptr<object>> result;

    if (tiefe <= 0 || radius < 0.01)
        return result;

    Vector3D dir = richtung.normalized();

    // Endpunkt des aktuellen Stamms
    Vector3D endPos = pos + dir * laenge;

    // 🌲 Stamm / Ast (Stab)
    Material holz;
    holz.diffuse = Farbe(139, 69, 19);

    result.push_back(
        std::make_unique<Stab>(pos, endPos, radius, holz)
    );

    // 🍃 BLÄTTER (am Ende oder in kleinen Tiefen)
    if (tiefe <= 2)
    {
        Material leafMat;
        leafMat.diffuse = Farbe(0, 200, 0);

        result.push_back(Blatt(endPos, laenge * 0.3, leafMat));
    }

    // 🌿 Wahrscheinlichkeit für weiteres Stamm-Glied
    if (wkeitWeiteresGlied <= randomDouble(0.0, 1.0))
    {
        Vector3D newDir = dir;

        newDir.x += randomDouble(-0.2, 0.2);
        newDir.y += randomDouble(0.2, 0.6);
        newDir.z += randomDouble(-0.2, 0.2);
        newDir.normalize();

        auto next = generateTree(
            endPos,
            newDir,
            laenge * randomDouble(0.75, 0.9),
            radius * 0.85,
            tiefe - 1,
            wkeitWeiteresGlied
        );

        result.insert(
            result.end(),
            std::make_move_iterator(next.begin()),
            std::make_move_iterator(next.end())
        );
    }

    // 🌿 ÄSTE mit Positionsverschiebung (wie dein alter Code)
    int anzahlAeste = 2 + rand() % 3;

    for (int i = 0; i < anzahlAeste; i++)
    {
        // Verschiebung entlang des Stamms (wichtig!)
        double t = randomDouble(0.2, 1.0);
        Vector3D basePoint = pos + (endPos - pos) * t;

        Vector3D newDir = dir;

        newDir.x += randomDouble(-0.6, 0.6);
        newDir.y += randomDouble(0.1, 0.8);
        newDir.z += randomDouble(-0.6, 0.6);
        newDir.normalize();

        auto branch = generateTree(
            basePoint,
            newDir,
            laenge * randomDouble(0.6, 0.85),
            radius * 0.65,
            tiefe - 1,
            wkeitWeiteresGlied
        );

        result.insert(
            result.end(),
            std::make_move_iterator(branch.begin()),
            std::make_move_iterator(branch.end())
        );
    }

    return result;
}

double baum::randomDouble(double min, double max)
{
    return min + (max - min) * (rand() / (double)RAND_MAX);
}

std::unique_ptr<object> baum::Blatt(
    const Vector3D& pos,
    double size,
    const Material& mat)
{
    std::vector<Dreieck> tris;

    // Mittelpunkt
    Vector3D center = pos;

    // 10 Randpunkte
    std::vector<Vector3D> points;

    for (int i = 0; i < 10; i++)
    {
        double angle = (2.0 * M_PI * i) / 10.0;

        double radius = size * randomDouble(0.5, 1.0);

        double x = cos(angle) * radius;
        double y = sin(angle) * radius;

        // leichte Wölbung
        double z = randomDouble(-size * 0.1, size * 0.1);

        points.push_back(
            pos + Vector3D(x, y, z)
        );
    }

    // Dreiecke erzeugen
    for (int i = 0; i < 10; i++)
    {
        Vector3D p1 = center;
        Vector3D p2 = points[i];
        Vector3D p3 = points[(i + 1) % 10];

        tris.push_back(
            Dreieck(p1, p2, p3, mat)
        );
    }

    return std::make_unique<Mesh>(tris, center);
}