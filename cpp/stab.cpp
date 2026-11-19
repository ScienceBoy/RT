#include "stab.h"
#include "wireframe.h"
#include "camera.h"
#include <cmath>
#include "rotations.h"

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

    grenzenBerechnen();
}

// Bounding-Sphere und AABB aus p1, p2, radius und position
void Stab::grenzenBerechnen()
{
    // Bounding Sphere (lokal → unabhängig von position)
    double halfLength = (p2 - p1).length() * 0.5;
    boundingRadius = halfLength + radius;

    boundingCenter = position;

    // AABB
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

    // lokal -> Welt (vorher "position - minBound": Box gespiegelt, Stab nicht anklickbar)
    minBound = position + minBound;
    maxBound = position + maxBound;
}

void Stab::rotate(const Vector3D &rotation)
{
    double rx = rotation.x * M_PI / 180.0;
    double ry = rotation.y * M_PI / 180.0;
    double rz = rotation.z * M_PI / 180.0;

    auto rotP = [&](Vector3D &p)
    {
        p = rotateX(p, rx);
        p = rotateY(p, ry);
        p = rotateZ(p, rz);
    };

    // p1/p2 sind relativ zur Mitte 'position'
    rotP(p1);
    rotP(p2);
    grenzenBerechnen();
}

void Stab::scale(const Vector3D &scaleVec)
{
    p1 = Vector3D(p1.x * scaleVec.x, p1.y * scaleVec.y, p1.z * scaleVec.z);
    p2 = Vector3D(p2.x * scaleVec.x, p2.y * scaleVec.y, p2.z * scaleVec.z);
    radius *= (scaleVec.x + scaleVec.y + scaleVec.z) / 3.0;
    grenzenBerechnen();
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
    if (ca_dot_ca < 1e-12)
        return;

    Vector3D x = d * ca_dot_ca - ca * ca_dot_d;
    Vector3D y = oc * ca_dot_ca - ca * ca_dot_oc;

    double a = x * x;
    if (std::abs(a) < 1e-12)
        return;
    double b = 2 * (x * y);
    double c = (y * y) - radius*radius * ca_dot_ca * ca_dot_ca;

    double disc = b*b - 4*a*c;
    if (disc < 0) return;

    double sqrtD = sqrt(disc);
    double t0 = (-b - sqrtD) / (2*a);
    double t1 = (-b + sqrtD) / (2*a);

    // sortieren
    if (t0 > t1) std::swap(t0, t1);

    // gültiges t wählen: der nächste Schnittpunkt VOR dem Strahlanfang, der auf dem Stababschnitt liegt.
    // Vorher: lagen beide Schnittpunkte hinter dem Strahlanfang, wurde t1 trotzdem genommen (negativ) und
    // als naechster Treffer gemeldet. Ein Strahl, dessen Verlaengerung nach hinten durch einen Stab lief
    // (z.B. Streustrahl von der Tischplatte nach oben, Tischbein darunter), "traf" so den Stab:
    // dunkle Flecken in Form des Stabs im indirekten Licht und in Spiegelungen.
    // Ausserdem: lag der vordere Schnittpunkt ausserhalb des Abschnitts, wurde der hintere nicht mehr geprueft.
    double t = -1.0, h = 0.0;
    Vector3D p;
    for (double kandidat : {t0, t1})
    {
        if (kandidat < 1e-4 || kandidat >= hit.t)
            continue;
        Vector3D q = ray.origin + d * kandidat;
        double hq = ((q - localP1) * ca) / ca_dot_ca; // Projektion auf die Achse (0..1 = auf dem Stab)
        if (hq < 0.0 || hq > 1.0)
            continue;
        t = kandidat;
        p = q;
        h = hq;
        break;
    }
    if (t < 0.0)
        return;

    // Treffer setzen
    hit.hit = true;
    hit.t = t;
    hit.obj = this;
    hit.material = &mat;
    hit.hitObject = true;    
    hit.position = p;  

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

    double u = 0.5 + phi / (2 * M_PI);
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
        double a0 = 2 * M_PI * i/steps;
        double a1 = 2 * M_PI * (i+1)/steps;

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
        //Vector3D L = (lights[0].position - M).normalized();
        Vector3D L(0,0,0);

        for (const auto& light : lights)
        {
            L += light->directionFrom(M);
        }

        L = L.normalized();
        //Vector3D L = cam[0].position.normalized();
        double intensity = std::max(0.0, N * L);

        Farbe base = mat.diffuse;
        if (mode == DrawMode::HIGHLIGHT)
            base = invertColorHighlight(mat.diffuse);
        else if (mode == DrawMode::HOVER)
            base = invertColor(mat.diffuse);

        Farbe shaded = Farbe(
            base.r * intensity,
            base.g * intensity,
            base.b * intensity
        );

        Material shadedMat = makeMaterialSimple(shaded);

        int x0,y0,x1,y1,x2,y2,x3,y3;
        double z0,z1,z2,z3;

        wf.project(p0,x0,y0,z0);
        wf.project(p1_,x1,y1,z1);
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
        matHere = makeMaterialSimple(invertColorHighlight(mat.diffuse));
    else if (mode == DrawMode::HOVER)
        matHere = makeMaterialSimple(invertColor(mat.diffuse));
    else
        matHere = makeMaterialSimple(mat.diffuse);

    for(int i=0;i<steps;i++)
    {
        double a0 = 2 * M_PI * i/steps;
        double a1 = 2 * M_PI * (i+1)/steps;

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