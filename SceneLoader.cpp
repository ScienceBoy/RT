#include "SceneLoader.h"
#include "camera.h"
#include "light.h"
#include "kugel.h"
#include "wand.h"
#include "stab.h"
#include "material_library.h"
#include "mesh_importer.h"
#include "scene.h"
#include "SimpleXML.h"
#include "mesh_factory.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <random>
#include <vector>
#include <cmath>

extern std::vector<std::unique_ptr<object>> scene;
extern std::vector<light> lights;
extern std::vector<Camera> cam;
extern Vector3D sceneMin, sceneMax;
extern Kugel* jumpingSphere;

std::unordered_map<std::string, Material> SceneLoader::materialMap;

/*void computeSceneBounds()
{
    if(scene.empty()) return;

    sceneMin = scene[0]->minBound;
    sceneMax = scene[0]->maxBound;

    for(const auto& obj : scene)
    {
        sceneMin.x = std::min(sceneMin.x, obj->minBound.x);
        sceneMin.y = std::min(sceneMin.y, obj->minBound.y);
        sceneMin.z = std::min(sceneMin.z, obj->minBound.z);

        sceneMax.x = std::max(sceneMax.x, obj->maxBound.x);
        sceneMax.y = std::max(sceneMax.y, obj->maxBound.y);
        sceneMax.z = std::max(sceneMax.z, obj->maxBound.z);
    }

    std::cout << "Szene Breite: " << sceneMin.x << " - " << sceneMax.x << "\n";
    std::cout << "Szene Hoehe: " << sceneMin.y << " - " << sceneMax.y<< "\n";
    std::cout << "Szene Tiefe: " << sceneMin.z << " - " << sceneMax.z << "\n";
    std::cout << "Szene Anzahl Objekte: " << scene.size() << "\n";
    
    int triangleCount = 0;

    for (const auto& obj : scene)
    {
        triangleCount += obj->getTriangleCount();
    }

    std::cout << "Anzahl Dreiecke: " << triangleCount << "\n";
}*/

MaterialType SceneLoader::stringToMaterialType(const std::string& s)
{
    if (s == "mirror") return MaterialType::Mirror;
    if (s == "gold") return MaterialType::Gold;
    if (s == "glass") return MaterialType::Glass;
    if (s == "bluePlastic") return MaterialType::BluePlastic;
    if (s == "greenGlass") return MaterialType::GreenGlass;

    return MaterialType::BluePlastic;
    //throw std::runtime_error("Unknown material: " + s);
}

