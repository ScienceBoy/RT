#include "pointlight.h"
#include <cmath>
#include <vector>
#include <numbers>

PointLight::PointLight()
: position(0,0,0)
{
    farbe = Farbe(1,1,1);
}

PointLight::PointLight(
    const Vector3D& pos,
    const Farbe& col,
    double power)
{
    position = pos;
    farbe = col;
    power = 100000.0;
}

Vector3D PointLight::samplePoint() const
{
    return position;
}

Vector3D PointLight::directionFrom(
    const Vector3D& p) const
{
    return (position - p).normalized();
}

double PointLight::distanceFrom(
    const Vector3D& p) const
{
    return (position - p).length();
}

Farbe PointLight::intensityAt(
    const Vector3D& p) const
{
    double d = distanceFrom(p);

    return farbe *
           (power / (4.0 * 3.14159265 * d * d));
}

void PointLight::save(
    std::ostream& out) const
{
    out << "        <light type=\"point\">\n";

    out << "            <position x=\""
        << position.x
        << "\" y=\""
        << position.y
        << "\" z=\""
        << position.z
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

Vector3D PointLight::editorPosition() const
{
    return position;
}

void PointLight::drawFlat(Wireframe& wf, DrawMode mode) const
{
    drawWireframePixels(wf, mode);
}

void PointLight::drawWireframePixels(Wireframe& wf, DrawMode mode) const
{
    const int steps = 32;
    const double radius = 10.0;

    int x0,y0,x1,y1;
    double z0,z1;

    // =========================
    // Farbe abhängig vom Mode
    // =========================
    Farbe color;

    if (mode == DrawMode::HIGHLIGHT)
        color = Farbe(1,1,0);      // Gelb
    else if (mode == DrawMode::HOVER)
        color = Farbe(0,1,1);      // Cyan
    else
        color = farbe;

    // =========================================================
    // 1. Kreis XZ
    // =========================================================
    for (int i = 0; i < steps; i++)
    {
        double a0 = 2 * 3.14159265 * i / steps;
        double a1 = 2 * 3.14159265 * (i+1) / steps;

        Vector3D p0 = position + Vector3D(radius*cos(a0), 0, radius*sin(a0));
        Vector3D p1 = position + Vector3D(radius*cos(a1), 0, radius*sin(a1));

        wf.project(p0,x0,y0,z0);
        wf.project(p1,x1,y1,z1);
        wf.drawLine(x0,y0,z0,x1,y1,z1,makeMaterialSimple(color));
    }

    // =========================================================
    // 2. Kreis XY
    // =========================================================
    for (int i = 0; i < steps; i++)
    {
        double a0 = 2 * 3.14159265 * i / steps;
        double a1 = 2 * 3.14159265 * (i+1) / steps;

        Vector3D p0 = position + Vector3D(radius*cos(a0), radius*sin(a0), 0);
        Vector3D p1 = position + Vector3D(radius*cos(a1), radius*sin(a1), 0);

        wf.project(p0,x0,y0,z0);
        wf.project(p1,x1,y1,z1);
        wf.drawLine(x0,y0,z0,x1,y1,z1,makeMaterialSimple(color));
    }

    // =========================================================
    // 3. Kreis YZ
    // =========================================================
    for (int i = 0; i < steps; i++)
    {
        double a0 = 2 * 3.14159265 * i / steps;
        double a1 = 2 * 3.14159265 * (i+1) / steps;

        Vector3D p0 = position + Vector3D(0, radius*cos(a0), radius*sin(a0));
        Vector3D p1 = position + Vector3D(0, radius*cos(a1), radius*sin(a1));

        wf.project(p0,x0,y0,z0);
        wf.project(p1,x1,y1,z1);
        wf.drawLine(x0,y0,z0,x1,y1,z1,makeMaterialSimple(color));
    }

    // =========================================================
    // 4. Strahlen (Gizmo-Achsen)
    // =========================================================
    std::vector<Vector3D> dirs =
    {
        {1,0,0},{-1,0,0},
        {0,1,0},{0,-1,0},
        {0,0,1},{0,0,-1},

        {1,1,1},{-1,1,1},
        {1,-1,1},{1,1,-1},
        {-1,-1,1},{-1,1,-1},
        {1,-1,-1},{-1,-1,-1}
    };

    double rayLength = radius * 2.0;

    for (const auto& d : dirs)
    {
        Vector3D dir = d.normalized();

        Vector3D p0 = position;
        Vector3D p1 = position + dir * rayLength;

        wf.project(p0,x0,y0,z0);
        wf.project(p1,x1,y1,z1);

        wf.drawLine(x0,y0,z0,x1,y1,z1,makeMaterialSimple(color));
    }
}

void PointLight::intersect(const Ray& ray, Hit& hit) const
{
    double radius = 10.0;

    Vector3D v = ray.origin - position;
    double a = ray.direction * ray.direction;
    double b = 2.0 * (v * ray.direction);
    double c = v * v - radius * radius;

    double d = b*b - 4*a*c;
    if (d < 0) return;

    double t = (-b - sqrt(d)) / (2*a);
    if (t < 1e-6) return;

    if (t < hit.t)
    {
        //if (hit.hitObject)
        //    return;
            
        hit.t = t;
        hit.hit = true;
        hit.position = ray.origin + ray.direction * t;
        hit.hitLight = true;
        hit.light = this;

    }
}

void PointLight::translate(const Vector3D& delta)
{
    position = position - delta;
}

LightSample PointLight::sample(const Vector3D& p) const
{
    LightSample s;

    Vector3D dir = getPosition() - p;
    double d = dir.length();
    dir = dir.normalized();
    
    s.position = getPosition();
    s.directionToLight = dir;
    s.distance = d;
    s.normal = dir;

    double attenuation = power / (4.0 * 3.14159265 * d * d);

    s.radiance = farbe * attenuation;
    s.pdf = 1.0;

    return s;
}

Farbe PointLight::getEmission() const
{
    return farbe * power;
}