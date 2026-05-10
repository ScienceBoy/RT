#include "SimpleXML.h"
#include "SceneLoader.h"
#include "scene.h"
#include "kugel.h"
#include "dreieck.h"
#include "vector3d.h"
#include "farbe.h"
#include "fenster.h"
#include "wand.h"
#include "cube.h"
#include "pfeil.h"
#include "text3d.h"
#include "camera.h"
#include "material.h"
#include "light.h"
#include "stab.h"
#include "material_library.h"
#include "mesh_importer.h"
#include "mesh_factory.h"
#include "obj_import_dialog.h"
#include <memory>
#include <vector>
#include <cstdlib>
#include <iostream>
#include "TextureManager.h"

// Definition der globalen Variablen
std::vector<std::unique_ptr<object>> scene;
std::vector<light> lights;
std::vector<Camera> cam;
Kugel* jumpingSphere = nullptr;

bool nebelVorhanden = false;
double fogDensity = 0.015;
bool nebelBeleuchtet = true;
Farbe fogColor      = Farbe(0.05, 0.05, 0.05);
Farbe fogLightColor = Farbe(0.7, 0.7, 0.7);
double fogPartVisible    = 0.15; // Anteil "Nebel sichtbar"
double fogPartLightning  = 0.85; // Anteil "Nebel leuchtet"

bool rauchVorhanden = false;
Vector3D RauchboxMin(-1.0, 0.0, -1.0);
Vector3D RauchboxMax( 1.0, 6.0,  1.0);

bool wolkeVolumeVorhanden = false;
bool wolkeKugelnVorhanden = false;
Vector3D SkyMin(0.0, 30.0, -100.0);
Vector3D SkyMax(100.0, 100.0,  100.0);
Vector3D GroundMin(-100.0, -20.0, -100.0);
Vector3D GroundMax(100.0, 00.0,  100.0);
Vector3D WolkeboxMin(0.0, 1.0, 0.0);
Vector3D WolkeboxMax(90.0, 20.0, -90.0);

bool terrainVorhanden = false;
bool wasserVorhanden = false;

bool antialiasing = true;

extern Texture backgroundTexture;


//Camera cam(Vector3D(0.5,0.5,-250), Vector3D(0.5,0.5,1.0), 90.0, 16.0/9.0);

Vector3D sceneMin;
Vector3D sceneMax;

// Setup der Szene
void initScene(Fenster& f)
{
    TextureManager::loadTextureList("N:/RayTracing/FS26/Neu2026 (minimalistisch)/textures.txt");

    if (!backgroundTexture.loadBMP("N:/RayTracing/FS26/SW08/Hintergrund.bmp"))
       std::cerr << "Skybox failed\n";
        
    initCam(f);
    initLights(f);
    initObjects(f);
    //initMeshLoaded(f);
    //addCoordinateSystem();
    
    //SceneLoader::loadSceneFromXML("N:/RayTracing/FS26/Neu2026 (minimalistisch)/scenes/Siedlung.xml", f);
    if (wolkeKugelnVorhanden) addWolkenAusKugeln(1, 10, 50);
    if (terrainVorhanden) addTerrain(512, (GroundMax.x - GroundMin.x) / 4.0, 3, 6);
    if (wasserVorhanden) addWasser(512, GroundMax.x - GroundMin.x, 3, 3);
    computeSceneBounds();
}
void initMeshLoaded(Fenster& f)
{
    scene.push_back(MeshImporter::importOBJ_fast("N:/RayTracing/FS26/SW08/Ebenen als Szenenbegrenzung mit Kugel.obj", makeMaterialSimple(Farbe(1.0,0.0,0.0))));
}

void initCam(Fenster& f)
{
    //cam = Camera(Vector3D(f.breite()/2,f.hoehe()/2,-250.0), Vector3D(f.breite()/2,f.hoehe()/2,1.0), 90.0, 16.0/9.0);
    cam.push_back(Camera(Vector3D(f.breite()/2,f.hoehe()/2,-400.0), Vector3D(f.breite()/2,f.hoehe()/2,1.0), 120.0, 16.0/9.0));
}

