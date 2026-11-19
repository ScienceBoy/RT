#include "mesh.h"
#include "textur_abbildung.h"
#include "BVHNode.h"
#include "bvh.h"
#include <fstream>
#include <sstream>
#include "PerlinNoise.h"
#include "rotations.h"
#include <algorithm>
#include <limits>

Mesh::Mesh(const Material &m)
    : object(m)
{
    bvhRoot = nullptr;
}

Mesh::Mesh(const std::vector<Dreieck> &tris, const Vector3D &pos)
    : object(Material()) // oder ein echtes Material!
{
    bvhRoot = nullptr;
    triangles = tris;
    position = pos;
    recomputeMeshBoundsFromTriangles(); // vorher blieben minBound/maxBound 0 (Waende, Wuerfel)
    buildBVH();
}

Mesh::~Mesh()
{
    bvhFreigeben(bvhRoot);
}

static inline void updateTriDerived(Dreieck &tri)
{
    // AABB
    tri.minBound.x = std::min({tri.a.x, tri.b.x, tri.c.x});
    tri.minBound.y = std::min({tri.a.y, tri.b.y, tri.c.y});
    tri.minBound.z = std::min({tri.a.z, tri.b.z, tri.c.z});

    tri.maxBound.x = std::max({tri.a.x, tri.b.x, tri.c.x});
    tri.maxBound.y = std::max({tri.a.y, tri.b.y, tri.c.y});
    tri.maxBound.z = std::max({tri.a.z, tri.b.z, tri.c.z});

    // BVH-Split-Center (wichtig wenn bvh.h auf boundingCenter sortiert)
    tri.boundingCenter = 0.5 * (tri.minBound + tri.maxBound);

    // Normale (wichtig falls intersect/ shading cached normal nutzt)
    Vector3D e1 = tri.b - tri.a;
    Vector3D e2 = tri.c - tri.a;
    tri.geomNormale = e1.cross(e2).normalized();
    tri.normale = tri.geomNormale;

    tri.rebuild();
}

void Mesh::recomputeMeshBoundsFromTriangles()
{
    if (triangles.empty())
        return;

    minBound = Vector3D(std::numeric_limits<double>::infinity(),
                        std::numeric_limits<double>::infinity(),
                        std::numeric_limits<double>::infinity());

    maxBound = Vector3D(-std::numeric_limits<double>::infinity(),
                        -std::numeric_limits<double>::infinity(),
                        -std::numeric_limits<double>::infinity());

    for (auto &tri : triangles)
    {
        updateTriDerived(tri);

        minBound.x = std::min(minBound.x, tri.minBound.x);
        minBound.y = std::min(minBound.y, tri.minBound.y);
        minBound.z = std::min(minBound.z, tri.minBound.z);

        maxBound.x = std::max(maxBound.x, tri.maxBound.x);
        maxBound.y = std::max(maxBound.y, tri.maxBound.y);
        maxBound.z = std::max(maxBound.z, tri.maxBound.z);
    }

    boundingCenter = 0.5 * (minBound + maxBound);
    boundingRadius = (maxBound - boundingCenter).length();

    if (texture || textureMode != TextureMode::None)
        texturBoxSetzen(); // Textur geht mit dem Objekt mit
}

void Mesh::recomputeTriangleDataAndMeshBounds()
{
    if (triangles.empty())
        return;

    // Mesh bounds initialisieren
    minBound = Vector3D(1e30, 1e30, 1e30);
    maxBound = Vector3D(-1e30, -1e30, -1e30);

    for (auto &tri : triangles)
    {
        // 1) Triangle AABB neu
        tri.minBound.x = std::min({tri.a.x, tri.b.x, tri.c.x});
        tri.minBound.y = std::min({tri.a.y, tri.b.y, tri.c.y});
        tri.minBound.z = std::min({tri.a.z, tri.b.z, tri.c.z});

        tri.maxBound.x = std::max({tri.a.x, tri.b.x, tri.c.x});
        tri.maxBound.y = std::max({tri.a.y, tri.b.y, tri.c.y});
        tri.maxBound.z = std::max({tri.a.z, tri.b.z, tri.c.z});

        // 2) boundingCenter neu (BVH nutzt das zum Splitten!)
        tri.boundingCenter = 0.5 * (tri.minBound + tri.maxBound);

        // 3) Normale neu (Flat; robust nach rotate/scale)
        Vector3D e1 = tri.b - tri.a;
        Vector3D e2 = tri.c - tri.a;
        tri.geomNormale = e1.cross(e2).normalized();
        tri.normale = tri.geomNormale;

        // 4) Mesh AABB aggregieren
        minBound.x = std::min(minBound.x, tri.minBound.x);
        minBound.y = std::min(minBound.y, tri.minBound.y);
        minBound.z = std::min(minBound.z, tri.minBound.z);

        maxBound.x = std::max(maxBound.x, tri.maxBound.x);
        maxBound.y = std::max(maxBound.y, tri.maxBound.y);
        maxBound.z = std::max(maxBound.z, tri.maxBound.z);
    }

    // Optional: Mesh bounding sphere data updaten, falls du es nutzt
    boundingCenter = 0.5 * (minBound + maxBound);
    boundingRadius = (maxBound - boundingCenter).length();
}

