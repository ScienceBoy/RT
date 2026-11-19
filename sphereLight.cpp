#include "sphereLight.h"
#include "material.h"
#include "random.h"
#include <random>
#include <algorithm>
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
    this->power = power; // vorher "power = power": Parameter sich selbst zugewiesen, Leistung blieb 50
}

LightSample SphereLight::samplePoint() const
{
    Vector3D dir = randomUnitVector();
    Vector3D p = center + dir * radius;

    Vector3D n = dir.normalized();

    double area = 4.0 * M_PI * radius * radius;
    double pdf = 1.0 / area;

    return {
        p,
        n,
        Vector3D(),   // directionToLight später
        0.0,          // distance später
        farbe,
        pdf
    };
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
           (power / (4.0 * M_PI * d * d));
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
            double a0 = 2.0 * M_PI * i / steps;
            double a1 = 2.0 * M_PI * (i+1) / steps;

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
        2.0 * M_PI * random01();

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
    out << "        <light type=\"sphere\" power=\"" << power << "\">\n"; // Leistung (im Editor einstellbar)

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

// Abtastpunkt auf der Kugel fuer den beleuchteten Punkt p.
// Vorher: Punkt irgendwo auf der ganzen Kugel und Helligkeit = power / Oberflaeche, OHNE Abstand:
//  - jede Wand war gleich hell wie die Flaeche direkt neben der Lampe (bei r = 0.045 und 2 W
//    schon fast 80-fach), daher immer viel zu hell, egal welche Leistung;
//  - lag der Punkt auf der Rueckseite, verdeckte ihn die Kugel selbst (Schattentest); bei nur
//    8 Abtastpunkten waren alle mit 1/256 Wahrscheinlichkeit verdeckt -> schwarze Pixel.
// Jetzt: nur die p zugewandte Halbkugel (pdf = 2 / A), Lambert-Strahler mit Strahldichte
// L = power / (pi * A):  E = L * cos(Licht) * (A / 2) / d^2 = power * cos(Licht) / (2 pi d^2).
// Im Mittel ueber die Halbkugel ergibt das wie beim Punktlicht power / (4 pi d^2).
LightSample SphereLight::sample(const Vector3D& p) const
{
    LightSample s;
    Vector3D zuP = p - center;
    double abstandMitte = zuP.length();

    // Punkt in der Kugel: wie ein Punktlicht in der Mitte
    if (abstandMitte <= radius * 1.000001)
    {
        double d = std::max(abstandMitte, 1e-9);
        s.position = center;
        s.normal = abstandMitte > 1e-12 ? zuP * (1.0 / abstandMitte) : Vector3D(0, 1, 0);
        s.directionToLight = abstandMitte > 1e-12 ? -s.normal : Vector3D(0, 1, 0);
        s.distance = d;
        s.radiance = farbe * (power / (4.0 * M_PI * std::max(d * d, radius * radius)));
        s.pdf = 1.0;
        return s;
    }

    // Richtung auf der zugewandten Halbkugel
    Vector3D n = randomUnitVector();
    if (n * zuP < 0)
        n = -n;
    Vector3D samplePos = center + n * radius;

    Vector3D dir = samplePos - p;
    double d = dir.length();
    dir = dir * (1.0 / d);

    double cosLicht = std::max(0.0, -(dir * n)); // Winkel an der Kugeloberflaeche

    s.position = samplePos;
    s.normal = n;
    s.directionToLight = dir;
    s.distance = d;
    s.pdf = 2.0 / (4.0 * M_PI * radius * radius);
    s.radiance = farbe * (power * cosLicht / (2.0 * M_PI * d * d));

    return s;
}

// sichtbare Helligkeit der Kugel: Strahldichte L = power / (pi * A) (wie Flaechen- und Mesh-Licht)
Farbe SphereLight::getEmission() const
{
    double area = 4.0 * M_PI * radius * radius;
    return farbe * (power / (M_PI * std::max(area, 1e-12)));
}

int SphereLight::numberOfSamples(const Vector3D& hitPos) const
{
    int maxSamples = 256;
    double dist = (center - hitPos).length();
    if (dist <= radius) return maxSamples;

    double solidAngle =
        2.0 * M_PI * (1.0 - sqrt(std::max(0.0, 1.0 - (radius*radius)/(dist*dist))));

    double scale = 100.0;
    int samples = int(solidAngle * scale);

    return std::clamp(samples, 8, maxSamples);
}