#include "SceneSaver.h"
#include "fortschritt.h"
#include "SceneLoader.h"
#include "scene.h"
#include "camera.h"
#include "light.h"
#include "kugel.h"
#include "stab.h"
#include "pfeil.h"
#include "text3d.h"
#include "wand.h"
#include "mesh.h"
#include "material_library.h"
#include "tree.h"
#include "drehung.h"
#include "obj_export.h"
#include "gruppe.h"
#include "instanz.h"
#include "animation.h"
#include "volumenbox.h"
#include "render_einstellungen.h"
#include "regen.h"
#include "affin.h"
#include "mesh_importer.h"
#include "textur_abbildung.h"
#include "fell.h"
#include <functional>
#include "toolwindow.h"

#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <tuple>
#include <cwchar>

extern std::vector<std::unique_ptr<object>> scene;
//extern std::vector<light> lights;
extern std::vector<std::unique_ptr<light>> lights;
extern std::vector<Camera> cam;
extern std::unordered_map<std::string, Material> materialMap;

// Zieldatei des laufenden Speicherns (fuer exportierte OBJ-Dateien daneben)
static std::string zielDatei;

// Sonderzeichen fuer XML-Attribute maskieren
static std::string xmlText(const std::string& text)
{
    std::string ergebnis;
    for (char c : text)
    {
        switch (c)
        {
            case '&': ergebnis += "&amp;"; break;
            case '<': ergebnis += "&lt;"; break;
            case '>': ergebnis += "&gt;"; break;
            case '"': ergebnis += "&quot;"; break;
            default: ergebnis += c;
        }
    }
    return ergebnis;
}

// name="..." (nur wenn das Objekt einen Namen hat)
static std::string nameAttribut(const object* obj)
{
    return obj->name.empty() ? "" : " name=\"" + xmlText(obj->name) + "\"";
}

// rotation="x y z" (Grad, nur zur Anzeige im Objektfenster; die Geometrie ist schon gedreht gespeichert)
static std::string drehAttribut(const object* obj)
{
    // ausgeblendet (Objektliste) mitspeichern; steht hier, weil jeder Objekttyp diese Attribute schreibt
    std::string versteckt = obj->versteckt ? " hidden=\"1\"" : "";
    Vector3D w = objektDrehwinkel(obj);
    if (w.length() < 1e-9)
        return versteckt;
    std::ostringstream o;
    o << " rotation=\"" << w.x << " " << w.y << " " << w.z << "\"" << versteckt;
    return o.str();
}

//////////////////////////////////////////////////////////////
// Eigene Materialien (Reiter "Material"): vollstaendig in <materials> als type="custom"
//////////////////////////////////////////////////////////////

// Vorlagen der MaterialLibrary: nur die id wird geschrieben
static bool istVorlage(const std::string& id)
{
    static const char* vorlagen[] = {"mirror", "gold", "earth", "bluePlastic", "glass", "greenGlass", "wall", "simple", "wasser"};
    for (const char* v : vorlagen)
        if (id == v)
            return true;
    return false;
}

static bool materialGleich(const Material& a, const Material& b)
{
    auto f = [](const Farbe& x, const Farbe& y)
    { return std::fabs(x.r - y.r) < 1e-9 && std::fabs(x.g - y.g) < 1e-9 && std::fabs(x.b - y.b) < 1e-9; };
    auto w = [](double x, double y)
    { return std::fabs(x - y) < 1e-9; };
    return f(a.ambient, b.ambient) && f(a.diffuse, b.diffuse) && f(a.specular, b.specular) && f(a.emission, b.emission) &&
           f(a.absorption, b.absorption) && w(a.kA, b.kA) && w(a.kD, b.kD) && w(a.shininess, b.shininess) &&
           w(a.specularStrength, b.specularStrength) && w(a.roughness, b.roughness) && w(a.reflection, b.reflection) &&
           w(a.IndexOfRefraction, b.IndexOfRefraction) && w(a.transparency, b.transparency);
}

static std::vector<std::pair<std::string, Material>> eigeneMaterialien; // dieses Speichern
static std::map<const object*, std::string> eigeneMaterialIds;           // Objekt -> id

// id fuer ein eigenes Material (gleiche Materialien teilen sich eine id)
static std::string eigeneMaterialId(const Material& m)
{
    for (auto& e : eigeneMaterialien)
        if (materialGleich(e.second, m))
            return e.first;
    for (auto& [id, mat] : SceneLoader::materialMap) // z.B. beim Laden gelesene eigene Materialien
        if (!istVorlage(id) && materialGleich(mat, m))
            return id;

    std::string id;
    for (int n = int(eigeneMaterialien.size()) + 1;; ++n)
    {
        id = "material_" + std::to_string(n);
        bool frei = !SceneLoader::materialMap.count(id);
        for (auto& e : eigeneMaterialien)
            frei = frei && e.first != id;
        if (frei)
            break;
    }
    eigeneMaterialien.push_back({id, m});
    return id;
}

