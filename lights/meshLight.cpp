#include "meshLight.h"
#include "material.h"
#include <cmath>
#include <random>
#include <cstdlib>
#include "random.h"
#include "lightSample.h"
#include <iostream>

extern double random01();

MeshLight::MeshLight(std::unique_ptr<Mesh> m, Farbe c, double p)
    : mesh(std::move(m)),
      color(c),
      power(p)
{
    totalArea = 0.0;
    cdf.clear();

    for (const auto& tri : mesh->triangles)
    {
        double a = tri.area();
        totalArea += a;
        cdf.push_back(totalArea);
    }
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

LightSample MeshLight::samplePoint() const
{
    const Dreieck& t = randomTriangle();

    double u = random01();
    double v = random01();

    if (u + v > 1.0)
    {
        u = 1.0 - u;
        v = 1.0 - v;
    }

    Vector3D p =
        t.a + (t.b - t.a) * u + (t.c - t.a) * v;

    Vector3D n =
        (t.b - t.a).cross(t.c - t.a).normalized();

    LightSample s;
    s.position = p;
    s.normal = n;
    s.pdf = 1.0 / totalArea;
    s.radiance = color;
    return s;
}

Vector3D MeshLight::directionFrom(const Vector3D& point) const
{
    LightSample s = samplePoint();
    Vector3D p = s.position;

    return (p - point).normalized();
}

double MeshLight::distanceFrom(
    const Vector3D& point) const
{
    LightSample s = samplePoint();
    Vector3D p = s.position;
    return (p - point).length();
}

Farbe MeshLight::intensityAt(const Vector3D& point) const
{
    LightSample s = samplePoint();
    Vector3D p = s.position;

    if (s.distance <= 1e-6)
        return Farbe(0,0,0);

    double cosTheta = std::max(0.0, -(s.directionToLight * s.normal));
    if (cosTheta <= 0.0)
        return Farbe(0,0,0);

    double areaPDF = 1.0 / totalArea;

    double attenuation =
        (power * cosTheta * totalArea) / (4.0 * M_PI * s.distance * s.distance);

    return farbe * attenuation;
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
    const double EPS = 1e-6;

    Vector3D edge1 = tri.b - tri.a;
    Vector3D edge2 = tri.c - tri.a;

    Vector3D h = (ray.direction).cross(edge2);
    double a = edge1 * h;

    if (fabs(a) < EPS)
        return false; // parallel

    double f = 1.0 / a;
    Vector3D s = ray.origin - tri.a;

    double u = f * s * h;
    if (u < 0.0 || u > 1.0)
        return false;

    Vector3D q = s.cross(edge1);
    double v = f * ray.direction * q;
    if (v < 0.0 || u + v > 1.0)
        return false;

    double t = f * edge2 * q;

    if (t > EPS && t < hit.t)
    {
        hit.t = t;
        hit.position = ray.origin + ray.direction * t;

        // Normale
        hit.normale = edge1.cross(edge2).normalized();

        return true;
    }

    return false;
}

bool MeshLight::intersectWithUmhuellende(const Ray& ray) const
{
    Vector3D oc = ray.origin - mesh->boundingCenter;

    float a = ray.direction * ray.direction;
    float b = 2.0f * (oc * ray.direction);
    float c = (oc * oc) - mesh->boundingRadius * mesh->boundingRadius;

    float discriminant = b * b - 4.0f * a * c;

    if (discriminant < 0.0f)
        return false;

    float sqrtD = sqrt(discriminant);

    float t1 = (-b - sqrtD) / (2.0f * a);
    float t2 = (-b + sqrtD) / (2.0f * a);

    return (t1 > 1e-4f || t2 > 1e-4f);
}


bool MeshLight::intersectAABB(const Ray& ray) const
{
    float tmin = (mesh->minBound.x - ray.origin.x) / ray.direction.x;
    float tmax = (mesh->maxBound.x - ray.origin.x) / ray.direction.x;

    if (tmin > tmax)
        std::swap(tmin, tmax);

    float tymin = (mesh->minBound.y - ray.origin.y) / ray.direction.y;
    float tymax = (mesh->maxBound.y - ray.origin.y) / ray.direction.y;

    if (tymin > tymax)
        std::swap(tymin, tymax);

    if ((tmin > tymax) || (tymin > tmax))
        return false;

    if (tymin > tmin)
        tmin = tymin;

    if (tymax < tmax)
        tmax = tymax;

    float tzmin = (mesh->minBound.z - ray.origin.z) / ray.direction.z;
    float tzmax = (mesh->maxBound.z - ray.origin.z) / ray.direction.z;

    if (tzmin > tzmax)
        std::swap(tzmin, tzmax);

    if ((tmin > tzmax) || (tzmin > tmax))
        return false;

    return true;
}

void MeshLight::intersect(const Ray& ray, Hit& hit) const
{
    //if (!intersectWithUmhuellende(ray))
    //    return;

    if (!intersectAABB(ray))        // AABB-Würfel-Umhüllende ist näher am Objekt als Kugel-Umhüllende
        return;

    Hit temp;

    for (const auto& tri : mesh->triangles)   
    {
        if (intersectTriangle(ray, tri, temp))
        {
            if (temp.t < hit.t)
            {
                hit = temp;
                hit.hit = true;
                hit.hitLight = true;
                hit.light = this;
            }
        }
    }
}


void MeshLight::save(std::ostream& out) const
{
    out << "        <light type=\"mesh\">\n";

    out << "            <color r=\""
        << color.r
        << "\" g=\""
        << color.g
        << "\" b=\""
        << color.b
        << "\"/>\n";

    out << "            <power value=\""
        << power
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


Vector3D MeshLight::editorPosition() const
{
    return mesh->boundingCenter;
}

void MeshLight::translate(const Vector3D& d)
{
    for (auto& tri : mesh->triangles)
    {
        tri.a -= d;
        tri.b -= d;
        tri.c -= d;
    }

    mesh->position -= d;

    mesh->minBound -= d;
    mesh->maxBound -= d;
    mesh->boundingCenter -= d;

}

LightSample MeshLight::sample(const Vector3D& p) const
{
    const Dreieck& t = randomTriangle();

    double u = random01();
    double v = random01();

    if (u + v > 1.0)
    {
        u = 1.0 - u;
        v = 1.0 - v;
    }

    Vector3D pos =
        t.a + (t.b - t.a) * u + (t.c - t.a) * v;

    Vector3D normal =
        (t.b - t.a).cross(t.c - t.a).normalized();

    Vector3D dir = pos - p;
    double dist = dir.length();
    dir = dir * (1.0 / dist);

    double areaPDF = 1.0 / totalArea;

    return {
        pos,
        normal,
        dir,
        dist,
        color,
        areaPDF
    };
}

bool MeshLight::isArea() const
{
    return false;
}

int MeshLight::numberOfSamples(const Vector3D& p) const
{
    double dist2 = (mesh->boundingCenter - p).lengthSquared();
    dist2 = std::max(dist2, 1e-6);

    int samples = int(totalArea * 300.0 / dist2);
    return std::clamp(samples, 8, 256);
}

Farbe MeshLight::getEmission() const
{
    double area = totalArea;
    double scale = 1.0; 
    //std::cout << color * (power / area) * scale << std::endl;
    return color * (power / area) * scale;
}