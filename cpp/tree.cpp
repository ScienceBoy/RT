#include "tree.h"
#include <cmath>
#include "rotations.h"
#include <math.h>
#include <algorithm>
#include <limits>

Tree::Tree(const Vector3D& pos, const Vector3D& richtung, double laenge, double radiusHolz, int tiefe, double wkeitWeiteresGlied)
: object(Material()), position(pos), bvhRoot(nullptr)
{
    //woodTriangles.reserve(250000);
    //leaves.reserve(250000);

    build(pos, richtung.normalized(), laenge, radiusHolz, tiefe, wkeitWeiteresGlied);

    updateBounds();
    
    // BVH bauen
    //bvhObjects.reserve(stabs.size() + leaves.size());
    //bvhObjects.reserve(woodTriangles.size() + leaves.size());

    //for (const auto& s : stabs)
    //    bvhObjects.push_back(new StabObject(&s));

    /*for (const auto& t : woodTriangles)
        bvhObjects.push_back(new TriangleObject(&t));

    for (const auto& l : leaves)
        bvhObjects.push_back(new LeafObject(&l));

    bvhRoot = buildBVH(bvhObjects);*/
    buildBVH();

}

Tree::~Tree()
{
    delete bvhRoot;

    //for (auto obj : bvhObjects)
    //    delete obj;
}

static inline void rebuildTri(Dreieck& tri)
{
    tri.rebuild(); return;
}

void Tree::build(const Vector3D& pos, const Vector3D& dir, double len, double radiusHolz, int depth, double wkeitWeiteresGlied)
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

    //stabs.emplace_back(pos, end, radiusHolz, wood);
    addBranchMesh(pos, end, radiusHolz, 8, wood);

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
        double angle = randRange(0, 2 * M_PI);

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

        for (int i = 0; i < density*3; i++)
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

void Tree::recomputeBounds()
{
    minBound = Vector3D( 1e9, 1e9, 1e9);
    maxBound = Vector3D(-1e9,-1e9,-1e9);

    auto updateTri = [&](const Dreieck& tri)
    {
        auto updatePoint = [&](const Vector3D& p)
        {
            minBound.x = std::min(minBound.x, p.x);
            minBound.y = std::min(minBound.y, p.y);
            minBound.z = std::min(minBound.z, p.z);

            maxBound.x = std::max(maxBound.x, p.x);
            maxBound.y = std::max(maxBound.y, p.y);
            maxBound.z = std::max(maxBound.z, p.z);
        };

        updatePoint(tri.a);
        updatePoint(tri.b);
        updatePoint(tri.c);
    };

    for (auto& t : woodTriangles) updateTri(t);
    for (auto& t : leaves) updateTri(t);

    boundingCenter = (minBound + maxBound) * 0.5;
}

void Tree::rotate(const Vector3D& rotation)
{
    recomputeBounds();                 
    Vector3D center = boundingCenter;

    double rx = rotation.x * M_PI / 180.0;
    double ry = rotation.y * M_PI / 180.0;
    double rz = rotation.z * M_PI / 180.0;

    auto rotP = [&](Vector3D& p)
    {
        p -= center;
        p = rotateX(p, rx);
        p = rotateY(p, ry);
        p = rotateZ(p, rz);
        p += center;
    };

    for (auto& tri : woodTriangles)
    {
        rotP(tri.a); rotP(tri.b); rotP(tri.c);
        rebuildTri(tri);              // <<< NEU
    }

    for (auto& tri : leaves)
    {
        rotP(tri.a); rotP(tri.b); rotP(tri.c);
        rebuildTri(tri);              // <<< NEU
    }

    recomputeBounds();
    buildBVH();
}

void Tree::scale(const Vector3D& scaleVec)
{
    recomputeBounds();                 // <<< NEU
    Vector3D center = boundingCenter;

    auto sclP = [&](Vector3D& p)
    {
        p -= center;
        p.x *= scaleVec.x;
        p.y *= scaleVec.y;
        p.z *= scaleVec.z;
        p += center;
    };

    for (auto& tri : woodTriangles)
    {
        sclP(tri.a); sclP(tri.b); sclP(tri.c);
        rebuildTri(tri);              // <<< NEU
    }

    for (auto& tri : leaves)
    {
        sclP(tri.a); sclP(tri.b); sclP(tri.c);
        rebuildTri(tri);              // <<< NEU
    }

    recomputeBounds();
    buildBVH();
}