// Vorab: welches Objekt braucht ein eigenes Material? (vor <materials> noetig)
static void eigeneMaterialienSammeln()
{
    eigeneMaterialien.clear();
    eigeneMaterialIds.clear();

    std::vector<object*> alle; // Objekte in Gruppen einzeln
    for (const auto& s : scene)
        gruppenAufloesen(s.get(), alle);

    for (object* o : alle)
    {
        if (dynamic_cast<Tree*>(o))
            continue;

        if (auto m = dynamic_cast<Mesh*>(o))
        {
            // Mesh: alle Dreiecke mit demselben, nicht einfachen Material -> beim Laden wieder anwenden
            if (m->triangles.empty())
                continue;
            const Material& erstes = m->triangles.front().mat;
            bool einheitlich = true;
            for (auto& t : m->triangles)
                if (!materialGleich(t.mat, erstes))
                {
                    einheitlich = false;
                    break;
                }
            if (einheitlich && !materialGleich(erstes, makeMaterialSimple(erstes.diffuse)))
                eigeneMaterialIds[m] = eigeneMaterialId(erstes);
            continue;
        }

        const Material& mat = o->mat;
        auto it = SceneLoader::materialMap.find(o->materialName);
        if (!o->materialName.empty() && it != SceneLoader::materialMap.end() && materialGleich(it->second, mat))
            continue; // unveraendert: material="id"
        if (materialGleich(mat, makeMaterialSimple(mat.diffuse)))
            continue; // einfache Farbe: color="r g b"
        eigeneMaterialIds[o] = eigeneMaterialId(mat);
    }
}

static std::string farbText(const Farbe& f)
{
    std::ostringstream o;
    o << f.r << " " << f.g << " " << f.b;
    return o.str();
}

static void eigenesMaterialSchreiben(std::ostream& out, const std::string& id, const Material& m)
{
    out << "        <material id=\"" << xmlText(id) << "\" type=\"custom\""
        << " ambient=\"" << farbText(m.ambient) << "\" diffuse=\"" << farbText(m.diffuse) << "\""
        << " specular=\"" << farbText(m.specular) << "\" emission=\"" << farbText(m.emission) << "\""
        << " absorption=\"" << farbText(m.absorption) << "\""
        << " kA=\"" << m.kA << "\" kD=\"" << m.kD << "\" shininess=\"" << m.shininess << "\""
        << " specularStrength=\"" << m.specularStrength << "\" roughness=\"" << m.roughness << "\""
        << " reflection=\"" << m.reflection << "\" ior=\"" << m.IndexOfRefraction << "\""
        << " transparency=\"" << m.transparency << "\"/>\n";
}

// material="id" fuer bekannte Materialien, sonst color="r g b" (liest SceneLoader)
static std::string materialAttribut(const object* obj)
{
    auto eigen = eigeneMaterialIds.find(obj);
    if (eigen != eigeneMaterialIds.end())
        return " material=\"" + xmlText(eigen->second) + "\"";

    if (!obj->materialName.empty() && SceneLoader::materialMap.count(obj->materialName))
        return " material=\"" + xmlText(obj->materialName) + "\"";

    std::ostringstream o;
    o << " color=\"" << obj->mat.diffuse.r << " " << obj->mat.diffuse.g << " " << obj->mat.diffuse.b << "\"";
    return o.str();
}

// Name des Texturmodus (auch von SceneSaver::textureModeToString verwendet, das privat ist)
static std::string texturModusText(int mode)
{
    switch (mode)
    {
        case 1: return "SPHERE";
        case 2: return "PLANAR";
        case 3: return "UV";
        case 4: return "CYLINDER";
        case 5: return "BOX";
        case 6: return "TRIPLANAR";
        default: return "None";
    }
}

