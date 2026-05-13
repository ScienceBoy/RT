#include "kugel.h"
#include "wireframe.h"
#include <cmath>
#include <numbers>
#include <vector>
#include <random>
#include "camera.h"
#include "TextureManager.h"

// Standardkonstruktor
Kugel::Kugel() : object(Material()), radius(1.0)
{
    position = Vector3D(0,0,0);

    minBound = position - Vector3D(radius, radius, radius);
    maxBound = position + Vector3D(radius, radius, radius);
    boundingRadius = radius;
    boundingCenter = position;
}

// Konstruktor mit Parameter
Kugel::Kugel(const Vector3D& pos, double r, const Material& mat)
    : object(mat), radius(r)
{
    position = pos;

    minBound = position - Vector3D(radius, radius, radius);
    maxBound = position + Vector3D(radius, radius, radius);
    boundingRadius = radius;
    boundingCenter = position;
}

// Schnitt mit Ray
void Kugel::intersect(const Ray& ray, Hit& hit) const
{
    // BVH / AABB Early-Out
    if (!intersectBounding(ray))
        return;

    // Lokale Berechnung: Ray relativ zu position
    Vector3D v = ray.origin - position; // p - c

    double a = ray.direction * ray.direction;
    double b = 2.0 * (v * ray.direction);
    double c = v * v - radius*radius;

    double d = b*b - 4*a*c;
    if (d < 0) return;

    double sqrtD = std::sqrt(d);
    double t1 = (-b - sqrtD) / (2*a);
    double t2 = (-b + sqrtD) / (2*a);

    double t = (t1 > 1e-6) ? t1 : ((t2 > 1e-6) ? t2 : -1);
    if (t < 0) return;
    //if(t < 1e-6) return;

    if(t < hit.t)
    {
        hit.hit = true;
        hit.t = t;
        hit.obj = this;
        hit.material = &mat;

        hit.position = ray.punkt(t);                   // Weltkoordinate
        Vector3D N = (hit.position - position).normalized(); // Normale relativ zu Zentrum
        if (N * ray.direction > 0) N = N * -1;
        hit.normale = N;
        hit.geomNormale = N;
    }
}

void Kugel::getWorldAABB(Vector3D& min, Vector3D& max) const
{
    Vector3D r(radius, radius, radius);
    min = position - r;
    max = position + r;
}

void Kugel::drawFlat(Wireframe& wf, DrawMode mode) const
{
    int slices = 32;   // um Y-Achse
    int stacks = 16;   // von oben nach unten

    /*Material matHere = mat;

    if (mode == DrawMode::HIGHLIGHT)
        matHere = makeMaterialSimple(Farbe(1,1,0));
    else if (mode == DrawMode::HOVER)
        matHere = makeMaterialSimple(Farbe(0,1,1));
    else
        //matHere = makeMaterialSimple(mat.diffuse);
        matHere = makeMaterialSimple(getDebugColor(mat));
    */

    for (int i = 0; i < stacks; i++)
    {
        double phi0 = 3.14159265 * i / stacks;
        double phi1 = 3.14159265 * (i + 1) / stacks;

        for (int j = 0; j < slices; j++)
        {
            double theta0 = 2 * 3.14159265 * j / slices;
            double theta1 = 2 * 3.14159265 * (j + 1) / slices;

            // 4 Punkte auf Kugel
            Vector3D p0(
                radius * sin(phi0) * cos(theta0),
                radius * cos(phi0),
                radius * sin(phi0) * sin(theta0)
            );

            Vector3D p1(
                radius * sin(phi1) * cos(theta0),
                radius * cos(phi1),
                radius * sin(phi1) * sin(theta0)
            );

            Vector3D p2(
                radius * sin(phi1) * cos(theta1),
                radius * cos(phi1),
                radius * sin(phi1) * sin(theta1)
            );

            Vector3D p3(
                radius * sin(phi0) * cos(theta1),
                radius * cos(phi0),
                radius * sin(phi0) * sin(theta1)
            );

            // Verschieben
            p0 += position;
            p1 += position;
            p2 += position;
            p3 += position;

            // Projektion
            int x0,y0,x1,y1,x2,y2,x3,y3;
            double z0,z1,z2,z3;

            // Weltkoordinaten
            Vector3D A = p0;
            Vector3D B = p1;
            Vector3D C = p2;
            Vector3D D = p3;
            Vector3D M = (A+B+C+D) * (1.0 / 4.0);

            Vector3D normale = (C-A).cross(B-A).normalized();

            //Vector3D viewDir = (M - cam[0].position).normalized();
            //Vector3D N = (normale * viewDir < 0) ? normale : -normale;
            Vector3D N = normale;

            // Licht (Weltlicht)
            Vector3D L = (lights[0].position - M).normalized();

            double intensity = std::max(0.0, N * L);

            Farbe base =
                (mode == DrawMode::HIGHLIGHT) ? Farbe(1,1,0) :
                (mode == DrawMode::HOVER)     ? Farbe(0,1,1) :
                                                mat.diffuse;

            Farbe shaded(
                base.r * intensity,
                base.g * intensity,
                base.b * intensity
            );

            Material matHere = makeMaterialSimple(shaded);

            wf.project(p0,x0,y0,z0);
            wf.project(p1,x1,y1,z1);
            wf.project(p2,x2,y2,z2);
            wf.project(p3,x3,y3,z3);

            if (!useFlat)
            {
                wf.drawLine(x0,y0,z0,x1,y1,z1, matHere);
                wf.drawLine(x2,y2,z2,x3,y3,z3, matHere);
            }
            
            //  Zwei Dreiecke
            wf.fillTriangle(x0,y0,z0,x1,y1,z1,x2,y2,z2, matHere);
            wf.fillTriangle(x0,y0,z0,x2,y2,z2,x3,y3,z3, matHere);
        }
    }
}

