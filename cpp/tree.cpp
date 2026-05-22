#include "tree.h"
#include <cmath>

Tree::Tree(const Vector3D& pos,
           const Vector3D& richtung,
           double laenge,
           double radiusHolz,
           int tiefe,
           double wkeitWeiteresGlied)
: object(Material()), position(pos)
{
    build(pos, richtung.normalized(), laenge, radiusHolz, tiefe, wkeitWeiteresGlied);

    updateBounds();
    
    // BVH bauen
    bvhObjects.reserve(stabs.size() + leaves.size());

    for (const auto& s : stabs)
        bvhObjects.push_back(new StabObject(&s));

    for (const auto& l : leaves)
        bvhObjects.push_back(new LeafObject(&l));

    bvhRoot = buildBVH(bvhObjects);

}

Tree::~Tree()
{
    delete bvhRoot;

    for (auto obj : bvhObjects)
        delete obj;
}


void Tree::build(const Vector3D& pos,
                 const Vector3D& dir,
                 double len,
                 double radiusHolz,
                 int depth,
                 double wkeitWeiteresGlied)
{
    if (depth <= 0 || radiusHolz < 0.01)
        return;

    Vector3D end = pos + dir * len;

    Material wood = Material(
        Farbe(0.0, 0.0, 0.0),      // ambient (Ka)
        Farbe(0.36, 0.22, 0.12),   // diffuse (Kd)
        Farbe(0.0, 0.0, 0.0),      // specular (Ks)
        Farbe(0.0, 0.0, 0.0),      // emission (Ke)

        0.0,   // kA
        1.0,   // kD
        0.0,  // shininess (Ns)
        0.0,   // specularStrength (optional aus Ks ableitbar)

        0.01,   // reflection
        0.0,   // roughness (nicht aus MTL ableitbar)
        0.0,   // IOR (Ni)
        0.0,   // transparency (d = 1 → opaque)
        Farbe(0,0,0) // absorption
    );
    // =====================================================
    // 🌲 STAB statt Triangles
    // =====================================================

    stabs.emplace_back(pos, end, radiusHolz, wood);

    // =====================================================
    // 🍃 Blätter
    // =====================================================

    if (depth <= 2)
    {
        Vector3D surfacePoint = end;

        // kleine zufällige Verschiebung entlang der lokalen Querschnittsfläche
        Vector3D up = std::abs(dir.y) > 0.9 ? Vector3D(1,0,0) : Vector3D(0,1,0);

        Vector3D right = dir.cross(up).normalized();
        Vector3D realUp = right.cross(dir).normalized();

        double r = sqrt(randRange(0.0, 1.0)) * radiusHolz; // radial verteilt
        double angle = randRange(0, 2 * 3.14159265);

        Vector3D radialOffset =
            right * (cos(angle) * r) +
            realUp * (sin(angle) * r);

        // leicht entlang der Spitze nach außen
        double forward = randRange(0.0, 0.4) * radiusHolz;

        Vector3D leafPos =
            end
            + radialOffset
            + dir * forward;

        int density = (int)std::max(1.0, pow(radiusHolz, 1.5) * 50.0);

        for (int i = 0; i < density; i++)
        {
            Vector3D offset = randomInSphere() * 0.1;
            addLeaf(leafPos + offset, dir);
        }
    }

    // =====================================================
    // 🌲 Hauptast
    // =====================================================

    Vector3D newDir = dir;
    newDir.x += randRange(-0.3, 0.3);
    newDir.y += randRange(-0.1, 0.4);
    newDir.z += randRange(-0.3, 0.3);
    newDir.y *= 0.7;    // WACHSTUMS- “Gravity”
    newDir.normalize();

    if (randRange(0.0, 1.0) < wkeitWeiteresGlied)
    {
        build(end,
              newDir,
              len * randRange(0.75, 0.9),
              radiusHolz * 0.65,
              depth - 1,
              wkeitWeiteresGlied);
    }

    // =====================================================
    // 🌿 Seitenäste
    // =====================================================

    int branches = 2 + rand() % 3;

    for (int i = 0; i < branches; i++)
    {
        double t = randRange(0.2, 1.0);
        Vector3D base = pos + (end - pos) * t;

        Vector3D branchDir = dir;
        branchDir.x += randRange(-0.6, 0.6);
        branchDir.y += randRange(0.1, 0.8);
        branchDir.z += randRange(-0.6, 0.6);
        branchDir.y *= 0.6; // WACHSTUMS- “Gravity”
        branchDir.normalize();

        build(base,
              branchDir,
              len * randRange(0.75, 0.9),
              radiusHolz * 0.65,
              depth - 1,
              wkeitWeiteresGlied);
    }

}