static std::string texturAttribute(const object* obj)
{
    std::string a;
    if (obj->texture && !obj->textureName.empty())
        a += " texture=\"" + xmlText(obj->textureName) + "\"";
    if ((int)obj->textureMode)
        a += " textureMode=\"" + texturModusText((int)obj->textureMode) + "\"";
    if (obj->texturSpiegelU)
        a += " textureFlipU=\"1\"";
    if (obj->texturSpiegelV)
        a += " textureFlipV=\"1\"";
    if (obj->texturDrehung & 3)
        a += " textureRotate=\"" + std::to_string((obj->texturDrehung & 3) * 90) + "\"";
    // Fell-Imitation: Koerperteil bzw. Huelle (fell.h); beim Laden werden beide wieder verbunden
    for (const fell::Huelle &h : fell::gHuellen)
    {
        if (h.basis == obj)
        {
            a += " furBase=\"" + std::to_string(h.id) + "\"";
            break;
        }
        if (h.huelle == obj)
        {
            std::ostringstream o;
            o.precision(10);
            o << " furOf=\"" << h.id << "\" furLayer=\"" << h.k << "\" furCount=\"" << h.n << "\" furLength=\"" << h.laenge
              << "\" furHang=\"" << h.haengen << "\"";
            a += o.str();
            break;
        }
    }
    // weitere Einstellungen (textur_param.h)
    if (obj->texPar)
    {
        const TexturParameter &p = *obj->texPar;
        auto zahl = [](double v)
        {
            std::ostringstream o;
            o.precision(10);
            o << v;
            return o.str();
        };
        if (p.kachel > 0)
            a += " textureTile=\"" + zahl(p.kachel) + "\"";
        if (p.versatzU != 0 || p.versatzV != 0)
            a += " textureOffset=\"" + zahl(p.versatzU) + " " + zahl(p.versatzV) + "\"";
        if (p.winkel != 0)
            a += " textureAngle=\"" + zahl(p.winkel) + "\"";
        if (p.kugelDrehung.x != 0 || p.kugelDrehung.y != 0 || p.kugelDrehung.z != 0)
            a += " textureSphereRot=\"" + zahl(p.kugelDrehung.x) + " " + zahl(p.kugelDrehung.y) + " " +
                 zahl(p.kugelDrehung.z) + "\"";
        if (p.objektfest)
        {
            a += " textureObject=\"1\"";
            // Netz: Abbildung jetzige Lage -> Ruhelage, damit die Textur nach dem Laden gleich liegt
            Affin rahmen;
            if (auto *m = dynamic_cast<const Mesh *>(obj))
                if (texturRuheAbbildung(m->triangles, rahmen))
                    a += " textureFrame=\"" + rahmen.text() + "\"";
        }
        if (p.regenRinge > 0)
            a += " rainRipples=\"" + zahl(p.regenRinge) + "\"";
        if (p.nass > 0)
            a += " wet=\"" + zahl(p.nass) + "\"";
        static const char *const BILD_ATTR[BILD_ANZAHL] = {"textureBump", "textureAlphaMap", "textureGloss", "textureGlow",
                                                           "textureBelly"};
        static const char *const STAERKE_ATTR[BILD_ANZAHL] = {"textureBumpDepth", "", "textureGlossStrength",
                                                              "textureGlowStrength", "textureBellyHeight"};
        for (int i = 0; i < BILD_ANZAHL; ++i)
            if (p.bild[i] && !p.bildName[i].empty())
            {
                a += std::string(" ") + BILD_ATTR[i] + "=\"" + xmlText(p.bildName[i]) + "\"";
                if (*STAERKE_ATTR[i])
                    a += std::string(" ") + STAERKE_ATTR[i] + "=\"" + zahl(p.staerke[i]) + "\"";
            }
    }
    return a;
}

// Dateiname der Form ..._objekt<Zahl>.obj (von alsOBJExportieren geschrieben)
static bool istEigenerExport(const std::string& datei)
{
    size_t pos = datei.rfind("_objekt");
    if (pos == std::string::npos || datei.size() < 4 || datei.compare(datei.size() - 4, 4, ".obj") != 0)
        return false;

    std::string zahl = datei.substr(pos + 7, datei.size() - 4 - (pos + 7));
    return !zahl.empty() && zahl.find_first_not_of("0123456789") == std::string::npos;
}

// Dreiecke als OBJ (+ MTL fuer die Farben) neben die XML schreiben; liefert den OBJ-Pfad.
// Fuer Objekte ohne eigene Datei: Waende, Wuerfel, Baum, im Tool-Fenster eingefuegte Grundformen.
static std::string alsOBJExportieren(const std::vector<const std::vector<Dreieck>*>& listen,
                                     const std::string& name, size_t nummer)
{
    std::string basis = zielDatei;
    size_t punkt = basis.find_last_of('.');
    size_t trenner = basis.find_last_of("/\\");
    if (punkt != std::string::npos && (trenner == std::string::npos || punkt > trenner))
        basis = basis.substr(0, punkt);
    basis += "_objekt" + std::to_string(nummer);

    std::string objDatei = basis + ".obj";
    std::string mtlDatei = basis + ".mtl";
    std::string mtlName = mtlDatei.substr(mtlDatei.find_last_of("/\\") + 1);

    std::ofstream obj(objDatei);
    std::ofstream mtl(mtlDatei);
    if (!obj || !mtl)
    {
        std::cout << "Export fehlgeschlagen: " << objDatei << "\n";
        return "";
    }

    obj << std::setprecision(10);
    mtl << std::setprecision(6);

    obj << "mtllib " << mtlName << "\n";
    obj << "o " << (name.empty() ? "Objekt" + std::to_string(nummer) : name) << "\n";

    // je Farbe ein Material
    std::map<std::tuple<double, double, double>, int> materialien;
    int letztesMaterial = -1;
    size_t vertex = 0;
    size_t gesamt = 0, fertig = 0;
    for (const auto* liste : listen)
        gesamt += liste->size();
    fortschritt::Abschnitt fortschrittObj(L"OBJ-Datei schreiben");

    for (const auto* liste : listen)
    {
        for (const auto& t : *liste)
        {
            if ((++fertig & 0x3FFF) == 0)
                fortschritt::melden(double(fertig) / double(std::max<size_t>(1, gesamt)), L"OBJ-Datei schreiben");
            auto farbe = std::make_tuple(t.mat.diffuse.r, t.mat.diffuse.g, t.mat.diffuse.b);
            auto it = materialien.find(farbe);
            if (it == materialien.end())
            {
                int nr = int(materialien.size());
                it = materialien.emplace(farbe, nr).first;
                mtl << "newmtl farbe" << nr << "\n";
                mtl << "Kd " << t.mat.diffuse.r << " " << t.mat.diffuse.g << " " << t.mat.diffuse.b << "\n";
                mtl << "Ka 0 0 0\nKs 0 0 0\nKe 0 0 0\nillum 1\n\n";
            }
            if (it->second != letztesMaterial)
            {
                obj << "usemtl farbe" << it->second << "\n";
                letztesMaterial = it->second;
            }

            Vector3D a = t.a + t.position, b = t.b + t.position, c = t.c + t.position;
            obj << "v " << a.x << " " << a.y << " " << a.z << "\n";
            obj << "v " << b.x << " " << b.y << " " << b.z << "\n";
            obj << "v " << c.x << " " << c.y << " " << c.z << "\n";
            obj << "f " << vertex + 1 << " " << vertex + 2 << " " << vertex + 3 << "\n";
            vertex += 3;
        }
    }

    std::cout << "--- exportiert: " << objDatei << "\n";
    return objDatei;
}