std::string SceneLoader::loadFile(const std::string& path)
{
    std::ifstream file(path);
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

void SceneLoader::loadSceneFromXML(const std::string& path, Fenster& f)
{
    std::string xml = loadFile(path);
    if (xml.empty())
        throw std::runtime_error("XML file is empty or not found");

    auto applyTexture = [&](auto& obj, auto& node)
    {
        auto texIt = node->attr.find("texture");
        if (texIt != node->attr.end())
        {
            obj->setTexture(
                TextureManager::get().getTexture(texIt->second)
            );
            obj->textureName = texIt->second;
        }

        auto modeIt = node->attr.find("textureMode");
        if (modeIt != node->attr.end())
        {
            obj->setTextureMode(parseTextureMode(modeIt->second));
        }
    };

    auto root = SimpleXML::parse(xml);

    if (!root || root->name != "scene")
        throw std::runtime_error("Invalid XML root");

    scene.clear();
    lights.clear();
    cam.clear();

    // === FIND NODES BY NAME ===
    XMLNode* materialsNode = findNode(root.get(), "materials");
    XMLNode* camNode       = findNode(root.get(), "camera");
    XMLNode* lightsNode    = findNode(root.get(), "lights");
    XMLNode* objNode       = findNode(root.get(), "objects");

    // === MATERIALS FIRST ===
    loadMaterials(materialsNode);

    // === CAMERA ===
    {
        auto pos = camNode->children[0].get();
        auto tgt = camNode->children[1].get();

        cam.push_back(Camera(
            Vector3D(std::stod(pos->attr["x"]),
                     std::stod(pos->attr["y"]),
                     std::stod(pos->attr["z"])),
            Vector3D(std::stod(tgt->attr["x"]),
                     std::stod(tgt->attr["y"]),
                     std::stod(tgt->attr["z"])),
            std::stod(camNode->children[2]->text),
            std::stod(camNode->children[3]->text)
        ));
    }

    // === LIGHTS ===
    for (auto& l : lightsNode->children)
    {
        auto p = l->children[0].get();
        auto c = l->children[1].get();

        lights.emplace_back(light(
            Vector3D(std::stod(p->attr["x"]),
                     std::stod(p->attr["y"]),
                     std::stod(p->attr["z"])),
            Farbe(std::stod(c->attr["r"]),
                  std::stod(c->attr["g"]),
                  std::stod(c->attr["b"]))
        ));
    }

    // === OBJECTS ===
    for (auto& o : objNode->children)
    {
        std::string type = o->name;

        Material mat;
        auto matIt = materialMap.find(o->attr["material"]);
        if (matIt == materialMap.end())
            //throw std::runtime_error("Unknown material: " + o->attr["material"]);
            mat = MaterialLibrary::get(MaterialType::Simple);
        else
            mat = matIt->second;

        // =========================
        // SPHERE
        // =========================
        if (type == "SPHERE")
        {
            auto p = o->children[0].get();
            double r = std::stod(o->attr["radius"]);

            auto obj = std::make_unique<Kugel>(
                Vector3D(std::stod(p->attr["x"]),
                        std::stod(p->attr["y"]),
                        std::stod(p->attr["z"])),
                r,
                mat
            );

            obj->materialName = o->attr["material"];
            applyTexture(obj, o);

            scene.push_back(std::move(obj));
        }
        
        // 100'000 gleiche Kugeln (für Nebel benutzt)
        /*if (type == "SPHERE")
        {
            // ---- Vorlage auslesen ----
            auto p = o->children[0].get();
            double radius = std::stod(o->attr["radius"]);
            std::string materialName = o->attr["material"];

            const int NUM_SPHERES = 100000;
            const double MIN_DIST = 2.0 * radius;
            const double COORD_MIN = -10.0;
            const double COORD_MAX =  10.0;

            // ---- Zufall ----
            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_real_distribution<double> dist(COORD_MIN, COORD_MAX);

            // gespeicherte Positionen zur Kollisionsprüfung
            std::vector<Vector3D> placedCenters;
            placedCenters.reserve(NUM_SPHERES);

            for (int i = 0; i < NUM_SPHERES; ++i)
            {
                bool placed = false;
                const int MAX_TRIES = 10'000;

                for (int attempt = 0; attempt < MAX_TRIES && !placed; ++attempt)
                {
                    Vector3D candidate(
                        dist(gen),
                        dist(gen),
                        dist(gen)
                    );

                    bool overlap = false;
                    //for (const auto& c : placedCenters)
                    //{
                    //    if (candidate.distance(c) < MIN_DIST)
                    //    {
                    //        overlap = true;
                    //        break;
                    //    }
                    //}

                    if (!overlap)
                    {
                        auto obj = std::make_unique<Kugel>(
                            candidate,
                            radius,
                            mat
                        );

                        obj->materialName = materialName;
                        applyTexture(obj, o);

                        scene.push_back(std::move(obj));
                        placedCenters.push_back(candidate);

                        placed = true;
                    }
                }

                if (!placed)
                {
                    // Sicherheitsabbruch falls der Raum zu voll wird
                    break;
                }
            }
        }*/

        // =========================
        // CUBE
        // =========================
        else if (type == "cube")
        {
            auto p = o->children[0].get();
            double size = std::stod(o->attr["size"]);

            auto obj = createCubeMesh(
                Vector3D(std::stod(p->attr["x"]),
                        std::stod(p->attr["y"]),
                        std::stod(p->attr["z"])),
                size,
                mat
            );

            applyTexture(obj, o);

            scene.push_back(std::move(obj));
        }

        // =========================
        // CYLINDER
        // =========================
        else if (type == "cylinder")
        {
            auto s = o->children[0].get();
            auto e = o->children[1].get();

            auto obj = std::make_unique<Stab>(
                Vector3D(std::stod(s->attr["x"]),
                        std::stod(s->attr["y"]),
                        std::stod(s->attr["z"])),
                Vector3D(std::stod(e->attr["x"]),
                        std::stod(e->attr["y"]),
                        std::stod(e->attr["z"])),
                std::stod(o->attr["radius"]),
                mat
            );

            obj->materialName = o->attr["material"];
            applyTexture(obj, o);

            scene.push_back(std::move(obj));
        }

        // =========================
        // MESH
        // =========================
        else if (type == "mesh")
        {
            std::string file = o->attr["file"];
            auto obj = MeshImporter::importOBJ_fast(file, mat);
            obj->materialName = file;

            applyTexture(obj, o);

            scene.push_back(std::move(obj));
        }

        // =========================
        // WALL
        // =========================
        else if (type == "wall")
        {
            auto obj = createWall(
                Vector3D(std::stod(o->children[0]->attr["x"]),
                        std::stod(o->children[0]->attr["y"]),
                        std::stod(o->children[0]->attr["z"])),
                Vector3D(std::stod(o->children[1]->attr["x"]),
                        std::stod(o->children[1]->attr["y"]),
                        std::stod(o->children[1]->attr["z"])),
                Vector3D(std::stod(o->children[2]->attr["x"]),
                        std::stod(o->children[2]->attr["y"]),
                        std::stod(o->children[2]->attr["z"])),
                Vector3D(std::stod(o->children[3]->attr["x"]),
                        std::stod(o->children[3]->attr["y"]),
                        std::stod(o->children[3]->attr["z"])),
                mat
            );

            applyTexture(obj, o);

            scene.push_back(std::move(obj));
        }
    }

    computeSceneBounds();
    //addCoordinateSystem();
}

void SceneLoader::loadMaterials(XMLNode* materialsNode)
{
    materialMap.clear();

    for (auto& m : materialsNode->children)
    {
        std::string id = m->attr["id"];

        std::string type = "";
        if (m->attr.count("type"))
            type = m->attr["type"];

        // === CASE 1: Preset Material ===
        if (type.empty() || type == "preset")
        {
            materialMap[id] =
                MaterialLibrary::get(stringToMaterialType(id));
            std::cout << "id: " << id << "\n";
        }

        // === CASE 2: Custom Material ===
        else if (type == "simple")
        {
            auto c = m->children[0].get();

            Farbe color(
                std::stod(c->attr["r"]),
                std::stod(c->attr["g"]),
                std::stod(c->attr["b"])
            );

            materialMap[id] = makeMaterialSimple(color);
            std::cout << "id: " << id << "\n";
        }

        else
        {
            throw std::runtime_error("Unknown material type: " + type);
            std::cout << "id: " << id << "\n";

        }
    }
}

XMLNode* SceneLoader::findNode(XMLNode* root, const std::string& name)
{
    if (!root) return nullptr;

    if (root->name == name)
        return root;

    for (auto& c : root->children)
    {
        XMLNode* result = findNode(c.get(), name);
        if (result)
            return result;
    }

    return nullptr;
}

TextureMode SceneLoader::parseTextureMode(const std::string& m)
{
    if (m == "SPHERE") return TextureMode::SPHERE;
    if (m == "PLANAR") return TextureMode::PLANAR;
    if (m == "UV") return TextureMode::UV;
    return TextureMode::None;
}