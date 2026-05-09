#include "mesh_importer.h"
#include "mesh.h"
#include "dreieck.h"
#include "material.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <map>
#include <vector>
#include <memory>
#include <algorithm>
#include "scene.h"
#include <filesystem>
#include <future>

std::string currentMatName;

// --- Hilfsstruktur für Material ---
struct MaterialInfo {
    Farbe ambient = Farbe(0,0,0);
    Farbe diffuse = Farbe(1,1,1);
    Farbe specular = Farbe(0,0,0);
    double shininess = 0.0;
    double alpha = 1.0; // (1 = opak)
    double IndexOfRefraction = 1.0;
    bool hasIOR = false;
};

#include <charconv>

inline void skipSpaces(const char*& p) {
    while (*p == ' ' || *p == '\t') ++p;
}

inline double parsedouble(const char*& p) {
    double value = 0.0;
    auto res = std::from_chars(p, p + 64, value);
    p = res.ptr;
    return value;
}

inline int parseInt(const char*& p) {
    int value = 0;
    auto res = std::from_chars(p, p + 32, value);
    p = res.ptr;
    return value;
}

inline void trimRange(const char*& start, const char*& end) {
    while (start < end && (*start == ' ' || *start == '\t')) ++start;
    while (end > start && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r')) --end;
}

inline int fixIndex(int idx, int size) {
    return (idx < 0) ? size + idx : idx - 1;
}

// --- MTL Loader ---
std::map<std::string, MaterialInfo> loadMTL(const std::string& filename) 
{
    std::map<std::string, MaterialInfo> materials;
    std::ifstream file(filename);
    if(!file.is_open()) {
        std::cerr << "MTL-Datei " << filename << " konnte nicht geoeffnet werden.\n";
        std::cerr << "MTL-Datei " << filename << "\n";

        return materials;
    }

    std::string line, currentMat;
    MaterialInfo mat;

    while(std::getline(file, line)) {
        std::istringstream iss(line);
        std::string token;
        iss >> token;
        if(token == "newmtl") {
            if(!currentMat.empty()) materials[currentMat] = mat;
            iss >> currentMat;
            mat = MaterialInfo();
        } else if(token == "Kd") {
            double r,g,b; iss >> r >> g >> b;
            mat.diffuse = Farbe(r,g,b);
        } else if(token == "Ka") {
            double r,g,b; iss >> r >> g >> b;
            mat.ambient = Farbe(r,g,b);
        } else if(token == "Ks") {
            double r,g,b; iss >> r >> g >> b;
            mat.specular = Farbe(r,g,b);
        } else if(token == "Ns") {
            double s; iss >> s;
            mat.shininess = s;
        } else if(token == "d") {
            iss >> mat.alpha;
        } else if(token == "Tr") {
            iss >> mat.alpha;
            mat.alpha = 1.0 - mat.alpha; 
        } else if(token == "Ni") {
            iss >> mat.IndexOfRefraction;
            mat.hasIOR = true;
        }
    }

    if(!currentMat.empty()) materials[currentMat] = mat;
    return materials;
}

// --- OBJ Loader ---
std::unique_ptr<Mesh> MeshImporter::importOBJ(const std::string& filename, const Material& defaultMat) {
    auto mesh = std::make_unique<Mesh>(defaultMat);

    std::ifstream file(filename);
    if(!file.is_open()) {
        std::cerr << "OBJ-Datei " << filename << " konnte nicht geöffnet werden.\n";
        return nullptr;
    }

    std::vector<Vector3D> vertices;
    std::vector<Vector3D> normals;
    std::map<std::string, MaterialInfo> materials;
    MaterialInfo currentMat;
    
    std::string line;
    while(std::getline(file, line)) {
        std::istringstream iss(line);
        std::string type;
        iss >> type;

        if(type == "v") { // Vertex
            double x,y,z;
            iss >> x >> y >> z;
            vertices.emplace_back(x,y,z);
        } else if(type == "vn") { // Vertex normal
            double nx,ny,nz;
            iss >> nx >> ny >> nz;
            normals.emplace_back(nx,ny,nz);
        } else if(type == "mtllib") { // Material-Datei
            std::string mtlFile;
            iss >> mtlFile;
            //auto mtlMaterials = loadMTL(mtlFile);
            std::filesystem::path objPath(filename);
            std::filesystem::path objDir = objPath.parent_path();
            std::filesystem::path fullMTLPath = objDir / mtlFile;
            auto mtlMaterials = loadMTL(fullMTLPath.string());
            materials.insert(mtlMaterials.begin(), mtlMaterials.end());
        } else if(type == "usemtl") { // Material für Faces
            std::string matName;
            iss >> matName;
            if(materials.count(matName)) currentMat = materials[matName];
        } else if(type == "f") { // Face
            std::string s1,s2,s3;
            iss >> s1 >> s2 >> s3;

            auto parseFace = [](const std::string& s, int& vi, int& ni){
                size_t pos = s.find("//"); // v//vn
                if(pos != std::string::npos){
                    vi = std::stoi(s.substr(0,pos)) - 1;
                    ni = std::stoi(s.substr(pos+2)) - 1;
                } else {
                    vi = std::stoi(s) - 1;
                    ni = -1;
                }
            };

            int i1,i2,i3,n1,n2,n3;
            parseFace(s1,i1,n1);
            parseFace(s2,i2,n2);
            parseFace(s3,i3,n3);

            Material matHere(defaultMat);
            if(!materials.empty()){
                //matHere = makeMaterialSimple(currentMat.diffuse);
                double transparency = 1.0 - currentMat.alpha;

                double IndexOfRefraction = 0.0;
                if(currentMat.hasIOR) {
                    IndexOfRefraction = currentMat.IndexOfRefraction;
                } else if(transparency > 0.0) {
                    IndexOfRefraction = 1.5;
                }

                matHere = Material(
                    currentMat.ambient,
                    currentMat.diffuse,
                    currentMat.specular,
                    0.2,                      // kA
                    0.8,                      // kD
                    currentMat.shininess,     // kS
                    0.0,                      // reflection
                    IndexOfRefraction,
                    transparency,
                    Farbe(0,0,0)              // absorption optional
                );
            }

            Dreieck tri(vertices[i1], vertices[i2], vertices[i3], matHere);

            // Normale setzen
            if(n1>=0 && n2>=0 && n3>=0){
                tri.normale = (normals[n1] + normals[n2] + normals[n3]).normalized();
            } else {
                Vector3D e1 = vertices[i2]-vertices[i1];
                Vector3D e2 = vertices[i3]-vertices[i1];
                tri.normale = e1.cross(e2).normalized();
            }

            mesh->triangles.push_back(tri);
        }
    }

    // Bounding-Box & Sphere berechnen
    if(!mesh->triangles.empty()) {
        mesh->minBound = mesh->triangles[0].minBound;
        mesh->maxBound = mesh->triangles[0].maxBound;

        for(const auto& tri : mesh->triangles){
            mesh->minBound.x = std::min(mesh->minBound.x, tri.minBound.x);
            mesh->minBound.y = std::min(mesh->minBound.y, tri.minBound.y);
            mesh->minBound.z = std::min(mesh->minBound.z, tri.minBound.z);

            mesh->maxBound.x = std::max(mesh->maxBound.x, tri.maxBound.x);
            mesh->maxBound.y = std::max(mesh->maxBound.y, tri.maxBound.y);
            mesh->maxBound.z = std::max(mesh->maxBound.z, tri.maxBound.z);
        }

        mesh->boundingCenter = 0.5 * (mesh->minBound + mesh->maxBound);
        mesh->boundingRadius = (mesh->maxBound - mesh->boundingCenter).length();
    }

    mesh->buildBVH();

    return mesh;
}

std::unique_ptr<Mesh> MeshImporter::importOBJ_fast(
    const std::string& filename,
    const Material& defaultMat)
{
    auto mesh = std::make_unique<Mesh>(defaultMat);

    std::ifstream file(filename, std::ios::binary);
    if (!file) {
        std::cerr << "OBJ konnte nicht geöffnet werden\n";
        return nullptr;
    }

    // Datei komplett einlesen (viel schneller als getline)
    std::string data((std::istreambuf_iterator<char>(file)),
                      std::istreambuf_iterator<char>());

    const char* ptr = data.c_str();
    const char* end = ptr + data.size();

    std::vector<Vector3D> vertices;
    std::vector<Vector3D> normals;
    std::vector<Vector2D> uvs;
    vertices.reserve(500000);
    normals.reserve(500000);
    uvs.reserve(500000);
    mesh->triangles.reserve(1000000);

    std::unordered_map<std::string_view, MaterialInfo> materials;
    MaterialInfo currentMat;

    std::filesystem::path objDir = std::filesystem::path(filename).parent_path();

    while (ptr < end) {
        // nächste Zeile
        const char* lineStart = ptr;

        while (ptr < end && *ptr != '\n' && *ptr != '\r') ++ptr;
        const char* lineEnd = ptr;

        // CRLF sauber überspringen
        if (ptr < end && *ptr == '\r') ++ptr;
        if (ptr < end && *ptr == '\n') ++ptr;
        if (lineEnd <= lineStart) continue;

        const char* p = lineStart;

        // Kommentar abschneiden
        const char* comment = std::find(p, lineEnd, '#');
        const char* effectiveEnd = (comment != lineEnd) ? comment : lineEnd;
        trimRange(p, effectiveEnd);

        // --- Vertex ---
        if (p[0] == 'v' && p[1] == ' ') {
            p += 2;
            skipSpaces(p);
            double x = parsedouble(p);
            skipSpaces(p);
            double y = parsedouble(p);
            skipSpaces(p);
            double z = parsedouble(p);
            vertices.emplace_back(x, y, z);
        }

        // --- Normal ---
        else if (p[0] == 'v' && p[1] == 'n') {
            p += 3;
            skipSpaces(p);
            double x = parsedouble(p);
            skipSpaces(p);
            double y = parsedouble(p);
            skipSpaces(p);
            double z = parsedouble(p);
            normals.emplace_back(x, y, z);
        }

        // --- Texture Coord (UV) ---
        else if (p[0] == 'v' && p[1] == 't') {
            p += 3;
            skipSpaces(p);

            double u = parsedouble(p);
            skipSpaces(p);

            double v = parsedouble(p);

            uvs.emplace_back(u, v);
        }

        // --- mtllib ---
        else if ((effectiveEnd - p) >= 6 &&
         memcmp(p, "mtllib", 6) == 0 &&
         (p[6] == ' ' || p[6] == '\t'))
        {
            p += 6;
            skipSpaces(p);

            const char* nameStart = p;
            const char* nameEnd = effectiveEnd;

            // trim rechts
            while (nameEnd > nameStart &&
                (nameEnd[-1] == ' ' || nameEnd[-1] == '\t' || nameEnd[-1] == '\r'))
                --nameEnd;

            std::string mtlFile(nameStart, nameEnd - nameStart);

            // Slash normalisieren
            std::replace(mtlFile.begin(), mtlFile.end(), '\\', '/');

            auto full = (objDir / mtlFile).string();
            auto mtl = loadMTL(full);
            currentMatName = full;

            materials.insert(mtl.begin(), mtl.end());
        }

        // --- usemtl ---
        else if (strncmp(p, "usemtl", 6) == 0) {
            p += 6;
            skipSpaces(p);

            const char* nameStart = p;
            const char* nameEnd = effectiveEnd;

            // trim (rechts)
            while (nameEnd > nameStart &&
                (nameEnd[-1] == ' ' || nameEnd[-1] == '\t' || nameEnd[-1] == '\r'))
                --nameEnd;

            std::string_view name(nameStart, nameEnd - nameStart);

            auto it = materials.find(name);
            if (it != materials.end())
                currentMat = it->second;
        }

        // --- Face ---
        else if (p[0] == 'f' && p[1] == ' ') {
            p += 2;

            std::vector<int> vIdx;
            std::vector<int> tIdx; 
            std::vector<int> nIdx;

            while (p < effectiveEnd) {
                skipSpaces(p);
                if (p >= effectiveEnd) break;

                int vi_raw = parseInt(p);
                int vi = fixIndex(vi_raw, vertices.size());
                int ti_raw = -1;
                int ti = -1; 
                int ni_raw = -1;
                int ni = -1;

                if (*p == '/') {
                    ++p;

                    // --- TEXTURE INDEX ---
                    if (*p != '/') {
                        ti_raw = parseInt(p);
                        //ti = fixIndex(ti_raw, vertices.size());
                        ti = fixIndex(ti_raw, uvs.size());
                    }

                    // --- NORMAL INDEX ---
                    if (*p == '/') {
                        ++p;
                        ni_raw = parseInt(p);
                        //ni = fixIndex(ni_raw, vertices.size());
                        ni = fixIndex(ni_raw, normals.size());
                    }
                }

                vIdx.push_back(vi);
                tIdx.push_back(ti); 
                nIdx.push_back(ni);
            }

            // 🔺 TRIANGULIERUNG
            for (size_t i = 1; i + 1 < vIdx.size(); ++i) {

                // --- Indizes für das Dreieck ---
                int vi[3] = { vIdx[0], vIdx[i], vIdx[i+1] };
                int ti[3] = { tIdx[0], tIdx[i], tIdx[i+1] };
                int ni[3] = { nIdx[0], nIdx[i], nIdx[i+1] };

                // --- Vertex-Bounds prüfen ---
                if (vi[0] < 0 || vi[1] < 0 || vi[2] < 0) continue;
                if (vi[0] >= (int)vertices.size() ||
                    vi[1] >= (int)vertices.size() ||
                    vi[2] >= (int)vertices.size())
                    continue;

                // --- Material ---
                double transparency = 1.0 - currentMat.alpha;
                double IndexOfRefraction = currentMat.hasIOR ? currentMat.IndexOfRefraction : (transparency > 0.0 ? 1.5 : 0.0);

                Material matHere(
                    currentMat.ambient,
                    currentMat.diffuse,
                    currentMat.specular,
                    0.2,
                    0.8,
                    currentMat.shininess,
                    0.0,
                    IndexOfRefraction,
                    transparency,
                    Farbe(0,0,0)
                );

                // --- Eckpunkte ---
                Vector3D A = vertices[vi[0]];
                Vector3D B = vertices[vi[1]];
                Vector3D C = vertices[vi[2]];

                // ============================================================
                // ✅ ROBUSTE NORMALENLOGIK
                // ============================================================

                // 1) Face-Normale aus Geometrie (Flat Shading)
                Vector3D faceN = (B - A).cross(C - A).normalized();

                // Referenz-Normale aus OBJ
                Vector3D refN(0,0,0);
                int refCount = 0;
                for (int k = 0; k < 3; ++k) {
                    if (ni[k] >= 0 && ni[k] < normals.size()) {
                        refN += normals[ni[k]];
                        ++refCount;
                    }
                }

                if (refCount > 0) {
                    refN = refN.normalized();
                    if (faceN * refN < 0.0) {
                        std::swap(B, C);   // ✅ VOR Konstruktor
                        faceN = -faceN;
                    }
                }

                Dreieck tri(A, B, C, matHere);
                tri.normale = faceN;

                // Vertex-Normalen aus OBJ (falls vorhanden)
                if (refCount == 3) {
                    tri.nA = normals[ni[0]].normalized();
                    tri.nB = normals[ni[1]].normalized();
                    tri.nC = normals[ni[2]].normalized();
                } else {
                    // Fallback: Flat Shading
                    tri.nA = tri.nB = tri.nC = faceN;
                }


                // ============================================================
                // ✅ UVs (optional)
                // ============================================================
                if (ti[0] >= 0 && ti[1] >= 0 && ti[2] >= 0 &&
                    ti[0] < (int)uvs.size() &&
                    ti[1] < (int)uvs.size() &&
                    ti[2] < (int)uvs.size())
                {
                    tri.uvA = uvs[ti[0]];
                    tri.uvB = uvs[ti[1]];
                    tri.uvC = uvs[ti[2]];
                }


                // ============================================================
                // ✅ Dreieck übernehmen
                // ============================================================
                mesh->triangles.push_back(tri);
            }
        }
    }

    // --- Bounding Box ---
    if (!mesh->triangles.empty()) {
        mesh->minBound = mesh->triangles[0].minBound;
        mesh->maxBound = mesh->triangles[0].maxBound;

        for (const auto& t : mesh->triangles) {
            mesh->minBound.x = std::min(mesh->minBound.x, t.minBound.x);
            mesh->minBound.y = std::min(mesh->minBound.y, t.minBound.y);
            mesh->minBound.z = std::min(mesh->minBound.z, t.minBound.z);

            mesh->maxBound.x = std::max(mesh->maxBound.x, t.maxBound.x);
            mesh->maxBound.y = std::max(mesh->maxBound.y, t.maxBound.y);
            mesh->maxBound.z = std::max(mesh->maxBound.z, t.maxBound.z);
        }

        mesh->boundingCenter = 0.5 * (mesh->minBound + mesh->maxBound);
        mesh->boundingRadius = (mesh->maxBound - mesh->boundingCenter).length();
    }

    // BVH
    //auto future = std::async(std::launch::async, [&]{
        mesh->buildBVH();
    //});
    //future.wait(); 

    return mesh;
}