//////////////////////////////////////////////////////////////
// EXPORT OBJ: ganze Szene in eine Datei
//////////////////////////////////////////////////////////////

// schreibt Dreiecke als OBJ, je Farbe ein Material in der MTL
struct ObjSchreiber
{
    std::ofstream obj, mtl;
    std::map<std::tuple<double, double, double>, int> materialien;
    int letztesMaterial = -1;
    size_t vertex = 0;
    size_t dreiecke = 0;

    void objekt(const std::string& name)
    {
        // Leerzeichen im Gruppennamen trennen manche Programme ab
        std::string n = name;
        for (char& c : n)
            if (c == ' ' || c == '\t')
                c = '_';
        obj << "o " << n << "\n";
        letztesMaterial = -1;
    }

    void dreieck(const Vector3D& a, const Vector3D& b, const Vector3D& c, const Farbe& f)
    {
        auto farbe = std::make_tuple(f.r, f.g, f.b);
        auto it = materialien.find(farbe);
        if (it == materialien.end())
        {
            int nr = int(materialien.size());
            it = materialien.emplace(farbe, nr).first;
            mtl << "newmtl farbe" << nr << "\n";
            mtl << "Kd " << f.r << " " << f.g << " " << f.b << "\n";
            mtl << "Ka 0 0 0\nKs 0 0 0\nKe 0 0 0\nillum 1\n\n";
        }
        if (it->second != letztesMaterial)
        {
            obj << "usemtl farbe" << it->second << "\n";
            letztesMaterial = it->second;
        }

        obj << "v " << a.x << " " << a.y << " " << a.z << "\n";
        obj << "v " << b.x << " " << b.y << " " << b.z << "\n";
        obj << "v " << c.x << " " << c.y << " " << c.z << "\n";
        obj << "f " << vertex + 1 << " " << vertex + 2 << " " << vertex + 3 << "\n";
        vertex += 3;
        ++dreiecke;
    }

    // Dreiecke mit eigener Farbe; versatz: Objektposition bei lokal gespeicherten Dreiecken
    void liste(const std::vector<Dreieck>& tris, const Vector3D& versatz)
    {
        for (const auto& t : tris)
            dreieck(t.a + t.position + versatz, t.b + t.position + versatz, t.c + t.position + versatz, t.mat.diffuse);
    }
};

static void kugelExportieren(ObjSchreiber& s, const Kugel& k)
{
    const int ringe = 16, segmente = 32;
    auto punkt = [&](int i, int j)
    {
        double theta = M_PI * i / ringe, phi = 2.0 * M_PI * j / segmente;
        return k.position + Vector3D(std::sin(theta) * std::cos(phi), std::cos(theta), std::sin(theta) * std::sin(phi)) * k.radius;
    };
    for (int i = 0; i < ringe; ++i)
        for (int j = 0; j < segmente; ++j)
        {
            Vector3D a = punkt(i, j), b = punkt(i + 1, j), c = punkt(i + 1, j + 1), d = punkt(i, j + 1);
            if (i > 0)
                s.dreieck(a, d, c, k.mat.diffuse);
            if (i < ringe - 1)
                s.dreieck(a, c, b, k.mat.diffuse);
        }
}

static void stabExportieren(ObjSchreiber& s, const Stab& st)
{
    const int segmente = 24;
    Vector3D p1 = st.p1 + st.position, p2 = st.p2 + st.position;
    Vector3D achse = (p2 - p1).normalized();
    Vector3D hilf = std::fabs(achse.y) < 0.99 ? Vector3D(0, 1, 0) : Vector3D(1, 0, 0);
    Vector3D u = achse.cross(hilf).normalized(), v = achse.cross(u).normalized();

    auto rand = [&](const Vector3D& mitte, int j)
    {
        double w = 2.0 * M_PI * j / segmente;
        return mitte + (u * std::cos(w) + v * std::sin(w)) * st.radius;
    };
    for (int j = 0; j < segmente; ++j)
    {
        Vector3D a = rand(p1, j), b = rand(p1, j + 1), c = rand(p2, j + 1), d = rand(p2, j);
        s.dreieck(a, b, c, st.mat.diffuse); // Mantel
        s.dreieck(a, c, d, st.mat.diffuse);
        s.dreieck(p1, b, a, st.mat.diffuse); // Deckel
        s.dreieck(p2, d, c, st.mat.diffuse);
    }
}

