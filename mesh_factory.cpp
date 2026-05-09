#include "mesh_factory.h"
#include "mesh.h"


std::unique_ptr<object> createCubeMesh(const Vector3D& pos, double size, const Material& mat)
{
    double h = size / 2.0;

    std::vector<Dreieck> tris;

    Vector3D p000(-h,-h,-h);
    Vector3D p001(-h,-h, h);
    Vector3D p010(-h, h,-h);
    Vector3D p011(-h, h, h);
    Vector3D p100( h,-h,-h);
    Vector3D p101( h,-h, h);
    Vector3D p110( h, h,-h);
    Vector3D p111( h, h, h);

    tris.emplace_back(p001,p101,p111,mat);
    tris.emplace_back(p001,p111,p011,mat);

    tris.emplace_back(p000,p110,p100,mat);
    tris.emplace_back(p000,p010,p110,mat);

    tris.emplace_back(p000,p011,p010,mat);
    tris.emplace_back(p000,p001,p011,mat);

    tris.emplace_back(p100,p110,p111,mat);
    tris.emplace_back(p100,p111,p101,mat);

    tris.emplace_back(p010,p011,p111,mat);
    tris.emplace_back(p010,p111,p110,mat);

    tris.emplace_back(p000,p100,p101,mat);
    tris.emplace_back(p000,p101,p001,mat);

    return std::make_unique<Mesh>(tris, pos);
}

static void makeTri(
    const Vector3D& A,
    const Vector3D& B,
    const Vector3D& C,
    const Material& mat,
    std::vector<Dreieck>& tris)
{
    Dreieck t(A, B, C, mat);

    // explizite Face-Normale
    Vector3D n = (B - A).cross(C - A).normalized();

    t.normale = n;
    t.nA = n;
    t.nB = n;
    t.nC = n;

    tris.push_back(t);
}


std::unique_ptr<object> createWall(
    const Vector3D& a,
    const Vector3D& b,
    const Vector3D& c,
    const Vector3D& d,
    const Material& mat)
{
    std::vector<Dreieck> tris;

    // ===== 1. Wandnormal berechnen =====
    Vector3D edge1 = b - a;
    Vector3D edge2 = d - a;
    Vector3D normal = edge1.cross(edge2).normalized();

    // ===== 2. Dicke der Wand =====
    double thickness = 0.1; 
    Vector3D offset = normal * thickness;

    // ===== 3. 8 Eckpunkte =====
    Vector3D a1 = a;
    Vector3D b1 = b;
    Vector3D c1 = c;
    Vector3D d1 = d;

    Vector3D a2 = a + offset;
    Vector3D b2 = b + offset;
    Vector3D c2 = c + offset;
    Vector3D d2 = d + offset;

    // =========================================================
    // FRONT (Originalfläche)
    // =========================================================
    makeTri(a1, b1, c1, mat, tris);
    makeTri(a1, c1, d1, mat, tris);

    // =========================================================
    // BACK (Offset-Fläche)
    // ✅ bewusst gleiche Winding (Normal zeigt automatisch korrekt)
    // =========================================================
    makeTri(a2, c2, b2, mat, tris);
    makeTri(a2, d2, c2, mat, tris);

    // =========================================================
    // SEITENFLÄCHEN
    // =========================================================

    // AB
    makeTri(a1, b1, b2, mat, tris);
    makeTri(a1, b2, a2, mat, tris);

    // BC
    makeTri(b1, c1, c2, mat, tris);
    makeTri(b1, c2, b2, mat, tris);

    // CD
    makeTri(c1, d1, d2, mat, tris);
    makeTri(c1, d2, c2, mat, tris);

    // DA
    makeTri(d1, a1, a2, mat, tris);
    makeTri(d1, a2, d2, mat, tris); 

    // ===== Mesh zurückgeben =====
    return std::make_unique<Mesh>(tris, Vector3D(0,0,0));
}

