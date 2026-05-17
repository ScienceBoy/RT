#include "directionallight.h"
#include "material.h"
#include <random>
#include <limits>
#include <cmath>

DirectionalLight::DirectionalLight(
    const Vector3D& dir,
    const Farbe& col,
    double irradiance)

{
    direction = dir.normalized();
    farbe = col;
    irradiance = 1.0;
}

Vector3D DirectionalLight::samplePoint() const
{
    return Vector3D(0,0,0);
}

Vector3D DirectionalLight::directionFrom(
    const Vector3D&) const
{
    return direction;
}

double DirectionalLight::distanceFrom(
    const Vector3D&) const
{
    return std::numeric_limits<double>::infinity();
}

Farbe DirectionalLight::intensityAt(
    const Vector3D&) const
{
    return farbe * irradiance;
}

void DirectionalLight::drawFlat(
    Wireframe& wf,
    DrawMode mode) const
{
    drawWireframePixels(wf, mode);
}

void DirectionalLight::drawWireframePixels(
    Wireframe& wf,
    DrawMode mode) const
{
    Farbe color;

    if (mode == DrawMode::HIGHLIGHT)
        color = Farbe(1,1,0);
    else if (mode == DrawMode::HOVER)
        color = Farbe(0,1,1);
    else
        color = farbe;

    // ======================================================
    // Editor-Position
    // ======================================================
    Vector3D center = editorPosition(); 
    Vector3D dir = direction.normalized();

    double len = 50.0;

    Vector3D end = center + dir * len;

    int x0,y0,x1,y1;
    double z0,z1;

    // Hauptlinie (Lichtrichtung)
    wf.project(center,x0,y0,z0);
    wf.project(end,x1,y1,z1);

    wf.drawLine(x0,y0,z0,x1,y1,z1,makeMaterialSimple(color));

    // ======================================================
    // Pfeilspitze (besseres Debug-Visual)
    // ======================================================

    Vector3D right = dir.cross(Vector3D(0,1,0));
    if (right.length() < 0.001)
        right = dir.cross(Vector3D(1,0,0));

    right = right.normalized();

    double headSize = len * 0.2;

    Vector3D up = right.cross(dir).normalized();

    Vector3D p1 = end - dir * headSize + right * headSize * 0.5;
    Vector3D p2 = end - dir * headSize - right * headSize * 0.5;
    Vector3D p3 = end - dir * headSize + up * headSize * 0.5;
    Vector3D p4 = end - dir * headSize - up * headSize * 0.5;

    wf.project(end,x0,y0,z0);

    int ax,ay; double az;

    wf.project(p1,ax,ay,az);
    wf.drawLine(x0,y0,z0,ax,ay,az,makeMaterialSimple(color));

    wf.project(p2,ax,ay,az);
    wf.drawLine(x0,y0,z0,ax,ay,az,makeMaterialSimple(color));

    wf.project(p3,ax,ay,az);
    wf.drawLine(x0,y0,z0,ax,ay,az,makeMaterialSimple(color));

    wf.project(p4,ax,ay,az);
    wf.drawLine(x0,y0,z0,ax,ay,az,makeMaterialSimple(color));
}

void DirectionalLight::intersect(
    const Ray& ray,
    Hit& hit) const
{
    // absichtlich leer
}

void DirectionalLight::save(
    std::ostream& out) const
{
    out << "        <light type=\"directional\">\n";

    out << "            <direction x=\""
        << direction.x
        << "\" y=\""
        << direction.y
        << "\" z=\""
        << direction.z
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


Vector3D DirectionalLight::editorPosition() const
{
    return -direction * 20.0; // oder origin
}

void DirectionalLight::translate(const Vector3D& delta)
{
    direction = (direction - delta).normalized();
}

LightSample DirectionalLight::sample(const Vector3D& p) const
{
    LightSample s;

    s.directionToLight = direction.normalized();
    s.distance = std::numeric_limits<double>::infinity();
    s.radiance = farbe * irradiance;

    return s;
}

Farbe DirectionalLight::getEmission() const
{
    return Farbe(0,0,0);
}