bool szeneAlsOBJExportieren(const std::string& datei, std::string& meldung)
{
    std::string basis = datei;
    size_t punkt = basis.find_last_of('.');
    size_t trenner = basis.find_last_of("/\\");
    if (punkt != std::string::npos && (trenner == std::string::npos || punkt > trenner))
        basis = basis.substr(0, punkt);

    std::string objDatei = basis + ".obj";
    std::string mtlDatei = basis + ".mtl";
    std::string mtlName = mtlDatei.substr(mtlDatei.find_last_of("/\\") + 1);

    ObjSchreiber s;
    s.obj.open(objDatei);
    s.mtl.open(mtlDatei);
    if (!s.obj || !s.mtl)
    {
        meldung = "Datei kann nicht geschrieben werden: " + objDatei;
        return false;
    }

    s.obj << std::setprecision(10);
    s.mtl << std::setprecision(6);
    s.obj << "# Export aus dem Szenen-Editor\n";
    s.obj << "mtllib " << mtlName << "\n";

    size_t nummer = 0, objekte = 0, uebersprungen = 0;
    std::vector<object*> alle; // Objekte in Gruppen einzeln
    if (objExportNur) // nur die Auswahl (Menue Datei -> Export Objekt)
        for (object* g : *objExportNur)
            gruppenAufloesen(g, alle);
    else
        for (const auto& g : scene)
            gruppenAufloesen(g.get(), alle);
    fortschritt::Abschnitt fortschrittExport(L"OBJ exportieren");
    for (object* o : alle)
    {
        fortschritt::schritt(nummer, alle.size(), L"Objekt " + std::to_wstring(nummer + 1) + L" von " + std::to_wstring(alle.size()));
        ++nummer;
        std::string name = o->name.empty() ? "Objekt" + std::to_string(nummer) : o->name;
        size_t vorher = s.dreiecke;
        s.objekt(name);

        if (auto m = dynamic_cast<const Mesh*>(o))
            s.liste(m->triangles, Vector3D(0, 0, 0));
        else if (auto t = dynamic_cast<const Tree*>(o))
        {
            s.liste(t->woodTriangles, Vector3D(0, 0, 0));
            s.liste(t->leaves, Vector3D(0, 0, 0));
        }
        else if (auto k = dynamic_cast<const Kugel*>(o))
            kugelExportieren(s, *k);
        else if (auto pf = dynamic_cast<const Pfeil*>(o))
            s.liste(pf->tris, pf->position);
        else if (auto tx = dynamic_cast<const Text3D*>(o))
            s.liste(tx->tris, tx->position);
        else if (auto st = dynamic_cast<const Stab*>(o))
            stabExportieren(s, *st);

        if (s.dreiecke > vorher)
            ++objekte;
        else
            ++uebersprungen;
    }

    meldung = std::to_string(objekte) + " Objekte, " + std::to_string(s.dreiecke) + " Dreiecke nach\n" + objDatei;
    if (uebersprungen > 0)
        meldung += "\n(" + std::to_string(uebersprungen) + " Objekte ohne Dreiecke uebersprungen)";
    std::cout << "--- OBJ exportiert: " << objDatei << "\n";
    return true;
}

//////////////////////////////////////////////////////////////
// RENDER-EINSTELLUNGEN (Fenster "Render-Einstellungen"; globale Werte aus scene.cpp)
//////////////////////////////////////////////////////////////
static std::string vektorText(const Vector3D& v)
{
    std::ostringstream o;
    o << v.x << " " << v.y << " " << v.z;
    return o.str();
}

