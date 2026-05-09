#include "wand.h"
#include "wireframe.h"
#include <limits>
#include <algorithm>

// ===== Konstruktor =====
Wand::Wand() : object(Material()), t1(), t2()
{
}

Wand::Wand(const Vector3D& a, const Vector3D& b, const Vector3D& c, const Vector3D& d, const Material& mat)
    : object(mat)
{
    t1 = Dreieck(a, b, c, mat);
    t2 = Dreieck(a, c, d, mat);

    position = Vector3D(0,0,0);

    // AABB korrekt berechnen
    minBound.x = std::min({a.x, b.x, c.x, d.x});
    minBound.y = std::min({a.y, b.y, c.y, d.y});
    minBound.z = std::min({a.z, b.z, c.z, d.z});

    maxBound.x = std::max({a.x, b.x, c.x, d.x});
    maxBound.y = std::max({a.y, b.y, c.y, d.y});
    maxBound.z = std::max({a.z, b.z, c.z, d.z});

    // Bounding Sphere
    /*minBound = position - minBound;
    maxBound = position - maxBound;

    Vector3D center = (a + b + c + d) * 0.25 + position;
    boundingCenter = center;

    boundingRadius = std::max({
        (a - center).length(),
        (b - center).length(),
        (c - center).length(),
        (d - center).length()
    }); */
}

// ===== Weltposition setzen =====
void Wand::setPosition(const Vector3D& pos)
{
    position = pos;
}

// ===== Intersection für Picking =====
void Wand::intersect(const Ray& ray, Hit& hit) const
{    
    // BVH / AABB Early-Out
    if (!intersectBounding(ray))
        return;

    Ray localRay;
    localRay.origin = ray.origin - position;
    localRay.direction = ray.direction;

    Hit hit1, hit2;
    hit1.t = hit2.t = std::numeric_limits<double>::infinity();
    hit1.hit = hit2.hit = false;

    t1.intersect(localRay, hit1);
    t2.intersect(localRay, hit2);

    Hit bestHit;
    bestHit.hit = false;
    bestHit.t = std::numeric_limits<double>::infinity();

    if(hit1.hit && hit1.t < bestHit.t)
        bestHit = hit1;

    if(hit2.hit && hit2.t < bestHit.t)
        bestHit = hit2;

    if(bestHit.hit)
    {
        hit = bestHit;

        // zurück in Weltkoordinaten
        hit.position += position;
    }
}

// ===== Wireframe =====
void Wand::drawWireframePixels(Wireframe& wf, DrawMode mode) const
{
    int x0, y0, x1, y1;
    double z0, z1;
    Material matHere = mat;

    if (mode == DrawMode::HIGHLIGHT)
        matHere = makeMaterialSimple(Farbe(1,1,0));  // Gelb für selektiertes Objekt
    else if (mode == DrawMode::HOVER)
        matHere = makeMaterialSimple(Farbe(0,1,1));  // Cyan für Hover
    else
        matHere = makeMaterialSimple(mat.diffuse);   // Standardfarbe

    Vector3D a = t1.a + position;
    Vector3D b = t1.b + position;
    Vector3D c = t1.c + position;
    Vector3D d = t2.c + position; 

    wf.project(a, x0, y0, z0); wf.project(b, x1, y1, z1); wf.drawLine(x0,y0,z0,x1,y1,z1,matHere);
    wf.project(b, x0, y0, z0); wf.project(c, x1, y1, z1); wf.drawLine(x0,y0,z0,x1,y1,z1,matHere);
    wf.project(c, x0, y0, z0); wf.project(d, x1, y1, z1); wf.drawLine(x0,y0,z0,x1,y1,z1,matHere);
    wf.project(d, x0, y0, z0); wf.project(a, x1, y1, z1); wf.drawLine(x0,y0,z0,x1,y1,z1,matHere);
}

void Wand::drawFlat(Wireframe& wf, DrawMode mode) const
{
    Material matHere;

    if (mode == DrawMode::HIGHLIGHT)
        matHere = makeMaterialSimple(Farbe(1,1,0));
    else if (mode == DrawMode::HOVER)
        matHere = makeMaterialSimple(Farbe(0,1,1));
    else
        matHere = makeMaterialSimple(mat.diffuse);

    // Weltpunkte der Wand
    Vector3D A = t1.a + position;
    Vector3D B = t1.b + position;
    Vector3D C = t1.c + position;
    Vector3D D = t2.c + position;

    // Projektion
    int ax, ay, bx, by, cx, cy, dx, dy;
    double az, bz, cz, dz;

    wf.project(A, ax, ay, az);
    wf.project(B, bx, by, bz);
    wf.project(C, cx, cy, cz);
    wf.project(D, dx, dy, dz);

    // Wand zeichnen (2 Dreiecke)
    wf.fillTriangle(ax, ay, az, bx, by, bz, cx, cy, cz, matHere);
    wf.fillTriangle(ax, ay, az, cx, cy, cz, dx, dy, dz, matHere);

    /*
    // ============================================
    // DEBUG: Zentrum als kleines Dreieck
    // ============================================

    // Zentrum in Weltkoordinaten
    Vector3D center = boundingCenter + position;

    // lokale Kanten der Wand (für Ebene)
    Vector3D edge1 = (t1.b - t1.a).normalized();
    Vector3D edge2 = (t1.c - t1.a).normalized();

    Vector3D normal = edge1.cross(edge2).normalized();
    Vector3D centerOffset = center + normal * 0.1;

    double s = 10; // Größe

    // kleines Dreieck in Wandebene
    Vector3D p1 = centerOffset;
    Vector3D p2 = centerOffset + edge1 * s;
    Vector3D p3 = centerOffset + edge2 * s;

    // Projektion
    int x0,y0,x1,y1,x2,y2;
    wf.project(p1, x0, y0);
    wf.project(p2, x1, y1);
    wf.project(p3, x2, y2);

    // schwarz zeichnen
    Material black = makeMaterialSimple(Farbe(0,0,0));
    wf.fillTriangle(x0, y0, x1, y1, x2, y2, black);*/
}

void Wand::getWorldAABB(Vector3D& minOut, Vector3D& maxOut) const
{
    const Vector3D pts[4] = {
        t1.a + position,
        t1.b + position,
        t1.c + position,
        t2.c + position
    };

    minOut = maxOut = pts[0];

    for (int i = 1; i < 4; i++)
    {
        minOut.x = std::min(minOut.x, pts[i].x);
        minOut.y = std::min(minOut.y, pts[i].y);
        minOut.z = std::min(minOut.z, pts[i].z);

        maxOut.x = std::max(maxOut.x, pts[i].x);
        maxOut.y = std::max(maxOut.y, pts[i].y);
        maxOut.z = std::max(maxOut.z, pts[i].z);
    }
}