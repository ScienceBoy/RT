#include "dreieck.h"
#include "wireframe.h"
#include "camera.h"
#include <algorithm>
#include "stats.h"
#include <cmath>
#include "rotations.h"

// Standardkonstruktor
Dreieck::Dreieck()
    : object(Material()), a(0, 0, 0), b(1, 0, 0), c(0, 1, 0)
{
    position = Vector3D(0, 0, 0);

    edge1 = b - a;
    edge2 = c - a;
    normale = edge1.cross(edge2).normalized();
    geomNormale = normale;

    hasVertexNormals =
        nA.lengthSquared() > 0.0f &&
        nB.lengthSquared() > 0.0f &&
        nC.lengthSquared() > 0.0f;

    // Bounding Box (LOKAL!)
    minBound = Vector3D(
        std::min({a.x, b.x, c.x}),
        std::min({a.y, b.y, c.y}),
        std::min({a.z, b.z, c.z}));

    maxBound = Vector3D(
        std::max({a.x, b.x, c.x}),
        std::max({a.y, b.y, c.y}),
        std::max({a.z, b.z, c.z}));

    // Bounding Sphere
    Vector3D center = 1.0 / 3.0 * (a + b + c);
    boundingCenter = center;
    boundingRadius = std::max({(a - center).length(),
                               (b - center).length(),
                               (c - center).length()});

    uvA = {0.0, 0.0};
    uvB = {1.0, 0.0};
    uvC = {0.0, 1.0};
}

// Konstruktor mit Punkten
Dreieck::Dreieck(const Vector3D &A, const Vector3D &B, const Vector3D &C, const Material &mat)
    : object(mat), a(A), b(B), c(C)
{
    position = Vector3D(0, 0, 0);

    edge1 = b - a;
    edge2 = c - a;
    normale = edge1.cross(edge2).normalized();
    geomNormale = normale;

    hasVertexNormals =
        nA.lengthSquared() > 0.0f &&
        nB.lengthSquared() > 0.0f &&
        nC.lengthSquared() > 0.0f;

    // Bounding Box (LOKAL!)
    minBound = Vector3D(
        std::min({a.x, b.x, c.x}),
        std::min({a.y, b.y, c.y}),
        std::min({a.z, b.z, c.z}));

    maxBound = Vector3D(
        std::max({a.x, b.x, c.x}),
        std::max({a.y, b.y, c.y}),
        std::max({a.z, b.z, c.z}));

    // Bounding Sphere
    Vector3D center = 1.0 / 3.0 * (a + b + c);
    boundingCenter = center;
    boundingRadius = std::max({(a - center).length(),
                               (b - center).length(),
                               (c - center).length()});

    uvA = {0.0, 0.0};
    uvB = {1.0, 0.0};
    uvC = {0.0, 1.0};
}

Dreieck::Dreieck(
    const Vector3D &A,
    const Vector3D &B,
    const Vector3D &C,

    const Vector3D &NA,
    const Vector3D &NB,
    const Vector3D &NC,

    const Material &mat)

    : object(mat),
      a(A), b(B), c(C),
      nA(NA), nB(NB), nC(NC)
{
    position = Vector3D(0, 0, 0);

    edge1 = b - a;
    edge2 = c - a;

    geomNormale =
        edge1.cross(edge2).normalized();

    normale = geomNormale;

    hasVertexNormals = true;

    // Bounding Box
    minBound = Vector3D(
        std::min({a.x, b.x, c.x}),
        std::min({a.y, b.y, c.y}),
        std::min({a.z, b.z, c.z}));

    maxBound = Vector3D(
        std::max({a.x, b.x, c.x}),
        std::max({a.y, b.y, c.y}),
        std::max({a.z, b.z, c.z}));

    // Sphere
    Vector3D center =
        (a + b + c) / 3.0;

    boundingCenter = center;

    boundingRadius = std::max({(a - center).length(),
                               (b - center).length(),
                               (c - center).length()});

    uvA = {0.0, 0.0};
    uvB = {1.0, 0.0};
    uvC = {0.0, 1.0};
}

void Dreieck::rebuild()
{
    edge1 = b - a;
    edge2 = c - a;

    normale = edge1.cross(edge2).normalized();
    geomNormale = normale;

    minBound = Vector3D(
        std::min({a.x, b.x, c.x}),
        std::min({a.y, b.y, c.y}),
        std::min({a.z, b.z, c.z}));

    maxBound = Vector3D(
        std::max({a.x, b.x, c.x}),
        std::max({a.y, b.y, c.y}),
        std::max({a.z, b.z, c.z}));

    Vector3D center = (a + b + c) / 3.0;

    // boundingCenter = center;
    boundingCenter = 0.5 * (minBound + maxBound);

    boundingRadius = std::max({(a - center).length(),
                               (b - center).length(),
                               (c - center).length()});
}

// Dreieck verschieben
void Dreieck::setPosition(const Vector3D &pos)
{
    position = pos;
}

void Dreieck::rotate(const Vector3D &rotation)
{
    Vector3D center = (a + b + c) / 3.0;

    double rx = rotation.x * M_PI / 180.0;
    double ry = rotation.y * M_PI / 180.0;
    double rz = rotation.z * M_PI / 180.0;

    auto rot = [&](Vector3D &p)
    {
        p = p - center;

        p = rotateX(p, rx);
        p = rotateY(p, ry);
        p = rotateZ(p, rz);

        p = p + center;
    };

    rot(a);
    rot(b);
    rot(c);

    rebuild();
}