static void einstellungenSpeichern(std::ostream& out)
{
    out << "    <settings"
        << " exposure=\"" << exposure << "\""
        << " umgebungsbild=\"" << (umgebungsbild ? 1 : 0) << "\"";
    if (!UmgebungsbildDatei().empty())
        out << " bild=\"" << xmlText(UmgebungsbildDatei()) << "\"";
    out << " himmelHorizont=\"" << farbText(himmelHorizont) << "\" himmelZenit=\"" << farbText(himmelZenit)
        << "\" himmelHelligkeit=\"" << himmelHelligkeit << "\"";
    out << " antialiasing=\"" << (antialiasing ? 1 : 0) << "\""
        << " antialiasingSamples=\"" << antialiasingSamples << "\""
        << " pathSamples=\"" << pathSamples << "\""
        << " renderingDepth=\"" << renderingDepth << "\""
        << " indirekt=\"" << (indirektesLicht ? 1 : 0) << "\""
        << " adaptiv=\"" << (adaptivAn ? 1 : 0) << "\" adaptivMin=\"" << adaptivMin << "\" adaptivMax=\"" << adaptivMax
        << "\" rauschSchwelle=\"" << rauschSchwelle << "\" indirektGrenze=\"" << indirektGrenze
        << "\" schnellBlock=\"" << schnellBlock << "\" kachelGroesse=\"" << kachelGroesse << "\" bildBreite=\"" << renderBreite << "\" bildHoehe=\"" << renderHoehe
        << "\" volumenSchatten=\"" << (volumenSchatten ? 1 : 0) << "\" volumenSchattenSchritte=\"" << volumenSchattenSchritte
        << "\" nebelDaempftLicht=\"" << (nebelDaempftLicht ? 1 : 0)
        << "\" zeichentrick=\"" << (zeichentrick ? 1 : 0) << "\" tonStufen=\"" << tonStufen << "\" saettigung=\"" << saettigung
        << "\" konturen=\"" << (konturen ? 1 : 0) << "\" konturDicke=\"" << konturDicke << "\" randlicht=\"" << randlicht << "\"\n"
        << "              blende=\"" << blende << "\" fokusAbstand=\"" << fokusAbstand << "\" bewegungTeilbilder=\""
        << bewegungTeilbilder << "\" verschluss=\"" << verschluss << "\"\n"
        << "              nebel=\"" << (nebelVorhanden ? 1 : 0) << "\""
        << " fogDensity=\"" << fogDensity << "\""
        << " fogColor=\"" << farbText(fogColor) << "\""
        << " nebelBeleuchtet=\"" << (nebelBeleuchtet ? 1 : 0) << "\""
        << " fogLightColor=\"" << farbText(fogLightColor) << "\""
        << " fogPartVisible=\"" << fogPartVisible << "\""
        << " fogPartLightning=\"" << fogPartLightning << "\"\n"
        << "              rauch=\"" << (rauchVorhanden ? 1 : 0) << "\""
        << " wolkenVolumen=\"" << (wolkeVolumeVorhanden ? 1 : 0) << "\""
        << " skyMin=\"" << vektorText(SkyMin) << "\""
        << " skyMax=\"" << vektorText(SkyMax) << "\""
        << " wolkeMin=\"" << vektorText(WolkeboxMin) << "\""
        << " wolkeMax=\"" << vektorText(WolkeboxMax) << "\"\n";
    // Regen (regen.h)
    const regen::Einstellungen &rg = regen::gRegen;
    out << "              regen=\"" << (rg.an ? 1 : 0) << "\" regenDichte=\"" << rg.dichte << "\" regenAbstand=\"" << rg.abstand
        << "\" regenGeschwindigkeit=\"" << rg.geschwindigkeit << "\" regenLaenge=\"" << rg.laenge << "\" regenDicke=\"" << rg.dicke
        << "\" regenHelligkeit=\"" << rg.helligkeit << "\" regenDeckkraft=\"" << rg.deckkraft << "\" regenWind=\"" << rg.windX << " "
        << rg.windZ << "\" regenSichtweite=\"" << rg.sichtweite << "\"\n"
        << "              regenRingAbstand=\"" << rg.ringAbstand << "\" regenRingTakt=\"" << rg.ringTakt
        << "\" regenRingGroesse=\"" << rg.ringGroesse << "\" regenSpritzer=\"" << (rg.spritzer ? 1 : 0) << "\" regenAllesNass=\""
        << (rg.allesNass ? 1 : 0) << "\" regenNass=\"" << rg.nassStaerke << "\">\n";

    for (const auto& b : smokeBoxes)
        out << "        <smoke min=\"" << vektorText(b.min) << "\" max=\"" << vektorText(b.max) << "\""
            << " color=\"" << farbText(b.color) << "\" density=\"" << b.densityMultiplier << "\""
            << " seed=\"" << b.seed << "\"/>\n";

    out << "    </settings>\n\n";
}

void SceneSaver::saveSceneToXML(const std::string& filename)
{
    std::cout << "Speichere: " << filename << std::endl;
    zielDatei = filename;
    std::ofstream out(filename);
    if (!out)
        throw std::runtime_error("Cannot open file for writing");
    fortschritt::Abschnitt fortschrittSpeichern(L"Szene speichern");

    out << "<scene>\n\n";

    std::cout << "- Kamera..." << std::endl;
    saveCamera(out);
    std::cout << "- Render-Einstellungen..." << std::endl;
    einstellungenSpeichern(out);
    std::cout << "- Lichter..." << std::endl;
    saveLights(out);
    std::cout << "- Material..." << std::endl;
    eigeneMaterialienSammeln();
    saveMaterials(out);
    std::cout << "- Objekte..." << std::endl;
    fortschritt::bereich(0.05, 0.95, L"Objekte"); // Objekte: der groesste Teil des Balkens
    {
        fortschritt::Abschnitt objekteFortschritt(L"Objekte");
        saveObjects(out);
    }
    std::cout << "- Animation..." << std::endl;
    fortschritt::bereich(0.95, 1.0, L"Animation");
    animationSpeichern(out); // Snapshots (relativ zu den eben gespeicherten Objekten)

    out << "\n</scene>\n";

    std::cout << "Scene saved to: " << filename << "\n";
}

//////////////////////////////////////////////////////////////
// CAMERA
//////////////////////////////////////////////////////////////
void SceneSaver::saveCamera(std::ostream& out)
{
    if (cam.empty()) return;

    const Camera& c = cam[0];

    out << "    <camera>\n";

    out << "        <position x=\"" << c.position.x
        << "\" y=\"" << c.position.y
        << "\" z=\"" << c.position.z << "\"/>\n";

    out << "        <target x=\"" << c.lookAt.x
        << "\" y=\"" << c.lookAt.y
        << "\" z=\"" << c.lookAt.z << "\"/>\n";

    out << "        <fov>" << c.fov << "</fov>\n";
    out << "        <aspect>" << c.aspectRatio << "</aspect>\n";

    out << "    </camera>\n\n";
}

