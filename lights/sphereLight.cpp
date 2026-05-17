#include "sphereLight.h"
#include "material.h"
#include "random.h"
#include <random>
#include <cmath>
#include <vector>

extern Vector3D randomUnitVector();

SphereLight::SphereLight(
    const Vector3D& c,
    double r,
    const Farbe& col,
    double power)
{
    center = c;
    radius = r;
    farbe = col;
    power = power;
}

Vector3D SphereLight::samplePoint() const
{
    Vector3D dir = randomUnitVector();
    lastSamplePoint = center + dir * radius;
    hasSample = true;
    return lastSamplePoint;
}

Vector3D SphereLight::directionFrom(const Vector3D& point) const
{
    if (!hasSample)
        samplePoint();

    return (lastSamplePoint - point).normalized();
}

double SphereLight::distanceFrom(const Vector3D& point) const
{
    if (!hasSample)
        samplePoint();

    return (lastSamplePoint - point).length();
}

Farbe SphereLight::intensityAt(
    const Vector3D& point) const
{
    double d =
        distanceFrom(point);

    return farbe *
           (power / (4.0 * 3.14159265 * d * d));
}

void SphereLight::drawFlat(
    Wireframe& wf,
    DrawMode mode) const
{
    drawWireframePixels(wf, mode);
}

void SphereLight::drawWireframePixels(
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

    const int steps = 32;

    int x0,y0,x1,y1;
    double z0,z1;

    Vector3D center = getPosition(); 

    auto drawCircle =
    [&](int axis)
    {
        for(int i=0;i<steps;i++)
        {
            double a0 = 2.0 * 3.14159265 * i / steps;
            double a1 = 2.0 * 3.14159265 * (i+1) / steps;

            Vector3D p0, p1;

            switch(axis)
            {
                case 0: // XY
                    p0 = center + Vector3D(radius*cos(a0), radius*sin(a0), 0);
                    p1 = center + Vector3D(radius*cos(a1), radius*sin(a1), 0);
                    break;

                case 1: // XZ
                    p0 = center + Vector3D(radius*cos(a0), 0, radius*sin(a0));
                    p1 = center + Vector3D(radius*cos(a1), 0, radius*sin(a1));
                    break;

                case 2: // YZ
                    p0 = center + Vector3D(0, radius*cos(a0), radius*sin(a0));
                    p1 = center + Vector3D(0, radius*cos(a1), radius*sin(a1));
                    break;
            }

            wf.project(p0,x0,y0,z0);
            wf.project(p1,x1,y1,z1);

            wf.drawLine(
                x0,y0,z0,
                x1,y1,z1,
                makeMaterialSimple(color));
        }
    };

    drawCircle(0);
    drawCircle(1);
    drawCircle(2);
}

void SphereLight::intersect(
    const Ray& ray,
    Hit& hit) const
{
    Vector3D v =
        ray.origin - center;

    double a =
        ray.direction * ray.direction;

    double b =
        2.0 * (v * ray.direction);

    double c =
        v * v - radius * radius;

    double d =
        b*b - 4*a*c;

    if(d < 0)
        return;

    double t =
        (-b - sqrt(d)) / (2*a);

    if(t < 1e-6)
        return;

    if(t < hit.t)
    {
        //if (hit.hitObject)
        //    return;

        hit.t = t;
        hit.hit = true;

        hit.position =
            ray.origin +
            ray.direction * t;

        hit.hitLight = true;
        hit.light = this;


    }
}

Vector3D randomUnitVector()
{
    double z =
        2.0 * random01() - 1.0;

    double a =
        2.0 * 3.14159265 * random01();

    double r =
        sqrt(1.0 - z*z);

    return Vector3D(
        r*cos(a),
        r*sin(a),
        z);
}

void SphereLight::save(
    std::ostream& out) const
{
    out << "        <light type=\"sphere\">\n";

    out << "            <center x=\""
        << center.x
        << "\" y=\""
        << center.y
        << "\" z=\""
        << center.z
        << "\"/>\n";

    out << "            <radius value=\""
        << radius
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

Vector3D SphereLight::editorPosition() const
{
    return center;
}

void SphereLight::translate(const Vector3D& delta)
{
    center -= delta;
}

LightSample SphereLight::sample(const Vector3D& p) const
{
    LightSample s;

    Vector3D samplePos = samplePoint(); 

    Vector3D dir = samplePos - p;
    double d = dir.length();
    dir = dir * (1.0 / d);

    s.position = samplePos;
    s.directionToLight = dir;
    s.distance = d;

    // Sphere normal
    s.normal = (samplePos - center).normalized();

    double area = 4.0 * 3.14159265 * radius * radius;
    double pdf = 1.0 / area;

    // emission per area (korrekt für area light)
    double intensity = power / area;

    // no inverse-square inside emission for area lights
    s.radiance = farbe * intensity;

    s.pdf = pdf;

    return s;
}

Farbe SphereLight::getEmission() const
{
    double area = 4.0 * 3.14159265 * radius * radius;
    return farbe * (power / area);
}

int SphereLight::numberOfSamples(const Vector3D& hitPos) const
{
    int maxSamples = 256;
    double dist = (center - hitPos).length();
    if (dist <= radius) return maxSamples;

    double solidAngle =
        2.0 * 3.14159265 * (1.0 - sqrt(std::max(0.0, 1.0 - (radius*radius)/(dist*dist))));

    double scale = 100.0;
    int samples = int(solidAngle * scale);

    return std::clamp(samples, 8, maxSamples);
}