void Tree::translate(const Vector3D& delta)
{
    for (auto& tri : woodTriangles)
    {
        tri.a += delta;
        tri.b += delta;
        tri.c += delta;
        rebuildTri(tri);              
    }

    for (auto& tri : leaves)
    {
        tri.a += delta;
        tri.b += delta;
        tri.c += delta;
        rebuildTri(tri);              
    }

    position += delta;

    recomputeBounds();
    buildBVH();
}

void Tree::updateBounds()
{
    if (woodTriangles.empty())
        return;

    Vector3D minB( 1e9, 1e9, 1e9);
    Vector3D maxB(-1e9,-1e9,-1e9);

    //for (const auto& s : stabs)
    for (const auto& t : woodTriangles)
    {
        Vector3D mi = t.getMinBound();
        Vector3D ma = t.getMaxBound();

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

void Tree::addBranchMesh(const Vector3D& start, const Vector3D& end, double radius, int segments, const Material& mat)
    {
        Vector3D axis = (end - start).normalized();

        Vector3D up =
            std::abs(axis.y) > 0.9
            ? Vector3D(1,0,0)
            : Vector3D(0,1,0);

        Vector3D right = axis.cross(up).normalized();
        Vector3D forward = right.cross(axis).normalized();

        std::vector<Vector3D> ringStart;
        std::vector<Vector3D> ringEnd;
        std::vector<Vector3D> normalsStart;
        std::vector<Vector3D> normalsEnd;

        double r1 = radius;
        double r2 = radius * randRange(0.6, 0.9);

        for (int i = 0; i < segments; i++)
        {
            double a = (2.0 * M_PI * i) / segments;

            double c = cos(a);
            double s = sin(a);

            // unten
            Vector3D offsetStart =
                right * (c * r1) +
                forward * (s * r1);

            // oben
            Vector3D offsetEnd =
                right * (c * r2) +
                forward * (s * r2);

            Vector3D normalStart =
                offsetStart.normalized();

            Vector3D normalEnd =
                offsetEnd.normalized();

            normalsStart.push_back(normalStart);
            normalsEnd.push_back(normalEnd);

            ringStart.push_back(start + offsetStart);
            ringEnd.push_back(end + offsetEnd);
        }

        for (int i = 0; i < segments; i++)
        {
            int next = (i + 1) % segments;

            Vector3D p1 = ringStart[i];
            Vector3D p2 = ringStart[next];

            Vector3D p3 = ringEnd[i];
            Vector3D p4 = ringEnd[next];

            Vector3D n1 = normalsStart[i];
            Vector3D n2 = normalsStart[next];

            Vector3D n3 = normalsEnd[i];
            Vector3D n4 = normalsEnd[next];

            // Triangle 1
            woodTriangles.emplace_back(
                p1, p2, p3,
                n1, n2, n3,
                mat);

            woodTriangles.emplace_back(
                p2, p4, p3,
                n2, n4, n3,
                mat);
        }
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

    double angle = randRange(0, 2 * M_PI);

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

    if (!intersectAABB(ray, hit.t))
        return;

    bvhRoot->intersect(ray, hit);
}

int Tree::getTriangleCount() const
{
    //return (int)leaves.size();
    return (int)(leaves.size() + woodTriangles.size());
}

void Tree::drawFlat(Wireframe& wf, DrawMode mode) const
{
    //for (const auto& s : stabs)
    for (const auto& t : woodTriangles)
        t.drawFlat(wf, mode);

    for (const auto& l : leaves)
        l.drawFlat(wf, mode);
}

void Tree::drawWireframePixels(Wireframe& wf, DrawMode mode) const
{
    //for (const auto& s : stabs)
    for (const auto& t : woodTriangles)
        t.drawWireframePixels(wf, mode);

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

/*void Tree::buildBVH()
{
    for (auto obj : bvhObjects)
        delete obj;

    bvhObjects.clear();

    bvhObjects.reserve(woodTriangles.size() + leaves.size());

    for (auto& t : woodTriangles)
        bvhObjects.push_back(&t);

    for (auto& l : leaves)
        bvhObjects.push_back(&l);

    bvhRoot = ::buildBVH(bvhObjects);
}*/
void Tree::buildBVH()
{
    if (bvhRoot)
    {
        //delete bvhRoot;
        bvhRoot = nullptr;
    }

    bvhObjects.clear();
    bvhObjects.reserve(woodTriangles.size() + leaves.size());

    for (auto& t : woodTriangles) bvhObjects.push_back(&t);
    for (auto& l : leaves)        bvhObjects.push_back(&l);

    bvhRoot = ::buildBVH(bvhObjects);
}