void addTerrain(int gridResolution, int sizeXZ, int heightScale, int octaves)
{
    /*
    gridResolution → Anzahl Punkte pro Achse
    sizeXZ → physikalische Größe
    heightScale → Höhenamplitude
    octaves → Detailstufen (fraktal)
    */
    auto terrain = std::make_unique<Mesh>(Material(Farbe(0,1,0),Farbe(0,1,0),Farbe(1,1,1),0.3,0.7,0.0,0.0,0.0, 0.0, Farbe(0,0,0)));
    terrain->generateTerrain(gridResolution, sizeXZ, heightScale, octaves);
    scene.push_back(std::move(terrain));
}

void addWasser(int gridResolution, int sizeXZ, int heightScale, int octaves)
{
    /*
    gridResolution → Anzahl Punkte pro Achse
    sizeXZ → physikalische Größe
    heightScale → Höhenamplitude
    octaves → Detailstufen (fraktal)
    */
    //auto Wasserboden = std::make_unique<Mesh>(Material(Farbe(0,0,1),Farbe(0,0,1),Farbe(0,0,0),0.3,1.0,0.0,0.0,0.0, 0.0, Farbe(0,0,0)));
    //Wasserboden->generateTerrain(gridResolution, sizeXZ, 0, 1);
    //scene.push_back(std::move(Wasserboden));
    //Material mat = Material(Farbe(0,0,1),Farbe(0,0,1),Farbe(0,0,1),0.2,0.8,150,0.5,1.33, 0.3, Farbe(0.0,0.0,0.0));
    Material mat = MaterialLibrary::get(MaterialType::Wasser);
    auto wasser = std::make_unique<Mesh>(mat);
    wasser->generateTerrain(gridResolution, sizeXZ, heightScale, octaves);
    scene.push_back(std::move(wasser));
}

void addWolkenAusKugeln(int Anzahl, int Tiefe, int Groesse)
{
    std::vector<Kugel> wolke;
    for (int i = 0; i < Anzahl; i++)
    {
        Vector3D center(
            randomDouble(SkyMin.x, SkyMax.x),
            randomDouble(SkyMin.y, SkyMax.y),
            randomDouble(SkyMin.z, SkyMax.z)
        );

        wolke = generateWolkeAusKugeln(center, Groesse, Tiefe, Material(Farbe(1,1,1),Farbe(1,1,1),Farbe(1,1,1),0.3,0.7,0.0,0.0,0.0, 0.0, Farbe(0,0,0)));

        for (const auto& k : wolke)
        {
            scene.push_back(std::make_unique<Kugel>(k));
        }
    }
}

// Initialisierung der lights
void initLights(Fenster& f)
{
    lights.push_back(light(Vector3D(400.0, 500.0, -100.0), Farbe(1,1,1)));     // oben hinter der Kamera
    //lights.push_back(light(Vector3D(200.0, 500.0, 300.0), Farbe(0.8,0.8,0.8))); // seitlich
    //lights.push_back(light(Vector3D(50.0, 50.0, -50.0), Farbe(1,1,0.2))); // vorne
}