Vector3D Tree::randomInSphere()
{
    Vector3D v;

    do {
        v = Vector3D(
            randRange(-1, 1),
            randRange(-1, 1),
            randRange(-1, 1)
        );
    } while (v.length() > 1.0);

    return v;
}

void Tree::updateBounds()
{
    if (stabs.empty())
        return;

    Vector3D minB( 1e9, 1e9, 1e9);
    Vector3D maxB(-1e9,-1e9,-1e9);

    for (const auto& s : stabs)
    {
        Vector3D mi = s.getMinBound();
        Vector3D ma = s.getMaxBound();

        minB.x = std::min(minB.x, mi.x);
        minB.y = std::min(minB.y, mi.y);
        minB.z = std::min(minB.z, mi.z);

        maxB.x = std::max(maxB.x, ma.x);
        maxB.y = std::max(maxB.y, ma.y);
        maxB.z = std::max(maxB.z, ma.z);
    }

    for (const auto& l : leaves)
    {
        Vector3D mi = l.getMinBound();
        Vector3D ma = l.getMaxBound();

        minB.x = std::min(minB.x, mi.x);
        minB.y = std::min(minB.y, mi.y);
        minB.z = std::min(minB.z, mi.z);

        maxB.x = std::max(maxB.x, ma.x);
        maxB.y = std::max(maxB.y, ma.y);
        maxB.z = std::max(maxB.z, ma.z);
    }

    boundingCenter = (minB + maxB) * 0.5;
    boundingRadius = (maxB - minB).length() * 0.5;

    minBound = minB;
    maxBound = maxB;
}

void Tree::addLeaf(const Vector3D& pos, const Vector3D& dir)
{
    Material leaf = Material(
        Farbe(0.0, 0.0, 0.0),      // ambient (Ka)
        Farbe(0.20, 0.55, 0.20),   // diffuse (Kd)
        Farbe(0.0, 0.0, 0.0),      // specular (Ks)
        Farbe(0.0, 0.0, 0.0),      // emission (Ke)

        0.0,   // kA
        1.0,   // kD
        0.0,  // shininess (Ns)
        0.0,   // specularStrength (optional aus Ks ableitbar)

        0.01,   // reflection
        0.0,   // roughness (nicht aus MTL ableitbar)
        0.0,   // IOR (Ni)
        0.0,   // transparency (d = 1 → opaque)
        Farbe(0,0,0) // absorption
    );

    Vector3D up = std::abs(dir.y) > 0.9 ? Vector3D(1,0,0) : Vector3D(0,1,0);

    Vector3D right = dir.cross(up).normalized();
    Vector3D realUp = right.cross(dir).normalized();

    double angle = randRange(0, 2 * 3.14159265);

    double c = cos(angle);
    double s = sin(angle);

    // stabile Rotation im Leaf-Plane
    Vector3D r = right * c + realUp * s;
    Vector3D u = right * (-s) + realUp * c;

    double size = randRange(0.1, 0.2);

    Vector3D p1 = pos + r * size + u * size;
    Vector3D p2 = pos - r * size + u * size;
    Vector3D p3 = pos - r * size - u * size;
    Vector3D p4 = pos + r * size - u * size;

    // zwei Dreiecke
    leaves.emplace_back(p1, p2, p3, leaf);
    leaves.emplace_back(p1, p3, p4, leaf);
}

bool Tree::intersectAABB(const Ray& ray, double tMax) const
{
    double tmin = 0.0;
    double tmax = tMax;

    for (int i = 0; i < 3; ++i)
    {
        double t0 = (minBound[i] - ray.origin[i]) * ray.invDirection[i];
        double t1 = (maxBound[i] - ray.origin[i]) * ray.invDirection[i];

        if (ray.sign[i])
            std::swap(t0, t1);

        tmin = std::max(tmin, t0);
        tmax = std::min(tmax, t1);

        if (tmax <= tmin)
            return false;
    }
    return true;
}

void Tree::intersect(const Ray& ray, Hit& hit) const
{
    if (!bvhRoot) 
        return;

    //if (!intersectAABB(ray, hit.t))
    //    return;

    bvhRoot->intersect(ray, hit);
}

int Tree::getTriangleCount() const
{
    return (int)leaves.size();
}

void Tree::drawFlat(Wireframe& wf, DrawMode mode) const
{
    for (const auto& s : stabs)
        s.drawFlat(wf, mode);

    for (const auto& l : leaves)
        l.drawFlat(wf, mode);
}

void Tree::drawWireframePixels(Wireframe& wf, DrawMode mode) const
{
    for (const auto& s : stabs)
        s.drawWireframePixels(wf, mode);

    for (const auto& l : leaves)
        l.drawWireframePixels(wf, mode);
}

void Tree::setPosition(const Vector3D& pos)
{
    position = pos;
}

double Tree::randRange(double a, double b)
{
    return a + (b - a) * (rand() / (double)RAND_MAX);
}

