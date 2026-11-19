#include "areaLight.h"
#include "material.h"
#include <algorithm>
#include <cmath>
#include <random>
#include <cstdlib>
#include "random.h"
#include "lightSample.h"
#include <iostream>

extern double random01();

AreaLight::AreaLight(
    const Vector3D& pos,
    const Vector3D& u,
    const Vector3D& v,
    const Farbe& col,
    double power)
{
    position = pos;
    edgeU = u;
    edgeV = v;
    farbe = col;
    this->power = power; // vorher immer 50 (Parameter ueberdeckt)
}

LightSample AreaLight::samplePoint() const
{
    double su = random01();
    double sv = random01();

    Vector3D p =
        position + edgeU * su + edgeV * sv;

    double area = flaeche();
    double pdf = area > 1e-300 ? 1.0 / area : 1.0;

    return {
        p,
        normale(),
        Vector3D(),   // directionToLight (wird später im Shading gesetzt)
        0.0,          // distance (ebenfalls später)
        farbe,        // radiance
        pdf
    };
}

Vector3D AreaLight::directionFrom(const Vector3D& point) const
{
    return (mitte() - point).normalized();
}

double AreaLight::distanceFrom(
    const Vector3D& point) const
{
    return (mitte() - point).length();
}

// Beleuchtung durch einen Punkt der Flaeche: Lambert-Strahler mit Gesamtleistung 'power'
// (Strahldichte L = power / (pi * A)); je Abtastpunkt (pdf = 1/A):
//   E = L * cos(Licht) * A / d^2 = power * cos(Licht) / (pi * d^2)
// Der Winkel am beleuchteten Punkt (N * L) kommt im Shading dazu.
// Vorher: power * cos * A / (4 pi d^2) -> eine doppelt so grosse Lampe leuchtete doppelt so hell
// bei gleicher Leistung.
static double flaechenBeitrag(double power, double cosLicht, double d)
{
    if (cosLicht <= 0.0 || d <= 1e-12)
        return 0.0;
    return power * cosLicht / (M_PI * d * d);
}

Farbe AreaLight::intensityAt(const Vector3D& point) const
{
    LightSample s = samplePoint();
    Vector3D dir = s.position - point;
    double d = dir.length();
    if (d <= 1e-12)
        return Farbe(0, 0, 0);
    dir = dir * (1.0 / d);
    return farbe * flaechenBeitrag(power, -(dir * s.normal), d);
}

void AreaLight::drawFlat(
    Wireframe& wf,
    DrawMode mode) const
{
    drawWireframePixels(wf, mode);
}

void AreaLight::drawWireframePixels(
    Wireframe& wf,
    DrawMode mode) const
{
    Farbe color;

    if(mode == DrawMode::HIGHLIGHT)
        color = Farbe(1,1,0);
    else if(mode == DrawMode::HOVER)
        color = Farbe(0,1,1);
    else
        color = farbe;

    Vector3D p0 = position;
    Vector3D p1 = position + edgeU;
    Vector3D p2 = position + edgeU + edgeV;
    Vector3D p3 = position + edgeV;

    int x0,y0,x1,y1;
    double z0,z1;

    auto drawEdge =
    [&](const Vector3D& a, const Vector3D& b)
    {
        wf.project(a,x0,y0,z0);
        wf.project(b,x1,y1,z1);

        wf.drawLine(
            x0,y0,z0,
            x1,y1,z1,
            makeMaterialSimple(color));
    };

    drawEdge(p0,p1);
    drawEdge(p1,p2);
    drawEdge(p2,p3);
    drawEdge(p3,p0);
    drawEdge(p0,p2); // Diagonalen: als Flaeche erkennbar
    drawEdge(p1,p3);

    // Leuchtrichtung: kurzer Strich von der Mitte aus
    double laenge = 0.5 * std::sqrt(std::max(flaeche(), 1e-12));
    drawEdge(mitte(), mitte() + normale() * laenge);
}

void AreaLight::intersect(const Ray& ray, Hit& hit) const
{
    Vector3D normal = normale();

    double denom =
        normal * ray.direction;

    if(std::abs(denom) < 1e-12)
        return;

    double t = ((position - ray.origin) * normal) / denom;

    if(t < 1e-6)
        return;

    Vector3D p =
        ray.origin + ray.direction * t;

    Vector3D rel = p - position;

    // Anteile entlang der Kanten (auch fuer nicht rechtwinklige Kanten richtig)
    double uu2 = edgeU * edgeU, vv2 = edgeV * edgeV, uv = edgeU * edgeV;
    double det = uu2 * vv2 - uv * uv;
    if (det <= 1e-300)
        return;
    double ru = rel * edgeU, rv = rel * edgeV;
    double a = (ru * vv2 - rv * uv) / det;
    double b = (rv * uu2 - ru * uv) / det;

    if(a < 0 || a > 1 || b < 0 || b > 1)
        return;

    if(t < hit.t)
    {
        hit.t = t;
        hit.hit = true;
        hit.position = p;
        hit.normale = normal;
        hit.hitLight = true;
        hit.light = this;
    }
}

void AreaLight::save(
    std::ostream& out) const
{
    out << "        <light type=\"area\" power=\"" << power << "\">\n";

    out << "            <position x=\""
        << position.x
        << "\" y=\""
        << position.y
        << "\" z=\""
        << position.z
        << "\"/>\n";

    out << "            <edgeU x=\""
        << edgeU.x
        << "\" y=\""
        << edgeU.y
        << "\" z=\""
        << edgeU.z
        << "\"/>\n";

    out << "            <edgeV x=\""
        << edgeV.x
        << "\" y=\""
        << edgeV.y
        << "\" z=\""
        << edgeV.z
        << "\"/>\n";

    out << "            <color r=\""
        << farbe.r
        << "\" g=\""
        << farbe.g
        << "\" b=\""
        << farbe.b
        << "\"/>\n";

    out << "        </light>\n\n";
}


Vector3D AreaLight::editorPosition() const
{
    return mitte();
}

// wie bei den anderen Lichtern: verschiebt um -delta
void AreaLight::translate(const Vector3D& delta)
{
    position -= delta;
}

LightSample AreaLight::sample(const Vector3D& p) const
{
    LightSample s;

    // 1. Punkt auf Fläche sampeln
    double su = random01();
    double sv = random01();

    Vector3D x = position + edgeU * su + edgeV * sv;

    // 2. Richtung + Distanz
    Vector3D dir = x - p;
    double d = dir.length();
    dir = d > 1e-300 ? dir * (1.0 / d) : Vector3D(0, 1, 0);

    s.position = x;
    s.normal = normale();
    s.directionToLight = dir;
    s.distance = d;
    s.pdf = flaeche() > 1e-300 ? 1.0 / flaeche() : 1.0;

    // 3. nur die Vorderseite leuchtet
    s.radiance = farbe * flaechenBeitrag(power, -(dir * s.normal), d);

    return s;
}

bool AreaLight::isArea() const
{
    return true;
}

// mehr Abtastpunkte, je groesser die Lampe von hier aus erscheint
int AreaLight::numberOfSamples(const Vector3D& hitPos) const
{
    double dist2 = std::max((mitte() - hitPos).lengthSquared(), 1e-9);
    return std::clamp(int(flaeche() * 300.0 / dist2), 8, 128);
}

// sichtbare Helligkeit der Leuchtflaeche: Strahldichte L = power / (pi * A)
Farbe AreaLight::getEmission() const
{
    double area = std::max(flaeche(), 1e-12);
    return farbe * (power / (M_PI * area));
}