void initObjects(Fenster& f)
{
    scene.clear();

    double w = f.breite(); //*5.0/6.0;
    double h = f.hoehe(); //*5.0/6.0;
    double nullBreite = 0; // *1.0/6.0;
    double nullHoehe = 0; // *1.0/6.0;
    double z = 800; //maxTiefe; // Tiefe des Raums

    // Text
    std::make_unique<Text3D>(
        "HALLO",
        Vector3D(0,0,0) + Vector3D(0, 500, 0),
        h * 0.2,
        w * 0.05,  
        makeMaterialSimple(Farbe(0.0,0.0,0.7))
    );
    
    // 🟦 Boden
    scene.push_back(createWall(
        Vector3D(nullBreite, h, 0),
        Vector3D(w, h, 0),
        Vector3D(w, h, z),
        Vector3D(nullBreite, h, z),
        makeMaterialSimple(Farbe(0.0,0.0,0.7)),
        0.1
    ));

    // 🟦 Decke
    scene.push_back(createWall(
        Vector3D(nullBreite, nullHoehe, 0),
        Vector3D(w, nullHoehe, 0),
        Vector3D(w, nullHoehe, z),
        Vector3D(nullBreite, nullHoehe, z),
        makeMaterialSimple(Farbe(1,1,0)),
        0.1
    ));

    // 🟥 Linke Wand
    scene.push_back(createWall(
        Vector3D(nullBreite, nullHoehe, 0),
        Vector3D(nullBreite, h, 0),
        Vector3D(nullBreite, h, z),
        Vector3D(nullBreite, nullHoehe, z),
        makeMaterialSimple(Farbe(1,0,0)),
        0.1
    ));

    // 🟩 Rechte Wand
    scene.push_back(createWall(
        Vector3D(w, nullHoehe, 0),
        Vector3D(w, h, 0),
        Vector3D(w, h, z),
        Vector3D(w, nullHoehe, z),
        makeMaterialSimple(Farbe(0,1,0)),
        0.1
    ));

    // ⬜ Hintere Wand
    scene.push_back(createWall(
        Vector3D(nullBreite, nullHoehe, z),
        Vector3D(w, nullHoehe, z),
        Vector3D(w, h, z),
        Vector3D(nullBreite, h, z),
        makeMaterialSimple(Farbe(0,1,1)),
        0.1
    ));

    // Vorderwand bewusst NICHT hinzufügen

    // 🔵 Kugeln im Raum
    addSphere(Vector3D(w*0.2, h*0.7, 400), 80, MaterialType::Mirror);
    addSphere(Vector3D(w*0.7, h*0.7, 500), 150, MaterialType::Gold);
    addSphere(Vector3D(w*0.6, h*0.4, 350), 90, MaterialType::Glass);
    addSphere(Vector3D(w*0.3, h*0.3, 300), 70, MaterialType::BluePlastic);
    addSphere(Vector3D(w*0.5, h*0.6, 450), 100, MaterialType::GreenGlass);

    // „Strohhalm im Glas“ (leicht geneigt)
    scene.push_back(std::make_unique<Stab>(
        Vector3D(w*0.5, h*0.5, 400.0),   // Startpunkt
        Vector3D(w*0.7, h*0.7, 600.0),   // Endpunkt (schräg)
        20.0,                            // Radius
        MaterialLibrary::get(MaterialType::BluePlastic) // Material
    ));
    
    // Box definieren
    double cubeSize = 150;
    Vector3D cubePos(w * 0.5, h * 0.3 + cubeSize/2, 400); // y = Boden + Hälfte der Größe
    auto cubeMat = makeMaterialSimple(Farbe(0.7,0.3,0.2));
    auto cube = createCubeMesh(cubePos, cubeSize, cubeMat);
    scene.push_back(std::move(cube));

    // Kugel direkt über der Box
    double sphereRadius = 50;
    Vector3D spherePos(cubePos.x, cubePos.y + cubeSize/2 + sphereRadius, cubePos.z);
    jumpingSphere = new Kugel(spherePos, sphereRadius, MaterialLibrary::get(MaterialType::Mirror));
    scene.push_back(std::unique_ptr<object>(jumpingSphere));
}

void computeSceneBounds()
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
}

