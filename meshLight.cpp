#include "meshLight.h"
#include "material.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <random>
#include <cstdlib>
#include "random.h"
#include "lightSample.h"
#include <iostream>

extern double random01();

MeshLight::MeshLight(std::unique_ptr<Mesh> m, Farbe c, double p)
    : mesh(std::move(m)),
      power(p)
{
    farbe = c; // vorher eigenes Feld 'color': Aenderungen der Farbe im Editor wirkten nicht
    flaechenBerechnen();
}

// Flaechen (Auswahl der Dreiecke nach Groesse) und Huellquader neu bestimmen
void MeshLight::flaechenBerechnen()
{
    totalArea = 0.0;
    cdf.clear();
    const double gross = std::numeric_limits<double>::max();
    boxMin = Vector3D(gross, gross, gross);
    boxMax = Vector3D(-gross, -gross, -gross);

    for (const auto& tri : mesh->triangles)
    {
        totalArea += tri.area();
        cdf.push_back(totalArea);
        for (const Vector3D& q : {tri.a, tri.b, tri.c})
        {
            boxMin = Vector3D(std::min(boxMin.x, q.x), std::min(boxMin.y, q.y), std::min(boxMin.z, q.z));
            boxMax = Vector3D(std::max(boxMax.x, q.x), std::max(boxMax.y, q.y), std::max(boxMax.z, q.z));
        }
    }
    if (mesh->triangles.empty())
        boxMin = boxMax = mesh->position;
}

std::unique_ptr<MeshLight> MeshLight::kopie() const
{
    auto netz = std::make_unique<Mesh>(mesh->triangles, mesh->position);
    return std::make_unique<MeshLight>(std::move(netz), farbe, power);
}

const Dreieck& MeshLight::randomTriangle() const
{
    double r = random01() * totalArea;

    auto it = std::lower_bound(cdf.begin(), cdf.end(), r);

    if (it == cdf.end())
        it = std::prev(cdf.end());

    int idx = int(it - cdf.begin());

    return mesh->triangles[idx];
}

Vector3D MeshLight::samplePointOnTriangle(const Dreieck& t) const
{
    double u = random01();
    double v = random01();

    if (u + v > 1.0)
    {
        u = 1.0 - u;
        v = 1.0 - v;
    }
    return t.a + (t.b - t.a) * u + (t.c - t.a) * v;
}

LightSample MeshLight::samplePoint() const
{
    LightSample s;
    if (mesh->triangles.empty())
        return s;
    const Dreieck& t = randomTriangle();
    s.position = samplePointOnTriangle(t);
    s.normal = (t.b - t.a).cross(t.c - t.a).normalized();
    s.pdf = totalArea > 1e-300 ? 1.0 / totalArea : 1.0;
    s.radiance = farbe;
    return s;
}

Vector3D MeshLight::directionFrom(const Vector3D& point) const
{
    return (getPosition() - point).normalized();
}

double MeshLight::distanceFrom(
    const Vector3D& point) const
{
    return (getPosition() - point).length();
}

// wie beim Flaechenlicht: Lambert-Strahler, Strahldichte L = power / (pi * A);
// je Abtastpunkt E = power * |cos(Licht)| / (pi * d^2). Vorher: nur die Farbe, ohne Abstand
// und Winkel -> das Licht war ueberall gleich hell.
static double netzBeitrag(double power, double cosLicht, double d)
{
    cosLicht = std::fabs(cosLicht);
    if (cosLicht <= 0.0 || d <= 1e-12)
        return 0.0;
    return power * cosLicht / (M_PI * d * d);
}

Farbe MeshLight::intensityAt(const Vector3D& point) const
{
    LightSample s = sample(point);
    return s.radiance;
}

void MeshLight::drawFlat(
    Wireframe& wf,
    DrawMode mode) const
{
    drawWireframePixels(wf, mode);
}

void MeshLight::drawWireframePixels(Wireframe& wf, DrawMode mode) const
{
    for (const auto& tri : mesh->triangles)
        tri.drawWireframePixels(wf, mode);
}

bool intersectTriangle(const Ray& ray, const Dreieck& tri, Hit& hit)
{
    const double EPS = 1e-12;

    Vector3D edge1 = tri.b - tri.a;
    Vector3D edge2 = tri.c - tri.a;

    Vector3D h = (ray.direction).cross(edge2);
    double a = edge1 * h;

    if (fabs(a) < EPS)
        return false; // parallel

    double f = 1.0 / a;
    Vector3D s = ray.origin - tri.a;

    double u = f * (s * h);
    if (u < 0.0 || u > 1.0)
        return false;

    Vector3D q = s.cross(edge1);
    double v = f * (ray.direction * q);
    if (v < 0.0 || u + v > 1.0)
        return false;

    double t = f * (edge2 * q);

    if (t > 1e-6 && t < hit.t)
    {
        hit.t = t;
        hit.position = ray.origin + ray.direction * t;

        // Normale
        hit.normale = edge1.cross(edge2).normalized();

        return true;
    }

    return false;
}

