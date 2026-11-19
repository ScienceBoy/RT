#include "SceneLoader.h"
#include "fortschritt.h"
#include "camera.h"
#include "light.h"
#include "kugel.h"
#include "wand.h"
#include "stab.h"
#include "pfeil.h"
#include "text3d.h"
#include "drehung.h"
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
#include "directionalLight.h"
#include "lichtSymbol.h"
#include "gruppe.h"
#include "instanz.h"
#include "animation.h"
#include "volumenbox.h"
#include "render_einstellungen.h"
#include "regen.h"
#include "affin.h"
#include <functional>
#include "toolwindow.h"
#include "sphereLight.h"
#include "areaLight.h"
#include "pointlight.h"
#include "spotlight.h"
#include "mesh.h"
#include "meshLight.h"
#include "textur_abbildung.h"
#include "fell.h"
#include <memory>
#include <map>

extern std::vector<std::unique_ptr<object>> scene;
//extern std::vector<light> lights;
extern std::vector<std::unique_ptr<light>> lights;
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
    if (s == "earth") return MaterialType::Earth;    
    if (s == "bluePlastic") return MaterialType::BluePlastic;
    if (s == "glass") return MaterialType::Glass;
    if (s == "greenGlass") return MaterialType::GreenGlass;
    if (s == "wall") return MaterialType::Wall;
    if (s == "simple") return MaterialType::Simple;        
    if (s == "wasser") return MaterialType::Wasser;    

    return MaterialType::Simple;
    //throw std::runtime_error("Unknown material: " + s);
}

