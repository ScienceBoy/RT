#include "text3d.h"
#include "wireframe.h"
#include <cmath>
#include <algorithm>

// ============================================
// Konstruktor
// ============================================

Text3D::Text3D(const std::string& t,
               const Vector3D& pos,
               double s,
               const Material& mat)
    : object(mat), text(t), size(s)
{
    position = pos;   // Weltposition
    buildText();      // Triangles lokal erstellen
    // Bounding Sphere berechnen
    boundingCenter = Vector3D(0,0,0);

    // 1. alle Punkte aufsummieren für den Mittelpunkt
    int count = 0;
    for(const auto& t : tris)
    {
        boundingCenter += t.a + t.b + t.c;
        count += 3;
    }
    boundingCenter = 1/count * boundingCenter;

    // 2. Radius als maximaler Abstand vom Zentrum
    boundingRadius = 0.0;
    for(const auto& t : tris)
    {
        boundingRadius = std::max(boundingRadius, (t.a - boundingCenter).length());
        boundingRadius = std::max(boundingRadius, (t.b - boundingCenter).length());
        boundingRadius = std::max(boundingRadius, (t.c - boundingCenter).length());
    }
}

// ============================================
// Weltposition setzen (verschiebbar)
// ============================================

void Text3D::setPosition(const Vector3D& pos)
{
    Vector3D delta = pos - position;
    position = pos;

    for(auto& t : tris)
    {
        t.a += delta;
        t.b += delta;
        t.c += delta;
    }

    minBound += delta;
    maxBound += delta;
}

// ============================================
// Linie als dünner Quader
// ============================================

void Text3D::addLine(const Vector3D& a, const Vector3D& b)
{
    double thickness = size * 0.1;

    Vector3D dir = (b - a).normalized();
    Vector3D up(0,1,0);

    if (std::abs(dir * up) > 0.99)
        up = Vector3D(1,0,0);

    Vector3D side = dir.cross(up).normalized() * thickness;
    Vector3D up2  = dir.cross(side).normalized() * thickness;

    Vector3D p1 = a + side;
    Vector3D p2 = a - side;
    Vector3D p3 = b - side;
    Vector3D p4 = b + side;

    tris.emplace_back(p1,p2,p3,mat);
    tris.emplace_back(p1,p3,p4,mat);
}

// ============================================
// Ziffer bauen (lokal!)
// ============================================

void Text3D::addDigit(char d, const Vector3D& offset)
{
    Vector3D p = offset;

    Vector3D A = p + Vector3D(0, size, 0);
    Vector3D B = p + Vector3D(size, size, 0);
    Vector3D C = p + Vector3D(0, 0, 0);
    Vector3D D = p + Vector3D(size, 0, 0);

    Vector3D midL = p + Vector3D(0, size/2, 0);
    Vector3D midR = p + Vector3D(size, size/2, 0);

    auto hTop    = [&](){ addLine(A, B); };
    auto hMid    = [&](){ addLine(midL, midR); };
    auto hBottom = [&](){ addLine(C, D); };
    auto vLeftTop    = [&](){ addLine(A, midL); };
    auto vLeftBottom = [&](){ addLine(midL, C); };
    auto vRightTop   = [&](){ addLine(B, midR); };
    auto vRightBottom= [&](){ addLine(midR, D); };

    switch(d)
    {
        case '0': hTop(); hBottom(); vLeftTop(); vLeftBottom(); vRightTop(); vRightBottom(); break;
        case '1': vRightTop(); vRightBottom(); break;
        case '2': hTop(); hMid(); hBottom(); vRightTop(); vLeftBottom(); break;
        case '3': hTop(); hMid(); hBottom(); vRightTop(); vRightBottom(); break;
        case '4': hMid(); vLeftTop(); vRightTop(); vRightBottom(); break;
        case '5': hTop(); hMid(); hBottom(); vLeftTop(); vRightBottom(); break;
        case '6': hTop(); hMid(); hBottom(); vLeftTop(); vLeftBottom(); vRightBottom(); break;
        case '7': hTop(); vRightTop(); vRightBottom(); break;
        case '8': hTop(); hMid(); hBottom(); vLeftTop(); vLeftBottom(); vRightTop(); vRightBottom(); break;
        case '9': hTop(); hMid(); hBottom(); vLeftTop(); vRightTop(); vRightBottom(); break;
        case '-': hMid(); break;
    }
}

// ============================================
// Text zusammensetzen
// ============================================

void Text3D::buildText()
{
    double spacing = size * 1.3;

    for(size_t i = 0; i < text.size(); i++)
    {
        Vector3D offset(i * spacing, 0, 0);
        addDigit(text[i], offset);
    }
}

// ============================================
// Intersection (Picking)
// ============================================

void Text3D::intersect(const Ray& ray, Hit& hit) const
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

void Text3D::drawFlat(Wireframe& wf, DrawMode mode) const
{
    int x0,y0,x1,y1,x2,y2;
    double z0, z1, z2;
    Material matHere = mat;

    // Material je nach DrawMode
    if (mode == DrawMode::HIGHLIGHT)
        matHere = makeMaterialSimple(Farbe(1,1,0));  // Gelb
    else if (mode == DrawMode::HOVER)
        matHere = makeMaterialSimple(Farbe(0,1,1));  // Cyan
    else
        matHere = makeMaterialSimple(mat.diffuse);   // Standardfarbe

    // Alle Dreiecke füllen
    for(const auto& tri : tris)
    {
        Vector3D a = tri.a + position;
        Vector3D b = tri.b + position;
        Vector3D c = tri.c + position;

        // 3D → 2D Projektion
        wf.project(a, x0, y0, z0);
        wf.project(b, x1, y1, z1);
        wf.project(c, x2, y2, z2);

        // Fläche füllen
        wf.fillTriangle(x0, y0, z0, x1, y1, z1, x2, y2, z2, matHere);

        wf.drawLine(x0,y0,z0,x1,y1,z1,matHere);
        wf.drawLine(x1,y1,z1,x2,y2,z2,matHere);
        wf.drawLine(x2,y2,z2,x0,y0,z0, matHere);
    }
}


// ============================================
// Wireframe zeichnen
// ============================================

void Text3D::drawWireframePixels(Wireframe& wf, DrawMode mode) const
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

void Text3D::getWorldAABB(Vector3D& minOut, Vector3D& maxOut) const
{
    if (tris.empty()) return;

    minOut = tris[0].a + position;
    maxOut = tris[0].a + position;

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