// Strahl trifft den Huellquader? (Slab-Test, auch fuer Richtungskomponenten 0)
bool MeshLight::intersectAABB(const Ray& ray) const
{
    double t0 = -std::numeric_limits<double>::infinity(), t1 = std::numeric_limits<double>::infinity();
    const double o[3] = {ray.origin.x, ray.origin.y, ray.origin.z};
    const double d[3] = {ray.direction.x, ray.direction.y, ray.direction.z};
    const double mn[3] = {boxMin.x, boxMin.y, boxMin.z};
    const double mx[3] = {boxMax.x, boxMax.y, boxMax.z};
    for (int k = 0; k < 3; ++k)
    {
        if (std::fabs(d[k]) < 1e-300)
        {
            if (o[k] < mn[k] - 1e-9 || o[k] > mx[k] + 1e-9)
                return false;
            continue;
        }
        double ta = (mn[k] - o[k]) / d[k], tb = (mx[k] - o[k]) / d[k];
        if (ta > tb)
            std::swap(ta, tb);
        t0 = std::max(t0, ta);
        t1 = std::min(t1, tb);
        if (t0 > t1 + 1e-9)
            return false;
    }
    return t1 > 0;
}

void MeshLight::intersect(const Ray& ray, Hit& hit) const
{
    if (!intersectAABB(ray))
        return;

    for (const auto& tri : mesh->triangles)
    {
        Hit temp;
        temp.t = hit.t; // nur naehere Treffer
        if (intersectTriangle(ray, tri, temp) && temp.t < hit.t)
        {
            hit.t = temp.t;
            hit.position = temp.position;
            hit.normale = temp.normale;
            hit.hit = true;
            hit.hitLight = true;
            hit.light = this;
        }
    }
}


// Dreiecke direkt in der XML (SceneLoader liest <mesh> mit <triangle> oder <mesh file="...">)
void MeshLight::save(std::ostream& out) const
{
    out << "        <light type=\"mesh\" power=\"" << power << "\">\n";

    out << "            <color r=\""
        << farbe.r
        << "\" g=\""
        << farbe.g
        << "\" b=\""
        << farbe.b
        << "\"/>\n";

    out << "            <mesh>\n";

    for (const auto& tri : mesh->triangles)
    {
        out << "                <triangle>\n";

        out << "                    <a x=\"" << tri.a.x << "\" y=\"" << tri.a.y << "\" z=\"" << tri.a.z << "\"/>\n";
        out << "                    <b x=\"" << tri.b.x << "\" y=\"" << tri.b.y << "\" z=\"" << tri.b.z << "\"/>\n";
        out << "                    <c x=\"" << tri.c.x << "\" y=\"" << tri.c.y << "\" z=\"" << tri.c.z << "\"/>\n";

        out << "                </triangle>\n";
    }

    out << "            </mesh>\n";

    out << "        </light>\n\n";
}


Vector3D MeshLight::getPosition() const
{
    return (boxMin + boxMax) * 0.5;
}

Vector3D MeshLight::editorPosition() const
{
    return getPosition();
}

// wie bei den anderen Lichtern: verschiebt um -delta
void MeshLight::translate(const Vector3D& d)
{
    mesh->translate(-d); // Mesh::translate verschiebt um +delta und rechnet Grenzen/BVH neu
    flaechenBerechnen();
}

// vorher mit vertauschtem Vorzeichen (bewegte sich vom Ziel weg)
void MeshLight::setPosition(const Vector3D& p)
{
    translate(getPosition() - p);
}

LightSample MeshLight::sample(const Vector3D& p) const
{
    LightSample s = samplePoint();
    if (mesh->triangles.empty())
        return s;

    Vector3D dir = s.position - p;
    double dist = dir.length();
    dir = dist > 1e-300 ? dir * (1.0 / dist) : Vector3D(0, 1, 0);

    s.directionToLight = dir;
    s.distance = dist;
    s.radiance = farbe * netzBeitrag(power, dir * s.normal, dist);
    return s;
}

bool MeshLight::isArea() const
{
    return true;
}

int MeshLight::numberOfSamples(const Vector3D& p) const
{
    double dist2 = (getPosition() - p).lengthSquared();
    dist2 = std::max(dist2, 1e-6);

    // mindestens 16: Punkte auf der abgewandten Seite verdeckt das Netz selbst (Schattentest);
    // bei 8 waeren bei einem geschlossenen Koerper alle mit ~1/256 verdeckt -> schwarze Pixel
    int samples = int(totalArea * 300.0 / dist2);
    return std::clamp(samples, 16, 256);
}

// sichtbare Helligkeit der Oberflaeche: L = power / (pi * A)
Farbe MeshLight::getEmission() const
{
    double area = std::max(totalArea, 1e-12);
    return farbe * (power / (M_PI * area));
}