std::string SceneLoader::loadFile(const std::string& path)
{
    std::ifstream file(path);
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

void scaleMesh(Mesh& mesh, double scale)
{
    for (auto& tri : mesh.triangles)
    {
        tri.a *= scale;
        tri.b *= scale;
        tri.c *= scale;
    }
}


void SceneLoader::loadSceneFromXML(const std::string& path, Fenster& f)
{
    std::string xml = loadFile(path);
    if (xml.empty())
        throw std::runtime_error("XML file is empty or not found");
    fortschritt::Abschnitt fortschrittLaden(L"Szene laden");

    // Snapshots der alten Szene zeigen auf Objekte, die gleich zerstoert werden
    animationLeeren();

    // Fell-Imitation: Koerperteile und Huellen, nach dem Laden verbunden (fell.h)
    std::map<int, object *> fellBasen;
    std::vector<fell::Huelle> fellHuellen;

    // wird fuer jedes Objekt aufgerufen: setzt auch den Namen aus name="..." (falls vorhanden)
    auto applyTexture = [&](auto& obj, auto& node)
    {
        if (node->attr.count("furBase"))
            fellBasen[std::atoi(node->attr["furBase"].c_str())] = obj.get();
        if (node->attr.count("furOf"))
        {
            fell::Huelle h;
            h.huelle = obj.get();
            h.id = std::atoi(node->attr["furOf"].c_str());
            h.k = std::atoi(node->attr["furLayer"].c_str());
            h.n = std::max(1, std::atoi(node->attr["furCount"].c_str()));
            h.laenge = std::atof(node->attr["furLength"].c_str());
            h.haengen = std::atof(node->attr["furHang"].c_str());
            fellHuellen.push_back(h);
        }
        auto nameIt = node->attr.find("name");
        if (nameIt != node->attr.end() && !nameIt->second.empty())
            obj->name = nameIt->second;
        // in den Ansichten ausgeblendet (Haken in der Objektliste)
        obj->versteckt = node->attr.count("hidden") && node->attr["hidden"] == "1";

        // gespeicherte Drehwinkel (Geometrie ist schon gedreht; bei rotateOnLoad="1" jetzt drehen)
        auto drehIt = node->attr.find("rotation");
        if (drehIt != node->attr.end())
        {
            std::istringstream werte(drehIt->second);
            Vector3D w(0, 0, 0);
            werte >> w.x >> w.y >> w.z;
            if (node->attr.count("rotateOnLoad") && node->attr["rotateOnLoad"] == "1")
                objektDrehwinkelSetzen(obj.get(), w);
            else
                objektAusrichtungMerken(obj.get(), w);
        }

        auto texIt = node->attr.find("texture");
        if (texIt != node->attr.end())
        {
            // Name aus textures.txt, sonst Pfad einer Bilddatei (Dateiauswahl im Material-Reiter)
            auto tex = TextureManager::get().getTexture(texIt->second);
            if (!tex)
                tex = TextureManager::get().load(texIt->second);
            obj->setTexture(tex);
            obj->textureName = texIt->second;
        }

        auto modeIt = node->attr.find("textureMode");
        if (modeIt != node->attr.end())
        {
            obj->setTextureMode(parseTextureMode(modeIt->second));
        }

        // Ausrichtung der Textur (Material-Reiter)
        obj->texturSpiegelU = node->attr.count("textureFlipU") && node->attr["textureFlipU"] == "1";
        obj->texturSpiegelV = node->attr.count("textureFlipV") && node->attr["textureFlipV"] == "1";
        if (node->attr.count("textureRotate"))
            obj->texturDrehung = (std::atoi(node->attr["textureRotate"].c_str()) / 90) & 3;

        // weitere Einstellungen (Kachel in m, Versatz, Winkel, Kugel-Drehung, haftet am Objekt, weitere Bilder)
        static const char *const BILD_ATTR[BILD_ANZAHL] = {"textureBump", "textureAlphaMap", "textureGloss", "textureGlow",
                                                           "textureBelly"};
        static const char *const STAERKE_ATTR[BILD_ANZAHL] = {"textureBumpDepth", "", "textureGlossStrength",
                                                              "textureGlowStrength", "textureBellyHeight"};
        bool weitere = false;
        for (const char *a : {"textureTile", "textureOffset", "textureAngle", "textureSphereRot", "textureObject", "rainRipples", "wet"})
            weitere = weitere || node->attr.count(a);
        for (int i = 0; i < BILD_ANZAHL; ++i)
            weitere = weitere || node->attr.count(BILD_ATTR[i]);
        Affin rahmen;
        bool mitRahmen = false;
        if (weitere)
        {
            auto par = std::make_shared<TexturParameter>();
            auto zahlAttr = [&](const char *name, double &ziel)
            {
                if (node->attr.count(name))
                    ziel = std::atof(node->attr[name].c_str());
            };
            zahlAttr("textureTile", par->kachel);
            zahlAttr("textureAngle", par->winkel);
            if (node->attr.count("textureOffset"))
            {
                std::istringstream w(node->attr["textureOffset"]);
                w >> par->versatzU >> par->versatzV;
            }
            if (node->attr.count("textureSphereRot"))
            {
                std::istringstream w(node->attr["textureSphereRot"]);
                w >> par->kugelDrehung.x >> par->kugelDrehung.y >> par->kugelDrehung.z;
            }
            par->objektfest = node->attr.count("textureObject") && node->attr["textureObject"] == "1";
            zahlAttr("rainRipples", par->regenRinge); // Wellenringe bei Regen
            zahlAttr("wet", par->nass);               // nasse Oberflaeche
            for (int i = 0; i < BILD_ANZAHL; ++i)
            {
                if (node->attr.count(BILD_ATTR[i]))
                {
                    const std::string n = node->attr[BILD_ATTR[i]];
                    auto t = TextureManager::get().getTexture(n);
                    if (!t)
                        t = TextureManager::get().load(n);
                    if (t && t->width() > 0)
                    {
                        par->bild[i] = t;
                        par->bildName[i] = n;
                    }
                }
                if (*STAERKE_ATTR[i])
                    zahlAttr(STAERKE_ATTR[i], par->staerke[i]);
            }
            obj->texPar = par;
            if (node->attr.count("textureFrame"))
                mitRahmen = rahmen.lesen(node->attr["textureFrame"]);
        }
        if (auto *m = dynamic_cast<Mesh *>(obj.get()))
        {
            // haftet am Objekt: Ruhelage ueber die gespeicherte Abbildung (jetzige Lage -> Ruhelage)
            if (obj->texturPar().objektfest)
                texturRuheSetzen(m->triangles, mitRahmen ? &rahmen : nullptr);
            m->texturBoxSetzen(); // Ausrichtung und Einstellungen auf die Dreiecke
        }
    };

    auto root = SimpleXML::parse(xml);

    if (!root || root->name != "scene")
        throw std::runtime_error("Invalid XML root");

    // === FIND NODES BY NAME ===
    XMLNode* materialsNode = findNode(root.get(), "materials");
    XMLNode* camNode       = findNode(root.get(), "camera");
    XMLNode* lightsNode    = findNode(root.get(), "lights");
    XMLNode* objNode       = findNode(root.get(), "objects");

    // erst pruefen, dann die bisherige Szene verwerfen (Load XML: bei falscher Datei bleibt sie erhalten)
    if (!camNode || camNode->children.size() < 4)
        throw std::runtime_error("XML: <camera> fehlt oder ist unvollstaendig");
    if (!lightsNode)
        throw std::runtime_error("XML: <lights> fehlt");
    if (!objNode)
        throw std::runtime_error("XML: <objects> fehlt");

    scene.clear();
    fell::gHuellen.clear(); // Fell-Imitation der alten Szene
    lights.clear();
    cam.clear();

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

    // === RENDER-EINSTELLUNGEN (optional; fehlt in aelteren Dateien) ===
    // Himmel: aeltere Dateien ohne Angaben bekommen den bisherigen festen Verlauf
    himmelHorizont = Farbe(0.6, 0.8, 1.0);
    himmelZenit = Farbe(0.1, 0.3, 0.6);
    himmelHelligkeit = 1.0;
    regen::gRegen = regen::Einstellungen(); // ohne Angaben: kein Regen
    if (XMLNode* settingsNode = findNode(root.get(), "settings"))
    {
        auto& a = settingsNode->attr;
        auto zahl = [&](const char* name, double& ziel)
        {
            if (a.count(name))
                ziel = std::stod(a[name]);
        };
        auto ganz = [&](const char* name, int& ziel)
        {
            if (a.count(name))
                ziel = std::stoi(a[name]);
        };
        auto schalter = [&](const char* name, bool& ziel)
        {
            if (a.count(name))
                ziel = a[name] == "1";
        };
        auto drei = [](const std::string& text, double& x, double& y, double& z)
        {
            std::istringstream w(text);
            w >> x >> y >> z;
        };
        auto vektor = [&](const char* name, Vector3D& ziel)
        {
            if (a.count(name))
                drei(a[name], ziel.x, ziel.y, ziel.z);
        };
        auto farbe = [&](const char* name, Farbe& ziel)
        {
            if (a.count(name))
                drei(a[name], ziel.r, ziel.g, ziel.b);
        };

        zahl("exposure", exposure);
        schalter("umgebungsbild", umgebungsbild);
        if (a.count("bild") && !a["bild"].empty())
        {
            backgroundTexture.loadBMP(a["bild"].c_str());
            UmgebungsbildGeladen(a["bild"]);
        }
        farbe("himmelHorizont", himmelHorizont);
        farbe("himmelZenit", himmelZenit);
        zahl("himmelHelligkeit", himmelHelligkeit);
        schalter("antialiasing", antialiasing);
        ganz("antialiasingSamples", antialiasingSamples);
        ganz("pathSamples", pathSamples);
        ganz("renderingDepth", renderingDepth);
        indirektesLicht = true; // aeltere Dateien: wie bisher mit indirektem Licht
        schalter("indirekt", indirektesLicht);
        schalter("adaptiv", adaptivAn);
        ganz("adaptivMin", adaptivMin);
        ganz("adaptivMax", adaptivMax);
        zahl("rauschSchwelle", rauschSchwelle);
        zahl("indirektGrenze", indirektGrenze);
        ganz("schnellBlock", schnellBlock);
        ganz("kachelGroesse", kachelGroesse);
        ganz("bildBreite", renderBreite);
        ganz("bildHoehe", renderHoehe);
        schalter("volumenSchatten", volumenSchatten);
        ganz("volumenSchattenSchritte", volumenSchattenSchritte);
        schalter("nebelDaempftLicht", nebelDaempftLicht);
        schalter("zeichentrick", zeichentrick);
        ganz("tonStufen", tonStufen);
        zahl("saettigung", saettigung);
        schalter("konturen", konturen);
        zahl("konturDicke", konturDicke);
        zahl("randlicht", randlicht);
        // Kamera und Bewegungsunschaerfe (fehlen in aelteren Dateien: dann aus)
        blende = 0.0;
        fokusAbstand = 0.0;
        bewegungTeilbilder = 1;
        verschluss = 0.5;
        zahl("blende", blende);
        zahl("fokusAbstand", fokusAbstand);
        ganz("bewegungTeilbilder", bewegungTeilbilder);
        zahl("verschluss", verschluss);
        schalter("nebel", nebelVorhanden);
        zahl("fogDensity", fogDensity);
        farbe("fogColor", fogColor);
        schalter("nebelBeleuchtet", nebelBeleuchtet);
        farbe("fogLightColor", fogLightColor);
        zahl("fogPartVisible", fogPartVisible);
        zahl("fogPartLightning", fogPartLightning);
        schalter("rauch", rauchVorhanden);
        schalter("wolkenVolumen", wolkeVolumeVorhanden);
        vektor("skyMin", SkyMin);
        vektor("skyMax", SkyMax);
        vektor("wolkeMin", WolkeboxMin);
        vektor("wolkeMax", WolkeboxMax);
        // Regen (fehlt in aelteren Dateien: aus, Vorgaben)
        regen::gRegen = regen::Einstellungen();
        {
            regen::Einstellungen &rg = regen::gRegen;
            schalter("regen", rg.an);
            zahl("regenDichte", rg.dichte);
            zahl("regenAbstand", rg.abstand);
            zahl("regenGeschwindigkeit", rg.geschwindigkeit);
            zahl("regenLaenge", rg.laenge);
            zahl("regenDicke", rg.dicke);
            zahl("regenHelligkeit", rg.helligkeit);
            zahl("regenDeckkraft", rg.deckkraft);
            if (settingsNode->attr.count("regenWind"))
            {
                std::istringstream w(settingsNode->attr["regenWind"]);
                w >> rg.windX >> rg.windZ;
            }
            zahl("regenSichtweite", rg.sichtweite);
            zahl("regenRingAbstand", rg.ringAbstand);
            zahl("regenRingTakt", rg.ringTakt);
            zahl("regenRingGroesse", rg.ringGroesse);
            schalter("regenSpritzer", rg.spritzer);
            schalter("regenAllesNass", rg.allesNass);
            zahl("regenNass", rg.nassStaerke);
        }

        smokeBoxes.clear();
        for (auto& c : settingsNode->children)
        {
            if (c->name != "smoke")
                continue;
            Vector3D mn, mx;
            Farbe f(1, 1, 1);
            drei(c->attr["min"], mn.x, mn.y, mn.z);
            drei(c->attr["max"], mx.x, mx.y, mx.z);
            if (c->attr.count("color"))
                drei(c->attr["color"], f.r, f.g, f.b);
            double dichte = c->attr.count("density") ? std::stod(c->attr["density"]) : 1.0;
            double seed = c->attr.count("seed") ? std::stod(c->attr["seed"]) : 0.0;
            smokeBoxes.emplace_back(mn, mx, f, dichte, seed);
        }
    }

    // === LIGHTS ===
    for(auto& l : lightsNode->children)
    {
    std::string type =
        l->attr["type"];
    
        if(type == "point")
        {
            // aeltere Dateien ohne power="...": bisher hatte jedes Punktlicht wegen eines Fehlers im
            // Konstruktor 100000 W; so bleiben solche Szenen gleich hell
            double power = 100000.0;

            if (l->attr.count("power"))
                power = std::stod(l->attr["power"]);

            auto p =
                l->children[0].get();

            auto c =
                l->children[1].get();

            lights.emplace_back(
                std::make_unique<PointLight>(
                    Vector3D(
                        std::stod(p->attr["x"]),
                        std::stod(p->attr["y"]),
                        std::stod(p->attr["z"])
                    ),
                    Farbe(
                        std::stod(c->attr["r"]),
                        std::stod(c->attr["g"]),
                        std::stod(c->attr["b"])
                    ),
                    power
                )
            );
        }

        else if (type == "spot") // Spotlicht: Position, Farbe, Richtung; Leistung, Winkel, weicher Rand
        {
            double power = 300.0, winkel = 30.0, weich = 0.25;
            if (l->attr.count("power"))
                power = std::stod(l->attr["power"]);
            if (l->attr.count("angle"))
                winkel = std::stod(l->attr["angle"]);
            if (l->attr.count("soft"))
                weich = std::stod(l->attr["soft"]);
            Vector3D pos(0, 1, 0), dir(0, -1, 0);
            Farbe col(1, 1, 1);
            for (auto &k : l->children)
            {
                if (k->name == "position")
                    pos = Vector3D(std::stod(k->attr["x"]), std::stod(k->attr["y"]), std::stod(k->attr["z"]));
                else if (k->name == "color")
                    col = Farbe(std::stod(k->attr["r"]), std::stod(k->attr["g"]), std::stod(k->attr["b"]));
                else if (k->name == "direction")
                    dir = Vector3D(std::stod(k->attr["x"]), std::stod(k->attr["y"]), std::stod(k->attr["z"]));
            }
            lights.emplace_back(std::make_unique<SpotLight>(pos, dir, col, power, winkel, weich));
        }

        else if(type == "directional")
        {
            double irradiance = 1.0;

            if (l->attr.count("irradiance"))
                irradiance = std::stod(l->attr["irradiance"]);

            auto d =
                l->children[0].get();

            auto c =
                l->children[1].get();

            lights.emplace_back(
                std::make_unique<DirectionalLight>(
                    Vector3D(
                        std::stod(d->attr["x"]),
                        std::stod(d->attr["y"]),
                        std::stod(d->attr["z"])
                    ),
                    Farbe(
                        std::stod(c->attr["r"]),
                        std::stod(c->attr["g"]),
                        std::stod(c->attr["b"])
                    ),
                    irradiance
                )
            );

            // Symbolposition (fehlt in aelteren Dateien)
            if (l->children.size() > 2 && l->children[2]->name == "position")
            {
                auto p = l->children[2].get();
                richtungslichtSymbolSetzen(static_cast<DirectionalLight*>(lights.back().get()),
                                           Vector3D(std::stod(p->attr["x"]), std::stod(p->attr["y"]), std::stod(p->attr["z"])));
            }
        }

        else if(type == "sphere")
        {
            double power = 50.0;

            if (l->attr.count("power"))
                power = std::stod(l->attr["power"]);

            auto center =
                l->children[0].get();

            auto radius =
                l->children[1].get();

            auto color =
                l->children[2].get();

            lights.emplace_back(
                std::make_unique<SphereLight>(
                    Vector3D(
                        std::stod(center->attr["x"]),
                        std::stod(center->attr["y"]),
                        std::stod(center->attr["z"])
                    ),
                    std::stod(radius->attr["value"]),
                    Farbe(
                        std::stod(color->attr["r"]),
                        std::stod(color->attr["g"]),
                        std::stod(color->attr["b"])
                    ),
                    power
                )
            );
        }

        else if(type == "area")
        {
            double power = 50.0;

            if (l->attr.count("power"))
                power = std::stod(l->attr["power"]);

            auto pos =
                l->children[0].get();

            auto edgeU =
                l->children[1].get();

            auto edgeV =
                l->children[2].get();

            auto color =
                l->children[3].get();

            lights.emplace_back(
                std::make_unique<AreaLight>(
                    Vector3D(
                        std::stod(pos->attr["x"]),
                        std::stod(pos->attr["y"]),
                        std::stod(pos->attr["z"])
                    ),

                    Vector3D(
                        std::stod(edgeU->attr["x"]),
                        std::stod(edgeU->attr["y"]),
                        std::stod(edgeU->attr["z"])
                    ),

                    Vector3D(
                        std::stod(edgeV->attr["x"]),
                        std::stod(edgeV->attr["y"]),
                        std::stod(edgeV->attr["z"])
                    ),

                    Farbe(
                        std::stod(color->attr["r"]),
                        std::stod(color->attr["g"]),
                        std::stod(color->attr["b"])
                    ),
                    power
                )
            );
        }
        else if (type == "mesh")
        {
            // Mesh-Licht: <color r g b/>, power="..." (oder <power value="..."/>) und das Netz als
            // <mesh file="datei.obj"/> oder <mesh> mit <triangle><a/><b/><c/></triangle> (schreibt MeshLight::save).
            // Vorher wurde immer der erste Knoten in <lights> gelesen und das gespeicherte Format nicht verstanden.
            double power = 50.0;
            if (l->attr.count("power"))
                power = std::stod(l->attr["power"]);
            if (XMLNode* pw = findNode(l.get(), "power"); pw && pw->attr.count("value"))
                power = std::stod(pw->attr["value"]);

            Farbe color(1, 1, 1);
            if (XMLNode* c = findNode(l.get(), "color"))
                color = Farbe(std::stod(c->attr["r"]), std::stod(c->attr["g"]), std::stod(c->attr["b"]));

            Material mat = makeMaterialSimple(Farbe(1, 1, 1));
            mat.emission = color;

            std::unique_ptr<Mesh> mesh;
            XMLNode* meshNode = findNode(l.get(), "mesh");
            if (meshNode && meshNode->attr.count("file") && !meshNode->attr["file"].empty())
            {
                mesh = MeshImporter::importOBJ_fast(meshNode->attr["file"], mat);
                if (!mesh)
                    std::cout << "Mesh-Licht: OBJ nicht gefunden: " << meshNode->attr["file"] << "\n";
            }
            else if (meshNode)
            {
                auto punkt = [](XMLNode* n)
                {
                    return Vector3D(std::stod(n->attr["x"]), std::stod(n->attr["y"]), std::stod(n->attr["z"]));
                };
                std::vector<Dreieck> tris;
                for (auto& t : meshNode->children)
                {
                    if (t->name != "triangle")
                        continue;
                    XMLNode* a = findNode(t.get(), "a");
                    XMLNode* b = findNode(t.get(), "b");
                    XMLNode* c = findNode(t.get(), "c");
                    if (a && b && c)
                        tris.emplace_back(punkt(a), punkt(b), punkt(c), mat);
                }
                if (!tris.empty())
                    mesh = std::make_unique<Mesh>(tris, Vector3D(0, 0, 0));
            }

            if (mesh && !mesh->triangles.empty())
                lights.emplace_back(std::make_unique<MeshLight>(std::move(mesh), color, power));
            else
                std::cout << "Mesh-Licht ohne Dreiecke uebersprungen\n";
        }
    }

    // === OBJECTS ===
    // rekursiv: <group> enthaelt Objekte (auch weitere Gruppen)
    // Vorlagen der Instanzen (<template id="..."> mit genau einem Objekt), nicht in der Szene
    std::map<std::string, std::shared_ptr<object>> vorlagen;
    std::function<void(std::unique_ptr<XMLNode>&)> objektLaden = [&](std::unique_ptr<XMLNode>& o)
    {
        std::string type = o->name;

        if (type == "template")
        {
            const size_t vorher = scene.size();
            for (auto& kind : o->children)
                objektLaden(kind);
            if (scene.size() > vorher)
            {
                std::unique_ptr<object> v = std::move(scene[vorher]);
                if (auto* g = dynamic_cast<Gruppe*>(v.get()))
                    g->grenzenAktualisieren();
                vorlagen[o->attr["id"]] = std::shared_ptr<object>(std::move(v));
            }
            scene.resize(vorher);
            return;
        }
        if (type == "instance")
        {
            auto v = vorlagen.find(o->attr["template"]);
            Affin abb;
            if (v == vorlagen.end() || !abb.lesen(o->attr["transform"]))
            {
                std::cout << "Instanz ohne Vorlage uebersprungen\n";
                return;
            }
            auto obj = std::make_unique<Instanz>(v->second, abb);
            applyTexture(obj, o); // Name, Drehwinkel, ausgeblendet
            obj->texturUebernehmen();
            scene.push_back(std::move(obj));
            return;
        }

        Material mat;
        auto matIt = materialMap.find(o->attr["material"]);
        if (matIt == materialMap.end())
            //throw std::runtime_error("Unknown material: " + o->attr["material"]);
            mat = MaterialLibrary::get(MaterialType::Simple);
        else
            mat = matIt->second;

        // ohne bekanntes Material: einfache Farbe aus color="r g b" (schreibt SceneSaver)
        auto farbeIt = o->attr.find("color");
        if (matIt == materialMap.end() && farbeIt != o->attr.end())
        {
            std::istringstream werte(farbeIt->second);
            double r = 0.7, g = 0.7, b = 0.7;
            werte >> r >> g >> b;
            mat = makeMaterialSimple(Farbe(r, g, b));
        }

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

            // size="a" (Wuerfel) oder size="x y z" (Quader mit drei Kantenlaengen)
            double sx = 1.0, sy = 0.0, sz = 0.0;
            {
                std::istringstream werte(o->attr["size"]);
                werte >> sx;
                if (!(werte >> sy >> sz))
                    sy = sz = sx;
            }
            const bool quader = std::fabs(sx - sy) > 1e-12 || std::fabs(sx - sz) > 1e-12;

            auto obj = createCubeMesh(
                Vector3D(std::stod(p->attr["x"]),
                        std::stod(p->attr["y"]),
                        std::stod(p->attr["z"])),
                quader ? 1.0 : sx,
                mat
            );
            if (quader) // Einheitswuerfel um seine Mitte auf die drei Kantenlaengen strecken
                obj->scale(Vector3D(sx, sy, sz));

            // Material wie bei den anderen Objekten merken (Reiter "Material", Speichern)
            obj->materialName = o->attr["material"];

            // rotation="x y z" mit rotateOnLoad="1" dreht die Geometrie um die Mitte (in applyTexture)
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
        // ARROW (Pfeil)
        // =========================
        else if (type == "arrow")
        {
            auto s = findNode(o.get(), "start");
            auto e = findNode(o.get(), "end");
            if (!s || !e)
                return;

            auto obj = std::make_unique<Pfeil>(
                Vector3D(std::stod(s->attr["x"]),
                        std::stod(s->attr["y"]),
                        std::stod(s->attr["z"])),
                Vector3D(std::stod(e->attr["x"]),
                        std::stod(e->attr["y"]),
                        std::stod(e->attr["z"])),
                std::stod(o->attr["thickness"]),
                mat
            );

            obj->materialName = o->attr["material"];
            applyTexture(obj, o);

            scene.push_back(std::move(obj));
        }

        // =========================
        // TEXT (3D-Text, Name = Text)
        // =========================
        else if (type == "text")
        {
            auto p = findNode(o.get(), "position");
            Vector3D pos(0, 0, 0);
            if (p)
                pos = Vector3D(std::stod(p->attr["x"]),
                               std::stod(p->attr["y"]),
                               std::stod(p->attr["z"]));

            auto obj = std::make_unique<Text3D>(
                o->attr["value"],
                pos,
                std::stod(o->attr["size"]),
                std::stod(o->attr["depth"]),
                mat
            );

            // gedreht/skaliert gespeichert (fehlt in aelteren Dateien)
            auto ax = findNode(o.get(), "axisX");
            auto ay = findNode(o.get(), "axisY");
            auto az = findNode(o.get(), "axisZ");
            if (ax && ay && az)
            {
                auto vektor = [](XMLNode* n)
                {
                    return Vector3D(std::stod(n->attr["x"]), std::stod(n->attr["y"]), std::stod(n->attr["z"]));
                };
                obj->setAchsen(vektor(ax), vektor(ay), vektor(az));
            }

            obj->materialName = o->attr["material"];
            applyTexture(obj, o);
            obj->name = obj->getText();

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

            // Lage "Datei -> Szene" (verschoben, gedreht, skaliert); aeltere Dateien: rotateOnLoad
            auto lage = o->attr.find("transform");
            Affin abb;
            if (lage != o->attr.end() && abb.lesen(lage->second))
            {
                affinAnwenden(abb, obj->triangles);
                obj->recomputeMeshBoundsFromTriangles();
                obj->buildBVH();
            }

            // eigenes Material aus dem Reiter "Material": auf alle Dreiecke
            auto ueber = o->attr.find("override");
            if (ueber != o->attr.end() && materialMap.count(ueber->second))
            {
                const Material& m = materialMap[ueber->second];
                obj->mat = m;
                for (auto& t : obj->triangles)
                    t.mat = m;
            }

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
                mat,
                0.1
            );

            applyTexture(obj, o);

            scene.push_back(std::move(obj));
        }

        // =========================
        // GROUP
        // =========================
        else if (type == "group")
        {
            size_t vorher = scene.size();
            for (auto& kind : o->children)
                objektLaden(kind);

            auto gruppe = std::make_unique<Gruppe>();
            for (size_t i = vorher; i < scene.size(); ++i)
                gruppe->kinder.push_back(std::move(scene[i]));
            scene.resize(vorher);
            gruppe->grenzenAktualisieren();
            applyTexture(gruppe, o); // Name und Drehwinkel
            scene.push_back(std::move(gruppe));
        }
    };
    fortschritt::bereich(0.05, 0.95, L"Objekte");
    {
        fortschritt::Abschnitt objekteFortschritt(L"Objekte");
        const size_t n = objNode->children.size();
        for (size_t i = 0; i < n; ++i)
        {
            fortschritt::schritt(i, n, L"Objekt " + std::to_wstring(i + 1) + L" von " + std::to_wstring(n));
            objektLaden(objNode->children[i]);
        }
    }
    // Fell-Imitation verbinden: Huelle bekommt die Textur-Ruhelage ihres Koerperteils und folgt ihm
    for (fell::Huelle h : fellHuellen)
    {
        auto it = fellBasen.find(h.id);
        auto *m = dynamic_cast<Mesh *>(h.huelle);
        if (it == fellBasen.end() || !m)
            continue;
        h.basis = it->second;
        std::vector<Dreieck> basis;
        texturParAendern(m, [](TexturParameter &p) { p.fellHuelle = true; });
        if (fell::basisDreiecke(h.basis, basis) && basis.size() == m->triangles.size())
            fell::ruheUebernehmen(basis, m->triangles);
        m->texturBoxSetzen();
        h.pruef = 0; // beim ersten Nachfuehren neu berechnen
        fell::gHuellen.push_back(h);
    }
    fell::nachfuehren(scene);
    fortschritt::bereich(0.95, 1.0, L"Animation");

    // Rauchboxen und Wolkenbereich als Platzhalter (vor den Snapshots, die sich darauf beziehen)
    volumenBoxenAbgleichen();
    computeSceneBounds();

    // === ANIMATION (Snapshots; optional) ===
    if (XMLNode* animNode = findNode(root.get(), "animation"))
    {
        std::function<AnimationsKnoten(XMLNode*)> wandeln = [&](XMLNode* n)
        {
            AnimationsKnoten k;
            k.name = n->name;
            for (auto& [schluessel, wert] : n->attr)
                k.attr[schluessel] = wert;
            for (auto& kind : n->children)
                k.kinder.push_back(wandeln(kind.get()));
            return k;
        };
        animationLaden(wandeln(animNode));
    }
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

        // === CASE 3: eigenes Material (Reiter "Material"), alle Werte als Attribute ===
        else if (type == "custom")
        {
            auto farbe = [&](const char* name, const Farbe& standard)
            {
                if (!m->attr.count(name))
                    return standard;
                std::istringstream w(m->attr[name]);
                double r = 0, g = 0, b = 0;
                w >> r >> g >> b;
                return Farbe(r, g, b);
            };
            auto wert = [&](const char* name, double standard)
            {
                return m->attr.count(name) ? std::stod(m->attr[name]) : standard;
            };

            Material mat;
            mat.ambient = farbe("ambient", mat.ambient);
            mat.diffuse = farbe("diffuse", mat.diffuse);
            mat.specular = farbe("specular", mat.specular);
            mat.emission = farbe("emission", mat.emission);
            mat.absorption = farbe("absorption", mat.absorption);
            mat.kA = wert("kA", mat.kA);
            mat.kD = wert("kD", mat.kD);
            mat.shininess = wert("shininess", mat.shininess);
            mat.specularStrength = wert("specularStrength", mat.specularStrength);
            mat.roughness = wert("roughness", mat.roughness);
            mat.reflection = wert("reflection", mat.reflection);
            mat.IndexOfRefraction = wert("ior", mat.IndexOfRefraction);
            mat.transparency = wert("transparency", mat.transparency);

            materialMap[id] = mat;
            std::cout << "id: " << id << " (eigenes Material)\n";
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
    if (m == "CYLINDER") return TextureMode::CYLINDER;
    if (m == "BOX") return TextureMode::BOX;
    if (m == "TRIPLANAR") return TextureMode::TRIPLANAR;
    return TextureMode::None;
}