//////////////////////////////////////////////////////////////
// LIGHTS
//////////////////////////////////////////////////////////////
void SceneSaver::saveLights(std::ostream& out)
{
    out << "    <lights>\n";

    for(const auto& l : lights)
    {
        l->save(out);
    }

    out << "    </lights>\n\n"; // fehlte: ohne schliessendes Tag war die Datei ungueltig
}

//////////////////////////////////////////////////////////////
// MATERIALS
//////////////////////////////////////////////////////////////
void SceneSaver::saveMaterials(std::ostream& out)
{
    out << "    <materials>\n\n";

    for (const auto& [id, mat] : SceneLoader::materialMap)
    {
        if (istVorlage(id))
            out << "        <material id=\"" << id << "\"/>\n";
        else
            eigenesMaterialSchreiben(out, id, mat); // z.B. aus einer geladenen Datei
    }
    for (const auto& e : eigeneMaterialien)
        eigenesMaterialSchreiben(out, e.first, e.second);

    out << "    </materials>\n\n";
}

//////////////////////////////////////////////////////////////
// OBJECTS
//////////////////////////////////////////////////////////////
void SceneSaver::saveObjects(std::ostream& out)
{
    out << "    <objects>\n\n";

    size_t nummer = 0;
    // rekursiv, damit Gruppen ihre Objekte als <group> ... </group> enthalten
    // Vorlagen der Instanzen: je eine Kennung (T1, T2 ...), als <template> vor den Objekten geschrieben
    std::map<const object*, std::string> vorlagenIds;
    std::function<void(object*)> schreiben;
    schreiben = [&](object* obj)
    {
        if (dynamic_cast<VolumenBox*>(obj))
            return; // Rauchbox/Wolken: stehen in <settings>
        ++nummer;

        ////////////////////////////////////////////////////////
        // INSTANZ: Verweis auf die Vorlage und ihre Abbildung
        ////////////////////////////////////////////////////////
        if (auto in = dynamic_cast<Instanz*>(obj))
        {
            if (!in->vorlage)
                return;
            auto it = vorlagenIds.find(in->vorlage.get());
            if (it == vorlagenIds.end())
            {
                const std::string id = "T" + std::to_string(vorlagenIds.size() + 1);
                it = vorlagenIds.emplace(in->vorlage.get(), id).first;
                out << "        <template id=\"" << id << "\">\n\n";
                schreiben(in->vorlage.get()); // darf selbst Instanzen enthalten (deren Vorlagen kommen davor)
                out << "        </template>\n\n";
            }
            out << "        <instance template=\"" << it->second << "\" transform=\"" << in->abb.text() << "\""
                << nameAttribut(in) << drehAttribut(in) << "/>\n\n";
            return;
        }

        ////////////////////////////////////////////////////////
        // GROUP: enthaltene Objekte darin
        ////////////////////////////////////////////////////////
        if (auto g = dynamic_cast<Gruppe*>(obj))
        {
            out << "        <group" << nameAttribut(g) << drehAttribut(g) << ">\n\n";
            for (auto& k : g->kinder)
                schreiben(k.get());
            out << "        </group>\n\n";
            return;
        }


        ////////////////////////////////////////////////////////
        // SPHERE
        ////////////////////////////////////////////////////////
        if (auto s = dynamic_cast<Kugel*>(obj))
        {
            out << "        <SPHERE" << materialAttribut(s) << " radius=\"" << s->radius << "\""
                << nameAttribut(s) << drehAttribut(s) << texturAttribute(s) << ">\n";

            out << "            <position x=\"" << s->position.x
                << "\" y=\"" << s->position.y
                << "\" z=\"" << s->position.z << "\"/>\n";

            out << "        </SPHERE>\n\n";
        }

        ////////////////////////////////////////////////////////
        // CYLINDER (p1/p2 sind relativ zur Mitte 'position')
        ////////////////////////////////////////////////////////
        else if (auto c = dynamic_cast<Stab*>(obj))
        {
            Vector3D start = c->p1 + c->position;
            Vector3D ende = c->p2 + c->position;

            out << "        <cylinder" << materialAttribut(c) << " radius=\"" << c->radius << "\""
                << nameAttribut(c) << drehAttribut(c) << texturAttribute(c) << ">\n";

            out << "            <start x=\"" << start.x
                << "\" y=\"" << start.y
                << "\" z=\"" << start.z << "\"/>\n";

            out << "            <end x=\"" << ende.x
                << "\" y=\"" << ende.y
                << "\" z=\"" << ende.z << "\"/>\n";

            out << "        </cylinder>\n\n";
        }

        ////////////////////////////////////////////////////////
        // ARROW (endeLokal ist relativ zum Start 'position')
        ////////////////////////////////////////////////////////
        else if (auto pf = dynamic_cast<Pfeil*>(obj))
        {
            Vector3D start = pf->position;
            Vector3D ende = pf->position + pf->endeLokal;

            out << "        <arrow" << materialAttribut(pf) << " thickness=\"" << pf->dicke << "\""
                << nameAttribut(pf) << drehAttribut(pf) << ">\n";

            out << "            <start x=\"" << start.x
                << "\" y=\"" << start.y
                << "\" z=\"" << start.z << "\"/>\n";

            out << "            <end x=\"" << ende.x
                << "\" y=\"" << ende.y
                << "\" z=\"" << ende.z << "\"/>\n";

            out << "        </arrow>\n\n";
        }

        ////////////////////////////////////////////////////////
        // TEXT (der Name ist der Text selbst)
        ////////////////////////////////////////////////////////
        else if (auto tx = dynamic_cast<Text3D*>(obj))
        {
            out << "        <text" << materialAttribut(tx) << " value=\"" << xmlText(tx->text) << "\""
                << " size=\"" << tx->size << "\" depth=\"" << tx->depth << "\"" << drehAttribut(tx) << ">\n";

            out << "            <position x=\"" << tx->position.x
                << "\" y=\"" << tx->position.y
                << "\" z=\"" << tx->position.z << "\"/>\n";

            // Achsen nach Drehen/Skalieren (Schreibrichtung, Hoehe, Tiefe)
            auto achse = [&](const char* name, const Vector3D& v)
            {
                out << "            <" << name << " x=\"" << v.x << "\" y=\"" << v.y << "\" z=\"" << v.z << "\"/>\n";
            };
            achse("axisX", tx->achseX);
            achse("axisY", tx->achseY);
            achse("axisZ", tx->achseZ);

            out << "        </text>\n\n";
        }

        ////////////////////////////////////////////////////////
        // MESH aus OBJ-Datei: Verweis auf die Datei
        // MESH ohne Datei (Wand, Wuerfel, eingefuegt) und TREE: als OBJ exportieren
        ////////////////////////////////////////////////////////
        else if (auto m = dynamic_cast<Mesh*>(obj))
        {
            // SceneLoader merkt sich hier die OBJ-Datei. Eigene Exporte (..._objektN.obj) werden
            // neu geschrieben, damit spaetere Aenderungen (Verschieben, Farbe) erhalten bleiben.
            std::string datei = m->materialName;
            bool exportiert = datei.empty() || istEigenerExport(datei);

            // Verweis auf die eigene OBJ-Datei: Lage "Datei -> Szene" als transform="..." (Verschieben,
            // Drehen, Skalieren). Vorher wurde nur die Drehung beim Laden wiederholt, Verschieben und
            // Skalieren gingen verloren. Passt die Datei nicht mehr (anders bearbeitet): als eigenes OBJ.
            std::string lage;
            if (!exportiert)
            {
                std::unique_ptr<Mesh> original;
                try
                {
                    original = MeshImporter::importOBJ_fast(datei, Material());
                }
                catch (...)
                {
                }
                Affin abb;
                if (original && affinAnpassen({&original->triangles}, {&m->triangles}, abb))
                {
                    if (!abb.istEins(1e-9))
                        lage = " transform=\"" + abb.text() + "\"";
                }
                else
                    exportiert = true;
            }
            if (exportiert)
                datei = alsOBJExportieren({&m->triangles}, m->name, nummer);

            // Drehwinkel nur zur Anzeige (die Lage steckt in transform bzw. im eigenen OBJ)
            std::string drehung = drehAttribut(m) + lage;

            // einheitliches eigenes Material: nach dem Import auf alle Dreiecke (MTL kann nicht alles)
            std::string uebersteuern;
            auto eigen = eigeneMaterialIds.find(m);
            if (eigen != eigeneMaterialIds.end())
                uebersteuern = " override=\"" + xmlText(eigen->second) + "\"";

            if (!datei.empty())
                out << "        <mesh material=\"bluePlastic\" file=\"" << xmlText(datei) << "\""
                    << nameAttribut(m) << drehung << uebersteuern << texturAttribute(m) << "/>\n\n";
        }
        else if (auto t = dynamic_cast<Tree*>(obj))
        {
            std::string datei = alsOBJExportieren({&t->woodTriangles, &t->leaves}, t->name, nummer);
            if (!datei.empty())
                out << "        <mesh material=\"bluePlastic\" file=\"" << xmlText(datei) << "\""
                    << nameAttribut(t) << drehAttribut(t) << "/>\n\n";
        }

        ////////////////////////////////////////////////////////
        // OTHER
        ////////////////////////////////////////////////////////
        else
        {
            std::cout << "Unknown object type during save: "
                      << std::string(obj->getTypeName(), obj->getTypeName() + wcslen(obj->getTypeName())) << "\n";
        }
    };
    for (size_t i = 0; i < scene.size(); ++i)
    {
        object *o = scene[i].get();
        std::wstring name = L"Objekt " + std::to_wstring(i + 1) + L" von " + std::to_wstring(scene.size());
        fortschritt::schritt(i, scene.size(), name);
        schreiben(o);
    }

    out << "    </objects>\n";
}

//////////////////////////////////////////////////////////////
// TEXTURE MODE
//////////////////////////////////////////////////////////////
std::string SceneSaver::textureModeToString(int mode)
{
    return texturModusText(mode);
}