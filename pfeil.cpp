#include "pfeil.h"
#include "wireframe.h"
#include <algorithm>
#include "camera.h"
#include <cmath>

// Konstruktor
Pfeil::Pfeil(const Vector3D& start, const Vector3D& end, double thickness, const Material& mat)
    : object(mat)
{
    position = start; // Weltposition

    Vector3D localStart(0,0,0);
    Vector3D localEnd = end - start; // lokal

    Vector3D dir = localEnd.normalized();
    double length = localEnd.length();

    double headLength = length * 0.2;
    double shaftLength = length - headLength;

    Vector3D shaftEnd = localStart + dir * shaftLength;

    // Orthonormales System für Rechteck-Schaft
    Vector3D up(0,1,0);
    if (std::abs(dir * up) > 0.99) up = Vector3D(1,0,0);
    Vector3D side = dir.cross(up).normalized() * thickness;
    Vector3D up2  = dir.cross(side).normalized() * thickness;

    // Schaft (Quader)
    Vector3D s1 = localStart + side + up2;
    Vector3D s2 = localStart - side + up2;
    Vector3D s3 = localStart - side - up2;
    Vector3D s4 = localStart + side - up2;

    Vector3D e1 = shaftEnd + side + up2;
    Vector3D e2 = shaftEnd - side + up2;
    Vector3D e3 = shaftEnd - side - up2;
    Vector3D e4 = shaftEnd + side - up2;

    addQuad(s1,s2,e2,e1,mat);
    addQuad(s2,s3,e3,e2,mat);
    addQuad(s3,s4,e4,e3,mat);
    addQuad(s4,s1,e1,e4,mat);

    // Spitze
    Vector3D tip = localEnd;
    addTriangle(e1,e2,tip,mat);
    addTriangle(e2,e3,tip,mat);
    addTriangle(e3,e4,tip,mat);
    addTriangle(e4,e1,tip,mat);

    // Bounding Box
    minBound = localStart;
    maxBound = localStart;
    auto expand = [&](const Vector3D& p)
    {
        minBound.x = std::min(minBound.x, p.x);
        minBound.y = std::min(minBound.y, p.y);
        minBound.z = std::min(minBound.z, p.z);

        maxBound.x = std::max(maxBound.x, p.x);
        maxBound.y = std::max(maxBound.y, p.y);
        maxBound.z = std::max(maxBound.z, p.z);
    };
    minBound = position - minBound;
    maxBound = position - maxBound;

    expand(localStart); expand(localEnd);
    expand(s1); expand(s2); expand(s3); expand(s4);
    expand(e1); expand(e2); expand(e3); expand(e4);

    boundingCenter = (minBound + maxBound) * 0.5;
    boundingRadius = 0.0;
    auto testPoint = [&](const Vector3D& p)
    {
        double d = (p - boundingCenter).length();
        if (d > boundingRadius) boundingRadius = d;
    };

    testPoint(localStart);
    testPoint(localEnd);
    testPoint(s1); testPoint(s2); testPoint(s3); testPoint(s4);
    testPoint(e1); testPoint(e2); testPoint(e3); testPoint(e4);
}

// Dreieck hinzufügen
void Pfeil::addQuad(const Vector3D& a, const Vector3D& b, const Vector3D& c, const Vector3D& d, const Material& mat)
{
    tris.emplace_back(a,b,c,mat);
    tris.emplace_back(a,c,d,mat);
}

void Pfeil::addTriangle(const Vector3D& a, const Vector3D& b, const Vector3D& c, const Material& mat)
{
    tris.emplace_back(a,b,c,mat);
}

// Weltposition setzen
void Pfeil::setPosition(const Vector3D& pos)
{
    position = pos;
}

// Schnitt mit Ray
void Pfeil::intersect(const Ray& ray, Hit& hit) const
{    
    // BVH / AABB Early-Out
    if (!intersectBounding(ray))
        return;

    Ray localRay;
    localRay.origin = ray.origin - position;
    localRay.direction = ray.direction;

    for(const auto& t : tris)
    {
        t.intersect(localRay, hit);
    }

    if(hit.t > 0)
        hit.position = localRay.origin + localRay.direction * hit.t + position;
}

void Pfeil::drawFlat(Wireframe& wf, DrawMode mode) const
{
    int x0,y0,x1,y1,x2,y2;
    double z0, z1, z2;

    /*Material matHere = mat;

    if (mode == DrawMode::HIGHLIGHT)
        matHere = makeMaterialSimple(Farbe(1,1,0));  // Gelb
    else if (mode == DrawMode::HOVER)
        matHere = makeMaterialSimple(Farbe(0,1,1));  // Cyan
    else
        matHere = makeMaterialSimple(mat.diffuse);   // Standard

    */

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

// Wireframe
void Pfeil::drawWireframePixels(Wireframe& wf, DrawMode mode) const
{
    int x0,y0,x1,y1;
    double z0,z1;
    Material matHere = mat;

    if (mode == DrawMode::HIGHLIGHT)
        matHere = makeMaterialSimple(Farbe(1,1,0));  // Gelb für selektiertes Objekt
    else if (mode == DrawMode::HOVER)
        matHere = makeMaterialSimple(Farbe(0,1,1));  // Cyan für Hover
    else
        matHere = makeMaterialSimple(mat.diffuse);         // Standardfarbe

    for(const auto& tri : tris)
    {
        Vector3D a = tri.a + position;
        Vector3D b = tri.b + position;
        Vector3D c = tri.c + position;

        wf.project(a,x0,y0,z0); wf.project(b,x1,y1,z1); wf.drawLine(x0,y0,z0,x1,y1,z1,matHere);
        wf.project(b,x0,y0,z0); wf.project(c,x1,y1,z1); wf.drawLine(x0,y0,z0,x1,y1,z1,matHere);
        wf.project(c,x0,y0,z0); wf.project(a,x1,y1,z1); wf.drawLine(x0,y0,z0,x1,y1,z1,matHere);
    }
}

void Pfeil::getWorldAABB(Vector3D& minOut, Vector3D& maxOut) const
{
    minOut = tris[0].a;
    maxOut = tris[0].a;

    auto expand = [&](const Vector3D& p)
    {
        minOut.x = std::min(minOut.x, p.x);
        minOut.y = std::min(minOut.y, p.y);
        minOut.z = std::min(minOut.z, p.z);

        maxOut.x = std::max(maxOut.x, p.x);
        maxOut.y = std::max(maxOut.y, p.y);
        maxOut.z = std::max(maxOut.z, p.z);
    };

    for (const auto& t : tris)
    {
        expand(t.a + position);
        expand(t.b + position);
        expand(t.c + position);
    }
}