#include "dreieck.h"
#include "wireframe.h"
#include "camera.h"
#include <algorithm>
#include <cmath>

// Standardkonstruktor
Dreieck::Dreieck() 
    : object(Material()), a(0,0,0), b(1,0,0), c(0,1,0)
    {
        position = Vector3D(0,0,0);

        edge1 = b - a;
        edge2 = c - a;
        normale = edge1.cross(edge2).normalized();  
        geomNormale = normale;

        // Bounding Box (LOKAL!)
        minBound = Vector3D(
            std::min({a.x, b.x, c.x}),
            std::min({a.y, b.y, c.y}),
            std::min({a.z, b.z, c.z})
        );

        maxBound = Vector3D(
            std::max({a.x, b.x, c.x}),
            std::max({a.y, b.y, c.y}),
            std::max({a.z, b.z, c.z})
        );

        // Bounding Sphere
        Vector3D center = 1.0 / 3.0 * (a + b + c);
        boundingCenter = center;
        boundingRadius = std::max({
            (a - center).length(),
            (b - center).length(),
            (c - center).length()
        });

        uvA = {0.0, 0.0};
        uvB = {1.0, 0.0};
        uvC = {0.0, 1.0};
    }

// Konstruktor mit Punkten
Dreieck::Dreieck(const Vector3D& A, const Vector3D& B, const Vector3D& C, const Material& mat)
    : object(mat), a(A), b(B), c(C)
    {
        position = Vector3D(0,0,0);

        edge1 = b - a;
        edge2 = c - a;
        normale = edge1.cross(edge2).normalized();
        geomNormale = normale;

        // Bounding Box (LOKAL!)
        minBound = Vector3D(
            std::min({a.x, b.x, c.x}),
            std::min({a.y, b.y, c.y}),
            std::min({a.z, b.z, c.z})
        );

        maxBound = Vector3D(
            std::max({a.x, b.x, c.x}),
            std::max({a.y, b.y, c.y}),
            std::max({a.z, b.z, c.z})
        );

        // Bounding Sphere
        Vector3D center = 1.0 / 3.0 * (a + b + c);
        boundingCenter = center;
        boundingRadius = std::max({
            (a - center).length(),
            (b - center).length(),
            (c - center).length()
        });

        uvA = {0.0, 0.0};
        uvB = {1.0, 0.0};
        uvC = {0.0, 1.0};

    }

// Optional: Dreieck verschieben
void Dreieck::setPosition(const Vector3D& pos)
{
    position = pos;
}

// Möller-Trumbore Schnitt
void Dreieck::intersect(const Ray& ray, Hit& hit) const
{    
    const double EPS = 1e-6;

    //  Ray in lokalen Raum transformieren
    Ray localRay;
    localRay.origin = ray.origin - position;
    localRay.direction = ray.direction;

    Vector3D h = localRay.direction.cross(edge2);
    double det = edge1 * h;

    if(det > -EPS && det < EPS) return;

    double inv_det = 1.0 / det;

    Vector3D s = localRay.origin - a;
    double u = (s * h) * inv_det;
    if(u < 0.0 || u > 1.0) return;

    Vector3D q = s.cross(edge1);
    double v = (localRay.direction * q) * inv_det;
    if(v < 0.0 || u + v > 1.0) return;

    double t = (edge2 * q) * inv_det;

    if(t > EPS && t < hit.t)
    {
        hit.hit = true;
        hit.t = t;
        hit.obj = this;
        hit.material = &mat;
        hit.geomNormale = geomNormale;
        hit.hitObject = true;

        //  zurück in Welt
        hit.position = localRay.origin + localRay.direction * t;
        hit.position += position;

        // Baryzentrische Gewichte
        double alpha = 1.0 - u - v;
        double beta  = u;
        double gamma = v;
        
        //Vector3D N = normale;
        //if((N * ray.direction) > 0) N = N * -1;
        //hit.normale = N;

        // Interpolierte Shading-Normale
        Vector3D smoothN =
            (alpha * nA +
            beta  * nB +
            gamma * nC).normalized();

        // Frontface-Test mit GEOMETRISCHER Normale
        hit.frontFace = (geomNormale * ray.direction < 0);

        // Normale ggf. umdrehen 
        hit.normale = hit.frontFace ? smoothN : smoothN * -1;

        //hit.u = u;
        //hit.v = v;

        hit.u = alpha * uvA.x + beta * uvB.x + gamma * uvC.x;
        hit.v = alpha * uvA.y + beta * uvB.y + gamma * uvC.y;
    }
}

void Dreieck::getWorldAABB(Vector3D& min, Vector3D& max) const
{
    min = minBound + position;
    max = maxBound + position;
}

void Dreieck::drawFlat(Wireframe& wf, DrawMode mode) const
{
    int x0,y0,x1,y1,x2,y2;
    double z0,z1,z2;

    // Weltkoordinaten
    Vector3D A = a + position;
    Vector3D B = b + position;
    Vector3D C = c + position;
    Vector3D M = (A+B+C) * (1.0 / 3.0);

    Vector3D viewDir = (M - cam[0].position).normalized();
    Vector3D N = (normale * viewDir < 0) ? normale : -normale;

    // Licht (Weltlicht)
    //Vector3D L = (lights[0].position - M).normalized();
    Vector3D L(0,0,0);

    for (const auto& light : lights)
    {
        L += light->directionFrom(M);
    }

    L = L.normalized();

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

    wf.project(A,x0,y0,z0);
    wf.project(B,x1,y1,z1);
    wf.project(C,x2,y2,z2);

    wf.fillTriangle(x0,y0,z0, x1,y1,z1, x2,y2,z2, matHere);

    wf.drawLine(x0,y0,z0, x1,y1,z1, matHere);
    wf.drawLine(x1,y1,z1, x2,y2,z2, matHere);
    wf.drawLine(x2,y2,z2, x0,y0,z0, matHere);
}

// Wireframe
void Dreieck::drawWireframePixels(Wireframe& wf, DrawMode mode) const
{
    int x0, y0, x1, y1;
    double z0, z1;

    Material matHere = mat;

    if (mode == DrawMode::HIGHLIGHT)
        matHere = makeMaterialSimple(Farbe(1,1,0));
    else if (mode == DrawMode::HOVER)
        matHere = makeMaterialSimple(Farbe(0,1,1));
    else
        matHere = makeMaterialSimple(mat.diffuse);

    //  Weltkoordinaten
    Vector3D A = a + position;
    Vector3D B = b + position;
    Vector3D C = c + position;

    //  Linien zeichnen
    wf.project(A, x0, y0, z0);
    wf.project(B, x1, y1, z1);
    wf.drawLine(x0, y0, z0, x1, y1, z1, matHere);

    wf.project(B, x0, y0, z0);
    wf.project(C, x1, y1, z1);
    wf.drawLine(x0, y0, z0, x1, y1, z1, matHere);

    wf.project(C, x0, y0, z0);
    wf.project(A, x1, y1, z1);
    wf.drawLine(x0, y0, z0, x1, y1, z1, matHere);
}