void Mesh::intersect(const Ray &ray, Hit &hit) const
{
    if (bvhRoot)
        bvhRoot->intersect(ray, hit);
    else
        for (const auto &tri : triangles)
            tri.intersect(ray, hit);
}

void Mesh::drawWireframePixels(Wireframe &wf, DrawMode mode) const
{
    for (const auto &tri : triangles)
        tri.drawWireframePixels(wf, mode);
}

void Mesh::drawFlat(Wireframe &wf, DrawMode mode) const
{
    for (const auto &tri : triangles)
        tri.drawFlat(wf, mode);
}

void Mesh::setPosition(const Vector3D &pos)
{
    position = pos;
}

void Mesh::buildBVH()
{
    bvhFreigeben(bvhRoot); // alten Baum freigeben (vorher blieb er bei jedem translate/rotate/scale liegen)

    bvhObjects.clear();
    bvhObjects.reserve(triangles.size());

    for (auto &tri : triangles)
        bvhObjects.push_back(&tri);

    bvhRoot = ::buildBVH(bvhObjects);
}

bool Mesh::loadOBJ(const std::string &filename)
{
    std::ifstream file(filename);
    if (!file.is_open())
        return false;

    std::vector<Vector3D> vertices;
    std::vector<Vector3D> normals;

    std::string line;
    while (std::getline(file, line))
    {
        std::istringstream iss(line);
        std::string type;
        iss >> type;

        if (type == "v")
        { // Vertex
            double x, y, z;
            iss >> x >> y >> z;
            vertices.push_back(Vector3D(x, y, z));
        }
        else if (type == "vn")
        { // Vertex Normal
            double nx, ny, nz;
            iss >> nx >> ny >> nz;
            normals.push_back(Vector3D(nx, ny, nz).normalized());
        }
        else if (type == "f")
        { // Face
            std::string s1, s2, s3;
            iss >> s1 >> s2 >> s3;

            auto parseFace = [](const std::string &s, int &vi, int &ni)
            {
                size_t pos1 = s.find("//"); // Format v//vn
                if (pos1 != std::string::npos)
                {
                    vi = std::stoi(s.substr(0, pos1)) - 1;
                    ni = std::stoi(s.substr(pos1 + 2)) - 1;
                }
                else
                {
                    vi = std::stoi(s) - 1;
                    ni = -1; // Keine Normale
                }
            };

            int i1, i2, i3; // Vertex Indices
            int n1, n2, n3; // Normal Indices
            parseFace(s1, i1, n1);
            parseFace(s2, i2, n2);
            parseFace(s3, i3, n3);

            // Dreieck erstellen
            Dreieck tri(vertices[i1], vertices[i2], vertices[i3], mat);

            // Normale übernehmen (wenn vorhanden)
            if (n1 >= 0 && n2 >= 0 && n3 >= 0)
            {
                // Smooth shading: Durchschnitt der Vertex-Normals
                tri.normale = (normals[n1] + normals[n2] + normals[n3]).normalized();
            }
            else
            {
                // Flat shading: aus den Punkten berechnen
                Vector3D edge1 = vertices[i2] - vertices[i1];
                Vector3D edge2 = vertices[i3] - vertices[i1];
                tri.normale = edge1.cross(edge2).normalized();
            }
            tri.geomNormale = tri.normale;
            triangles.push_back(tri);
        }
    }

    // Bounding-Box & Radius berechnen
    if (!triangles.empty())
    {
        minBound = triangles[0].minBound;
        maxBound = triangles[0].maxBound;

        for (const auto &tri : triangles)
        {
            minBound.x = std::min(minBound.x, tri.minBound.x);
            minBound.y = std::min(minBound.y, tri.minBound.y);
            minBound.z = std::min(minBound.z, tri.minBound.z);

            maxBound.x = std::max(maxBound.x, tri.maxBound.x);
            maxBound.y = std::max(maxBound.y, tri.maxBound.y);
            maxBound.z = std::max(maxBound.z, tri.maxBound.z);
        }

        minBound += position;
        maxBound += position;

        boundingCenter = 0.5 * (minBound + maxBound);
        boundingRadius = (maxBound - boundingCenter).length();
    }

    this->buildBVH();

    return true;
}

