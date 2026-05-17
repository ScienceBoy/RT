#include "areaLight.h"
#include "material.h"
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
    power = 50.0;
}

Vector3D AreaLight::samplePoint() const
{
    double su = random01();
    double sv = random01();

    return position
        + edgeU * su
        + edgeV * sv;
}

Vector3D AreaLight::directionFrom(
    const Vector3D& point) const
{
    Vector3D p = samplePoint();

    return (p - point).normalized();
}

double AreaLight::distanceFrom(
    const Vector3D& point) const
{
    Vector3D p = samplePoint();
    return (p - point).length();
}

Farbe AreaLight::intensityAt(const Vector3D& point) const
{
    // Sample auf Fläche
    Vector3D p = samplePoint();

    Vector3D dir = p - point;
    double d = dir.length();
    dir = dir.normalized();

    // Flächennormale
    Vector3D Nl = (edgeU.cross(edgeV)).normalized();

    // Winkelterm (Lambert)
    double cosTheta = std::max(0.0, -(dir * Nl));

    if (cosTheta <= 0.0)
        return Farbe(0,0,0);

    double area = edgeU.cross(edgeV).length();

    // gleichmäßige Verteilung über Fläche
    double pdf = 1.0 / area;

    // physikalisch sauberer Term
    double attenuation =
        (power * cosTheta * area) / (4.0 * 3.14159265 * d * d);

    return farbe * attenuation;
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
}

void AreaLight::intersect(
    const Ray& ray,
    Hit& hit) const
{
    Vector3D normal =
        (edgeU.cross(edgeV)).normalized();

    double denom =
        normal * ray.direction;

    if(std::abs(denom) < 1e-6)
        return;

    double t =
        ((position - ray.origin) * normal)
        / denom;

    if(t < 1e-6)
        return;

    Vector3D p =
        ray.origin + ray.direction * t;

    Vector3D rel = p - position;

    double uu = rel * edgeU.normalized();
    double vv = rel * edgeV.normalized();

    if(uu < 0 || uu > edgeU.length())
        return;

    if(vv < 0 || vv > edgeV.length())
        return;

    if(t < hit.t)
    {
        //if (hit.hitObject)
        //    return;

        hit.t = t;
        hit.hit = true;
        hit.position = p;
        hit.hitLight = true;
        hit.light = this;

    }
}

void AreaLight::save(
    std::ostream& out) const
{
    out << "        <light type=\"area\">\n";

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
    return position;
}

void AreaLight::translate(const Vector3D& delta)
{
    position -= delta;
    //edgeU += delta;
    //edgeV += delta;
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
    dir = dir.normalized();

    s.directionToLight = dir;
    s.distance = d;

    // 3. Flächennormale
    Vector3D Nl = edgeU.cross(edgeV).normalized();

    // 4. Winkelterm (Lambert)
    double cosTheta = std::max(0.0, -(dir * Nl));

    if (cosTheta <= 0.0)
    {
        s.radiance = Farbe(0,0,0);
        return s;
    }

    // 5. Fläche
    double area = edgeU.cross(edgeV).length();

    // 6. PDF (Uniform Sampling)
    double pdf = 1.0 / area;

    // 7. Physikalischer Beitrag
    // power = Gesamtleistung des Lichts
    double factor =
        (power * cosTheta * area) /
        (4.0 * 3.14159265 * d * d);

    s.radiance = farbe * factor;

    return s;
}

bool AreaLight::isArea() const
{
    return true;
}

int AreaLight::numberOfSamples(const Vector3D& hitPos) const
{
    double area = edgeU.cross(edgeV).length();

    double dist2 = (position - hitPos).lengthSquared();

    //std::cout << std::clamp(int(area * 300.0 / dist2), 8, 128) << std::endl;
    return std::clamp(
        int(area * 300.0 / dist2),
        8,
        128
    );
}

Farbe AreaLight::getEmission() const
{
    double area = edgeU.cross(edgeV).length();
    double scale = 1.0; 
    return farbe * (power / area) * scale;
}