void addCoordinateSystem()
{
    double thickness = 3.0;
    double negativeFactor = 0.3;
    Vector3D origin = sceneMin;

    Material rot = makeMaterialSimple(Farbe(1,0,0));
    Material gruen = makeMaterialSimple(Farbe(0,1,0));
    Material blau = makeMaterialSimple(Farbe(0,0,1));

    double lenX = (sceneMax.x - sceneMin.x) * 1.3;
    double lenY = (sceneMax.y - sceneMin.y) * 1.3;
    double lenZ = (sceneMax.z - sceneMin.z) * 1.3;

    double tickSize = (lenX + lenY + lenZ) / 3.0 * 0.02;

    auto computeStep = [](double len)
    {
        double magnitude = pow(10.0, floor(log10(len)));
        return magnitude;
    };

    double stepX = computeStep(lenX);
    double stepY = computeStep(lenY);
    double stepZ = computeStep(lenZ);

    // ===== ACHSEN =====
    Vector3D xStart = origin + Vector3D(-lenX * negativeFactor, 0, 0);
    Vector3D xEnd   = origin + Vector3D(lenX, 0, 0);
    Vector3D yStart = origin + Vector3D(0, -lenY * negativeFactor, 0);
    Vector3D yEnd   = origin + Vector3D(0, lenY, 0);
    Vector3D zStart = origin + Vector3D(0, 0, -lenZ * negativeFactor);
    Vector3D zEnd   = origin + Vector3D(0, 0, lenZ);

    scene.push_back(std::make_unique<Pfeil>(xStart, xEnd, thickness, rot));
    scene.push_back(std::make_unique<Pfeil>(yStart, yEnd, thickness, gruen));
    scene.push_back(std::make_unique<Pfeil>(zStart, zEnd, thickness, blau));

    // ===== TICKMARKS =====
    auto startTick = [](double minVal, double step){ return floor(minVal / step) * step; };
    auto endTick   = [](double maxVal, double step){ return ceil(maxVal / step) * step; };

    // ---- X-Achse ----
    for(double tx = startTick(-lenX*negativeFactor, stepX) + stepX; tx < endTick(lenX, stepX); tx += stepX)
    {
        Vector3D px = origin + Vector3D(tx, 0, 0);
        scene.push_back(std::make_unique<Pfeil>(px, px + Vector3D(0, tickSize, 0), thickness*0.4, rot));

        // Text3D statt drawNumber direkt
        scene.push_back(std::make_unique<Text3D>(
            std::to_string((int)tx),
            px + Vector3D(0, tickSize*1.5, 0),
            tickSize*0.8,
            tickSize * 0.3,  
            rot
        ));
    }

    // ---- Y-Achse ----
    for(double ty = startTick(-lenY*negativeFactor, stepY) + stepY; ty < endTick(lenY, stepY); ty += stepY)
    {
        Vector3D py = origin + Vector3D(0, ty, 0);
        scene.push_back(std::make_unique<Pfeil>(py, py + Vector3D(tickSize, 0, 0), thickness*0.4, gruen));

        scene.push_back(std::make_unique<Text3D>(
            std::to_string((int)ty),
            py + Vector3D(tickSize*1.2, 0, 0),
            tickSize*0.8,
            tickSize * 0.3,  
            gruen
        ));
    }

    // ---- Z-Achse ----
    for(double tz = startTick(-lenZ*negativeFactor, stepZ) + stepZ; tz < endTick(lenZ, stepZ); tz += stepZ)
    {
        Vector3D pz = origin + Vector3D(0, 0, tz);
        scene.push_back(std::make_unique<Pfeil>(pz, pz + Vector3D(0, tickSize, 0), thickness*0.4, blau));

        scene.push_back(std::make_unique<Text3D>(
            std::to_string((int)tz),
            pz + Vector3D(0, tickSize*1.5, 0),
            tickSize*0.8,
            tickSize * 0.3,  
            blau
        ));
    }

    std::cout << "Szene Anzahl Objekte (mit Koordinatenachsen): " << scene.size() << "\n";
}

void addSphere(const Vector3D& pos, double r, MaterialType type)
{
    scene.push_back(std::make_unique<Kugel>(
        pos, r,
        MaterialLibrary::get(type)
    ));
}