/*void Mesh::buildBVH()
{
    if (triangles.empty())
    {
        bvhRoot = nullptr;
        return;
    }

    std::vector<object*> objs;
    objs.reserve(triangles.size());

    for (auto& tri : triangles)
        objs.push_back(&tri);

    bvhRoot = buildBVH(objs);
}*/

void Mesh::rotate(const Vector3D &rotation)
{
    // Center aus aktueller Geometrie bestimmen (robust, auch wenn min/max inkonsistent sind)
    recomputeMeshBoundsFromTriangles();
    Vector3D center = 0.5 * (minBound + maxBound);

    double rx = rotation.x * M_PI / 180.0;
    double ry = rotation.y * M_PI / 180.0;
    double rz = rotation.z * M_PI / 180.0;

    auto rotP = [&](Vector3D &p)
    {
        p -= center;
        p = rotateX(p, rx);
        p = rotateY(p, ry);
        p = rotateZ(p, rz);
        p += center;
    };

    for (auto &tri : triangles)
    {
        rotP(tri.a);
        rotP(tri.b);
        rotP(tri.c);

        updateTriDerived(tri); // <<< WICHTIG: Dreieck sofort konsistent halten
    }

    // Mesh-Bounds + BVH
    recomputeMeshBoundsFromTriangles();
    buildBVH();
}

void Mesh::scale(const Vector3D &scaleVec)
{
    recomputeMeshBoundsFromTriangles();
    Vector3D center = 0.5 * (minBound + maxBound);

    auto sclP = [&](Vector3D &p)
    {
        p -= center;
        p.x *= scaleVec.x;
        p.y *= scaleVec.y;
        p.z *= scaleVec.z;
        p += center;
    };

    for (auto &tri : triangles)
    {
        sclP(tri.a);
        sclP(tri.b);
        sclP(tri.c);

        updateTriDerived(tri); // <<< WICHTIG
    }

    recomputeMeshBoundsFromTriangles();
    buildBVH();
}

void Mesh::translate(const Vector3D &delta)
{
    for (auto &tri : triangles)
    {
        tri.a += delta;
        tri.b += delta;
        tri.c += delta;
    }

    position += delta;

    recomputeTriangleDataAndMeshBounds();
    buildBVH();
}

void Mesh::getWorldAABB(Vector3D &minOut, Vector3D &maxOut) const
{
    if (triangles.empty())
        return;

    minOut = triangles[0].minBound;
    maxOut = triangles[0].maxBound;

    for (const auto &tri : triangles)
    {
        minOut.x = std::min(minOut.x, tri.minBound.x);
        minOut.y = std::min(minOut.y, tri.minBound.y);
        minOut.z = std::min(minOut.z, tri.minBound.z);

        maxOut.x = std::max(maxOut.x, tri.maxBound.x);
        maxOut.y = std::max(maxOut.y, tri.maxBound.y);
        maxOut.z = std::max(maxOut.z, tri.maxBound.z);
    }

    // Die Dreiecke liegen schon in Weltkoordinaten (translate verschiebt sie direkt).
    // Vorher kam hier noch "+= position" dazu: bei eingefuegten oder verschobenen Meshes war die
    // Box um position versetzt (Dreh-/Skalierpunkt, Fokus, Auswahlrahmen, Raytracing-Culling).
}

void Mesh::setTexture(std::shared_ptr<Texture> tex)
{
    texture = tex;

    for (auto &tri : triangles)
        tri.setTexture(tex);
    texturBoxSetzen();
}

void Mesh::setTextureMode(TextureMode mode)
{
    textureMode = mode;

    for (auto &tri : triangles)
        tri.setTextureMode(mode);
    texturBoxSetzen();
}

