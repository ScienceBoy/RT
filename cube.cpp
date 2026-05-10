#include "cube.h"
#include "Wireframe.h"
#include "camera.h"
#include <iostream>

//Cube::Cube() : object(Farbe(1,1,1))
Cube::Cube() : object(Material())
{}

//Cube::Cube(const Vector3D& c, double s, const Farbe& f)
Cube::Cube(const Vector3D& c, double s, const Material& mat)
    : object(mat)
{
    position = c; // Weltposition

    halfSize = s / 2.0;

    // lokale Ecken um (0,0,0)
    Vector3D p000 = Vector3D(-halfSize,-halfSize,-halfSize);
    Vector3D p001 = Vector3D(-halfSize,-halfSize, halfSize);
    Vector3D p010 = Vector3D(-halfSize, halfSize,-halfSize);
    Vector3D p011 = Vector3D(-halfSize, halfSize, halfSize);
    Vector3D p100 = Vector3D( halfSize,-halfSize,-halfSize);
    Vector3D p101 = Vector3D( halfSize,-halfSize, halfSize);
    Vector3D p110 = Vector3D( halfSize, halfSize,-halfSize);
    Vector3D p111 = Vector3D( halfSize, halfSize, halfSize);

    // Front
    tris.emplace_back(p001,p101,p111,mat);
    tris.emplace_back(p001,p111,p011,mat);

    // Back
    tris.emplace_back(p000,p110,p100,mat);
    tris.emplace_back(p000,p010,p110,mat);

    // Left
    tris.emplace_back(p000,p011,p010,mat);
    tris.emplace_back(p000,p001,p011,mat);

    // Right
    tris.emplace_back(p100,p110,p111,mat);
    tris.emplace_back(p100,p111,p101,mat);

    // Top
    tris.emplace_back(p010,p011,p111,mat);
    tris.emplace_back(p010,p111,p110,mat);

    // Bottom
    tris.emplace_back(p000,p100,p101,mat);
    tris.emplace_back(p000,p101,p001,mat);

    // Bounding Box (lokal!)
    minBound = position - Vector3D( halfSize, halfSize, halfSize);
    maxBound = position + Vector3D( halfSize, halfSize, halfSize);
    boundingRadius = std::sqrt(3.0) * halfSize;
    boundingCenter = position;
}

void Cube::intersect(const Ray& ray, Hit& hit) const
{
    Ray localRay;
    localRay.origin = ray.origin - position;
    localRay.direction = ray.direction;

    // bounding ebenfalls LOCAL
    Vector3D minB(-halfSize, -halfSize, -halfSize);
    Vector3D maxB( halfSize,  halfSize,  halfSize);

    if (!intersectAABB(localRay, minB, maxB))
        return;

    Hit tempHit;
    tempHit.t = hit.t;
    tempHit.hit = false;
    tempHit.obj = nullptr;

    for (const auto& t : tris)
    {
        t.intersect(localRay, tempHit);
    }

    hit = tempHit;

    if (hit.t > 0)
    {
        hit.position = localRay.origin + localRay.direction * hit.t;
        hit.position += position;
    }
}

void Cube::drawFlat(Wireframe& wf, DrawMode mode) const
{
    int x0,y0,x1,y1,x2,y2;
    double z0,z1,z2;

    /*Material matHere = mat;

    if (mode == DrawMode::HIGHLIGHT)
        matHere = makeMaterialSimple(Farbe(1,1,0));
    else if (mode == DrawMode::HOVER)
        matHere = makeMaterialSimple(Farbe(0,1,1));
    else
        matHere = makeMaterialSimple(mat.diffuse);*/

    for(const auto& tri : tris)
    {
        Vector3D a = tri.a + position;
        Vector3D b = tri.b + position;
        Vector3D c = tri.c + position;

        Vector3D M = (a+b+c) * (1.0 / 3.0);
        Vector3D normale = (c-a).cross(b-a).normalized();

        Vector3D viewDir = (M - cam[0].position).normalized();
        Vector3D N = (normale * viewDir < 0) ? normale : -normale;

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


        // Projektion
        wf.project(a, x0, y0, z0);
        wf.project(b, x1, y1, z1);
        wf.project(c, x2, y2, z2);

        //  Fläche füllen
        wf.fillTriangle(x0, y0, z0, x1, y1, z1, x2, y2, z2, matHere);

        wf.drawLine(x0,y0,z0,x1,y1,z1,matHere);
        wf.drawLine(x1,y1,z1,x2,y2,z2,matHere);
        wf.drawLine(x2,y2,z2,x0,y0,z0, matHere);
    }
}

void Cube::drawWireframePixels(Wireframe& wf, DrawMode mode) const
{
    int x0,y0,x1,y1;
    double z0, z1;
    Material matHere = mat;
    
    if (mode == DrawMode::HIGHLIGHT)
    matHere = makeMaterialSimple(Farbe(1,1,0));

    for(const auto& tri : tris)
    {
        Vector3D a = tri.a + position;
        Vector3D b = tri.b + position;
        Vector3D c = tri.c + position;

        wf.project(a, x0, y0, z0);
        wf.project(b, x1, y1, z1);
        wf.drawLine(x0, y0, z0, x1, y1, z1, matHere);

        wf.project(b, x0, y0, z0);
        wf.project(c, x1, y1, z1);
        wf.drawLine(x0, y0, z0, x1, y1, z1, matHere);

        wf.project(c, x0, y0, z0);
        wf.project(a, x1, y1, z1);
        wf.drawLine(x0, y0, z0, x1, y1, z1, matHere);
    }
}

