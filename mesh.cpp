#include "mesh.h"
#include "BVHNode.h"
#include "bvh.h"
#include <fstream>
#include <sstream>
#include "PerlinNoise.h"

Mesh::Mesh(const Material& m)
    : object(m)
{
    bvhRoot = nullptr;
}

Mesh::Mesh(const std::vector<Dreieck>& tris, const Vector3D& pos)
    : object(Material())  // oder ein echtes Material!
{
    bvhRoot = nullptr;
    triangles = tris;
    position = pos;
    buildBVH();
}

Mesh::~Mesh()
{
    delete bvhRoot;
}

void Mesh::intersect(const Ray& ray, Hit& hit) const
{
    if (bvhRoot)
        bvhRoot->intersect(ray, hit);
    else
        for (const auto& tri : triangles)
            tri.intersect(ray, hit);
}

void Mesh::drawWireframePixels(Wireframe& wf, DrawMode mode) const {
    for (const auto& tri : triangles)
        tri.drawWireframePixels(wf, mode);
}

void Mesh::drawFlat(Wireframe& wf, DrawMode mode) const {
    for (const auto& tri : triangles)
        tri.drawFlat(wf, mode);
}

void Mesh::setPosition(const Vector3D& pos)
{
    position = pos;
}

void Mesh::buildBVH()
{
    bvhObjects.clear();
    bvhObjects.reserve(triangles.size());

    for (auto& tri : triangles)
        bvhObjects.push_back(&tri);

    bvhRoot = ::buildBVH(bvhObjects);
}

bool Mesh::loadOBJ(const std::string& filename) {
    std::ifstream file(filename);
    if(!file.is_open()) return false;

    std::vector<Vector3D> vertices;
    std::vector<Vector3D> normals;

    std::string line;
    while(std::getline(file, line)) {
        std::istringstream iss(line);
        std::string type;
        iss >> type;

        if(type == "v") { // Vertex
            double x, y, z;
            iss >> x >> y >> z;
            vertices.push_back(Vector3D(x, y, z));
        } else if(type == "vn") { // Vertex Normal
            double nx, ny, nz;
            iss >> nx >> ny >> nz;
            normals.push_back(Vector3D(nx, ny, nz).normalized());
        } else if(type == "f") { // Face
            std::string s1, s2, s3;
            iss >> s1 >> s2 >> s3;

            auto parseFace = [](const std::string& s, int& vi, int& ni) {
                size_t pos1 = s.find("//"); // Format v//vn
                if(pos1 != std::string::npos) {
                    vi = std::stoi(s.substr(0,pos1)) - 1;
                    ni = std::stoi(s.substr(pos1+2)) - 1;
                } else {
                    vi = std::stoi(s) - 1;
                    ni = -1; // Keine Normale
                }
            };

            int i1, i2, i3; // Vertex Indices
            int n1, n2, n3; // Normal Indices
            parseFace(s1,i1,n1);
            parseFace(s2,i2,n2);
            parseFace(s3,i3,n3);

            // Dreieck erstellen
            Dreieck tri(vertices[i1], vertices[i2], vertices[i3], mat);

            // Normale übernehmen (wenn vorhanden)
            if(n1>=0 && n2>=0 && n3>=0) {
                // Smooth shading: Durchschnitt der Vertex-Normals
                tri.normale = (normals[n1] + normals[n2] + normals[n3]).normalized();
            } else {
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
    if(!triangles.empty()) {
        minBound = triangles[0].minBound;
        maxBound = triangles[0].maxBound;

        for(const auto& tri : triangles) {
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

void Mesh::translate(const Vector3D& delta)
{
    for (auto& tri : triangles)
    {
        tri.a += delta;
        tri.b += delta;
        tri.c += delta;
    }

    position += delta;

    this->buildBVH();
}


void Mesh::getWorldAABB(Vector3D& minOut, Vector3D& maxOut) const
{
    if (triangles.empty()) return;

    minOut = triangles[0].minBound;
    maxOut = triangles[0].maxBound;

    for (const auto& tri : triangles)
    {
        minOut.x = std::min(minOut.x, tri.minBound.x);
        minOut.y = std::min(minOut.y, tri.minBound.y);
        minOut.z = std::min(minOut.z, tri.minBound.z);

        maxOut.x = std::max(maxOut.x, tri.maxBound.x);
        maxOut.y = std::max(maxOut.y, tri.maxBound.y);
        maxOut.z = std::max(maxOut.z, tri.maxBound.z);
    }

    minOut += position;
    maxOut += position;
}

void Mesh::setTexture(std::shared_ptr<Texture> tex) 
{
    texture = tex;

    for (auto& tri : triangles)
        tri.setTexture(tex);
}

void Mesh::setTextureMode(TextureMode mode) 
{
    textureMode = mode;

    for (auto& tri : triangles)
        tri.setTextureMode(mode);
}

void Mesh::generateTerrain(int gridResolution,
                           double sizeXZ,
                           double heightScale,
                           int octaves)
{
    // BVH vorher löschen (falls Mesh schon existiert)
    delete bvhRoot;
    bvhRoot = nullptr;

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

            Vector3D p0(x * step,         h00, z * step);
            Vector3D p1((x + 1) * step,   h10, z * step);
            Vector3D p2(x * step,         h01, (z + 1) * step);
            Vector3D p3((x + 1) * step,   h11, (z + 1) * step);

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

        for (const auto& t : triangles)
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