// Dreiecke bekommen die Box des ganzen Mesh als Bezug fuer die Textur (nach jedem Verschieben,
// Drehen, Skalieren neu, damit die Textur mit dem Objekt mitgeht)
// Textur haftet am Objekt (texturPar().objektfest): die Dreiecke behalten ihre Ruhelage (textur_abbildung.h);
// fehlt sie bei einem Dreieck (neue Textur, Netz-Operation), wird sie fuer alle aus der jetzigen Lage gesetzt.
// Sonst wie bisher: Bezug ist die Welt-Box des Netzes.
void Mesh::texturBoxSetzen()
{
    const bool fest = texturPar().objektfest && (texture || textureMode != TextureMode::None || texPar);
    if (fest)
    {
        bool alle = true;
        for (const auto &tri : triangles)
            alle = alle && tri.ruheGesetzt;
        if (!alle)
            texturRuheSetzen(triangles);
    }
    for (auto &tri : triangles)
    {
        if (!fest)
        {
            tri.ruheGesetzt = false;
            tri.texturBox = true;
            tri.texturMin = minBound;
            tri.texturMax = maxBound;
        }
        tri.texPar = texPar;
        tri.texturSpiegelU = texturSpiegelU;
        tri.texturSpiegelV = texturSpiegelV;
        tri.texturDrehung = texturDrehung;
    }
}

// Ruhelage verwerfen und aus der jetzigen Lage neu setzen ("Textur neu ausrichten", objektfest einschalten)
void Mesh::texturNeuFestmachen()
{
    for (auto &tri : triangles)
        tri.ruheGesetzt = false;
    texturBoxSetzen();
}

void Mesh::generateTerrain(int gridResolution,
                           double sizeXZ,
                           double heightScale,
                           int octaves)
{
    // BVH vorher löschen (falls Mesh schon existiert)
    bvhFreigeben(bvhRoot);

    // alte Geometrie löschen
    triangles.clear();

    const int quadCount = (gridResolution - 1) * (gridResolution - 1);
    triangles.reserve(quadCount * 2);

    PerlinNoise perlin(1337);

    double step = sizeXZ / (gridResolution - 1);
    const double noiseScale = 5.0;

    // 1. Heightmap (1D statt vector<vector>)
    std::vector<double> height;
    height.resize(gridResolution * gridResolution);

    auto idx = [gridResolution](int x, int z)
    {
        return z * gridResolution + x;
    };

    // 2. Noise generieren
    for (int z = 0; z < gridResolution; z++)
    {
        for (int x = 0; x < gridResolution; x++)
        {
            double nx = (double)x / (gridResolution - 1);
            double nz = (double)z / (gridResolution - 1);

            double h = fractalNoise(perlin,
                                    nx * noiseScale,
                                    nz * noiseScale,
                                    octaves);

            height[idx(x, z)] = h * heightScale;
        }
    }

    // 3. Mesh erzeugen
    for (int z = 0; z < gridResolution - 1; z++)
    {
        for (int x = 0; x < gridResolution - 1; x++)
        {
            double h00 = height[idx(x, z)];
            double h10 = height[idx(x + 1, z)];
            double h01 = height[idx(x, z + 1)];
            double h11 = height[idx(x + 1, z + 1)];

            Vector3D p0(x * step, h00, z * step);
            Vector3D p1((x + 1) * step, h10, z * step);
            Vector3D p2(x * step, h01, (z + 1) * step);
            Vector3D p3((x + 1) * step, h11, (z + 1) * step);

            // zwei Dreiecke pro Quad
            triangles.emplace_back(p0, p1, p2, mat);
            triangles.emplace_back(p2, p1, p3, mat);
        }
    }

    // 4. Bounding Box updaten
    if (!triangles.empty())
    {
        minBound = triangles[0].minBound;
        maxBound = triangles[0].maxBound;

        for (const auto &t : triangles)
        {
            minBound.x = std::min(minBound.x, t.minBound.x);
            minBound.y = std::min(minBound.y, t.minBound.y);
            minBound.z = std::min(minBound.z, t.minBound.z);

            maxBound.x = std::max(maxBound.x, t.maxBound.x);
            maxBound.y = std::max(maxBound.y, t.maxBound.y);
            maxBound.z = std::max(maxBound.z, t.maxBound.z);
        }

        boundingCenter = 0.5 * (minBound + maxBound);
        boundingRadius = (maxBound - boundingCenter).length();
    }

    // 5. BVH NUR EINMAL am Ende bauen
    buildBVH();
}