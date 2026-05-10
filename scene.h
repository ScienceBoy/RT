#pragma once
#include "light.h"
#include "fenster.h"
#include "kugel.h"
#include "material_library.h"
#include <vector>
#include <memory>
#include "object.h"
#include "TextureManager.h"

class light;  // Forward Declaration

// Globale Szenenobjekte
extern std::vector<std::unique_ptr<object>> scene;
extern std::vector<light> lights;
extern std::vector<Camera> cam;
extern Kugel* jumpingSphere;
extern Farbe fogColor;
extern bool nebelBeleuchtet;
extern double fogDensity;
extern Farbe fogLightColor;
extern bool nebelVorhanden;
extern double fogPartVisible;   // Anteil "Nebel sichtbar"
extern double fogPartLightning; // Anteil "Nebel leuchtet"
extern bool rauchVorhanden;
extern Vector3D RauchboxMin;
extern Vector3D RauchboxMax;
extern bool wolkeVolumeVorhanden;
extern bool wolkeKugelnVorhanden;
extern Vector3D WolkeboxMin;
extern Vector3D WolkeboxMax;
extern Vector3D SkyMin;
extern Vector3D SkyMax;
extern Vector3D GroundMin;
extern Vector3D GroundMax;

extern bool terrainVorhanden;
extern bool wasserVorhanden;

extern bool antialiasing;

extern Vector3D sceneMin;
extern Vector3D sceneMax;

void initCam(Fenster& f);

void initScene(Fenster& f);

void initMeshLoaded(Fenster &f);

// Funktion zum Initialisieren der lights
void initLights(Fenster& f);

// Funktion zum Setup der Szene
void initObjects(Fenster& f);

void computeSceneBounds();

void addCoordinateSystem();

void addSphere(const Vector3D &pos, double r, MaterialType type);

void addBar(const Vector3D& start, const Vector3D& end, double thickness, const Material& mat);

void showWireframe();

void clear();

void addWolkenAusKugeln(int Anzahl, int Tiefe, int Groesse);

void addTerrain(int gridResolution, int sizeXZ, int heightScale, int octaves);

void addWasser(int gridResolution, int sizeXZ, int heightScale, int octaves);