void Dreieck::scale(const Vector3D &scaleVec)
{
    Vector3D center = (a + b + c) / 3.0;

    auto scl = [&](Vector3D &p)
    {
        p = p - center;

        p.x *= scaleVec.x;
        p.y *= scaleVec.y;
        p.z *= scaleVec.z;

        p = p + center;
    };

    scl(a);
    scl(b);
    scl(c);

    rebuild();
}

void Dreieck::computeNormal()
{
    normale = (b - a).cross(c - a).normalized();

    nA = normale;
    nB = normale;
    nC = normale;
}

void Dreieck::updateBounds()
{
    minBound.x = std::min({a.x, b.x, c.x});
    minBound.y = std::min({a.y, b.y, c.y});
    minBound.z = std::min({a.z, b.z, c.z});

    maxBound.x = std::max({a.x, b.x, c.x});
    maxBound.y = std::max({a.y, b.y, c.y});
    maxBound.z = std::max({a.z, b.z, c.z});

    boundingCenter = (minBound + maxBound) * 0.5;
}

// Möller-Trumbore Schnitt
void Dreieck::intersect(const Ray &ray, Hit &hit) const
{
    // triangleTests++;
    //  BVH / AABB Early-Out
    if (!intersectBounding(ray))
        return;

    const double EPS = 1e-6;

    //  Ray in lokalen Raum transformieren
    Ray localRay;
    localRay.origin = ray.origin - position;
    localRay.direction = ray.direction;

    Vector3D h = localRay.direction.cross(edge2);
    double det = edge1 * h;

    if (det > -EPS && det < EPS)
        return;

    double inv_det = 1.0 / det;

    Vector3D s = localRay.origin - a;
    double u = (s * h) * inv_det;
    if (u < 0.0 || u > 1.0)
        return;

    Vector3D q = s.cross(edge1);
    double v = (localRay.direction * q) * inv_det;
    if (v < 0.0 || u + v > 1.0)
        return;

    double t = (edge2 * q) * inv_det;

    if (t > EPS && t < hit.t)
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
        double beta = u;
        double gamma = v;

        // Vector3D N = normale;
        // if((N * ray.direction) > 0) N = N * -1;
        // hit.normale = N;

        // Interpolierte Shading-Normale
        Vector3D shadingNormal;

        if (hasVertexNormals)
        {
            shadingNormal =
                (alpha * nA +
                 beta * nB +
                 gamma * nC);

            shadingNormal = shadingNormal.normalized();
        }
        else
        {
            shadingNormal = geomNormale;
        }

        // Frontface-Test immer mit geometrischer Normale
        hit.frontFace = (geomNormale * ray.direction < 0);

        // Normale ggf. drehen
        hit.normale = hit.frontFace
                          ? shadingNormal
                          : shadingNormal * -1;

        // hit.u = u;
        // hit.v = v;

        hit.u = alpha * uvA.x + beta * uvB.x + gamma * uvC.x;
        hit.v = alpha * uvA.y + beta * uvB.y + gamma * uvC.y;
    }
}

void Dreieck::getWorldAABB(Vector3D &min, Vector3D &max) const
{
    min = minBound + position;
    max = maxBound + position;
}

void Dreieck::drawFlat(Wireframe &wf, DrawMode mode) const
{
    int x0, y0, x1, y1, x2, y2;
    double z0, z1, z2;

    // Weltkoordinaten
    Vector3D A = a + position;
    Vector3D B = b + position;
    Vector3D C = c + position;
    Vector3D M = (A + B + C) * (1.0 / 3.0);

    Vector3D viewDir = (M - cam[0].position).normalized();
    Vector3D N = (normale * viewDir < 0) ? normale : -normale;

    // Licht (Weltlicht)
    // Vector3D L = (lights[0].position - M).normalized();
    Vector3D L(0, 0, 0);

    for (const auto &light : lights)
    {
        L += light->directionFrom(M);
    }

    L = L.normalized();

    double intensity = std::max(0.0, N * L);

    Farbe base = mat.diffuse;
    if (mode == DrawMode::HIGHLIGHT)
    {
        base = invertColorHighlight(mat.diffuse);
    }
    else if (mode == DrawMode::HOVER)
    {
        base = invertColor(mat.diffuse);
    }

    Farbe shaded(
        base.r * intensity,
        base.g * intensity,
        base.b * intensity);

    if (mode == DrawMode::HIGHLIGHT)
    {
        shaded = invertColorHighlight(mat.diffuse);
    }
    else if (mode == DrawMode::HOVER)
    {
        shaded = invertColor(mat.diffuse);
    }
    else
    {
        shaded = Farbe(
            mat.diffuse.r * intensity,
            mat.diffuse.g * intensity,
            mat.diffuse.b * intensity);
    }

    Material matHere = makeMaterialSimple(shaded);

    wf.project(A, x0, y0, z0);
    wf.project(B, x1, y1, z1);
    wf.project(C, x2, y2, z2);

    wf.fillTriangle(x0, y0, z0, x1, y1, z1, x2, y2, z2, matHere);

    /*wf.drawLine(x0,y0,z0, x1,y1,z1, matHere);
    wf.drawLine(x1,y1,z1, x2,y2,z2, matHere);
    wf.drawLine(x2,y2,z2, x0,y0,z0, matHere);*/
}

// Wireframe
void Dreieck::drawWireframePixels(Wireframe &wf, DrawMode mode) const
{
    int x0, y0, x1, y1;
    double z0, z1;

    Material matHere = mat;

    if (mode == DrawMode::HIGHLIGHT)
        matHere = makeMaterialSimple(invertColorHighlight(mat.diffuse));
    else if (mode == DrawMode::HOVER)
        matHere = makeMaterialSimple(invertColor(mat.diffuse));
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

double Dreieck::area() const
{
    return 0.5 *
           (edge1.cross(edge2)).length();
}