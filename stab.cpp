#include "stab.h"
#include "wireframe.h"
#include "camera.h"
#include <cmath>

Stab::Stab()
    : object(Material()), p1(0,0,0), p2(0,1,0), radius(0.1)
{}

Stab::Stab(const Vector3D& a, const Vector3D& b, double r, const Material& mat)
    : object(mat), radius(r)
{
    // Mittelpunkt berechnen
    position = (a + b) * 0.5;

    // WICHTIG: lokale Koordinaten!
    p1 = a - position;
    p2 = b - position;

    // Bounding Sphere (lokal → unabhängig von position)
    double halfLength = (p2 - p1).length() * 0.5;
    boundingRadius = halfLength + radius;

    boundingCenter = position;

    // AABB (auch lokal!)
    minBound = Vector3D(
        std::min(p1.x, p2.x) - radius,
        std::min(p1.y, p2.y) - radius,
        std::min(p1.z, p2.z) - radius
    );

    maxBound = Vector3D(
        std::max(p1.x, p2.x) + radius,
        std::max(p1.y, p2.y) + radius,
        std::max(p1.z, p2.z) + radius
    );

    minBound = position - minBound;
    maxBound = position - maxBound;
}

void Stab::intersect(const Ray& ray, Hit& hit) const
{    
    // BVH / AABB Early-Out
    if (!intersectBounding(ray))
        return;

    // Verschobene Endpunkte (Position = Mittelpunkt)
    Vector3D localP1 = p1 + position;
    Vector3D localP2 = p2 + position;

    Vector3D ca = localP2 - localP1;       // Achse
    Vector3D oc = ray.origin - localP1;    // Ray relativ zu p1
    Vector3D d  = ray.direction;

    double ca_dot_d  = ca * d;
    double ca_dot_oc = ca * oc;
    double ca_dot_ca = ca * ca;

    Vector3D x = d * ca_dot_ca - ca * ca_dot_d;
    Vector3D y = oc * ca_dot_ca - ca * ca_dot_oc;

    double a = x * x;
    double b = 2 * (x * y);
    double c = (y * y) - radius*radius * ca_dot_ca * ca_dot_ca;

    double disc = b*b - 4*a*c;
    if (disc < 0) return;

    double sqrtD = sqrt(disc);
    double t = (-b - sqrtD) / (2*a);
    if (t < 1e-6)
        t = (-b + sqrtD) / (2*a);

    if (t < 1e-6 || t >= hit.t) return;

    // Schnittpunkt
    Vector3D p = ray.origin + d * t;

    // Projektion auf Achse prüfen (ob innerhalb des Stabs)
    double h = ((p - localP1) * ca) / ca_dot_ca;
    if (h < 0.0 || h > 1.0) return;

    // Treffer setzen
    hit.hit = true;
    hit.t = t;
    hit.obj = this;
    hit.material = &mat;
    hit.position = p;  // jetzt korrekt in Weltkoordinaten

    // Normale berechnen
    Vector3D axisPoint = localP1 + ca * h;
    Vector3D N = (p - axisPoint).normalized();
    if (N * ray.direction > 0) N = N * -1;
    hit.normale = N;
    hit.geomNormale = N;

    Vector3D axisDir = ca.normalized();
    Vector3D radial = (p - axisPoint).normalized();

    // stabile Basis erzeugen
    Vector3D ref = Vector3D(0,1,0);
    if (std::abs(axisDir * ref) > 0.99)
        ref = Vector3D(1,0,0);

    Vector3D tangent = (ref.cross(axisDir)).normalized();
    Vector3D bitangent = axisDir.cross(tangent);

    double x_uv = radial * tangent;
    double y_uv = radial * bitangent;

    double phi = atan2(y_uv, x_uv);

    double u = 0.5 + phi / (2 * 3.14159265);
    double v = h;

    hit.u = u;
    hit.v = v;
}