// Wireframe-Darstellung
void Kugel::drawWireframePixels(Wireframe& wf, DrawMode mode) const
{
    int steps = 64;
    int x0,y0,x1,y1;
    double z0,z1;

    // XZ-Ebene
    for(int i=0;i<steps;i++)
    {
        double a0 = 2*3.14159265*i/steps;
        double a1 = 2*3.14159265*(i+1)/steps;

        Vector3D p0(position.x + radius*cos(a0), position.y, position.z + radius*sin(a0));
        Vector3D p1(position.x + radius*cos(a1), position.y, position.z + radius*sin(a1));

        Material matHere = mat;

        if (mode == DrawMode::HIGHLIGHT)
            matHere = makeMaterialSimple(Farbe(1,1,0));

        wf.project(p0,x0,y0,z0); wf.project(p1,x1,y1,z1);
        wf.drawLine(x0,y0,z0,x1,y1,z1,matHere);
    }

    // XY-Ebene
    for(int i=0;i<steps;i++)
    {
        double a0 = 2*3.14159265*i/steps;
        double a1 = 2*3.14159265*(i+1)/steps;

        Vector3D p0(position.x + radius*cos(a0), position.y + radius*sin(a0), position.z);
        Vector3D p1(position.x + radius*cos(a1), position.y + radius*sin(a1), position.z);

        wf.project(p0,x0,y0,z0); 
        wf.project(p1,x1,y1,z1);
        wf.drawLine(x0,y0,z0,x1,y1,z1,mat);
    }

    // YZ-Ebene
    for(int i=0;i<steps;i++)
    {
        double a0 = 2*3.14159265*i/steps;
        double a1 = 2*3.14159265*(i+1)/steps;

        Vector3D p0(position.x, position.y + radius*cos(a0), position.z + radius*sin(a0));
        Vector3D p1(position.x, position.y + radius*cos(a1), position.z + radius*sin(a1));

        wf.project(p0,x0,y0,z0); wf.project(p1,x1,y1,z1);
        wf.drawLine(x0,y0,z0,x1,y1,z1,mat);
    }
}

void Kugel::updateBounds()
{
    minBound = position - Vector3D(radius, radius, radius);
    maxBound = position + Vector3D(radius, radius, radius);
    boundingCenter = position;
    boundingRadius = radius;
}

// RNG
double randomDouble(double min, double max)
{
    static std::mt19937 rng(std::random_device{}());
    std::uniform_real_distribution<double> dist(min, max);
    return dist(rng);
}

// zufälliger Punkt in Kugel
Vector3D randomInSphere()
{
    Vector3D p(
        randomDouble(-1, 1),
        randomDouble(-1, 1),
        randomDouble(-1, 1)
    );

    return p.normalized(); // Punkt auf Einheitssphäre
}

Vector3D randomOnUnitSphere()
{
    double z = randomDouble(-1.0, 1.0);
    double t = randomDouble(0.0, 2.0 * 3.14159265);

    double r = std::sqrt(1.0 - z * z);

    return Vector3D(
        r * std::cos(t),
        z,
        r * std::sin(t)
    );
}

// rekursiv
void generateCloudRecursive(
    std::vector<Kugel>& kugeln,
    const Vector3D& center,
    double radius,
    int depth,
    int maxDepth,
    const Material& mat)
{
    if (depth >= maxDepth)
        return;

    kugeln.emplace_back(center, radius, mat);  // WICHTIG!

    int childCount = 3;

    for (int i = 0; i < childCount; i++)
    {
        Vector3D dir = randomOnUnitSphere();

        Vector3D newCenter = center + dir * radius;

        double newRadius = radius * randomDouble(0.6, 0.9);

        generateCloudRecursive(
            kugeln,
            newCenter,
            newRadius,
            depth + 1,
            maxDepth,
            mat
        );
    }
}

// öffentliche Funktion
std::vector<Kugel> generateWolkeAusKugeln(
    const Vector3D& center,
    double startRadius,
    int maxDepth,
    const Material& mat)
{
    std::vector<Kugel> kugeln;

    generateCloudRecursive(
        kugeln,
        center,
        startRadius,
        0,
        maxDepth,
        mat
    );

    return kugeln;
}