void Stab::drawFlat(Wireframe& wf, DrawMode mode) const
{
    int steps = 24; // mehr Segmente = runder

    /*Material matHere = mat;

    if (mode == DrawMode::HIGHLIGHT)
        matHere = makeMaterialSimple(Farbe(1,1,0));
    else if (mode == DrawMode::HOVER)
        matHere = makeMaterialSimple(Farbe(0,1,1));*/

    Vector3D axis = (p2 - p1).normalized();

    // stabiler Basisvektor
    Vector3D tmp = fabs(axis.y) < 0.99 ? Vector3D(0,1,0) : Vector3D(1,0,0);
    Vector3D right = axis.cross(tmp).normalized();
    Vector3D forward = axis.cross(right).normalized();

    for(int i = 0; i < steps; i++)
    {
        double a0 = 2 * 3.14159265 * i/steps;
        double a1 = 2 * 3.14159265 * (i+1)/steps;

        Vector3D r0 = right*cos(a0) + forward*sin(a0);
        Vector3D r1 = right*cos(a1) + forward*sin(a1);

        // 4 Punkte des Quads
        Vector3D P1 = p1 + position;
        Vector3D P2 = p2 + position;

        Vector3D p0 = P1 + r0*radius;
        Vector3D p1_ = P2 + r0*radius;
        Vector3D p2_ = P2 + r1*radius;
        Vector3D p3 = P1 + r1*radius;

        // Normale des Quads (Flat Shading)
        Vector3D edge1 = p1_ - p0;
        Vector3D edge2 = p3 - p0;
        Vector3D N = edge1.cross(edge2).normalized();

        // einfache Lichtquelle
        Vector3D M = (p0+p1_+p2_+p3) * (1.0 / 4.0);
        Vector3D L = (lights[0].position - M).normalized();
        //Vector3D L = cam[0].position.normalized();
        double intensity = std::max(0.0, N * L);

        Farbe base;
        if (mode == DrawMode::HIGHLIGHT)
            base = Farbe(1,1,0);
        else if (mode == DrawMode::HOVER)
            base = Farbe(0,1,1);
        else
            base = mat.diffuse;

        Farbe shaded = Farbe(
            base.r * intensity,
            base.g * intensity,
            base.b * intensity
        );

        Material shadedMat = makeMaterialSimple(shaded);

        int x0,y0,x1,y1,x2,y2,x3,y3;
        double z0,z1,z2,z3;

        wf.project(p0,x0,y0,z0);
        wf.project(p1_,x1,y1,z0);
        wf.project(p2_,x2,y2,z2);
        wf.project(p3,x3,y3,z3);

        // zwei Dreiecke
        wf.fillTriangle(x0,y0,z0,x1,y1,z1,x2,y2,z2, shadedMat);
        wf.fillTriangle(x0,y0,z0,x2,y2,z2,x3,y3,z3, shadedMat);
    }
}

void Stab::drawWireframePixels(Wireframe& wf, DrawMode mode) const
{
    int steps = 16;
    int x0,y0,x1,y1;
    double z0,z1;

    Material matHere = mat;

    if (mode == DrawMode::HIGHLIGHT)
        matHere = makeMaterialSimple(Farbe(1,1,0));

    for(int i=0;i<steps;i++)
    {
        double a0 = 2 * 3.14159265 * i/steps;
        double a1 = 2 * 3.14159265 * (i+1)/steps;

        Vector3D dir0(cos(a0),0,sin(a0));
        Vector3D dir1(cos(a1),0,sin(a1));

        Vector3D P1 = p1 + position;

        Vector3D pa = P1 + dir0*radius;
        Vector3D pb = P1 + dir1*radius;

        wf.project(pa,x0,y0,z0);
        wf.project(pb,x1,y1,z1);
        wf.drawLine(x0,y0,z0,x1,y1,z1,matHere);
    }
}

void Stab::getWorldAABB(Vector3D& minOut, Vector3D& maxOut) const
{
    Vector3D a = p1 + position;
    Vector3D b = p2 + position;

    minOut = Vector3D(
        std::min(a.x, b.x) - radius,
        std::min(a.y, b.y) - radius,
        std::min(a.z, b.z) - radius
    );

    maxOut = Vector3D(
        std::max(a.x, b.x) + radius,
        std::max(a.y, b.y) + radius,
        std::max(a.z, b.z) + radius
    );
}