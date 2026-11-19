#include "fell.h"
#include "bearbeitung.h"
#include "toolwindow.h"
#include "wireframe.h"
#include "scene.h"
#include "mesh.h"
#include "tree.h"
#include "kugel.h"
#include "stab.h"
#include "pfeil.h"
#include "text3d.h"
#include "gruppe.h"
#include "instanz.h"
#include "animation.h"
#include "camera.h"
#include "affin.h"
#include "volumenbox.h"
#include <cmath>
#include <cstring>
#include <map>
#include <sstream>
#include "pointlight.h"
#include "spotlight.h"
#include "sphereLight.h"
#include "directionalLight.h"
#include "areaLight.h"
#include "meshLight.h"
#include "lichtSymbol.h"
#include <algorithm>
#include <functional>
#include <atomic>
#include <deque>
#include <iostream>
#include <mutex>
#include <unordered_set>

extern std::vector<std::unique_ptr<object>> scene;
extern std::vector<std::unique_ptr<light>> lights;
extern std::mutex sceneMutex; // wireframe.cpp

// ======================================================================
// Mehrfachauswahl
// ======================================================================

// Die Liste wird auch vom Renderthread gelesen (Hervorhebung)
static std::mutex auswahlMutex;
static std::vector<object *> weitere;  // ausser dem Hauptobjekt
static object *anker = nullptr;        // Hauptobjekt, zu dem 'weitere' gehoert

static int objektPlatz(const object *o)
{
    for (size_t i = 0; i < scene.size(); ++i)
        if (scene[i].get() == o)
            return int(i);
    return -1;
}

// in der Szene, auch innerhalb von Gruppen (verschachtelt)
static bool inGruppe(const Gruppe *g, const object *o)
{
    for (auto &k : g->kinder)
    {
        if (k.get() == o)
            return true;
        if (auto *u = dynamic_cast<const Gruppe *>(k.get()))
            if (inGruppe(u, o))
                return true;
    }
    return false;
}

// in o oder darin (Gruppe, Vorlage einer Instanz)
static bool enthaeltObjekt(const object *s, const object *o, int tiefe = 0, bool vorlagen = true)
{
    if (!s || tiefe > 32)
        return false;
    if (s == o)
        return true;
    if (auto *g = dynamic_cast<const Gruppe *>(s))
    {
        for (auto &k : g->kinder)
            if (enthaeltObjekt(k.get(), o, tiefe + 1, vorlagen))
                return true;
    }
    else if (auto *in = dynamic_cast<const Instanz *>(s))
        return vorlagen && enthaeltObjekt(in->vorlage.get(), o, tiefe + 1, vorlagen);
    return false;
}

static bool objektVorhanden(const object *o)
{
    if (!o)
        return false;
    for (auto &s : scene)
        if (enthaeltObjekt(s.get(), o))
            return true;
    return false;
}

// direkt in der Szene oder in einer Gruppe (nicht in einer Vorlage): kann ausgewaehlt werden
static bool objektInSzene(const object *o)
{
    if (!o)
        return false;
    for (auto &s : scene)
        if (enthaeltObjekt(s.get(), o, 0, false))
            return true;
    return false;
}

void alleGruppenAktualisieren()
{
    // Instanzen zuerst (Vorlage kann sich geaendert haben: Grenzen, Textur), dann die Gruppen darum
    std::function<void(object *)> instanzen = [&](object *o)
    {
        if (auto *in = dynamic_cast<Instanz *>(o))
            in->grenzenAktualisieren();
        else if (auto *g = dynamic_cast<Gruppe *>(o))
            for (auto &k : g->kinder)
                instanzen(k.get());
    };
    for (auto &s : scene)
        instanzen(s.get());
    for (auto &s : scene)
        if (auto *g = dynamic_cast<Gruppe *>(s.get()))
            g->grenzenAktualisieren();
}

Gruppe *gruppeVon(const object *o)
{
    std::function<Gruppe *(Gruppe *)> suchen = [&](Gruppe *g) -> Gruppe *
    {
        for (auto &k : g->kinder)
        {
            if (k.get() == o)
                return g;
            if (auto *u = dynamic_cast<Gruppe *>(k.get()))
                if (Gruppe *t = suchen(u))
                    return t;
        }
        return nullptr;
    };
    for (auto &s : scene)
        if (auto *g = dynamic_cast<Gruppe *>(s.get()))
            if (Gruppe *t = suchen(g))
                return t;
    return nullptr;
}

static int lichtPlatz(const light *l)
{
    for (size_t i = 0; i < lights.size(); ++i)
        if (lights[i].get() == l)
            return int(i);
    return -1;
}

std::vector<object *> ausgewaehlteObjekte()
{
    std::vector<object *> r;
    object *haupt = selectedObject;
    if (selectedType != SelectedType::OBJECT || !haupt || !objektInSzene(haupt))
        return r;
    r.push_back(haupt);

    std::lock_guard<std::mutex> lock(auswahlMutex);
    if (anker != haupt)
        return r;
    if (weitere.size() < 16)
    {
        for (object *o : weitere)
            if (o != haupt && std::find(r.begin(), r.end(), o) == r.end() && objektInSzene(o))
                r.push_back(o);
        return r;
    }
    // grosse Mehrfachauswahl: einmal alle Objekte der Szene sammeln statt je Objekt die Szene zu durchsuchen
    // (vorher quadratisch - bei tausenden Objekten mehrmals je Sekunde spuerbar, "Keine Rueckmeldung")
    std::unordered_set<const object *> inSzene;
    std::function<void(const object *)> sammeln = [&](const object *o)
    {
        inSzene.insert(o);
        if (auto *g = dynamic_cast<const Gruppe *>(o))
            for (auto &k : g->kinder)
                sammeln(k.get());
    };
    for (auto &s : scene)
        sammeln(s.get());
    std::unordered_set<const object *> schon{haupt};
    for (object *o : weitere)
        if (inSzene.count(o) && schon.insert(o).second)
            r.push_back(o);
    return r;
}

void auswahlSetzen(const std::vector<object *> &objekte)
{
    std::lock_guard<std::mutex> lock(auswahlMutex);
    weitere.clear();

    if (objekte.empty())
    {
        anker = nullptr;
        if (selectedType == SelectedType::OBJECT)
        {
            selectedObject = nullptr;
            selectedType = SelectedType::NONE;
        }
        return;
    }

    selectedObject = objekte[0];
    selectedLight = nullptr;
    selectedType = SelectedType::OBJECT;
    selectionIndex = objektPlatz(objekte[0]);
    anker = objekte[0];
    weitere.assign(objekte.begin() + 1, objekte.end());
}

void auswahlUmschalten(object *o)
{
    std::vector<object *> alle = ausgewaehlteObjekte();
    auto it = std::find(alle.begin(), alle.end(), o);
    if (it != alle.end())
        alle.erase(it);
    else
        alle.insert(alle.begin(), o); // zuletzt angeklicktes wird Hauptobjekt
    auswahlSetzen(alle);
}

bool istAusgewaehlt(const object *o)
{
    auto alle = ausgewaehlteObjekte();
    return std::find(alle.begin(), alle.end(), o) != alle.end();
}

bool auswahlGrenzen(const std::vector<object *> &objekte, Vector3D &minimum, Vector3D &maximum)
{
    bool erstes = true;
    for (object *o : objekte)
    {
        Vector3D mn = o->position, mx = o->position;
        o->getWorldAABB(mn, mx);
        if (erstes)
        {
            minimum = mn;
            maximum = mx;
            erstes = false;
            continue;
        }
        minimum = Vector3D(std::min(minimum.x, mn.x), std::min(minimum.y, mn.y), std::min(minimum.z, mn.z));
        maximum = Vector3D(std::max(maximum.x, mx.x), std::max(maximum.y, mx.y), std::max(maximum.z, mx.z));
    }
    return !erstes;
}

// ======================================================================
// Zustand eines Objekts (fuer Rueckgaengig)
// ======================================================================

struct ObjektZustand
{
    object *o = nullptr;
    int art = 0; // 1 Mesh, 2 Tree, 3 Kugel, 4 Stab, 5 Pfeil, 6 Text3D, 7 Gruppe, 0 sonst (nur Gemeinsames)

    Material mat;
    std::string name;
    std::string materialName;
    std::shared_ptr<Texture> texture;
    std::string textureName;
    TextureMode textureMode = TextureMode::None;
    std::shared_ptr<const TexturParameter> texPar; // Kachel, Versatz, weitere Bilder ...
    bool texturSpiegelU = false, texturSpiegelV = false;
    int texturDrehung = 0;
    double ausrichtung[9];
    Vector3D position, minBound, maxBound, boundingCenter;
    double boundingRadius = 0;

    std::vector<Dreieck> d1, d2;
    std::vector<ObjektZustand> kinder; // Gruppe (art 7)
    double zahl = 0;
    Vector3D v1, v2, v3;
    std::string text;
    Affin abb; // Instanz (art 9): Vorlage -> Welt

    size_t bytes() const
    {
        size_t b = (d1.size() + d2.size()) * sizeof(Dreieck) + sizeof(*this);
        for (auto &k : kinder)
            b += k.bytes();
        return b;
    }
};

// Zugriff auf private Teile von Tree und Text3D (dort als friend eingetragen)
struct ZustandZugriff
{
    static ObjektZustand lesen(object *o)
    {
        ObjektZustand z;
        z.o = o;
        z.mat = o->mat;
        z.name = o->name;
        z.materialName = o->materialName;
        z.texture = o->texture;
        z.textureName = o->textureName;
        z.textureMode = o->textureMode;
        z.texPar = o->texPar;
        z.texturSpiegelU = o->texturSpiegelU;
        z.texturSpiegelV = o->texturSpiegelV;
        z.texturDrehung = o->texturDrehung;
        std::copy(o->ausrichtung, o->ausrichtung + 9, z.ausrichtung);
        z.position = o->position;
        z.minBound = o->minBound;
        z.maxBound = o->maxBound;
        z.boundingCenter = o->boundingCenter;
        z.boundingRadius = o->boundingRadius;

        if (auto in = dynamic_cast<Instanz *>(o))
        {
            z.art = 9; // Instanz: nur ihre Abbildung (die Vorlage ist ein eigenes Objekt)
            z.abb = in->abb;
        }
        else if (auto vb = dynamic_cast<VolumenBox *>(o))
        {
            z.art = 8; // Rauchbox/Wolkenbereich: die Box selbst
            vb->box(z.v1, z.v2);
        }
        else if (auto g = dynamic_cast<Gruppe *>(o))
        {
            z.art = 7;
            for (auto &k : g->kinder)
                z.kinder.push_back(lesen(k.get()));
        }
        else if (auto m = dynamic_cast<Mesh *>(o))
        {
            z.art = 1;
            z.d1 = m->triangles;
        }
        else if (auto t = dynamic_cast<Tree *>(o))
        {
            z.art = 2;
            z.d1 = t->woodTriangles;
            z.d2 = t->leaves;
            z.v1 = t->position; // Tree hat eine eigene position (verdeckt die von object)
        }
        else if (auto k = dynamic_cast<Kugel *>(o))
        {
            z.art = 3;
            z.zahl = k->radius;
        }
        else if (auto s = dynamic_cast<Stab *>(o))
        {
            z.art = 4;
            z.v1 = s->p1;
            z.v2 = s->p2;
            z.zahl = s->radius;
        }
        else if (auto p = dynamic_cast<Pfeil *>(o))
        {
            z.art = 5;
            z.d1 = p->tris;
            z.v1 = p->endeLokal;
            z.zahl = p->dicke;
        }
        else if (auto tx = dynamic_cast<Text3D *>(o))
        {
            z.art = 6;
            z.d1 = tx->tris;
            z.text = tx->text;
            z.v1 = tx->achseX;
            z.v2 = tx->achseY;
            z.v3 = tx->achseZ;
        }
        return z;
    }

    // Holz bzw. Blaetter eines Baums (Baum aufteilen)
    static const std::vector<Dreieck> &baumHolz(const Tree *t) { return t->woodTriangles; }
    // alle Dreieckslisten eines Objekts zum Bearbeiten (Reiter "Manipulation")
    static std::vector<std::vector<Dreieck> *> dreieckListen(object *o)
    {
        if (auto *m = dynamic_cast<Mesh *>(o))
            return {&m->triangles};
        if (auto *t = dynamic_cast<Tree *>(o))
            return {&t->woodTriangles, &t->leaves};
        if (auto *x = dynamic_cast<Text3D *>(o))
            return {&x->tris};
        if (auto *p = dynamic_cast<Pfeil *>(o))
            return {&p->tris};
        return {};
    }
    static const std::vector<Dreieck> &baumBlaetter(const Tree *t) { return t->leaves; }

    // Holz und Blaetter eines Baums zusammen (zum Kopieren als Mesh)
    static std::vector<Dreieck> baumDreiecke(const Tree *t)
    {
        std::vector<Dreieck> tris = t->woodTriangles;
        tris.insert(tris.end(), t->leaves.begin(), t->leaves.end());
        return tris;
    }

    static void setzen(const ObjektZustand &z)
    {
        ++gGeometrieStand; // Ansichten: Geometrie hat sich geaendert
        object *o = z.o;
        ++o->geometrieZaehler; // auch wenn nur einzelne Ecken anders sind (Rueckgaengig eines Pinselstrichs)
        o->mat = z.mat;
        o->name = z.name;
        o->materialName = z.materialName;
        o->texture = z.texture; // Dreiecke eines Meshs haben eigene Kopien (stecken in d1)
        o->textureName = z.textureName;
        o->textureMode = z.textureMode;
        o->texPar = z.texPar; // Dreiecke: stecken mit ihrer Ruhelage in d1
        o->texturSpiegelU = z.texturSpiegelU;
        o->texturSpiegelV = z.texturSpiegelV;
        o->texturDrehung = z.texturDrehung;
        std::copy(z.ausrichtung, z.ausrichtung + 9, o->ausrichtung);
        o->position = z.position;
        o->minBound = z.minBound;
        o->maxBound = z.maxBound;
        o->boundingCenter = z.boundingCenter;
        o->boundingRadius = z.boundingRadius;

        switch (z.art)
        {
        case 9:
            static_cast<Instanz *>(o)->abbSetzen(z.abb);
            break;
        case 8:
            static_cast<VolumenBox *>(o)->boxSetzen(z.v1, z.v2);
            break;
        case 7:
        {
            for (auto &k : z.kinder)
                setzen(k);
            static_cast<Gruppe *>(o)->grenzenAktualisieren();
            break;
        }
        case 1:
        {
            auto m = static_cast<Mesh *>(o);
            m->triangles = z.d1;
            m->recomputeMeshBoundsFromTriangles();
            m->buildBVH();
            break;
        }
        case 2:
        {
            auto t = static_cast<Tree *>(o);
            t->woodTriangles = z.d1;
            t->leaves = z.d2;
            t->position = z.v1;
            t->recomputeBounds();
            t->buildBVH();
            break;
        }
        case 3:
        {
            auto k = static_cast<Kugel *>(o);
            k->radius = z.zahl;
            k->updateBounds();
            break;
        }
        case 4:
        {
            auto s = static_cast<Stab *>(o);
            s->p1 = z.v1;
            s->p2 = z.v2;
            s->radius = z.zahl;
            break;
        }
        case 5:
        {
            auto p = static_cast<Pfeil *>(o);
            p->tris = z.d1;
            p->endeLokal = z.v1;
            p->dicke = z.zahl;
            break;
        }
        case 6:
        {
            auto tx = static_cast<Text3D *>(o);
            tx->tris = z.d1;
            tx->text = z.text;
            tx->achseX = z.v1;
            tx->achseY = z.v2;
            tx->achseZ = z.v3;
            break;
        }
        default:
            break;
        }
    }
};

struct LichtZustand
{
    light *l = nullptr;
    Vector3D position;
    Farbe farbe;
    Vector3D richtung; // nur Richtungslicht
    Vector3D kanteU, kanteV; // nur Flaechenlicht
    double leistung = -1;    // Kugel-, Flaechen- und Mesh-Licht
    double winkel = -1, weich = -1; // Spotlicht (die Richtung steht in 'richtung')
};

static LichtZustand lichtLesen(light *l)
{
    LichtZustand z{l, l->getPosition(), l->farbe, Vector3D(0, 0, 0)};
    if (auto d = dynamic_cast<DirectionalLight *>(l))
        z.richtung = d->direction;
    if (auto sp = dynamic_cast<SpotLight *>(l))
    {
        z.richtung = sp->richtung;
        z.winkel = sp->winkel;
        z.weich = sp->weich;
    }
    if (auto a = dynamic_cast<AreaLight *>(l))
    {
        z.kanteU = a->edgeU;
        z.kanteV = a->edgeV;
        z.leistung = a->power;
    }
    if (auto k = dynamic_cast<SphereLight *>(l))
        z.leistung = k->power;
    if (auto m = dynamic_cast<MeshLight *>(l))
        z.leistung = m->power;
    if (auto p = dynamic_cast<PointLight *>(l))
        z.leistung = p->power;
    if (auto r = dynamic_cast<DirectionalLight *>(l))
        z.leistung = r->irradiance;
    return z;
}

static void lichtSetzen(const LichtZustand &z)
{
    // Flaechenlicht: erst die Kanten (die Mitte bleibt), dann wie alle verschieben
    if (auto a = dynamic_cast<AreaLight *>(z.l))
    {
        Vector3D mitte = a->getPosition();
        a->edgeU = z.kanteU;
        a->edgeV = z.kanteV;
        a->setPosition(mitte);
    }
    // translate statt setPosition: verschiebt bei allen Lichtern um -delta, auch beim Richtungslicht
    z.l->translate(z.l->getPosition() - z.position);
    z.l->farbe = z.farbe;
    if (auto d = dynamic_cast<DirectionalLight *>(z.l))
        d->direction = z.richtung;
    if (auto sp = dynamic_cast<SpotLight *>(z.l))
    {
        if (z.richtung.length() > 1e-12)
            sp->richtung = z.richtung.normalized();
        if (z.winkel > 0)
            sp->winkel = z.winkel;
        if (z.weich >= 0)
            sp->weich = z.weich;
    }
    if (z.leistung >= 0) // -1: Licht ohne Leistung (Richtungslicht)
    {
        if (auto a = dynamic_cast<AreaLight *>(z.l))
            a->power = z.leistung;
        if (auto k = dynamic_cast<SphereLight *>(z.l))
            k->power = z.leistung;
        if (auto m = dynamic_cast<MeshLight *>(z.l))
            m->power = z.leistung;
        if (auto p = dynamic_cast<PointLight *>(z.l))
            p->power = z.leistung;
        if (auto r = dynamic_cast<DirectionalLight *>(z.l))
            r->irradiance = z.leistung;
    }
}

// ======================================================================
// Verlauf
// ======================================================================

// eingefuegtes bzw. geloeschtes Objekt/Licht; ausserhalb der Szene gehoert es dem Verlauf
struct Teil
{
    object *obj = nullptr;
    light *licht = nullptr;
    bool hinzu = true; // true: wurde eingefuegt, false: wurde entfernt
    size_t index = 0;
    Gruppe *eltern = nullptr; // Teil einer Gruppe: Platz in deren Kindern (sonst in der Szene)
    std::unique_ptr<object> objBesitz;
    std::unique_ptr<light> lichtBesitz;
};

struct Eintrag
{
    std::string name;
    std::vector<ObjektZustand> vorher, nachher;
    std::vector<LichtZustand> lichtVorher, lichtNachher;
    std::vector<Teil> teile;
    std::shared_ptr<struct GruppenSchritt> gruppe; // Gruppieren / Aufheben
    std::shared_ptr<struct UmzugSchritt> umzug;    // Objekte in eine Gruppe hinein bzw. aus ihr heraus
    bool gruppieren = false;                       // true: der Schritt hat gruppiert

    size_t bytes() const
    {
        size_t b = sizeof(*this);
        for (auto &z : vorher)
            b += z.bytes();
        for (auto &z : nachher)
            b += z.bytes();
        return b;
    }
};

static const size_t MAX_SCHRITTE = 100;
static const size_t MAX_BYTES = size_t(512) * 1024 * 1024; // Geometrie-Kopien grosser Meshes

static std::deque<Eintrag> rueckStapel;
static std::vector<Eintrag> vorStapel;
static bool offen = false;
static Eintrag laufend;

static std::atomic<bool> geaendert{false};
static std::string dateiName;

// ---------- Autom. Rendern: geaenderte Bereiche (Weltquader) sammeln ----------
namespace
{
std::mutex autoMutex;
std::vector<std::pair<Vector3D, Vector3D>> autoBereiche;
bool autoGanz = false;
} // namespace
std::atomic<unsigned> gAutoRenderStand{0};

void autoRenderAktivitaet() { ++gAutoRenderStand; }

void autoRenderBereich(const Vector3D &mn, const Vector3D &mx)
{
    {
        std::lock_guard<std::mutex> lock(autoMutex);
        if (autoBereiche.size() < 4096)
            autoBereiche.push_back({mn, mx});
        else
            autoGanz = true;
    }
    ++gAutoRenderStand;
}

void autoRenderGanz()
{
    {
        std::lock_guard<std::mutex> lock(autoMutex);
        autoGanz = true;
    }
    ++gAutoRenderStand;
}

bool autoRenderAbholen(std::vector<std::pair<Vector3D, Vector3D>> &bereiche, bool &ganz)
{
    std::lock_guard<std::mutex> lock(autoMutex);
    bereiche.insert(bereiche.end(), autoBereiche.begin(), autoBereiche.end());
    ganz = ganz || autoGanz;
    const bool etwas = !autoBereiche.empty() || autoGanz;
    autoBereiche.clear();
    autoGanz = false;
    return etwas;
}

static void autoObjektBereich(const object *o)
{
    if (!o)
        return;
    Vector3D mn = o->position, mx = o->position;
    o->getWorldAABB(mn, mx);
    autoRenderBereich(mn, mx);
}

// was ein Verlaufsschritt veraendert hat: Lage vorher und nachher; Lichter: ganzes Bild
static void autoBereichAusEintrag(const Eintrag &e)
{
    for (const auto *liste : {&e.vorher, &e.nachher})
        for (const ObjektZustand &z : *liste)
        {
            autoRenderBereich(z.minBound, z.maxBound);
            if (objektVorhanden(z.o))
                autoObjektBereich(z.o);
        }
    for (const Teil &t : e.teile)
    {
        if (t.obj)
            autoObjektBereich(t.obj);
        if (t.licht)
            autoRenderGanz();
    }
    if (!e.lichtVorher.empty() || !e.lichtNachher.empty())
        autoRenderGanz();
    autoRenderAktivitaet();
}

static void ablegen(Eintrag &&e)
{
    autoBereichAusEintrag(e);
    geaendert = true;
    vorStapel.clear(); // neue Aenderung: Wiederholen nicht mehr moeglich

    if (e.bytes() > MAX_BYTES)
    {
        std::cout << "Rueckgaengig: \"" << e.name << "\" ist zu gross fuer den Verlauf\n";
        return;
    }

    rueckStapel.push_back(std::move(e));

    size_t summe = 0;
    for (auto &x : rueckStapel)
        summe += x.bytes();
    while (rueckStapel.size() > 1 && (rueckStapel.size() > MAX_SCHRITTE || summe > MAX_BYTES))
    {
        summe -= rueckStapel.front().bytes();
        rueckStapel.pop_front(); // aelteste zuerst
    }
}

void aenderungBeginnen(const std::string &name, const std::vector<object *> &objekte,
                       const std::vector<light *> &lichter)
{
    if (offen)
        aenderungAbschliessen();
    if (objekte.empty() && lichter.empty())
        return;

    laufend = Eintrag();
    laufend.name = name;
    for (object *o : objekte)
        laufend.vorher.push_back(ZustandZugriff::lesen(o));
    for (light *l : lichter)
        laufend.lichtVorher.push_back(lichtLesen(l));
    offen = true;
}

void aenderungAbschliessen()
{
    if (!offen)
        return;
    offen = false;
    ++gGeometrieStand;

    alleGruppenAktualisieren(); // ein Objekt in einer Gruppe hat sich geaendert
    {
        std::lock_guard<std::mutex> lock(sceneMutex);
        fell::nachfuehren(scene); // Fell-Imitation folgt ihren Koerperteilen
    }
    for (auto &z : laufend.vorher)
        if (objektVorhanden(z.o))
            laufend.nachher.push_back(ZustandZugriff::lesen(z.o));
    for (auto &z : laufend.lichtVorher)
        if (lichtPlatz(z.l) >= 0)
            laufend.lichtNachher.push_back(lichtLesen(z.l));

    ablegen(std::move(laufend));
    laufend = Eintrag();
}

void eingefuegtMerken(const std::string &name, const std::vector<object *> &objekte,
                      const std::vector<light *> &lichter)
{
    if (offen)
        aenderungAbschliessen();

    Eintrag e;
    e.name = name;
    for (object *o : objekte)
    {
        Teil t;
        t.obj = o;
        e.teile.push_back(std::move(t));
    }
    for (light *l : lichter)
    {
        Teil t;
        t.licht = l;
        e.teile.push_back(std::move(t));
    }
    ablegen(std::move(e));
}

// Teile aus der Szene nehmen (Besitz geht an den Eintrag); in gSzeneAendern aufrufen
// Platz eines Objekts in seiner Gruppe (-1: nicht darin)
static int platzInGruppe(const Gruppe *g, const object *o)
{
    for (size_t i = 0; g && i < g->kinder.size(); ++i)
        if (g->kinder[i].get() == o)
            return int(i);
    return -1;
}

static void teileEntfernen(std::vector<Teil *> folge)
{
    for (Teil *t : folge)
    {
        t->eltern = nullptr;
        if (t->obj && objektPlatz(t->obj) < 0)
        {
            t->eltern = gruppeVon(t->obj); // Teil einer Gruppe
            t->index = size_t(std::max(0, platzInGruppe(t->eltern, t->obj)));
        }
        else
            t->index = size_t(std::max(0, t->obj ? objektPlatz(t->obj) : lichtPlatz(t->licht)));
    }

    // von hinten nach vorne, damit die gemerkten Plaetze beim Zuruecklegen stimmen
    std::sort(folge.begin(), folge.end(), [](Teil *a, Teil *b)
              { return a->index > b->index; });

    for (Teil *t : folge)
    {
        if (t->obj && t->eltern)
        {
            int i = platzInGruppe(t->eltern, t->obj);
            if (i < 0)
                continue;
            t->objBesitz = std::move(t->eltern->kinder[size_t(i)]);
            t->eltern->kinder.erase(t->eltern->kinder.begin() + i);
            t->eltern->grenzenAktualisieren();
        }
        else if (t->obj)
        {
            int i = objektPlatz(t->obj);
            if (i < 0)
                continue;
            t->objBesitz = std::move(scene[size_t(i)]);
            scene.erase(scene.begin() + i);
        }
        else
        {
            int i = lichtPlatz(t->licht);
            if (i < 0)
                continue;
            t->lichtBesitz = std::move(lights[size_t(i)]);
            lights.erase(lights.begin() + i);
        }
    }
}

static void teileEinfuegen(std::vector<Teil *> folge)
{
    std::sort(folge.begin(), folge.end(), [](Teil *a, Teil *b)
              { return a->index < b->index; });

    for (Teil *t : folge)
    {
        if (t->objBesitz && t->eltern)
        {
            auto &k = t->eltern->kinder;
            k.insert(k.begin() + std::min(t->index, k.size()), std::move(t->objBesitz));
            t->eltern->grenzenAktualisieren();
        }
        else if (t->objBesitz)
            scene.insert(scene.begin() + std::min(t->index, scene.size()), std::move(t->objBesitz));
        else if (t->lichtBesitz)
            lights.insert(lights.begin() + std::min(t->index, lights.size()), std::move(t->lichtBesitz));
    }
}

void loeschenMitVerlauf(const std::string &name, const std::vector<object *> &objekte,
                        const std::vector<light *> &lichter)
{
    if (!gSzeneAendern || (objekte.empty() && lichter.empty()))
        return;
    if (offen)
        aenderungAbschliessen();

    Eintrag e;
    e.name = name;
    for (object *o : objekte)
    {
        Teil t;
        t.obj = o;
        t.hinzu = false;
        e.teile.push_back(std::move(t));
    }
    for (light *l : lichter)
    {
        Teil t;
        t.licht = l;
        t.hinzu = false;
        e.teile.push_back(std::move(t));
    }

    auswahlSetzen({});
    selectedLight = nullptr;
    selectedType = SelectedType::NONE;
    selectionIndex = -1;

    std::vector<Teil *> alle;
    for (auto &t : e.teile)
        alle.push_back(&t);
    gSzeneAendern([&]
                  { teileEntfernen(alle); });
    ablegen(std::move(e));
}

// ======================================================================
// Gruppieren und Aufheben
// ======================================================================

// Die Gruppe und ihre Objekte; wer gerade nicht in der Szene ist, gehoert diesem Schritt
struct GruppenSchritt
{
    Gruppe *gruppe = nullptr;
    std::unique_ptr<Gruppe> gruppeBesitz;   // solange die Objekte einzeln in der Szene liegen
    std::vector<object *> kinder;           // in der Reihenfolge der Gruppe
    std::vector<size_t> plaetze;            // Plaetze der Objekte in der Szene, wenn einzeln (aufsteigend)
};

// bilden: einzelne Objekte -> Gruppe; sonst Gruppe -> einzelne Objekte. In gSzeneAendern aufrufen.
static void gruppenSchrittAusfuehren(GruppenSchritt &g, bool bilden)
{
    if (bilden)
    {
        // Objekte aus der Szene in die Gruppe (von hinten, damit die Plaetze stimmen)
        std::vector<std::unique_ptr<object>> heraus(g.kinder.size());
        for (size_t n = g.kinder.size(); n-- > 0;)
        {
            int i = objektPlatz(g.kinder[n]);
            if (i < 0)
                continue;
            heraus[n] = std::move(scene[size_t(i)]);
            scene.erase(scene.begin() + i);
        }
        for (auto &k : heraus)
            if (k)
                g.gruppeBesitz->kinder.push_back(std::move(k));
        g.gruppeBesitz->grenzenAktualisieren();
        size_t platz = g.plaetze.empty() ? scene.size() : std::min(g.plaetze.front(), scene.size());
        scene.insert(scene.begin() + platz, std::move(g.gruppeBesitz));
    }
    else
    {
        int i = objektPlatz(g.gruppe);
        if (i < 0)
            return;
        g.gruppeBesitz.reset(static_cast<Gruppe *>(scene[size_t(i)].release()));
        scene.erase(scene.begin() + i);

        std::vector<std::unique_ptr<object>> kinder = std::move(g.gruppeBesitz->kinder);
        g.gruppeBesitz->kinder.clear();
        for (size_t n = 0; n < kinder.size(); ++n)
        {
            size_t platz = n < g.plaetze.size() ? std::min(g.plaetze[n], scene.size()) : scene.size();
            scene.insert(scene.begin() + platz, std::move(kinder[n]));
        }
    }
}

Gruppe *gruppieren(const std::vector<object *> &objekte, const std::string &name)
{
    if (!gSzeneAendern || objekte.empty())
        return nullptr;
    if (offen)
        aenderungAbschliessen();

    // nur Objekte direkt in der Szene, in der Reihenfolge der Szene
    std::vector<std::pair<int, object *>> sortiert;
    for (object *o : objekte)
    {
        int i = objektPlatz(o);
        if (i >= 0)
            sortiert.push_back({i, o});
    }
    if (sortiert.empty())
        return nullptr;
    std::sort(sortiert.begin(), sortiert.end());

    auto schritt = std::make_shared<GruppenSchritt>();
    schritt->gruppeBesitz = std::make_unique<Gruppe>();
    schritt->gruppe = schritt->gruppeBesitz.get();
    schritt->gruppe->name = name;
    for (auto &p : sortiert)
    {
        schritt->kinder.push_back(p.second);
        schritt->plaetze.push_back(size_t(p.first));
    }

    gSzeneAendern([&]
                  { gruppenSchrittAusfuehren(*schritt, true); });

    Eintrag e;
    e.name = "Gruppieren";
    e.gruppe = schritt;
    e.gruppieren = true;
    ablegen(std::move(e));

    auswahlSetzen({schritt->gruppe});
    computeSceneBounds();
    return schritt->gruppe;
}

bool gruppeAufheben(Gruppe *gruppe)
{
    int i = objektPlatz(gruppe);
    if (!gSzeneAendern || i < 0)
        return false;
    if (offen)
        aenderungAbschliessen();

    auto schritt = std::make_shared<GruppenSchritt>();
    schritt->gruppe = gruppe;
    for (size_t n = 0; n < gruppe->kinder.size(); ++n)
    {
        schritt->kinder.push_back(gruppe->kinder[n].get());
        schritt->plaetze.push_back(size_t(i) + n); // an die Stelle der Gruppe
    }

    gSzeneAendern([&]
                  { gruppenSchrittAusfuehren(*schritt, false); });

    Eintrag e;
    e.name = "Gruppe aufheben";
    e.gruppe = schritt;
    e.gruppieren = false;
    ablegen(std::move(e));

    auswahlSetzen(schritt->kinder);
    computeSceneBounds();
    return true;
}

void ersetzenMitVerlauf(const std::string &name, object *alt, std::unique_ptr<object> neu)
{
    int i = objektPlatz(alt);
    if (!gSzeneAendern || i < 0 || !neu)
        return;
    if (offen)
        aenderungAbschliessen();

    object *neuZeiger = neu.get();
    Eintrag e;
    e.name = name;
    Teil weg;
    weg.obj = alt;
    weg.hinzu = false;
    Teil dazu;
    dazu.obj = neuZeiger;
    dazu.hinzu = true;
    dazu.index = size_t(i);
    dazu.objBesitz = std::move(neu);
    e.teile.push_back(std::move(weg));
    e.teile.push_back(std::move(dazu));

    gSzeneAendern([&]
                  {
        teileEntfernen({&e.teile[0]});
        teileEinfuegen({&e.teile[1]}); });
    ablegen(std::move(e));

    auswahlSetzen({neuZeiger});
    computeSceneBounds();
}

void ersetzenUndEntfernenMitVerlauf(const std::string &name, object *alt, std::unique_ptr<object> neu,
                                    const std::vector<object *> &entfernen)
{
    if (!gSzeneAendern || !neu || !objektVorhanden(alt))
        return;
    Gruppe *eltern = objektPlatz(alt) < 0 ? gruppeVon(alt) : nullptr; // Teil einer Gruppe: dort ersetzen
    int i = eltern ? platzInGruppe(eltern, alt) : objektPlatz(alt);
    if (offen)
        aenderungAbschliessen();

    object *neuZeiger = neu.get();
    Eintrag e;
    e.name = name;
    Teil weg;
    weg.obj = alt;
    weg.hinzu = false;
    e.teile.push_back(std::move(weg));
    for (object *o : entfernen)
        if (o != alt && objektVorhanden(o))
        {
            Teil w;
            w.obj = o;
            w.hinzu = false;
            e.teile.push_back(std::move(w));
        }
    Teil dazu;
    dazu.obj = neuZeiger;
    dazu.hinzu = true;
    dazu.objBesitz = std::move(neu);
    e.teile.push_back(std::move(dazu));

    gSzeneAendern([&]
                  {
        std::vector<Teil *> wegListe;
        for (size_t k = 0; k + 1 < e.teile.size(); ++k)
            wegListe.push_back(&e.teile[k]);
        // Platz des Ersatzes: wo das alte Objekt war (in der Szene bzw. in seiner Gruppe)
        Teil &ersatz = e.teile.back();
        ersatz.eltern = eltern;
        teileEntfernen(wegListe);
        ersatz.index = size_t(std::max(0, i));
        teileEinfuegen({&ersatz});
        alleGruppenAktualisieren(); });
    ablegen(std::move(e));

    auswahlSetzen({neuZeiger});
    computeSceneBounds();
}

namespace
{
bool polarZerlegen(const Affin &M, Affin &R, Affin &S);
void grenzenAusGeometrie(ObjektZustand &z);
void geometrieAbbilden(const ObjektZustand &q, ObjektZustand &z, const Affin &A, const Affin &drehung);
} // namespace

// mehrere Objekte je durch ein neues ersetzen (jeweils an derselben Stelle, auch in Gruppen) - ein Schritt
std::vector<object *> ersetzenMehrereMitVerlauf(const std::string &name,
                                                std::vector<std::pair<object *, std::unique_ptr<object>>> paare)
{
    std::vector<object *> neue;
    if (!gSzeneAendern)
        return neue;
    if (offen)
        aenderungAbschliessen();
    Eintrag e;
    e.name = name;
    for (auto &p : paare)
        if (p.second && objektVorhanden(p.first))
        {
            Teil w;
            w.obj = p.first;
            w.hinzu = false;
            e.teile.push_back(std::move(w));
        }
    const size_t anzahl = e.teile.size();
    if (anzahl == 0)
        return neue;
    for (auto &p : paare)
        if (p.second && objektVorhanden(p.first))
        {
            Teil d;
            d.obj = p.second.get();
            d.hinzu = true;
            neue.push_back(d.obj);
            d.objBesitz = std::move(p.second);
            e.teile.push_back(std::move(d));
        }
    gSzeneAendern([&]
                  {
        std::vector<Teil *> wegListe, dazuListe;
        for (size_t k = 0; k < anzahl; ++k)
            wegListe.push_back(&e.teile[k]);
        teileEntfernen(wegListe);
        for (size_t k = 0; k < anzahl; ++k) // an den Platz des alten Objekts
        {
            e.teile[anzahl + k].eltern = e.teile[k].eltern;
            e.teile[anzahl + k].index = e.teile[k].index;
            dazuListe.push_back(&e.teile[anzahl + k]);
        }
        teileEinfuegen(dazuListe);
        alleGruppenAktualisieren(); });
    ablegen(std::move(e));
    auswahlSetzen(neue);
    computeSceneBounds();
    return neue;
}

static void basisKopieren(object *ziel, const object *quelle); // weiter unten

// Instanz, oder Gruppe, deren Teile alle (auch verschachtelt) Instanzen sind
bool nurInstanzen(const object *o)
{
    if (dynamic_cast<const Instanz *>(o))
        return true;
    auto *g = dynamic_cast<const Gruppe *>(o);
    if (!g || g->kinder.empty())
        return false;
    for (auto &k : g->kinder)
        if (!dynamic_cast<const VolumenBox *>(k.get()) && !nurInstanzen(k.get()))
            return false;
    return true;
}

// enthaelt (auch verschachtelt) mindestens eine Instanz
bool hatInstanzen(const object *o)
{
    if (dynamic_cast<const Instanz *>(o))
        return true;
    if (auto *g = dynamic_cast<const Gruppe *>(o))
        for (auto &k : g->kinder)
            if (hatInstanzen(k.get()))
                return true;
    return false;
}

// Objekt -> Instanz: eine Kopie wird die Vorlage, die Instanz liegt genau an derselben Stelle.
// Gruppe: bleibt eine Gruppe (mit allen Teilen, einzeln waehlbar), jedes Teil darin wird eine Instanz.
// Vorher wurde die ganze Gruppe eine einzige Instanz - in der Liste ohne Teile ("Kopien ohne Elemente").
std::unique_ptr<object> alsInstanz(const object *o)
{
    if (!o || dynamic_cast<const Instanz *>(o) || dynamic_cast<const VolumenBox *>(o))
        return nullptr;
    if (auto *g = dynamic_cast<const Gruppe *>(o))
    {
        if (nurInstanzen(o))
            return nullptr; // schon umgewandelt
        auto neu = std::make_unique<Gruppe>();
        basisKopieren(neu.get(), o);
        neu->versteckt = o->versteckt;
        for (auto &kind : g->kinder)
        {
            std::unique_ptr<object> k = dynamic_cast<const Instanz *>(kind.get()) || nurInstanzen(kind.get())
                                            ? objektKopieren(kind.get()) // gleiche Vorlage(n)
                                            : alsInstanz(kind.get());
            if (!k)
                k = objektKopieren(kind.get()); // z.B. Rauchbox: als echte Kopie
            if (k)
            {
                k->versteckt = kind->versteckt;
                neu->kinder.push_back(std::move(k));
            }
        }
        if (neu->kinder.empty())
            return nullptr;
        neu->grenzenAktualisieren();
        return neu;
    }
    std::unique_ptr<object> kopie = objektKopieren(o);
    if (!kopie)
        return nullptr;
    kopie->versteckt = false;
    if (auto *g = dynamic_cast<Gruppe *>(kopie.get()))
        g->grenzenAktualisieren();
    auto in = std::make_unique<Instanz>(std::shared_ptr<object>(std::move(kopie)), Affin());
    in->name = o->name;
    in->versteckt = o->versteckt;
    std::copy(o->ausrichtung, o->ausrichtung + 9, in->ausrichtung);
    return in;
}

// Instanz -> eigenstaendige Kopie der Vorlage in der Lage der Instanz
std::unique_ptr<object> instanzAufloesen(const object *o)
{
    if (auto *g = dynamic_cast<const Gruppe *>(o)) // Gruppe mit Instanzen: alle Teile darin loesen
    {
        if (!hatInstanzen(o))
            return nullptr;
        auto neu = std::make_unique<Gruppe>();
        basisKopieren(neu.get(), o);
        neu->versteckt = o->versteckt;
        for (auto &kind : g->kinder)
        {
            std::unique_ptr<object> k = hatInstanzen(kind.get()) ? instanzAufloesen(kind.get()) : objektKopieren(kind.get());
            if (k)
            {
                k->versteckt = kind->versteckt;
                neu->kinder.push_back(std::move(k));
            }
        }
        neu->grenzenAktualisieren();
        return neu;
    }
    auto *in = dynamic_cast<const Instanz *>(o);
    if (!in || !in->vorlage)
        return nullptr;
    std::unique_ptr<object> k = objektKopieren(in->vorlage.get());
    if (!k)
        return nullptr;
    ObjektZustand z = ZustandZugriff::lesen(k.get());
    Affin R, S;
    if (!polarZerlegen(in->abb, R, S))
        R = Affin();
    R.t = Vector3D(0, 0, 0);
    geometrieAbbilden(z, z, in->abb, R);
    // Kugeln und Staebe: Radius mit der mittleren Vergroesserung
    const double f = std::cbrt(std::fabs(in->abb.determinante()));
    std::function<void(ObjektZustand &)> radien = [&](ObjektZustand &x)
    {
        if (x.art == 3 || x.art == 4)
            x.zahl *= f;
        for (auto &kz : x.kinder)
            radien(kz);
    };
    radien(z);
    if (z.art == 3 || z.art == 4 || z.art == 7)
        grenzenAusGeometrie(z);
    ZustandZugriff::setzen(z);
    if (auto *g = dynamic_cast<Gruppe *>(k.get()))
        g->grenzenAktualisieren();
    k->name = in->name;
    k->versteckt = in->versteckt;
    std::copy(in->ausrichtung, in->ausrichtung + 9, k->ausrichtung);
    return k;
}

// Instanzen in der Szene (auch in Gruppen), die diese Vorlage benutzen
size_t instanzenVon(const object *vorlage)
{
    size_t n = 0;
    std::function<void(const object *)> zaehlen = [&](const object *o)
    {
        if (auto *in = dynamic_cast<const Instanz *>(o))
            n += in->vorlage.get() == vorlage ? 1 : 0;
        else if (auto *g = dynamic_cast<const Gruppe *>(o))
            for (auto &k : g->kinder)
                zaehlen(k.get());
    };
    for (auto &s : scene)
        zaehlen(s.get());
    return n;
}

// ======================================================================
// Umzug: Objekte zwischen Szene und Gruppen verschieben (Besitz wandert mit), rueckgaengig machbar
// ======================================================================
struct UmzugSchritt
{
    struct Umzug
    {
        object *o = nullptr;
        Gruppe *von = nullptr; // nullptr = direkt in der Szene
        size_t vonPlatz = 0;
        Gruppe *nach = nullptr;
        size_t nachPlatz = 0;
    };
    std::vector<Umzug> liste;
};

// Objekt aus Szene oder Gruppe nehmen (Besitz geht an den Aufrufer); merkt den bisherigen Platz
static std::unique_ptr<object> herausnehmen(object *o, Gruppe *&von, size_t &platz)
{
    von = nullptr;
    int i = objektPlatz(o);
    if (i >= 0)
    {
        platz = size_t(i);
        std::unique_ptr<object> r = std::move(scene[size_t(i)]);
        scene.erase(scene.begin() + i);
        return r;
    }
    Gruppe *g = gruppeVon(o);
    int k = g ? platzInGruppe(g, o) : -1;
    if (k < 0)
        return nullptr;
    von = g;
    platz = size_t(k);
    std::unique_ptr<object> r = std::move(g->kinder[size_t(k)]);
    g->kinder.erase(g->kinder.begin() + k);
    g->grenzenAktualisieren();
    return r;
}

static void einsetzen(std::unique_ptr<object> o, Gruppe *ziel, size_t platz)
{
    if (!o)
        return;
    if (ziel)
    {
        auto &k = ziel->kinder;
        k.insert(k.begin() + std::min(platz, k.size()), std::move(o));
        ziel->grenzenAktualisieren();
    }
    else
        scene.insert(scene.begin() + std::min(platz, scene.size()), std::move(o));
}

// vorwaerts: jedes Objekt an sein Ziel (Platz beim Ausfuehren gemerkt); rueckwaerts: in umgekehrter Reihenfolge zurueck
static void umzugAusfuehren(UmzugSchritt &u, bool vorwaerts)
{
    if (vorwaerts)
        for (auto &m : u.liste)
        {
            Gruppe *von = nullptr;
            size_t platz = 0;
            std::unique_ptr<object> o = herausnehmen(m.o, von, platz);
            if (!o)
                continue;
            m.von = von;
            m.vonPlatz = platz;
            const size_t ziel = m.nach ? m.nach->kinder.size() : scene.size();
            m.nachPlatz = ziel;
            einsetzen(std::move(o), m.nach, ziel);
        }
    else
        for (size_t n = u.liste.size(); n-- > 0;)
        {
            auto &m = u.liste[n];
            Gruppe *jetzt = nullptr;
            size_t platz = 0;
            std::unique_ptr<object> o = herausnehmen(m.o, jetzt, platz);
            einsetzen(std::move(o), m.von, m.vonPlatz);
        }
    alleGruppenAktualisieren();
}

// ist 'g' (oder eine Gruppe darueber) gleich 'o'?
static bool inOderUnter(const object *o, Gruppe *g)
{
    for (Gruppe *x = g; x; x = gruppeVon(x))
        if (x == o)
            return true;
    return false;
}

static bool umzugMitVerlauf(const std::string &name, const std::vector<object *> &objekte, Gruppe *ziel)
{
    if (!gSzeneAendern)
        return false;
    if (ziel && !objektInSzene(ziel))
        return false;
    auto u = std::make_shared<UmzugSchritt>();
    for (object *o : objekte)
    {
        if (!o || !objektInSzene(o) || dynamic_cast<VolumenBox *>(o))
            continue;
        if (ziel && inOderUnter(o, ziel))
            continue; // eine Gruppe nicht in sich selbst
        if (gruppeVon(o) == ziel && (ziel || objektPlatz(o) >= 0))
            continue; // schon dort
        bool doppelt = false; // Teil einer ebenfalls umziehenden Gruppe: zieht mit ihr
        for (object *a : objekte)
            if (a != o && dynamic_cast<Gruppe *>(a))
                for (Gruppe *x = gruppeVon(o); x; x = gruppeVon(x))
                    doppelt = doppelt || x == a;
        if (doppelt)
            continue;
        UmzugSchritt::Umzug m;
        m.o = o;
        m.nach = ziel;
        u->liste.push_back(m);
    }
    if (u->liste.empty())
        return false;
    if (offen)
        aenderungAbschliessen();
    gSzeneAendern([&]
                  { umzugAusfuehren(*u, true); });
    Eintrag e;
    e.name = name;
    e.umzug = u;
    ablegen(std::move(e));
    std::vector<object *> neu;
    for (auto &m : u->liste)
        neu.push_back(m.o);
    auswahlSetzen(ziel ? std::vector<object *>{ziel} : neu);
    computeSceneBounds();
    return true;
}

bool zurGruppeHinzufuegen(Gruppe *ziel, const std::vector<object *> &objekte)
{
    return ziel && umzugMitVerlauf("Zur Gruppe hinzufügen", objekte, ziel);
}

bool ausGruppeNehmen(const std::vector<object *> &objekte)
{
    std::vector<object *> teile;
    for (object *o : objekte)
        if (gruppeVon(o))
            teile.push_back(o);
    return umzugMitVerlauf("Aus der Gruppe nehmen", teile, nullptr);
}

// Eintrag rueckwaerts (Rueckgaengig) oder vorwaerts (Wiederholen) anwenden
static void anwenden(Eintrag &e, bool rueckwaerts)
{
    gSzeneAendern([&]
                  {
        // erst entfernen, dann einfuegen (Ersetzen: altes Objekt raus, neues an seine Stelle)
        std::vector<Teil *> raus, rein;
        for (auto &t : e.teile)
        {
            bool sollDrin = rueckwaerts ? !t.hinzu : t.hinzu;
            (sollDrin ? rein : raus).push_back(&t);
        }
        teileEntfernen(raus);
        teileEinfuegen(rein);
        if (e.gruppe)
            gruppenSchrittAusfuehren(*e.gruppe, rueckwaerts ? !e.gruppieren : e.gruppieren);
        if (e.umzug)
            umzugAusfuehren(*e.umzug, !rueckwaerts);
        for (auto &z : rueckwaerts ? e.vorher : e.nachher)
            if (objektVorhanden(z.o))
                ZustandZugriff::setzen(z);
        alleGruppenAktualisieren();
        for (auto &z : rueckwaerts ? e.lichtVorher : e.lichtNachher)
            if (lichtPlatz(z.l) >= 0)
                lichtSetzen(z); });

    // betroffene Objekte auswaehlen, soweit sie (wieder) in der Szene sind
    std::vector<object *> objekte;
    light *licht = nullptr;
    for (auto &z : e.vorher)
        if (objektInSzene(z.o))
            objekte.push_back(z.o);
    if (e.gruppe)
    {
        if (objektVorhanden(e.gruppe->gruppe))
            objekte.push_back(e.gruppe->gruppe);
        else
            objekte.insert(objekte.end(), e.gruppe->kinder.begin(), e.gruppe->kinder.end());
    }
    if (e.umzug)
        for (auto &m : e.umzug->liste)
            if (objektInSzene(m.o))
                objekte.push_back(m.o);
    for (auto &t : e.teile)
    {
        if (t.obj && objektPlatz(t.obj) >= 0)
            objekte.push_back(t.obj);
        if (t.licht && lichtPlatz(t.licht) >= 0)
            licht = t.licht;
    }
    for (auto &z : e.lichtVorher)
        if (lichtPlatz(z.l) >= 0)
            licht = z.l;

    if (!objekte.empty())
        auswahlSetzen(objekte);
    else
    {
        auswahlSetzen({});
        selectedLight = licht;
        selectedType = licht ? SelectedType::LIGHT : SelectedType::NONE;
    }

    alleGruppenAktualisieren(); // auch Instanzen einer zurueckgesetzten Vorlage
    computeSceneBounds();
    geaendert = true;
    autoBereichAusEintrag(e); // Autom. Rendern: Lage vorher und nachher
}

bool rueckgaengig()
{
    if (offen)
        aenderungAbschliessen();
    if (rueckStapel.empty() || !gSzeneAendern)
        return false;

    Eintrag e = std::move(rueckStapel.back());
    rueckStapel.pop_back();
    anwenden(e, true);
    std::cout << "Rueckgaengig: " << e.name << "\n";
    vorStapel.push_back(std::move(e));
    return true;
}

bool wiederholen()
{
    if (offen)
        aenderungAbschliessen();
    if (vorStapel.empty() || !gSzeneAendern)
        return false;

    Eintrag e = std::move(vorStapel.back());
    vorStapel.pop_back();
    anwenden(e, false);
    std::cout << "Wiederholen: " << e.name << "\n";
    rueckStapel.push_back(std::move(e));
    return true;
}

std::string naechstesRueckgaengig()
{
    return rueckStapel.empty() ? std::string() : rueckStapel.back().name;
}

std::string naechstesWiederholen()
{
    return vorStapel.empty() ? std::string() : vorStapel.back().name;
}

void verlaufLeeren()
{
    offen = false;
    laufend = Eintrag();
    rueckStapel.clear();
    vorStapel.clear();
}

// ======================================================================
// Speicherstand
// ======================================================================

bool szeneGeaendert() { return geaendert; }
std::atomic<bool> gAutoAuswahlGeaendert{false};
// ohne Verlaufsschritt geaendert (z.B. Textur spiegeln): Autom. Rendern nimmt den Bereich der Auswahl
void szeneGeaendertMarkieren()
{
    geaendert = true;
    gAutoAuswahlGeaendert = true;
    ++gAutoRenderStand;
}
void szeneGespeichertMarkieren() { geaendert = false; }
const std::string &aktuelleDatei() { return dateiName; }
void aktuelleDateiSetzen(const std::string &datei)
{
    dateiName = datei;
    LetzteSzeneMerken(datei); // fuer die Frage beim naechsten Start (leer: nichts aendern)
}

// ======================================================================
// Kopieren
// ======================================================================

static void basisKopieren(object *ziel, const object *quelle)
{
    ziel->mat = quelle->mat;
    ziel->textureImg = quelle->textureImg;
    ziel->textureMode = quelle->textureMode;
    ziel->texture = quelle->texture;
    ziel->materialName = quelle->materialName;
    ziel->textureName = quelle->textureName;
    ziel->texPar = quelle->texPar;
    ziel->texturSpiegelU = quelle->texturSpiegelU;
    ziel->texturSpiegelV = quelle->texturSpiegelV;
    ziel->texturDrehung = quelle->texturDrehung;
    ziel->name = quelle->name;
    std::copy(quelle->ausrichtung, quelle->ausrichtung + 9, ziel->ausrichtung);
}

std::unique_ptr<object> objektKopieren(const object *o)
{
    if (auto in = dynamic_cast<const Instanz *>(o)) // weitere Instanz derselben Vorlage
    {
        auto k = std::make_unique<Instanz>(in->vorlage, in->abb);
        basisKopieren(k.get(), o);
        k->versteckt = false;
        return k;
    }
    if (auto g = dynamic_cast<const Gruppe *>(o))
    {
        auto k = std::make_unique<Gruppe>();
        basisKopieren(k.get(), o);
        for (auto &kind : g->kinder)
            if (auto kopie = objektKopieren(kind.get()))
            {
                kopie->versteckt = kind->versteckt; // ausgeblendete Teile bleiben ausgeblendet
                k->kinder.push_back(std::move(kopie));
            }
        k->grenzenAktualisieren();
        return k;
    }
    if (auto m = dynamic_cast<const Mesh *>(o))
    {
        auto k = std::make_unique<Mesh>(m->triangles, m->position);
        basisKopieren(k.get(), o);
        k->materialName = ""; // bei Meshes die OBJ-Datei: die Kopie wird beim Speichern exportiert
        return k;
    }
    if (auto t = dynamic_cast<const Tree *>(o))
    {
        auto k = std::make_unique<Mesh>(ZustandZugriff::baumDreiecke(t), static_cast<const object *>(t)->position);
        basisKopieren(k.get(), o);
        k->materialName = "";
        return k;
    }
    if (auto k = dynamic_cast<const Kugel *>(o))
        return std::make_unique<Kugel>(*k);
    if (auto s = dynamic_cast<const Stab *>(o))
        return std::make_unique<Stab>(*s);
    if (auto p = dynamic_cast<const Pfeil *>(o))
        return std::make_unique<Pfeil>(*p);
    if (auto tx = dynamic_cast<const Text3D *>(o))
        return std::make_unique<Text3D>(*tx);
    // jede andere Art mit Dreiecken: als Netz kopieren (vorher fiel sie weg - in Gruppen fehlte das Teil dann)
    if (!dynamic_cast<const VolumenBox *>(o))
    {
        std::vector<Dreieck> d;
        for (auto *liste : objektDreiecke(const_cast<object *>(o)))
            d.insert(d.end(), liste->begin(), liste->end());
        if (!d.empty())
        {
            auto k = std::make_unique<Mesh>(d, o->position);
            basisKopieren(k.get(), o);
            k->materialName = "";
            return k;
        }
    }
    return nullptr;
}

std::unique_ptr<object> baumZerlegen(const object *baum)
{
    auto *t = dynamic_cast<const Tree *>(baum);
    if (!t)
        return nullptr;

    auto gruppe = std::make_unique<Gruppe>();
    auto teil = [&](const std::vector<Dreieck> &tris, const char *name)
    {
        if (tris.empty())
            return;
        auto m = std::make_unique<Mesh>(tris, Vector3D(0, 0, 0));
        m->mat = tris.front().mat; // Holz bzw. Blaetter: ein Material je Teil
        m->name = name;
        gruppe->kinder.push_back(std::move(m));
    };
    teil(ZustandZugriff::baumHolz(t), "Stamm");
    teil(ZustandZugriff::baumBlaetter(t), "Bl\xC3\xA4tter"); // UTF-8
    std::copy(baum->ausrichtung, baum->ausrichtung + 9, gruppe->ausrichtung);
    gruppe->grenzenAktualisieren();
    return gruppe;
}

std::unique_ptr<light> lichtKopieren(const light *l)
{
    if (auto sp = dynamic_cast<const SpotLight *>(l)) // vor PointLight (Spotlicht ist eines)
        return std::make_unique<SpotLight>(*sp);
    if (auto p = dynamic_cast<const PointLight *>(l))
        return std::make_unique<PointLight>(*p);
    if (auto s = dynamic_cast<const SphereLight *>(l))
        return std::make_unique<SphereLight>(*s);
    if (auto a = dynamic_cast<const AreaLight *>(l))
        return std::make_unique<AreaLight>(*a);
    if (auto m = dynamic_cast<const MeshLight *>(l))
        return m->kopie();
    if (auto d = dynamic_cast<const DirectionalLight *>(l))
    {
        auto k = std::make_unique<DirectionalLight>(*d);
        richtungslichtSymbolSetzen(k.get(), richtungslichtSymbol(d)); // Symbol an derselben Stelle
        return k;
    }
    return nullptr;
}

// ======================================================================
// Animation: Snapshots und lineares Mischen dazwischen (animation.h)
// ======================================================================

namespace
{
struct Snapshot
{
    double zeit = 0;
    std::string label; // Beschriftung in der Zeitleiste (UTF-8)
    std::vector<ObjektZustand> objekte;
    std::vector<LichtZustand> lichter;
    bool mitKamera = false;
    Vector3D kameraPosition, kameraZiel;
    double kameraFov = 60;
    std::vector<Wiederholung> wiederholungen; // fuer den Abschnitt bis zum naechsten Snapshot
};

std::vector<Snapshot> snapshots; // nach Zeit sortiert
bool animationAn = false;
double zeitJetzt = 0;
int fps = 24;

double mischen(double a, double b, double f) { return a + (b - a) * f; }
Vector3D mischen(const Vector3D &a, const Vector3D &b, double f) { return a + (b - a) * f; }
Farbe mischen(const Farbe &a, const Farbe &b, double f)
{
    return Farbe(mischen(a.r, b.r, f), mischen(a.g, b.g, f), mischen(a.b, b.b, f));
}

Material mischen(const Material &a, const Material &b, double f)
{
    Material m = a;
    m.ambient = mischen(a.ambient, b.ambient, f);
    m.diffuse = mischen(a.diffuse, b.diffuse, f);
    m.specular = mischen(a.specular, b.specular, f);
    m.emission = mischen(a.emission, b.emission, f);
    m.absorption = mischen(a.absorption, b.absorption, f);
    m.kA = mischen(a.kA, b.kA, f);
    m.kD = mischen(a.kD, b.kD, f);
    m.shininess = mischen(a.shininess, b.shininess, f);
    m.specularStrength = mischen(a.specularStrength, b.specularStrength, f);
    m.roughness = mischen(a.roughness, b.roughness, f);
    m.reflection = mischen(a.reflection, b.reflection, f);
    m.IndexOfRefraction = mischen(a.IndexOfRefraction, b.IndexOfRefraction, f);
    m.transparency = mischen(a.transparency, b.transparency, f);
    return m;
}

// Dreiecke Ecke fuer Ecke mischen (Verschieben, Drehen, Skalieren); gleiche Anzahl vorausgesetzt
bool dreieckeMischen(const std::vector<Dreieck> &a, const std::vector<Dreieck> &b, double f, std::vector<Dreieck> &ziel)
{
    if (a.size() != b.size())
        return false;
    ziel = a;
    for (size_t i = 0; i < a.size(); ++i)
    {
        ziel[i].a = mischen(a[i].a, b[i].a, f);
        ziel[i].b = mischen(a[i].b, b[i].b, f);
        ziel[i].c = mischen(a[i].c, b[i].c, f);
        ziel[i].mat = mischen(a[i].mat, b[i].mat, f);
        if (a[i].hasVertexNormals && b[i].hasVertexNormals) // weiche Schattierung beim Drehen
        {
            ziel[i].nA = mischen(a[i].nA, b[i].nA, f).normalized();
            ziel[i].nB = mischen(a[i].nB, b[i].nB, f).normalized();
            ziel[i].nC = mischen(a[i].nC, b[i].nC, f).normalized();
        }
        ziel[i].rebuild(); // Normale, Grenzen
    }
    return true;
}

// ---------- Kreisbogen statt Sehne ----------
// Die Abbildung a -> b (affin, aus den Dreiecksecken) wird in Drehung R und Dehnung S zerlegen (M = R*S).
// Fuer den Anteil f: Drehung um f*Winkel, Dehnung linear, die Objektmitte laeuft geradlinig. Bisher wurden die
// Ecken geradlinig gemischt: ein um 90 Grad gedrehtes Objekt schrumpfte unterwegs auf ~70 %, eine volle
// Umdrehung war unsichtbar.

const double PI_A = 3.14159265358979323846;

// Drehung um 'achse' durch 'mitte' (Rodrigues)
Affin drehAffin(Vector3D achse, double winkel, const Vector3D &mitte)
{
    Affin r;
    const double l = achse.length();
    if (l < 1e-300 || winkel == 0.0)
        return r;
    achse = achse * (1.0 / l);
    const double c = std::cos(winkel), s = std::sin(winkel), t = 1.0 - c;
    const double x = achse.x, y = achse.y, z = achse.z;
    r.m[0][0] = t * x * x + c;
    r.m[0][1] = t * x * y - s * z;
    r.m[0][2] = t * x * z + s * y;
    r.m[1][0] = t * x * y + s * z;
    r.m[1][1] = t * y * y + c;
    r.m[1][2] = t * y * z - s * x;
    r.m[2][0] = t * x * z - s * y;
    r.m[2][1] = t * y * z + s * x;
    r.m[2][2] = t * z * z + c;
    r.t = mitte - r.richtung(mitte);
    return r;
}

// Polarzerlegung M = R * S (R Drehung, S symmetrisch positiv) mit der Iteration R <- (R + R^-T) / 2.
// false bei Spiegelung oder entarteter Abbildung (dann wird linear gemischt).
bool polarZerlegen(const Affin &M, Affin &R, Affin &S)
{
    if (!(M.determinante() > 1e-300))
        return false;
    R = M;
    R.t = Vector3D(0, 0, 0);
    for (int schritt = 0; schritt < 80; ++schritt)
    {
        const double det = R.determinante();
        if (!(std::fabs(det) > 1e-300))
            return false;
        double k[3][3];
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
            {
                const int i1 = (i + 1) % 3, i2 = (i + 2) % 3, j1 = (j + 1) % 3, j2 = (j + 2) % 3;
                k[i][j] = (R.m[i1][j1] * R.m[i2][j2] - R.m[i1][j2] * R.m[i2][j1]) / det; // (R^-1)^T
            }
        double aenderung = 0;
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
            {
                const double n = 0.5 * (R.m[i][j] + k[i][j]);
                aenderung += std::fabs(n - R.m[i][j]);
                R.m[i][j] = n;
            }
        if (aenderung < 1e-14)
            break;
    }
    S = Affin();
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            S.m[i][j] = R.m[0][i] * M.m[0][j] + R.m[1][i] * M.m[1][j] + R.m[2][i] * M.m[2][j]; // R^T * M
    return true;
}

// Achse und Winkel (0..pi) einer Drehmatrix
void achseUndWinkel(const Affin &R, Vector3D &achse, double &winkel)
{
    const double spur = R.m[0][0] + R.m[1][1] + R.m[2][2];
    winkel = std::acos(std::clamp((spur - 1.0) * 0.5, -1.0, 1.0));
    const Vector3D v(R.m[2][1] - R.m[1][2], R.m[0][2] - R.m[2][0], R.m[1][0] - R.m[0][1]); // 2 sin(w) * Achse
    if (v.length() > 1e-7)
    {
        achse = v * (1.0 / v.length());
        return;
    }
    if (winkel < 1e-6)
    {
        achse = Vector3D(0, 1, 0);
        winkel = 0;
        return;
    }
    // fast 180 Grad: (R + I) / 2 = Achse * Achse^T, groesste Spalte nehmen
    int i = 0;
    for (int k = 1; k < 3; ++k)
        if (R.m[k][k] > R.m[i][i])
            i = k;
    Vector3D sp(R.m[0][i] + (i == 0), R.m[1][i] + (i == 1), R.m[2][i] + (i == 2));
    achse = sp.length() > 1e-300 ? sp * (1.0 / sp.length()) : Vector3D(0, 1, 0);
}

// Abbildung fuer den Anteil f von A (um die Mitte c); 'drehung' bekommt den reinen Drehteil
Affin affinAnteil(const Affin &A, const Vector3D &c, double f, Affin &drehung)
{
    Affin R, S, L;
    drehung = Affin();
    if (polarZerlegen(A, R, S))
    {
        Vector3D achse;
        double w;
        achseUndWinkel(R, achse, w);
        drehung = drehAffin(achse, w * f, Vector3D(0, 0, 0));
        Affin Sf;
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
                Sf.m[i][j] = (i == j ? 1.0 : 0.0) + f * (S.m[i][j] - (i == j ? 1.0 : 0.0));
        L = drehung.nach(Sf); // erst dehnen, dann drehen
    }
    else
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
                L.m[i][j] = (i == j ? 1.0 : 0.0) + f * (A.m[i][j] - (i == j ? 1.0 : 0.0));
    L.t = Vector3D(0, 0, 0);
    const Vector3D ziel = c + (A.punkt(c) - c) * f; // Mitte geradlinig
    L.t = ziel - L.richtung(c);
    return L;
}

// Grenzen eines Zustands aus seiner Geometrie (Kinder muessen schon stimmen)
void grenzenAusGeometrie(ObjektZustand &z)
{
    bool leer = true;
    Vector3D mn, mx;
    auto dazu = [&](const Vector3D &p)
    {
        if (leer)
        {
            mn = mx = p;
            leer = false;
            return;
        }
        mn = Vector3D(std::min(mn.x, p.x), std::min(mn.y, p.y), std::min(mn.z, p.z));
        mx = Vector3D(std::max(mx.x, p.x), std::max(mx.y, p.y), std::max(mx.z, p.z));
    };
    for (const auto *liste : {&z.d1, &z.d2})
        for (const Dreieck &d : *liste)
        {
            dazu(d.a);
            dazu(d.b);
            dazu(d.c);
        }
    const Vector3D r3(z.zahl, z.zahl, z.zahl);
    switch (z.art)
    {
    case 3: // Kugel
        dazu(z.position - r3);
        dazu(z.position + r3);
        break;
    case 4: // Stab: Endpunkte relativ zu position
        dazu(z.position + z.v1 - r3);
        dazu(z.position + z.v1 + r3);
        dazu(z.position + z.v2 - r3);
        dazu(z.position + z.v2 + r3);
        break;
    case 7:
        for (const auto &k : z.kinder)
        {
            dazu(k.minBound);
            dazu(k.maxBound);
        }
        break;
    case 8:
        return; // Rauchbox: Box selbst
    case 9: // Instanz: Box der Vorlage abgebildet
        if (auto *in = dynamic_cast<Instanz *>(z.o))
            if (in->vorlage)
            {
                Vector3D a = in->vorlage->minBound, b = in->vorlage->maxBound;
                in->vorlage->getWorldAABB(a, b);
                for (int i = 0; i < 8; ++i)
                    dazu(z.abb.punkt(Vector3D((i & 1) ? b.x : a.x, (i & 2) ? b.y : a.y, (i & 4) ? b.z : a.z)));
            }
        break;
    default:
        break;
    }
    if (leer)
        return;
    if (z.art == 9)
        z.position = (mn + mx) * 0.5;
    z.minBound = mn;
    z.maxBound = mx;
    if (z.art == 3)
    {
        z.boundingCenter = z.position;
        z.boundingRadius = z.zahl;
    }
    else
    {
        z.boundingCenter = (mn + mx) * 0.5;
        z.boundingRadius = (mx - mn).length() * 0.5;
    }
}

// Geometrie von q mit A abbilden und nach z schreiben (Material usw. von z bleiben); q und z duerfen gleich sein.
// 'drehung' (nur der Drehteil) fuehrt die Ausrichtung fuer die Anzeige der Drehwinkel mit.
void geometrieAbbilden(const ObjektZustand &q, ObjektZustand &z, const Affin &A, const Affin &drehung)
{
    auto liste = [&](const std::vector<Dreieck> &von, std::vector<Dreieck> &nach)
    {
        if (von.size() != nach.size())
            return;
        for (size_t i = 0; i < von.size(); ++i)
        {
            const Dreieck &d = von[i];
            Dreieck &e = nach[i];
            const Vector3D a = A.punkt(d.a), b = A.punkt(d.b), c = A.punkt(d.c);
            e.a = a;
            e.b = b;
            e.c = c;
            if (d.hasVertexNormals)
            {
                const Vector3D na = A.normale(d.nA), nb = A.normale(d.nB), nc = A.normale(d.nC);
                e.nA = na;
                e.nB = nb;
                e.nC = nc;
                e.hasVertexNormals = true;
            }
            e.rebuild();
        }
    };
    liste(q.d1, z.d1);
    liste(q.d2, z.d2);
    z.position = A.punkt(q.position);
    double aus[9];
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c)
            aus[r * 3 + c] = drehung.m[r][0] * q.ausrichtung[c] + drehung.m[r][1] * q.ausrichtung[3 + c] +
                             drehung.m[r][2] * q.ausrichtung[6 + c];
    std::copy(aus, aus + 9, z.ausrichtung);
    switch (q.art)
    {
    case 2: // eigene Position des Baums
        z.v1 = A.punkt(q.v1);
        break;
    case 4: // Stab: Endpunkte relativ zu position (die schon abgebildet ist)
    {
        const Vector3D p1 = A.richtung(q.v1), p2 = A.richtung(q.v2);
        z.v1 = p1;
        z.v2 = p2;
        break;
    }
    case 5: // Pfeil: Spitze relativ zum Start
        z.v1 = A.richtung(q.v1);
        break;
    case 6: // Text: Achsen
    {
        const Vector3D x = A.richtung(q.v1), y = A.richtung(q.v2), w = A.richtung(q.v3);
        z.v1 = x;
        z.v2 = y;
        z.v3 = w;
        break;
    }
    case 7:
        if (q.kinder.size() == z.kinder.size())
            for (size_t k = 0; k < q.kinder.size(); ++k)
                geometrieAbbilden(q.kinder[k], z.kinder[k], A, drehung);
        break;
    case 9: // Instanz: Abbildung davor schalten
        z.abb = A.nach(q.abb);
        break;
    default:
        break;
    }
    if (q.art != 8)
        grenzenAusGeometrie(z);
}

// Dreieckslisten (auch der Kinder einer Gruppe) fuer die Abbildung a -> b; false, wenn der Aufbau nicht passt
bool dreieckListenPaare(const ObjektZustand &a, const ObjektZustand &b, std::vector<const std::vector<Dreieck> *> &la,
                        std::vector<const std::vector<Dreieck> *> &lb)
{
    if (a.art != b.art || a.d1.size() != b.d1.size() || a.d2.size() != b.d2.size() || a.kinder.size() != b.kinder.size())
        return false;
    if (!a.d1.empty())
    {
        la.push_back(&a.d1);
        lb.push_back(&b.d1);
    }
    if (!a.d2.empty())
    {
        la.push_back(&a.d2);
        lb.push_back(&b.d2);
    }
    for (size_t k = 0; k < a.kinder.size(); ++k)
        if (!dreieckListenPaare(a.kinder[k], b.kinder[k], la, lb))
            return false;
    return true;
}

// Abbildung a -> b je Snapshot-Paar nur einmal berechnen (sonst je Bild bis zu 4000 Dreiecke ausgleichen)
// Schraubung (Satz von Chasles): jede starre Bewegung = Drehung um eine Achse + Schub entlang der Achse.
// Bei einer Tuer ist die Achse das Scharnier. Geht die Achse durch das Objekt oder knapp daneben, wird um
// sie gedreht (Scharnier bleibt fest); liegt sie weit weg (Objekt faehrt weit und dreht sich dabei),
// laeuft die Mitte geradlinig und das Objekt dreht um sich selbst - sonst fuehre es einen grossen Bogen.
struct Schraubung
{
    bool ok = false;
    Vector3D punkt, achse, schub; // Punkt auf der Achse, Achsrichtung (normiert), Schub entlang der Achse
    double winkel = 0;
};

// R: reine Drehung (Achse u, Winkel w), Abbildung p -> R p + t; mitte/halbeDiagonale: Groesse des Objekts
Schraubung schraubungFinden(const Vector3D &u, double w, const Vector3D &t, const Vector3D &mitte, double halbeDiagonale)
{
    Schraubung s;
    if (w < 1e-7) // keine Drehung (bei 180 Grad ist cot(90) = 0, die Achse trotzdem eindeutig)
        return s;
    const Vector3D tPar = u * (t * u), tSenk = t - tPar;
    // Punkt q auf der Achse (senkrecht zu u): (I - R) q = tSenk  =>  q = (tSenk + cot(w/2) * u x tSenk) / 2
    const Vector3D q = (tSenk + u.cross(tSenk) * (1.0 / std::tan(w * 0.5))) * 0.5;
    const Vector3D zurMitte = mitte - q;
    const double abstand = (zurMitte - u * (zurMitte * u)).length();
    if (abstand > 1.1 * halbeDiagonale + 1e-12)
        return s; // Achse weit weg: kein Scharnier
    s.ok = true;
    s.punkt = q;
    s.achse = u;
    s.winkel = w;
    s.schub = tPar;
    return s;
}

struct MischAbbildung
{
    bool ok = false; // affin und mit Drehung (ohne Drehung ist das geradlinige Mischen der Ecken schon exakt)
    Affin A;
    Vector3D mitte;
    Schraubung schraube; // starre Bewegung um eine Achse am/im Objekt (Scharnier)
    bool affin = false;  // Abbildung A gefunden (auch ohne Drehung)
};
std::mutex mischMutex;
std::map<std::pair<const ObjektZustand *, const ObjektZustand *>, MischAbbildung> mischSpeicher;

void mischSpeicherLeeren()
{
    std::lock_guard<std::mutex> lock(mischMutex);
    mischSpeicher.clear();
}

MischAbbildung abbildungZwischen(const ObjektZustand &a, const ObjektZustand &b)
{
    {
        std::lock_guard<std::mutex> lock(mischMutex);
        auto it = mischSpeicher.find({&a, &b});
        if (it != mischSpeicher.end())
            return it->second;
    }
    MischAbbildung m;
    std::vector<const std::vector<Dreieck> *> la, lb;
    Affin R, S;
    Vector3D achse;
    double winkel = 0;
    // Instanz: die Abbildung a -> b ergibt sich direkt aus beiden Lagen
    const bool instanz = a.art == 9 && b.art == 9;
    auto abbildungFinden = [&]() -> bool
    {
        if (instanz)
        {
            m.A = b.abb.nach(affinInvers(a.abb));
            return true;
        }
        return dreieckListenPaare(a, b, la, lb) && !la.empty() && affinAnpassen(la, lb, m.A);
    };
    if (abbildungFinden() && (m.affin = true) && polarZerlegen(m.A, R, S) && (achseUndWinkel(R, achse, winkel), winkel > 1e-7))
    {
        m.ok = true;
        bool leer = !instanz;
        Vector3D mn = a.minBound, mx = a.maxBound;
        for (auto *l : la)
            for (const Dreieck &d : *l)
                for (const Vector3D &p : {d.a, d.b, d.c})
                {
                    if (leer)
                    {
                        mn = mx = p;
                        leer = false;
                    }
                    mn = Vector3D(std::min(mn.x, p.x), std::min(mn.y, p.y), std::min(mn.z, p.z));
                    mx = Vector3D(std::max(mx.x, p.x), std::max(mx.y, p.y), std::max(mx.z, p.z));
                }
        m.mitte = (mn + mx) * 0.5;
        // starr (keine Dehnung)? Dann Drehachse suchen
        double dehnung = 0;
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
                dehnung = std::max(dehnung, std::fabs(S.m[i][j] - (i == j ? 1.0 : 0.0)));
        if (dehnung < 1e-3)
            m.schraube = schraubungFinden(achse, winkel, m.A.t, m.mitte, (mx - mn).length() * 0.5);
    }
    std::lock_guard<std::mutex> lock(mischMutex);
    mischSpeicher[{&a, &b}] = m;
    return m;
}

// Stab allein: Richtung auf dem Kreisbogen, Laenge linear; Mitte geradlinig bzw. um ein Gelenk (Zeiger, Hebel)
void stabAufBogen(const ObjektZustand &a, const ObjektZustand &b, double f, ObjektZustand &z)
{
    const Vector3D da = a.v2 - a.v1, db = b.v2 - b.v1;
    const double la = da.length(), lb = db.length();
    if (la < 1e-12 || lb < 1e-12)
        return;
    const Vector3D kreuz = da.cross(db);
    const double w = std::atan2(kreuz.length(), da * db);
    if (w < 1e-9)
        return;
    const Affin R = drehAffin(kreuz, w * f, Vector3D(0, 0, 0));
    if (std::fabs(la - lb) < 1e-6 * la) // gleich lang: starr, Gelenk an einem Ende oder in der Mitte?
    {
        // Endpunkte in der Welt: position + v1/v2
        const Affin ganz = drehAffin(kreuz, w, Vector3D(0, 0, 0));
        const Vector3D t = (b.position + b.v1) - ganz.punkt(a.position + a.v1);
        const Schraubung sch =
            schraubungFinden(kreuz * (1.0 / kreuz.length()), w, t, a.position + (a.v1 + a.v2) * 0.5, la * 0.5 + a.zahl);
        if (sch.ok)
        {
            Affin L = drehAffin(sch.achse, sch.winkel * f, sch.punkt);
            L.t = L.t + sch.schub * f;
            z.position = L.punkt(a.position);
            z.v1 = L.richtung(a.v1);
            z.v2 = L.richtung(a.v2);
            grenzenAusGeometrie(z);
            return;
        }
    }
    const Vector3D richtung = R.richtung(da * (1.0 / la));
    const Vector3D mitte = mischen((a.v1 + a.v2) * 0.5, (b.v1 + b.v2) * 0.5, f); // lokal (position laeuft geradlinig)
    const double laenge = mischen(la, lb, f);
    z.v1 = mitte - richtung * (laenge * 0.5);
    z.v2 = mitte + richtung * (laenge * 0.5);
    grenzenAusGeometrie(z);
}
} // namespace

// Zustand zwischen a (f = 0) und b (f = 1). Passt der Aufbau nicht (z.B. anderer Text, andere
// Anzahl Dreiecke), gilt a bis zum Zeitpunkt von b.
static ObjektZustand zustandMischen(const ObjektZustand &a, const ObjektZustand &b, double f)
{
    if (a.art != b.art || f <= 0.0)
        return a;
    if (f >= 1.0)
        return b;

    ObjektZustand z = a;
    z.mat = mischen(a.mat, b.mat, f);
    for (int i = 0; i < 9; ++i)
        z.ausrichtung[i] = mischen(a.ausrichtung[i], b.ausrichtung[i], f); // nur fuer die Anzeige der Drehwinkel
    z.position = mischen(a.position, b.position, f);
    z.minBound = mischen(a.minBound, b.minBound, f);
    z.maxBound = mischen(a.maxBound, b.maxBound, f);
    z.boundingCenter = mischen(a.boundingCenter, b.boundingCenter, f);
    z.boundingRadius = mischen(a.boundingRadius, b.boundingRadius, f);
    z.zahl = mischen(a.zahl, b.zahl, f);
    z.v1 = mischen(a.v1, b.v1, f);
    z.v2 = mischen(a.v2, b.v2, f);
    z.v3 = mischen(a.v3, b.v3, f);

    if (a.art == 9) // Instanz: Abbildung geradlinig (Drehungen unten auf dem Kreisbogen)
    {
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
                z.abb.m[i][j] = mischen(a.abb.m[i][j], b.abb.m[i][j], f);
        z.abb.t = mischen(a.abb.t, b.abb.t, f);
        grenzenAusGeometrie(z);
    }
    bool passt = dreieckeMischen(a.d1, b.d1, f, z.d1) && dreieckeMischen(a.d2, b.d2, f, z.d2);
    if (a.art == 6 && a.text != b.text)
        passt = false;
    if (a.kinder.size() != b.kinder.size())
        passt = false;
    else
        for (size_t i = 0; i < a.kinder.size(); ++i)
            z.kinder[i] = zustandMischen(a.kinder[i], b.kinder[i], f);
    if (!passt)
        return a;

    // Drehungen auf dem Kreisbogen statt geradlinig: Abbildung a -> b zerlegen (auch eine ganze Gruppe
    // als Einheit, damit ihre Teile um die gemeinsame Mitte schwenken)
    if (a.art == 4)
        stabAufBogen(a, b, f, z);
    else if (a.art == 1 || a.art == 2 || a.art == 5 || a.art == 6 || a.art == 7 || a.art == 9)
    {
        const MischAbbildung m = abbildungZwischen(a, b);
        if (m.ok)
        {
            Affin drehung, L;
            if (m.schraube.ok) // um das Scharnier
            {
                drehung = drehAffin(m.schraube.achse, m.schraube.winkel * f, Vector3D(0, 0, 0));
                L = drehAffin(m.schraube.achse, m.schraube.winkel * f, m.schraube.punkt);
                L.t = L.t + m.schraube.schub * f;
            }
            else // um die eigene Mitte, die geradlinig laeuft
                L = affinAnteil(m.A, m.mitte, f, drehung);
            geometrieAbbilden(a, z, L, drehung);
        }
    }
    return z;
}

bool animationAktiv() { return animationAn; }
void animationAktivSetzen(bool an) { animationAn = an; }
int snapshotAnzahl() { return int(snapshots.size()); }
double snapshotZeit(int i) { return (i >= 0 && i < int(snapshots.size())) ? snapshots[size_t(i)].zeit : 0.0; }
double animationsBeginn() { return snapshots.empty() ? 0.0 : snapshots.front().zeit; }
double animationsEnde() { return snapshots.empty() ? 0.0 : snapshots.back().zeit; }
double aktuelleZeit() { return zeitJetzt; }
int bilderProSekunde() { return fps; }
void bilderProSekundeSetzen(int f) { fps = std::max(1, f); }

int anzahlBilder()
{
    if (snapshots.size() < 2)
        return snapshots.empty() ? 0 : 1;
    return int(std::floor((animationsEnde() - animationsBeginn()) * fps + 1e-9)) + 1;
}

// Beim Festhalten eines Snapshots nicht die Pose mit den Wiederholungen speichern (sonst doppelt):
// Grundzustand ohne Wiederholungen und Kennzeichen des angezeigten Zustands (unveraendert = Grundzustand nehmen)
struct Kennzeichen
{
    std::vector<double> werte;
    bool operator==(const Kennzeichen &x) const
    {
        if (werte.size() != x.werte.size())
            return false;
        for (size_t i = 0; i < werte.size(); ++i)
            if (std::fabs(werte[i] - x.werte[i]) > 1e-9 * (1.0 + std::fabs(werte[i])))
                return false;
        return true;
    }
};
static void kennzeichenSammeln(const object *o, Kennzeichen &k)
{
    for (const Vector3D &v : {o->minBound, o->maxBound})
        k.werte.insert(k.werte.end(), {v.x, v.y, v.z});
    k.werte.insert(k.werte.end(), {o->mat.diffuse.r, o->mat.diffuse.g, o->mat.diffuse.b, o->mat.transparency,
                                   o->mat.reflection, o->mat.emission.r, o->mat.emission.g, o->mat.emission.b});
    if (auto *g = dynamic_cast<const Gruppe *>(o))
        for (auto &kind : g->kinder)
            kennzeichenSammeln(kind.get(), k);
}
static std::map<const object *, std::pair<ObjektZustand, Kennzeichen>> grundZustaende;

static Snapshot szeneFesthalten(double zeit)
{
    Snapshot s;
    s.zeit = zeit;
    for (auto &o : scene)
    {
        auto it = grundZustaende.find(o.get());
        Kennzeichen jetzt;
        if (it != grundZustaende.end())
            kennzeichenSammeln(o.get(), jetzt);
        if (it != grundZustaende.end() && jetzt == it->second.second)
            s.objekte.push_back(it->second.first); // unveraendert seit der Anzeige: Pose ohne Wiederholungen
        else
            s.objekte.push_back(ZustandZugriff::lesen(o.get()));
    }
    for (auto &l : lights)
        s.lichter.push_back(lichtLesen(l.get()));
    if (!cam.empty())
    {
        s.mitKamera = true;
        s.kameraPosition = cam[0].position;
        s.kameraZiel = cam[0].lookAt;
        s.kameraFov = cam[0].fov;
    }
    return s;
}

static int einsortieren(Snapshot &&s)
{
    mischSpeicherLeeren();
    auto it = std::find_if(snapshots.begin(), snapshots.end(), [&](const Snapshot &x)
                           { return x.zeit > s.zeit; });
    int i = int(it - snapshots.begin());
    snapshots.insert(it, std::move(s));
    return i;
}

int snapshotAufnehmen(double zeit)
{
    // gleiche Zeit: ersetzen statt doppelt
    for (size_t i = 0; i < snapshots.size(); ++i)
        if (std::fabs(snapshots[i].zeit - zeit) < 1e-9)
        {
            mischSpeicherLeeren();
            std::string label = snapshots[i].label; // Beschriftung und Wiederholungen bleiben
            auto wdh = std::move(snapshots[i].wiederholungen);
            snapshots[i] = szeneFesthalten(zeit);
            snapshots[i].label = label;
            snapshots[i].wiederholungen = std::move(wdh);
            zeitJetzt = zeit;
            szeneGeaendertMarkieren();
            return int(i);
        }
    zeitJetzt = zeit;
    szeneGeaendertMarkieren();
    return einsortieren(szeneFesthalten(zeit));
}

void snapshotErsetzen(int i)
{
    if (i < 0 || i >= int(snapshots.size()))
        return;
    mischSpeicherLeeren();
    std::string label = snapshots[size_t(i)].label; // Beschriftung und Wiederholungen bleiben
    auto wdh = std::move(snapshots[size_t(i)].wiederholungen);
    snapshots[size_t(i)] = szeneFesthalten(snapshots[size_t(i)].zeit);
    snapshots[size_t(i)].label = label;
    snapshots[size_t(i)].wiederholungen = std::move(wdh);
    szeneGeaendertMarkieren();
}

std::string snapshotLabel(int i)
{
    return i >= 0 && i < int(snapshots.size()) ? snapshots[size_t(i)].label : std::string();
}

void snapshotLabelSetzen(int i, const std::string &label)
{
    if (i < 0 || i >= int(snapshots.size()) || snapshots[size_t(i)].label == label)
        return;
    snapshots[size_t(i)].label = label;
    szeneGeaendertMarkieren();
}

void snapshotLoeschen(int i)
{
    if (i < 0 || i >= int(snapshots.size()))
        return;
    mischSpeicherLeeren();
    snapshots.erase(snapshots.begin() + i);
    szeneGeaendertMarkieren();
}

int snapshotZeitSetzen(int i, double z)
{
    if (i < 0 || i >= int(snapshots.size()))
        return i;
    mischSpeicherLeeren();
    Snapshot s = std::move(snapshots[size_t(i)]);
    snapshots.erase(snapshots.begin() + i);
    s.zeit = z;
    szeneGeaendertMarkieren();
    return einsortieren(std::move(s));
}

void animationLeeren()
{
    mischSpeicherLeeren();
    grundZustaende.clear();
    snapshots.clear();
    ablaufEinsaetze().clear(); // die Objekte gibt es nicht mehr (Vorlagen bleiben)
    zeitJetzt = 0;
}

// gespeicherter Objekt-Zeiger: gibt es das Objekt noch, und ist es noch vom gleichen Typ?
// (Objekte aus dem Rueckgaengig-Verlauf koennen inzwischen zerstoert sein)
static bool zustandAnwendbar(const ObjektZustand &z)
{
    if (!objektVorhanden(z.o))
        return false;
    switch (z.art)
    {
    case 1:
        return dynamic_cast<Mesh *>(z.o) != nullptr;
    case 2:
        return dynamic_cast<Tree *>(z.o) != nullptr;
    case 3:
        return dynamic_cast<Kugel *>(z.o) != nullptr;
    case 4:
        return dynamic_cast<Stab *>(z.o) != nullptr;
    case 5:
        return dynamic_cast<Pfeil *>(z.o) != nullptr;
    case 6:
        return dynamic_cast<Text3D *>(z.o) != nullptr;
    case 7:
        return dynamic_cast<Gruppe *>(z.o) != nullptr;
    case 8:
        return dynamic_cast<VolumenBox *>(z.o) != nullptr && static_cast<VolumenBox *>(z.o)->gueltig();
    case 9:
        return dynamic_cast<Instanz *>(z.o) != nullptr;
    default:
        return true;
    }
}

// ---------- Wiederholungen (Drehen, Pendeln, Wippen, Abrollen je Abschnitt) ----------

// Anzahl Umdrehungen/Schwingungen im Abschnitt a -> b (ganze Zahl: am Ende wieder wie im Snapshot)
static int zyklenFuer(const Wiederholung &w, const Snapshot &a, const Snapshot &b, const ObjektZustand *za,
                      const ObjektZustand *zb, double *periode, double *radius)
{
    if (periode)
        *periode = 0;
    if (radius)
        *radius = 0;
    const double dauer = b.zeit - a.zeit;
    if (dauer <= 1e-9 || w.art == WIEDERHOLUNG_KEINE)
        return 0;
    if (w.art == WIEDERHOLUNG_ABROLLEN)
    {
        if (!za || !zb)
            return 0;
        const Vector3D ca = (za->minBound + za->maxBound) * 0.5, cb = (zb->minBound + zb->maxBound) * 0.5;
        const double r = ca.y - za->minBound.y; // Mitte bis Unterkante = Radius des Rads
        Vector3D d = cb - ca;
        d.y = 0;
        if (radius)
            *radius = r;
        if (r < 1e-9 || d.length() < 1e-12)
            return 0;
        const int n = int(std::lround(d.length() / (2.0 * PI_A * r)));
        if (periode && n > 0)
            *periode = dauer / n;
        return n;
    }
    if (!(w.periode > 1e-9))
        return 0;
    const int n = std::max(1, int(std::lround(dauer / w.periode)));
    if (periode)
        *periode = dauer / n;
    return n;
}

// Abbildung umkehren (false, wenn entartet)
static bool affinUmkehren(const Affin &A, Affin &inv)
{
    const double det = A.determinante();
    if (!(std::fabs(det) > 1e-300))
        return false;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
        {
            const int i1 = (j + 1) % 3, i2 = (j + 2) % 3, j1 = (i + 1) % 3, j2 = (i + 2) % 3;
            inv.m[i][j] = (A.m[i1][j1] * A.m[i2][j2] - A.m[i1][j2] * A.m[i2][j1]) / det;
        }
    inv.t = Vector3D(0, 0, 0);
    inv.t = -inv.richtung(A.t);
    return true;
}

// Abbildung des ganzen Objekts von Snapshot a zum gemischten Zustand beim Anteil f (dieselbe Rechnung wie
// beim Mischen); ohne passende Dreiecke: Verschiebung der Mitte. Fuer Gelenkpunkte und Achsen.
static Affin obenAbbildung(const ObjektZustand &za, const ObjektZustand *zb, double f)
{
    Affin L;
    if (!zb || f <= 0)
        return L;
    const MischAbbildung m = abbildungZwischen(za, *zb);
    if (m.affin)
    {
        if (m.ok)
        {
            Affin d;
            if (m.schraube.ok)
            {
                L = drehAffin(m.schraube.achse, m.schraube.winkel * f, m.schraube.punkt);
                L.t = L.t + m.schraube.schub * f;
            }
            else
                L = affinAnteil(m.A, m.mitte, f, d);
            return L;
        }
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
                L.m[i][j] = (i == j ? 1.0 : 0.0) + f * (m.A.m[i][j] - (i == j ? 1.0 : 0.0));
        L.t = m.A.t * f;
        return L;
    }
    L.t = ((zb->minBound + zb->maxBound) - (za.minBound + za.maxBound)) * (0.5 * f);
    return L;
}

// Achse der Wiederholung im Snapshot am Abschnittsbeginn
static Vector3D grundAchse(const Wiederholung &w, const ObjektZustand &za)
{
    if (w.achse == 3)
        return w.achsVektor.length() > 1e-12 ? w.achsVektor : Vector3D(0, 0, 1);
    const int k = std::clamp(w.achse, 0, 2);
    if (w.lokal) // Achse des Objekts: Spalte seiner Ausrichtung
        return Vector3D(za.ausrichtung[k], za.ausrichtung[3 + k], za.ausrichtung[6 + k]);
    return Vector3D(k == 0 ? 1 : 0, k == 1 ? 1 : 0, k == 2 ? 1 : 0);
}

struct WdhKontext
{
    const Snapshot &a, &b;
    double zeit;
    Affin oben;                   // Snapshot a -> gemischt, fuer das oberste Objekt
    const object *ziel = nullptr; // Abbildung fuer dieses Objekt festhalten (Gelenk aus der Ansicht)
    Affin *zielAbb = nullptr;
    bool angewendet = false;
};

// Bewegung einer Wiederholung zum Zeitpunkt; T = Abbildung Snapshot a -> jetzt (ohne die eigene Wiederholung)
static Affin wiederholungsAbbildung(const Wiederholung &w, const WdhKontext &k, const ObjektZustand &za,
                                    const ObjektZustand *zb, const ObjektZustand &z, const Affin &T)
{
    Affin A;
    const int n = zyklenFuer(w, k.a, k.b, &za, zb, nullptr, nullptr);
    if (n <= 0)
        return A;
    const double f = std::clamp((k.zeit - k.a.zeit) / (k.b.zeit - k.a.zeit), 0.0, 1.0);
    const double takt = 2.0 * PI_A * n * f + w.phase * PI_A / 180.0;
    const Vector3D mitte = w.mitGelenk ? T.punkt(w.gelenk) : (z.minBound + z.maxBound) * 0.5;
    Vector3D achse = T.richtung(grundAchse(w, za));
    if (achse.length() < 1e-12)
        return A;
    achse = achse * (1.0 / achse.length());
    const double vz = w.richtung < 0 ? -1.0 : 1.0;
    switch (w.art)
    {
    case WIEDERHOLUNG_DREHEN:
        return drehAffin(achse, vz * takt, mitte);
    case WIEDERHOLUNG_PENDELN:
        return drehAffin(achse, vz * (w.mittel + w.weite * std::sin(takt)) * PI_A / 180.0, mitte);
    case WIEDERHOLUNG_WIPPEN:
        A.t = achse * (vz * (w.mittel + w.weite * std::sin(takt)));
        return A;
    case WIEDERHOLUNG_ABROLLEN:
    {
        if (!zb)
            return A;
        Vector3D d = (zb->minBound + zb->maxBound) * 0.5 - (za.minBound + za.maxBound) * 0.5;
        d.y = 0;
        // rollt auf dem Boden (Y oben): Drehachse waagrecht, quer zur Fahrtrichtung
        return drehAffin(Vector3D(0, 1, 0).cross(d), 2.0 * PI_A * n * f, (z.minBound + z.maxBound) * 0.5);
    }
    default:
        return A;
    }
}

// Wiederholungen von z und (danach) seinen Teilen anwenden; K = schon angewendete Bewegungen der Eltern
static void wiederholungenRekursiv(ObjektZustand &z, const ObjektZustand &za, const ObjektZustand *zb, WdhKontext &k,
                                   Affin K)
{
    const Affin T = K.nach(k.oben);
    if (k.ziel && k.ziel == za.o && k.zielAbb)
        *k.zielAbb = T;
    for (const Wiederholung &w : k.a.wiederholungen)
        if (w.o == za.o && w.art != WIEDERHOLUNG_KEINE)
        {
            const Affin A = wiederholungsAbbildung(w, k, za, zb, z, T);
            if (A.istEins(1e-15))
                continue;
            Affin drehung = A;
            drehung.t = Vector3D(0, 0, 0);
            geometrieAbbilden(z, z, A, drehung);
            K = A.nach(K);
            k.angewendet = true;
        }
    if (z.kinder.size() == za.kinder.size())
        for (size_t i = 0; i < za.kinder.size(); ++i)
            wiederholungenRekursiv(z.kinder[i], za.kinder[i],
                                   zb && zb->kinder.size() == za.kinder.size() ? &zb->kinder[i] : nullptr, k, K);
    if (z.art == 7)
        grenzenAusGeometrie(z); // Gruppe: Grenzen aus den bewegten Teilen
}

// Zustand von o in einer Liste (auch in Gruppen)
static const ObjektZustand *zustandSuchen(const std::vector<ObjektZustand> &liste, const object *o)
{
    for (const auto &z : liste)
    {
        if (z.o == o)
            return &z;
        if (const ObjektZustand *k = zustandSuchen(z.kinder, o))
            return k;
    }
    return nullptr;
}

// oberstes Objekt eines Snapshots, das o ist oder enthaelt
static const ObjektZustand *obersterZustand(const Snapshot &s, const object *o)
{
    for (const auto &z : s.objekte)
        if (z.o == o || zustandSuchen(z.kinder, o))
            return &z;
    return nullptr;
}


std::vector<Wiederholung> wiederholungen(int i)
{
    std::vector<Wiederholung> r;
    if (i < 0 || i >= int(snapshots.size()))
        return r;
    for (const Wiederholung &w : snapshots[size_t(i)].wiederholungen)
        if (w.art != WIEDERHOLUNG_KEINE && objektVorhanden(w.o))
            r.push_back(w);
    return r;
}

bool wiederholungVon(int i, const object *o, Wiederholung &w)
{
    for (const Wiederholung &x : wiederholungen(i))
        if (x.o == o)
        {
            w = x;
            return true;
        }
    return false;
}

void wiederholungSetzen(int i, const Wiederholung &w)
{
    if (i < 0 || i >= int(snapshots.size()) || !w.o)
        return;
    auto &liste = snapshots[size_t(i)].wiederholungen;
    liste.erase(std::remove_if(liste.begin(), liste.end(), [&](const Wiederholung &x)
                               { return x.o == w.o || !objektVorhanden(x.o); }),
                liste.end());
    if (w.art != WIEDERHOLUNG_KEINE)
        liste.push_back(w);
    szeneGeaendertMarkieren();
}

void wiederholungenEntfernen(int i)
{
    if (i < 0 || i >= int(snapshots.size()) || snapshots[size_t(i)].wiederholungen.empty())
        return;
    snapshots[size_t(i)].wiederholungen.clear();
    szeneGeaendertMarkieren();
}

int wiederholungZyklen(int i, const Wiederholung &w, double *periode, double *radius)
{
    if (i < 0 || i + 1 >= int(snapshots.size()))
        return 0;
    const Snapshot &a = snapshots[size_t(i)], &b = snapshots[size_t(i) + 1];
    return zyklenFuer(w, a, b, zustandSuchen(a.objekte, w.o), zustandSuchen(b.objekte, w.o), periode, radius);
}

bool gelenkAusWelt(int i, const object *o, const Vector3D &welt, Vector3D &gelenk)
{
    if (i < 0 || i + 1 >= int(snapshots.size()))
        return false;
    const Snapshot &a = snapshots[size_t(i)], &b = snapshots[size_t(i) + 1];
    if (zeitJetzt < a.zeit - 1e-9 || zeitJetzt > b.zeit + 1e-9 || b.zeit - a.zeit <= 1e-12)
        return false;
    const ObjektZustand *za = obersterZustand(a, o);
    if (!za)
        return false;
    const ObjektZustand *zb = zustandSuchen(b.objekte, za->o);
    const double f = std::clamp((zeitJetzt - a.zeit) / (b.zeit - a.zeit), 0.0, 1.0);
    ObjektZustand z = zb ? zustandMischen(*za, *zb, f) : *za;
    Affin T;
    bool gefunden = false;
    WdhKontext k{a, b, zeitJetzt, obenAbbildung(*za, zb, f)};
    Affin abb;
    k.ziel = o;
    k.zielAbb = &abb;
    abb.m[0][0] = 0; // Markierung: nicht gefunden
    wiederholungenRekursiv(z, *za, zb, k, Affin());
    gefunden = abb.m[0][0] != 0 || abb.m[0][1] != 0 || abb.m[0][2] != 0;
    Affin inv;
    if (!gefunden || !affinUmkehren(abb, inv))
        return false;
    gelenk = inv.punkt(welt);
    return true;
}

void snapshotObjektEintragen(int i, object *o, const Vector3D &versatz)
{
    if (i < 0 || i >= int(snapshots.size()) || !o)
        return;
    mischSpeicherLeeren();
    ObjektZustand z = ZustandZugriff::lesen(o);
    Affin T, I;
    T.t = versatz;
    geometrieAbbilden(z, z, T, I);
    auto &liste = snapshots[size_t(i)].objekte;
    for (auto &x : liste)
        if (x.o == o)
        {
            x = std::move(z);
            szeneGeaendertMarkieren();
            return;
        }
    liste.push_back(std::move(z));
    szeneGeaendertMarkieren();
}

// ---------- Ablaeufe (animation.h) ----------
static std::vector<AblaufEinsatz> gAblaufEinsaetze;
static std::vector<AblaufVorlage> gAblaufVorlagen;
std::vector<AblaufEinsatz> &ablaufEinsaetze() { return gAblaufEinsaetze; }
std::vector<AblaufVorlage> &ablaufVorlagen() { return gAblaufVorlagen; }

bool ablaufGueltig(const AblaufEinsatz &e) { return e.o && objektInSzene(e.o); }

object *obersteGruppe(object *o)
{
    for (auto &s : scene)
        if (s.get() == o || enthaeltObjekt(s.get(), o, 0, false))
            return s.get();
    return nullptr;
}

// Teil mit diesem Namen unter o (Tiefensuche, das erste)
static object *teilMitNamen(object *o, const std::string &name)
{
    if (!o)
        return nullptr;
    if (o->name == name)
        return o;
    if (auto *g = dynamic_cast<Gruppe *>(o))
        for (auto &k : g->kinder)
            if (object *t = teilMitNamen(k.get(), name))
                return t;
    return nullptr;
}

// Namenspfad von oben bis teil ("Bein links/Unterschenkel links"); false, wenn teil nicht darin liegt
static bool namenPfad(const object *oben, const object *teil, std::string &pfad)
{
    if (oben == teil)
    {
        pfad.clear();
        return true;
    }
    if (auto *g = dynamic_cast<const Gruppe *>(oben))
        for (auto &k : g->kinder)
        {
            std::string unter;
            if (namenPfad(k.get(), teil, unter))
            {
                pfad = unter.empty() ? k->name : k->name + "/" + unter;
                return true;
            }
        }
    return false;
}

static object *teilAusPfad(object *oben, const std::string &pfad)
{
    object *o = oben;
    std::istringstream w(pfad);
    std::string name;
    while (o && std::getline(w, name, '/'))
    {
        auto *g = dynamic_cast<Gruppe *>(o);
        object *gefunden = nullptr;
        if (g)
            for (auto &k : g->kinder)
                if (k->name == name)
                {
                    gefunden = k.get();
                    break;
                }
        o = gefunden;
    }
    return o;
}

static Vector3D objektMitte(const object *o)
{
    Vector3D mn = o->position, mx = o->position;
    o->getWorldAABB(mn, mx);
    return (mn + mx) * 0.5;
}

// Rahmen eines Objekts: quer = von links nach rechts (Schultern einer Figur, Vorderbeine eines Tiers), oben = Y,
// vorne = oben x quer. Sonst die Weltachsen (vorne = X).
static void rahmenVon(object *o, Vector3D &vorne, Vector3D &oben, Vector3D &quer)
{
    oben = Vector3D(0, 1, 0);
    vorne = Vector3D(1, 0, 0);
    quer = Vector3D(0, 0, 1);
    object *l = teilMitNamen(o, "Schulter links"), *r = teilMitNamen(o, "Schulter rechts");
    if (!l || !r)
    {
        l = teilMitNamen(o, "Bein vorne links");
        r = teilMitNamen(o, "Bein vorne rechts");
    }
    if (!l || !r)
        return;
    Vector3D q = objektMitte(r) - objektMitte(l);
    q.y = 0;
    if (q.length() < 1e-9)
        return;
    quer = q.normalized();
    vorne = oben.cross(quer).normalized();
}

static Vector3D inRahmen(const Vector3D &v, const AblaufEinsatz &e) { return Vector3D(v * e.vorne, v * e.oben, v * e.quer); }
static Vector3D ausRahmen(const Vector3D &v, const AblaufEinsatz &e) { return e.vorne * v.x + e.oben * v.y + e.quer * v.z; }

bool ablaufAusVorlage(object *o, const AblaufVorlage &v, AblaufEinsatz &e)
{
    if (!o)
        return false;
    e.o = o;
    e.v = v;
    rahmenVon(o, e.vorne, e.oben, e.quer);
    e.dauer = v.dauer;
    return true;
}

bool ablaufBauen(object *o, int bauart, AblaufEinsatz &e, std::string &fehler)
{
    if (!o)
    {
        fehler = "Kein Objekt gewählt.";
        return false;
    }
    e = AblaufEinsatz();
    e.o = o;
    rahmenVon(o, e.vorne, e.oben, e.quer);
    const Vector3D mitte = objektMitte(o);
    Vector3D mn = o->position, mx = o->position;
    o->getWorldAABB(mn, mx);
    const double H = std::max(1e-9, mx.y - mn.y);
    auto glied = [&](object *teil, const Vector3D &gelenk, const Vector3D &achse, double mittel, double weite, double phase,
                     double periode)
    {
        AblaufGlied g;
        if (!teil || !namenPfad(o, teil, g.pfad))
            return false;
        g.art = WIEDERHOLUNG_PENDELN;
        g.gelenk = inRahmen(gelenk - mitte, e);
        g.achse = inRahmen(achse, e);
        g.mittel = mittel;
        g.weite = weite;
        g.phase = phase;
        g.periode = periode;
        e.v.glieder.push_back(g);
        return true;
    };
    auto kugelMitte = [&](const char *name, Vector3D &p)
    {
        auto *k = dynamic_cast<Kugel *>(teilMitNamen(o, name));
        if (k)
            p = k->position;
        return k != nullptr;
    };
    auto stabEnde = [&](const char *name, bool zweites, Vector3D &p)
    {
        auto *s = dynamic_cast<Stab *>(teilMitNamen(o, name));
        if (s)
            p = s->position + (zweites ? s->p2 : s->p1);
        return s != nullptr;
    };

    if (bauart == ABLAUF_RENNEN || bauart == ABLAUF_GEHEN || bauart == ABLAUF_WINKEN)
    {
        const bool rennen = bauart == ABLAUF_RENNEN;
        const double periode = rennen ? 0.7 : 1.1;
        int gefunden = 0;
        for (int s = 0; s < 2; ++s)
        {
            const std::string sn = s == 0 ? " links" : " rechts";
            Vector3D schulter, ellbogen, huefte, knie, knoechel;
            const bool ok = kugelMitte(("Schulter" + sn).c_str(), schulter) && kugelMitte(("Ellbogen" + sn).c_str(), ellbogen) &&
                            stabEnde(("Oberschenkel" + sn).c_str(), false, huefte) && kugelMitte(("Knie" + sn).c_str(), knie) &&
                            stabEnde(("Unterschenkel" + sn).c_str(), true, knoechel);
            if (!ok)
                continue;
            ++gefunden;
            object *arm = teilMitNamen(o, "Arm" + sn), *bein = teilMitNamen(o, "Bein" + sn);
            object *unterarm = arm ? teilMitNamen(arm, "Unterarm" + sn) : nullptr;     // die Gruppe (vor dem Stab gleichen Namens)
            object *unterschenkel = bein ? teilMitNamen(bein, "Unterschenkel" + sn) : nullptr;
            object *fuss = teilMitNamen(o, "Fuß" + sn);
            if (bauart == ABLAUF_WINKEN)
            {
                if (s == 1)
                {
                    glied(arm, schulter, e.vorne, -145, 0, 0, 1.0);
                    glied(unterarm, ellbogen, e.vorne, -10, 28, 0, 0.6);
                }
                continue;
            }
            const double bw = rennen ? 35 : 22, knieM = rennen ? -50 : -12, knieA = rennen ? 40 : 12;
            const double fw = rennen ? 12 : 8, aw = rennen ? 30 : 16, ellM = rennen ? 80 : 15, ellA = rennen ? 15 : 6;
            const double ph = s == 0 ? 0.0 : 180.0;
            glied(bein, huefte, e.quer, 0, bw, ph, periode);
            glied(unterschenkel, knie, e.quer, knieM, knieA, ph - 90, periode);
            glied(fuss, knoechel, e.quer, 0, fw, ph, periode);
            glied(arm, schulter, e.quer, 0, aw, ph + 180, periode);
            glied(unterarm, ellbogen, e.quer, ellM, ellA, ph + 180, periode);
        }
        if (gefunden < 2)
        {
            fehler = "Das gewählte Objekt ist keine Figur aus „+ Neu → Figur …“ (Schultern, Ellbogen, Hüften, Knie links "
                     "und rechts werden an ihren Namen erkannt).";
            return false;
        }
        if (bauart != ABLAUF_WINKEN)
        {
            AblaufGlied w; // Koerper wippt zweimal je Doppelschritt
            w.art = WIEDERHOLUNG_WIPPEN;
            w.mitGelenk = false;
            w.achse = Vector3D(0, 1, 0);
            w.periode = periode / 2;
            w.weite = (rennen ? 0.025 : 0.012) * H;
            w.phase = 90;
            e.v.glieder.push_back(w);
            e.v.geschwindigkeit = Vector3D((rennen ? 4.0 : 1.4) * H / 1.8, 0, 0);
        }
        e.v.name = rennen ? "Rennen" : bauart == ABLAUF_GEHEN ? "Gehen" : "Winken";
        e.v.dauer = bauart == ABLAUF_WINKEN ? 2.5 : 3.0;
        e.dauer = e.v.dauer;
        return true;
    }

    if (bauart == ABLAUF_TIER_GEHEN)
    {
        static const char *namen[4] = {"Bein vorne links", "Bein vorne rechts", "Bein hinten links", "Bein hinten rechts"};
        static const double phasen[4] = {90, 270, 0, 180}; // Kreuzgang
        const double periode = 1.0;
        int gefunden = 0;
        for (int b = 0; b < 4; ++b)
        {
            object *bein = teilMitNamen(o, namen[b]);
            auto *ober = dynamic_cast<Stab *>(bein ? teilMitNamen(bein, "Oberbein") : nullptr);
            object *unter = bein ? teilMitNamen(bein, "Unterbein") : nullptr; // Gruppe
            auto *knieK = dynamic_cast<Kugel *>(unter ? teilMitNamen(unter, "Knie") : nullptr);
            if (!ober || !unter || !knieK)
                continue;
            ++gefunden;
            glied(bein, ober->position + ober->p1, e.quer, 0, 22, phasen[b], periode);
            glied(unter, knieK->position, e.quer, -8, 12, phasen[b] - 90, periode);
        }
        if (gefunden < 4)
        {
            fehler = "Das gewählte Objekt ist kein Tier aus „+ Neu → Tier …“ (vier Beine mit Oberbein, Unterbein und Knie).";
            return false;
        }
        AblaufGlied w;
        w.art = WIEDERHOLUNG_WIPPEN;
        w.mitGelenk = false;
        w.achse = Vector3D(0, 1, 0);
        w.periode = periode / 2;
        w.weite = 0.01 * H;
        e.v.glieder.push_back(w);
        const double laenge = std::fabs((mx - mn) * e.vorne) > 1e-9 ? std::max(std::fabs(mx.x - mn.x), std::fabs(mx.z - mn.z)) : H;
        e.v.geschwindigkeit = Vector3D(0.8 * laenge, 0, 0);
        e.v.name = "Tier gehen";
        e.v.dauer = e.dauer = 4.0;
        return true;
    }

    if (bauart == ABLAUF_SCHWANZ)
    {
        auto *schwanz = dynamic_cast<Mesh *>(teilMitNamen(o, "Schwanz"));
        object *rumpf = teilMitNamen(o, "Rumpf");
        if (!schwanz || schwanz->triangles.empty() || !rumpf)
        {
            fehler = "Das gewählte Objekt hat keinen „Schwanz“ und „Rumpf“ (Tier aus „+ Neu → Tier …“).";
            return false;
        }
        // Wurzel: die Ecke des Schwanzes, die der Rumpfmitte am naechsten liegt
        const Vector3D rm = objektMitte(rumpf);
        Vector3D wurzel = schwanz->triangles.front().a + schwanz->triangles.front().position;
        double best = 1e300;
        for (const Dreieck &d : schwanz->triangles)
            for (const Vector3D &p : {d.a, d.b, d.c})
            {
                const Vector3D w = p + d.position;
                const double abst = (w - rm).length();
                if (abst < best)
                    best = abst, wurzel = w;
            }
        glied(schwanz, wurzel, e.oben, 0, 30, 0, 0.8);
        e.v.name = "Schwanz wedeln";
        e.v.dauer = e.dauer = 3.0;
        return true;
    }
    // ----- Schluesselposen (im Rahmen, bezogen auf die Mitte des Objekts) -----
    const double grad = PI_A / 180.0;
    auto rel = [&](const Vector3D &p) { return inRahmen(p - mitte, e); };
    auto drehung = [&](const Vector3D &achseRahmen, double winkel, const Vector3D &gelenkWelt)
    { return drehAffin(achseRahmen, winkel * grad, rel(gelenkWelt)); };
    auto schub = [](double x, double y, double z)
    {
        Affin T;
        T.t = Vector3D(x, y, z);
        return T;
    };
    auto pose = [&](double zeit) -> AblaufPose &
    {
        e.v.posen.push_back(AblaufPose());
        e.v.posen.back().zeit = zeit;
        return e.v.posen.back();
    };
    auto pfadVon = [&](object *teil, std::string &pfad) { return teil && namenPfad(o, teil, pfad); };
    const Vector3D Z(0, 0, 1), Y(0, 1, 0);
    const Vector3D ausdehnung = mx - mn;
    const double laenge = std::max(1e-9, std::fabs(ausdehnung.x * e.vorne.x) + std::fabs(ausdehnung.y * e.vorne.y) +
                                             std::fabs(ausdehnung.z * e.vorne.z));

    if (bauart == ABLAUF_KATZE_SPRINGEN)
    {
        static const char *namen[4] = {"Bein vorne links", "Bein vorne rechts", "Bein hinten links", "Bein hinten rechts"};
        static const double zeiten[6] = {0, 0.3, 0.5, 0.8, 1.05, 1.35};
        static const double huefteV[6] = {0, -10, -25, 45, 15, 0}, huefteH[6] = {0, 25, -35, -50, 10, 0};
        static const double knieV[6] = {0, -20, 0, 10, -15, 0}, knieH[6] = {0, -40, 15, 20, -20, 0};
        struct BeinInfo
        {
            std::string bein, unter;
            Vector3D huefte, knie;
            bool vorne;
        };
        std::vector<BeinInfo> beine;
        for (int b = 0; b < 4; ++b)
        {
            object *bein = teilMitNamen(o, namen[b]);
            auto *ober = dynamic_cast<Stab *>(bein ? teilMitNamen(bein, "Oberbein") : nullptr);
            object *unter = bein ? teilMitNamen(bein, "Unterbein") : nullptr;
            auto *knieK = dynamic_cast<Kugel *>(unter ? teilMitNamen(unter, "Knie") : nullptr);
            BeinInfo bi;
            if (!ober || !knieK || !pfadVon(bein, bi.bein) || !pfadVon(unter, bi.unter))
                continue;
            bi.huefte = ober->position + ober->p1;
            bi.knie = knieK->position;
            bi.vorne = b < 2;
            beine.push_back(bi);
        }
        if (beine.size() < 4)
        {
            fehler = "Das gewählte Objekt ist kein Tier aus „+ Neu → Tier …“ (vier Beine mit Oberbein, Unterbein und Knie).";
            return false;
        }
        for (int k = 0; k < 6; ++k)
        {
            AblaufPose &p = pose(zeiten[k]);
            Affin w;
            switch (k)
            {
            case 1: w = schub(0, -0.12 * H, 0); break;                                                    // ducken
            case 2: w = schub(0.25 * laenge, 0.2 * H, 0).nach(drehAffin(Z, 18 * grad, Vector3D())); break; // abspringen
            case 3: w = schub(0.9 * laenge, 0.55 * H, 0); break;                                           // fliegen
            case 4: w = schub(1.5 * laenge, 0, 0).nach(drehAffin(Z, -10 * grad, Vector3D())); break;      // landen
            case 5: w = schub(1.6 * laenge, 0, 0); break;
            default: break;
            }
            p.teile.push_back({std::string(), w});
            for (const BeinInfo &bi : beine)
            {
                p.teile.push_back({bi.bein, drehung(Z, bi.vorne ? huefteV[k] : huefteH[k], bi.huefte)});
                p.teile.push_back({bi.unter, drehung(Z, bi.vorne ? knieV[k] : knieH[k], bi.knie)});
            }
        }
        e.v.name = "Katze springt";
        e.v.dauer = e.dauer = zeiten[5];
        e.v.einlauf = 0.08;
        return true;
    }

    if (bauart == ABLAUF_FIGUR_SETZEN)
    {
        AblaufPose ruhe, sitzt;
        sitzt.zeit = 1.4;
        double delta = 0;
        int gefunden = 0;
        for (int s = 0; s < 2; ++s)
        {
            const std::string sn = s == 0 ? " links" : " rechts";
            Vector3D huefte, knie, schulter;
            std::string pb, pu, pa;
            object *bein = teilMitNamen(o, "Bein" + sn);
            object *unterschenkel = bein ? teilMitNamen(bein, "Unterschenkel" + sn) : nullptr;
            if (!stabEnde(("Oberschenkel" + sn).c_str(), false, huefte) || !kugelMitte(("Knie" + sn).c_str(), knie) ||
                !pfadVon(bein, pb) || !pfadVon(unterschenkel, pu))
                continue;
            ++gefunden;
            delta = std::max(delta, (huefte - knie).length());
            ruhe.teile.push_back({pb, Affin()});
            ruhe.teile.push_back({pu, Affin()});
            sitzt.teile.push_back({pb, drehung(Z, 90, huefte)});
            sitzt.teile.push_back({pu, drehung(Z, -90, knie)});
            if (kugelMitte(("Schulter" + sn).c_str(), schulter) && pfadVon(teilMitNamen(o, "Arm" + sn), pa))
            {
                ruhe.teile.push_back({pa, Affin()});
                sitzt.teile.push_back({pa, drehung(Z, 25, schulter)});
            }
        }
        if (gefunden < 2)
        {
            fehler = "Das gewählte Objekt ist keine Figur aus „+ Neu → Figur …“ (Oberschenkel, Knie und Unterschenkel links "
                     "und rechts werden an ihren Namen erkannt).";
            return false;
        }
        ruhe.teile.push_back({std::string(), Affin()});
        sitzt.teile.push_back({std::string(), schub(-delta, -delta, 0)});
        e.v.posen = {ruhe, sitzt};
        e.v.name = "Figur setzt sich";
        e.v.dauer = e.dauer = 1.4;
        return true;
    }

    if (bauart == ABLAUF_TUER || bauart == ABLAUF_SCHRANKE)
    {
        const bool tuer = bauart == ABLAUF_TUER;
        object *teil = teilMitNamen(o, tuer ? "Türblatt" : "Schlagbaum");
        if (!teil)
            teil = o;
        std::string pfad;
        namenPfad(o, teil, pfad);
        Vector3D a = teil->position, b = teil->position;
        teil->getWorldAABB(a, b);
        const Vector3D m = (a + b) * 0.5;
        const bool langX = std::fabs(b.x - a.x) >= std::fabs(b.z - a.z);
        // Drehpunkt: das Ende naeher am Pfosten (sonst das kleinere Ende)
        bool amMax = false;
        if (object *pfosten = teilMitNamen(o, tuer ? "Zarge" : "Pfosten"))
        {
            const Vector3D pm = objektMitte(pfosten);
            amMax = langX ? std::fabs(pm.x - b.x) < std::fabs(pm.x - a.x) : std::fabs(pm.z - b.z) < std::fabs(pm.z - a.z);
        }
        Vector3D gelenk = m;
        if (langX)
            gelenk.x = amMax ? b.x : a.x;
        else
            gelenk.z = amMax ? b.z : a.z;
        Affin offen;
        if (tuer)
        {
            gelenk.y = m.y;
            offen = drehAffin(inRahmen(Y, e), (amMax ? -90 : 90) * grad, rel(gelenk));
        }
        else if (langX)
            offen = drehAffin(inRahmen(Z, e), (amMax ? -80 : 80) * grad, rel(gelenk));
        else
            offen = drehAffin(inRahmen(Vector3D(1, 0, 0), e), (amMax ? 80 : -80) * grad, rel(gelenk));
        const double d = tuer ? 1.2 : 2.0;
        AblaufPose zu, auf;
        auf.zeit = d;
        zu.teile.push_back({pfad, Affin()});
        auf.teile.push_back({pfad, offen});
        e.v.posen = {zu, auf};
        e.v.name = tuer ? "Tür öffnen" : "Schranke öffnen";
        e.v.dauer = e.dauer = d;
        e.v.einlauf = tuer ? 0.3 : 0.5;
        return true;
    }

    fehler = "Unbekannte Bewegung.";
    return false;
}

bool ablaufAusAbschnitt(int i, object *oben, AblaufVorlage &v, std::string &fehler)
{
    if (i < 0 || i + 1 >= int(snapshots.size()) || !oben)
    {
        fehler = "Kein Abschnitt zwischen zwei Snapshots.";
        return false;
    }
    const Snapshot &a = snapshots[size_t(i)], &b = snapshots[size_t(i) + 1];
    const ObjektZustand *za = obersterZustand(a, oben);
    const ObjektZustand *zb = obersterZustand(b, oben);
    if (!za)
    {
        fehler = "Das Objekt steht nicht im Snapshot am Abschnittsbeginn.";
        return false;
    }
    AblaufEinsatz rahmen;
    rahmenVon(oben, rahmen.vorne, rahmen.oben, rahmen.quer);
    const Vector3D ca = (za->minBound + za->maxBound) * 0.5;
    v = AblaufVorlage();
    v.dauer = b.zeit - a.zeit;
    if (zb && v.dauer > 1e-9)
        v.geschwindigkeit = inRahmen(((zb->minBound + zb->maxBound) * 0.5 - ca) * (1.0 / v.dauer), rahmen);
    for (const Wiederholung &w : a.wiederholungen)
    {
        std::string pfad;
        if (w.art == WIEDERHOLUNG_KEINE || w.art == WIEDERHOLUNG_ABROLLEN || !objektVorhanden(w.o) || !namenPfad(oben, w.o, pfad))
            continue;
        const ObjektZustand *zw = zustandSuchen(a.objekte, w.o);
        if (!zw)
            continue;
        AblaufGlied g;
        g.pfad = pfad;
        g.art = w.art;
        g.mitGelenk = w.mitGelenk;
        g.gelenk = inRahmen(w.gelenk - ca, rahmen);
        g.achse = inRahmen(grundAchse(w, *zw), rahmen);
        double periode = w.periode;
        wiederholungZyklen(i, w, &periode);
        g.periode = periode > 1e-9 ? periode : w.periode;
        g.weite = w.weite;
        g.mittel = w.mittel;
        g.phase = w.phase;
        g.richtung = w.richtung;
        v.glieder.push_back(g);
    }
    if (v.glieder.empty() && v.geschwindigkeit.length() < 1e-12)
    {
        fehler = "Im Abschnitt hat das Objekt (und seine Teile) keine Wiederholungen und bewegt sich nicht.";
        return false;
    }
    return true;
}

std::vector<std::pair<double, double>> ablaufZeiten(const AblaufEinsatz &e)
{
    std::vector<std::pair<double, double>> r;
    const double d = std::max(1e-3, e.dauer);
    if (e.alle > 1e-6 && e.bis > e.start)
    {
        for (double s = e.start; s <= e.bis + 1e-9 && r.size() < 10000; s += std::max(e.alle, d * 0.5))
            r.push_back({s, s + d});
    }
    else
        r.push_back({e.start, e.start + d});
    return r;
}

void ablaufHinzufuegen(const AblaufEinsatz &e)
{
    if (!e.o)
        return;
    gAblaufEinsaetze.push_back(e);
    // Snapshots: mindestens zwei, bis zum Ende des letzten Vorkommens; das Objekt in allen
    double ende = e.start + e.dauer;
    for (auto &z : ablaufZeiten(e))
        ende = std::max(ende, z.second);
    const double jetzt = zeitJetzt;
    if (snapshots.empty())
        snapshotAufnehmen(std::min(0.0, e.start));
    if (snapshots.back().zeit < ende - 1e-9)
    {
        zeitpunktAnwenden(snapshots.back().zeit, false); // Grundzustand wie im letzten Snapshot
        snapshotAufnehmen(ende);
    }
    for (int i = 0; i < int(snapshots.size()); ++i)
        if (!obersterZustand(snapshots[size_t(i)], e.o))
            snapshotObjektEintragen(i, e.o, Vector3D(0, 0, 0));
    animationAn = true;
    mischSpeicherLeeren();
    szeneGeaendertMarkieren();
    zeitpunktAnwenden(jetzt, false);
}

// Weg bei weichem Ein- und Auslaufen (Rampe 'ein' am Anfang und Ende, glatt): Integral der Huelle bis x
static double ablaufWeg(double x, double d, double ein)
{
    auto S = [](double u) { return u * u * u - 0.5 * u * u * u * u; }; // Integral von 3u^2 - 2u^3
    x = std::clamp(x, 0.0, d);
    if (ein <= 1e-9)
        return x;
    if (x <= ein)
        return ein * S(x / ein);
    double w = 0.5 * ein + (std::min(x, d - ein) - ein);
    if (x > d - ein)
        w += ein * (0.5 - S((d - x) / ein));
    return w;
}

static double glatt(double u)
{
    u = std::clamp(u, 0.0, 1.0);
    return u * u * (3.0 - 2.0 * u);
}

static double ablaufEinlauf(const AblaufEinsatz &e)
{
    const double d = std::max(1e-3, e.dauer);
    const double ein = e.v.einlauf >= 0 ? e.v.einlauf : (e.v.posen.empty() ? 0.35 : std::min(0.6, 0.25 * d));
    return std::clamp(ein, 0.0, d / 2);
}

// Rahmen -> Welt (Spalten vorne, oben, quer; Ursprung c) und zurueck
static Affin rahmenMatrix(const AblaufEinsatz &e, const Vector3D &c)
{
    Affin B;
    const Vector3D sp[3] = {e.vorne, e.oben, e.quer};
    for (int j = 0; j < 3; ++j)
    {
        B.m[0][j] = sp[j].x;
        B.m[1][j] = sp[j].y;
        B.m[2][j] = sp[j].z;
    }
    B.t = c;
    return B;
}
static Affin inWelt(const Affin &F, const Affin &B, const Affin &Binv) { return B.nach(F.nach(Binv)); }

// Anteil f einer Abbildung D auf der Schraubbahn (starr: Drehung um feste Achse + Schub; sonst Polarzerlegung)
static Affin schraubAnteil(const Affin &D, double f)
{
    Affin R, S;
    Vector3D achse;
    double w = 0;
    if (polarZerlegen(D, R, S))
    {
        achseUndWinkel(R, achse, w);
        if (w > 1e-9)
        {
            const Schraubung s = schraubungFinden(achse, w, D.t, Vector3D(0, 0, 0), 1e300);
            if (s.ok)
            {
                Affin L = drehAffin(s.achse, s.winkel * f, s.punkt);
                L.t = L.t + s.schub * f;
                return L;
            }
        }
    }
    Affin d;
    return affinAnteil(D, Vector3D(0, 0, 0), f, d);
}

// Lage eines Teils (Pfad) zur Vorlagenzeit t (Rahmen, relativ zur Ruhelage)
static Affin poseBei(const AblaufVorlage &v, const std::string &pfad, double t)
{
    if (v.posen.empty())
        return Affin();
    auto lokal = [&](size_t k)
    {
        for (const auto &p : v.posen[k].teile)
            if (p.first == pfad)
                return p.second;
        return Affin();
    };
    if (t <= v.posen.front().zeit)
        return lokal(0);
    if (t >= v.posen.back().zeit)
        return lokal(v.posen.size() - 1);
    size_t k = 0;
    while (k + 1 < v.posen.size() && v.posen[k + 1].zeit <= t)
        ++k;
    const double t0 = v.posen[k].zeit, t1 = v.posen[k + 1].zeit;
    const double f = t1 - t0 > 1e-12 ? (t - t0) / (t1 - t0) : 1.0;
    const Affin A = lokal(k), B = lokal(k + 1);
    return schraubAnteil(B.nach(affinInvers(A)), f).nach(A);
}

// Vorlagenzeit der Posen bei tau: weich anfahren und anhalten, auf die Dauer der Vorlage umgerechnet
static double posenZeit(const AblaufEinsatz &e, double tau)
{
    const double d = std::max(1e-3, e.dauer), ein = ablaufEinlauf(e);
    const double w = ein > 1e-9 ? ablaufWeg(tau, d, ein) * d / std::max(1e-9, d - ein) : std::clamp(tau, 0.0, d);
    return w * (e.v.dauer > 1e-9 ? e.v.dauer / d : 1.0);
}

// Lage des ganzen Objekts bei tau im Rahmen: Pose "" und Fortbewegung
static Affin wurzelImRahmen(const AblaufEinsatz &e, double tau)
{
    const double d = std::max(1e-3, e.dauer), ein = ablaufEinlauf(e);
    Affin T;
    T.t = e.v.geschwindigkeit * (ablaufWeg(tau, d, ein) * e.tempo);
    return T.nach(poseBei(e.v, std::string(), posenZeit(e, tau)));
}

struct AblaufLauf
{
    const AblaufEinsatz *e;
    double tau;   // im letzten begonnenen Vorkommen (nach dem Ende: Dauer)
    bool laufend; // Vorkommen laeuft noch (Wiederholungen der Glieder nur dann)
    Affin B, Binv;
};

// Drehwinkel eines Rads (Abrollen) bis tau: Bahnlaenge seiner Mitte, Vorzeichen aus Fahrtrichtung und Achse
static double rollWinkel(const AblaufLauf &l, const AblaufGlied &g)
{
    const double r = std::max(1e-6, g.weite);
    const Vector3D mitte = l.B.punkt(g.gelenk), achse = l.B.richtung(g.achse);
    const int N = 32;
    double s = 0;
    Vector3D vorher = mitte;
    for (int i = 1; i <= N; ++i)
    {
        const Affin W = inWelt(wurzelImRahmen(*l.e, l.tau * i / N), l.B, l.Binv);
        const Vector3D p = W.punkt(mitte), d = p - vorher;
        const Vector3D a = W.richtung(achse);
        const double vz = (l.e->oben.cross(d) * a) >= 0 ? 1.0 : -1.0;
        s += vz * d.length();
        vorher = p;
    }
    return s / r;
}

static Vector3D zustandMitte(const ObjektZustand &z) { return (z.minBound + z.maxBound) * 0.5; }

// Lagen und Glieder anwenden: je Teil L (Ruhe-Welt, relativ zum Elternteil); K = Bewegung der Eltern bis hier
static void ablaufAnwendenRek(ObjektZustand &z, const std::string &pfad, Affin K, const std::map<std::string, Affin> &lokal,
                              const std::vector<AblaufLauf> &laeufe)
{
    Affin L;
    bool hat = false;
    auto it = lokal.find(pfad);
    if (it != lokal.end())
    {
        L = it->second;
        hat = true;
    }
    const Vector3D mitteRuhe = affinInvers(K).punkt(zustandMitte(z));
    for (const AblaufLauf &l : laeufe)
    {
        const AblaufEinsatz &e = *l.e;
        const double d = std::max(1e-3, e.dauer), ein = ablaufEinlauf(e);
        const double huelle = ein > 1e-9 ? glatt(l.tau / ein) * glatt((d - l.tau) / ein) : 1.0;
        for (const AblaufGlied &g : e.v.glieder)
        {
            if (g.pfad != pfad)
                continue;
            Vector3D achse = l.B.richtung(g.achse);
            if (achse.length() < 1e-12)
                continue;
            achse = achse * (1.0 / achse.length());
            const Vector3D gelenk = g.mitGelenk ? l.B.punkt(g.gelenk) : mitteRuhe;
            const double per = std::max(1e-6, g.periode);
            const double takt = 2.0 * PI_A * l.tau * e.tempo / per + g.phase * PI_A / 180.0;
            const double vz = g.richtung < 0 ? -1.0 : 1.0;
            Affin A;
            if (g.art == WIEDERHOLUNG_ABROLLEN)
                A = drehAffin(achse, vz * rollWinkel(l, g), gelenk); // auch nach dem Ende (Rad bleibt so stehen)
            else if (!l.laufend || huelle <= 0)
                continue;
            else if (g.art == WIEDERHOLUNG_PENDELN)
                A = drehAffin(achse, vz * huelle * (g.mittel + g.weite * std::sin(takt)) * PI_A / 180.0, gelenk);
            else if (g.art == WIEDERHOLUNG_DREHEN)
                A = drehAffin(achse, vz * 2.0 * PI_A * ablaufWeg(l.tau, d, ein) * e.tempo / per, gelenk);
            else if (g.art == WIEDERHOLUNG_WIPPEN)
                A.t = achse * (vz * huelle * (g.mittel + g.weite * std::sin(takt)));
            else
                continue;
            L = L.nach(A);
            hat = true;
        }
    }
    if (hat)
    {
        const Affin X = K.nach(L).nach(affinInvers(K));
        Affin drehung = X;
        drehung.t = Vector3D(0, 0, 0);
        geometrieAbbilden(z, z, X, drehung);
        K = K.nach(L);
    }
    for (auto &kind : z.kinder)
    {
        const std::string name = kind.o ? kind.o->name : std::string();
        ablaufAnwendenRek(kind, pfad.empty() ? name : pfad + "/" + name, K, lokal, laeufe);
    }
    if (z.art == 7)
        grenzenAusGeometrie(z);
}

// alle Einsaetze fuer das oberste Objekt von z; grund = Zustand aus den Snapshots (Ruhelage, Mitte des Rahmens).
// Mehrere Einsaetze und Vorkommen werden verkettet: ein spaeterer beginnt dort, wo der fruehere aufgehoert hat.
static bool ablaeufeAnwenden(ObjektZustand &z, const ObjektZustand &grund, double zeit)
{
    std::vector<const AblaufEinsatz *> liste;
    for (const AblaufEinsatz &e : gAblaufEinsaetze)
        if (e.o == z.o)
            liste.push_back(&e);
    if (liste.empty())
        return false;
    std::stable_sort(liste.begin(), liste.end(), [](const AblaufEinsatz *a, const AblaufEinsatz *b) { return a->start < b->start; });
    const Vector3D c = zustandMitte(grund);
    std::map<std::string, Affin> lokal;
    std::vector<AblaufLauf> laeufe;
    Affin wurzel;
    for (const AblaufEinsatz *e : liste)
    {
        const double d = std::max(1e-3, e->dauer);
        const Affin B = rahmenMatrix(*e, c), Binv = affinInvers(B);
        double letzterBeginn = 0;
        bool begonnen = false;
        for (const auto &vk : ablaufZeiten(*e))
        {
            if (zeit <= vk.first)
                continue;
            wurzel = wurzel.nach(inWelt(wurzelImRahmen(*e, std::min(zeit - vk.first, d)), B, Binv));
            letzterBeginn = vk.first;
            begonnen = true;
        }
        if (!begonnen)
            continue;
        const double tau = std::min(zeit - letzterBeginn, d);
        laeufe.push_back({e, tau, zeit - letzterBeginn < d, B, Binv});
        // Posen der Teile (nach dem Ende gehalten), mit frueheren Einsaetzen verkettet
        if (!e->v.posen.empty())
        {
            std::vector<std::string> pfade;
            for (const AblaufPose &p : e->v.posen)
                for (const auto &t : p.teile)
                    if (!t.first.empty() && std::find(pfade.begin(), pfade.end(), t.first) == pfade.end())
                        pfade.push_back(t.first);
            const double tv = posenZeit(*e, tau);
            for (const std::string &pf : pfade)
            {
                const Affin L = inWelt(poseBei(e->v, pf, tv), B, Binv);
                auto it = lokal.find(pf);
                lokal[pf] = it == lokal.end() ? L : it->second.nach(L);
            }
        }
    }
    if (laeufe.empty())
        return false;
    lokal[std::string()] = wurzel;
    ablaufAnwendenRek(z, std::string(), Affin(), lokal, laeufe);
    return true;
}

// ---------- Posen aus Snapshots, rueckwaerts ----------
static bool zustandHatName(const ObjektZustand &z, const char *name) { return z.o && z.o->name == name; }

// Bewegung eines Knotens von a nach b (Welt): Rumpf/Karosserie einer Gruppe, Stab, affine Anpassung, sonst Schub
static Affin knotenAbbildung(const ObjektZustand &a, const ObjektZustand &b)
{
    if (a.art == 7 && b.art == 7 && a.kinder.size() == b.kinder.size())
    {
        for (const char *n : {"Karosserie", "Rumpf", "Körper"})
            for (size_t i = 0; i < a.kinder.size(); ++i)
                if (zustandHatName(a.kinder[i], n))
                    return knotenAbbildung(a.kinder[i], b.kinder[i]);
    }
    if (a.art == 4 && b.art == 4)
    {
        const Vector3D da = a.v2 - a.v1, db = b.v2 - b.v1;
        Affin R;
        const Vector3D k = da.cross(db);
        if (k.length() > 1e-12 * std::max(1e-12, da.length() * db.length()))
            R = drehAffin(k, std::atan2(k.length(), da * db), Vector3D(0, 0, 0));
        const Vector3D ca = a.position + (a.v1 + a.v2) * 0.5, cb = b.position + (b.v1 + b.v2) * 0.5;
        R.t = cb - R.richtung(ca);
        return R;
    }
    if (a.art == 9 && b.art == 9)
        return b.abb.nach(affinInvers(a.abb));
    std::vector<const std::vector<Dreieck> *> la, lb;
    Affin A;
    if (dreieckListenPaare(a, b, la, lb) && !la.empty() && affinAnpassen(la, lb, A))
        return A;
    if (a.art == 7 && a.kinder.size() == b.kinder.size())
        for (size_t i = 0; i < a.kinder.size(); ++i)
        {
            la.clear();
            lb.clear();
            if (dreieckListenPaare(a.kinder[i], b.kinder[i], la, lb) && !la.empty() && affinAnpassen(la, lb, A))
                return A;
        }
    Affin T;
    T.t = (b.minBound + b.maxBound) * 0.5 - (a.minBound + a.maxBound) * 0.5;
    return T;
}

static bool istEinsGrob(const Affin &A, double laenge)
{
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            if (std::fabs(A.m[i][j] - (i == j ? 1.0 : 0.0)) > 1e-6)
                return false;
    return A.t.length() <= 1e-6 * std::max(1.0, laenge);
}

bool ablaufAusPosen(int von, int bis, object *oben, AblaufVorlage &v, std::string &fehler)
{
    if (!oben || von < 0 || bis <= von || bis >= int(snapshots.size()))
    {
        fehler = "Es braucht mindestens zwei Snapshots ab dem gewählten (Beginn und Ende der Bewegung).";
        return false;
    }
    std::vector<const ObjektZustand *> z;
    for (int k = von; k <= bis; ++k)
    {
        const ObjektZustand *zk = zustandSuchen(snapshots[size_t(k)].objekte, oben);
        if (!zk)
        {
            fehler = "Das Objekt steht nicht in allen Snapshots von " + std::to_string(von + 1) + " bis " + std::to_string(bis + 1) + ".";
            return false;
        }
        z.push_back(zk);
    }
    AblaufEinsatz rahmen;
    rahmenVon(oben, rahmen.vorne, rahmen.oben, rahmen.quer);
    const Vector3D c = zustandMitte(*z[0]);
    const Affin B = rahmenMatrix(rahmen, c), Binv = affinInvers(B);
    const double groesse = (z[0]->maxBound - z[0]->minBound).length();
    v = AblaufVorlage();
    v.name = oben->name.empty() ? "Bewegung" : oben->name + " Bewegung";
    const double t0 = snapshots[size_t(von)].zeit;
    v.dauer = snapshots[size_t(bis)].zeit - t0;
    for (int k = von; k <= bis; ++k)
    {
        AblaufPose p;
        p.zeit = snapshots[size_t(k)].zeit - t0;
        v.posen.push_back(p);
    }
    // je Knoten: L = K_Eltern^-1 * M (M = ganze Bewegung des Knotens), im Rahmen gespeichert
    bool bewegt = false;
    std::function<void(const std::vector<const ObjektZustand *> &, const std::string &, const std::vector<Affin> &)> rek;
    rek = [&](const std::vector<const ObjektZustand *> &zs, const std::string &pfad, const std::vector<Affin> &K)
    {
        std::vector<Affin> M(zs.size()), L(zs.size());
        bool eigen = false;
        for (size_t k = 0; k < zs.size(); ++k)
        {
            M[k] = k == 0 ? Affin() : knotenAbbildung(*zs[0], *zs[k]);
            L[k] = affinInvers(K[k]).nach(M[k]);
            eigen = eigen || !istEinsGrob(L[k], groesse);
        }
        if (eigen)
        {
            bewegt = true;
            for (size_t k = 0; k < zs.size(); ++k)
                v.posen[k].teile.push_back({pfad, Binv.nach(L[k]).nach(B)});
        }
        else
            M = K; // folgt den Eltern
        // Raeder (Stab "Rad ...") rollen mit der Bahn
        const ObjektZustand &z0 = *zs[0];
        if (!pfad.empty() && z0.art == 4 && z0.o && z0.o->name.rfind("Rad", 0) == 0 && z0.zahl > 1e-9)
        {
            AblaufGlied g;
            g.pfad = pfad;
            g.art = WIEDERHOLUNG_ABROLLEN;
            g.weite = z0.zahl;
            g.gelenk = Binv.punkt(z0.position + (z0.v1 + z0.v2) * 0.5);
            g.achse = Binv.richtung(z0.v2 - z0.v1);
            if (g.achse.length() > 1e-12)
            {
                g.achse = g.achse * (1.0 / g.achse.length());
                v.glieder.push_back(g);
            }
        }
        const size_t nk = z0.kinder.size();
        for (size_t i = 0; i < nk; ++i)
        {
            std::vector<const ObjektZustand *> kz;
            for (const ObjektZustand *x : zs)
                if (x->kinder.size() == nk)
                    kz.push_back(&x->kinder[i]);
            if (kz.size() != zs.size() || !z0.kinder[i].o)
                continue;
            const std::string name = z0.kinder[i].o->name;
            rek(kz, pfad.empty() ? name : pfad + "/" + name, M);
        }
    };
    rek(z, std::string(), std::vector<Affin>(z.size()));
    if (!bewegt)
    {
        fehler = "Das Objekt und seine Teile sind in allen diesen Snapshots gleich – es gibt nichts aufzunehmen.";
        return false;
    }
    return true;
}

AblaufVorlage ablaufRueckwaerts(const AblaufVorlage &v)
{
    AblaufVorlage r = v;
    const std::string zusatz = " (rückwärts)";
    if (r.name.size() > zusatz.size() && r.name.compare(r.name.size() - zusatz.size(), zusatz.size(), zusatz) == 0)
        r.name.erase(r.name.size() - zusatz.size());
    else
        r.name += zusatz;
    r.geschwindigkeit = v.geschwindigkeit * -1.0;
    for (AblaufGlied &g : r.glieder)
        if (g.art == WIEDERHOLUNG_DREHEN)
            g.richtung = -g.richtung;
    if (v.posen.empty())
        return r;
    // vom Endstand aus (bezogen auf ihn) zurueck zur Ruhelage: P'(t) = P(Ende)^-1 * P(D - t)
    std::vector<std::string> pfade;
    for (const AblaufPose &p : v.posen)
        for (const auto &t : p.teile)
            if (std::find(pfade.begin(), pfade.end(), t.first) == pfade.end())
                pfade.push_back(t.first);
    const double D = v.posen.back().zeit;
    r.posen.clear();
    for (size_t k = v.posen.size(); k-- > 0;)
    {
        AblaufPose p;
        p.zeit = D - v.posen[k].zeit;
        for (const std::string &pf : pfade)
        {
            const Affin ende = poseBei(v, pf, D), jetzt = poseBei(v, pf, v.posen[k].zeit);
            p.teile.push_back({pf, affinInvers(ende).nach(jetzt)});
        }
        r.posen.push_back(p);
    }
    return r;
}

// ---------- Wiederholungen kopieren / einfuegen ----------
namespace
{
struct WdhAblage
{
    struct Eintrag
    {
        Wiederholung w;
        object *oben = nullptr; // oberstes Objekt der Quelle
        std::string pfad;       // Namenspfad von oben zum Teil
        Vector3D gelenkRel;     // Gelenk relativ zur Mitte von 'oben' im Quell-Snapshot
    };
    std::vector<Eintrag> liste;
};
WdhAblage wdhAblage;
} // namespace

int wiederholungenKopieren(int i, const std::vector<object *> &nur)
{
    wdhAblage.liste.clear();
    if (i < 0 || i >= int(snapshots.size()))
        return 0;
    const Snapshot &a = snapshots[size_t(i)];
    for (const Wiederholung &w : a.wiederholungen)
    {
        if (w.art == WIEDERHOLUNG_KEINE || !objektVorhanden(w.o))
            continue;
        if (!nur.empty())
        {
            bool dabei = false;
            for (object *o : nur)
                dabei = dabei || o == w.o || enthaeltObjekt(o, w.o, 0, false);
            if (!dabei)
                continue;
        }
        const ObjektZustand *zo = obersterZustand(a, w.o);
        if (!zo)
            continue;
        WdhAblage::Eintrag e;
        e.w = w;
        e.oben = zo->o;
        namenPfad(zo->o, w.o, e.pfad);
        e.gelenkRel = w.gelenk - (zo->minBound + zo->maxBound) * 0.5;
        wdhAblage.liste.push_back(e);
    }
    return int(wdhAblage.liste.size());
}

int wiederholungenInAblage() { return int(wdhAblage.liste.size()); }

int wiederholungenEinfuegen(int i, object *ziel)
{
    if (i < 0 || i >= int(snapshots.size()))
        return 0;
    const Snapshot &a = snapshots[size_t(i)];
    int n = 0;
    for (const WdhAblage::Eintrag &e : wdhAblage.liste)
    {
        object *oben = ziel ? ziel : e.oben;
        if (!objektVorhanden(oben))
            continue;
        object *teil = (oben == e.oben && objektVorhanden(e.w.o)) ? e.w.o : teilAusPfad(oben, e.pfad);
        const ObjektZustand *zo = obersterZustand(a, oben);
        if (!teil || !zo)
            continue;
        Wiederholung w = e.w;
        w.o = teil;
        w.gelenk = e.gelenkRel + (zo->minBound + zo->maxBound) * 0.5; // Lage des Ziels im Snapshot dieses Abschnitts
        wiederholungSetzen(i, w);
        ++n;
    }
    return n;
}

static void zustandZeitpunkt(double zeit)
{
    if (snapshots.empty())
        return;

    // Nachbar-Snapshots a (vorher) und b (nachher), f = Anteil dazwischen
    size_t ia = 0, ib = 0;
    double f = 0;
    const size_t n = snapshots.size();
    if (zeit < snapshots.front().zeit)
        ia = ib = 0;
    else if (n >= 2 && std::fabs(zeit - snapshots.back().zeit) < 1e-9)
    {
        ia = n - 2; // genau am Ende: letzter Abschnitt mit f = 1 (Wiederholungen bis zum Schluss)
        ib = n - 1;
        f = 1.0;
    }
    else if (zeit >= snapshots.back().zeit)
        ia = ib = n - 1;
    else
    {
        while (ib < snapshots.size() && snapshots[ib].zeit <= zeit)
            ++ib;
        ia = ib - 1;
        double dauer = snapshots[ib].zeit - snapshots[ia].zeit;
        f = dauer > 1e-12 ? (zeit - snapshots[ia].zeit) / dauer : 0.0;
    }
    const Snapshot &a = snapshots[ia], &b = snapshots[ib];
    struct FellDanach // am Ende (auch bei frueher Rueckkehr): Fell-Huellen folgen den bewegten Koerperteilen
    {
        ~FellDanach() { fell::nachfuehren(scene); }
    } fellDanach;

    for (const auto &za : a.objekte)
    {
        if (!zustandAnwendbar(za))
            continue;
        const ObjektZustand *zb = nullptr;
        for (const auto &x : b.objekte)
            if (x.o == za.o)
                zb = &x;
        bool mitAblauf = false;
        for (const AblaufEinsatz &e : gAblaufEinsaetze)
            mitAblauf = mitAblauf || e.o == za.o;
        if ((ia == ib || a.wiederholungen.empty()) && !mitAblauf)
        {
            ZustandZugriff::setzen(zb ? zustandMischen(za, *zb, f) : za);
            grundZustaende.erase(za.o);
            continue;
        }
        ObjektZustand z = zb ? zustandMischen(za, *zb, f) : za;
        ObjektZustand grund = z;
        WdhKontext k{a, b, zeit, obenAbbildung(za, zb, f)};
        if (ia != ib && !a.wiederholungen.empty())
            wiederholungenRekursiv(z, za, zb, k, Affin());
        if (mitAblauf && ablaeufeAnwenden(z, grund, zeit)) // Ablaeufe ueber Snapshots und Wiederholungen
            k.angewendet = true;
        ZustandZugriff::setzen(z);
        if (k.angewendet)
        {
            Kennzeichen kz;
            kennzeichenSammeln(za.o, kz);
            grundZustaende[za.o] = {std::move(grund), kz};
        }
        else
            grundZustaende.erase(za.o);
    }
    for (const auto &la : a.lichter)
    {
        if (lichtPlatz(la.l) < 0)
            continue;
        LichtZustand l = la;
        for (const auto &lb : b.lichter)
            if (lb.l == la.l)
            {
                l.position = mischen(la.position, lb.position, f);
                l.farbe = mischen(la.farbe, lb.farbe, f);
                Vector3D r = mischen(la.richtung, lb.richtung, f);
                l.richtung = r.length() > 1e-12 ? r.normalized() : la.richtung;
                if (la.leistung >= 0 && lb.leistung >= 0)
                    l.leistung = mischen(la.leistung, lb.leistung, f);
                l.kanteU = mischen(la.kanteU, lb.kanteU, f);
                if (la.winkel > 0 && lb.winkel > 0) // Spotlicht: Kegel weitet sich bzw. zieht sich zusammen
                    l.winkel = mischen(la.winkel, lb.winkel, f);
                if (la.weich >= 0 && lb.weich >= 0)
                    l.weich = mischen(la.weich, lb.weich, f);
                l.kanteV = mischen(la.kanteV, lb.kanteV, f);
            }
        lichtSetzen(l);
    }
    if (a.mitKamera && b.mitKamera && !cam.empty())
    {
        Camera &c = cam[0];
        c.position = mischen(a.kameraPosition, b.kameraPosition, f);
        c.lookAt = mischen(a.kameraZiel, b.kameraZiel, f);
        c.fov = mischen(a.kameraFov, b.kameraFov, f);
        c.target = c.lookAt;
        // Kreisbahn (mittlere Maustaste) passend zur neuen Lage, wie in camera.cpp
        Vector3D d = c.position - c.target;
        c.distance = d.length();
        if (c.distance > 1e-12)
        {
            c.pitch = std::asin(std::clamp(d.y / c.distance, -1.0, 1.0));
            c.yaw = std::atan2(d.x, d.z);
        }
        c.update();
    }
    alleGruppenAktualisieren();
}

void zeitpunktAnwenden(double zeit, bool interaktiv)
{
    zeitJetzt = zeit;
    if (snapshots.empty())
        return;
    if (interaktiv && gSzeneAendern && (gRenderLaeufe > 0 || rendernRunning))
        gSzeneAendern([&]
                      { zustandZeitpunkt(zeit); }); // haelt das Raytracing an
    else
    {
        std::lock_guard<std::mutex> lock(sceneMutex); // GPU-Ansichten lesen die Geometrie
        zustandZeitpunkt(zeit);
    }
    computeSceneBounds();
}

// ======================================================================
// Snapshots in der XML: <animation> ... </animation> (SceneSaver/SceneLoader)
//
// Je Snapshot und Objekt wird nicht die ganze Geometrie geschrieben, sondern die Abbildung von der
// gespeicherten Geometrie (Stand beim Speichern) auf die des Snapshots: transform="3x4-Matrix".
// Dazu Position, Ausrichtung, Grenzen, Material (nur wenn anders) und die Werte der Grundkoerper.
// Objekte und Lichter werden ueber ihre Nummer in der Datei zugeordnet.
// ======================================================================

std::vector<std::vector<Dreieck> *> objektDreiecke(object *o)
{
    return ZustandZugriff::dreieckListen(o);
}

static std::string text3(const Vector3D &v)
{
    std::ostringstream o;
    o.precision(12);
    o << v.x << " " << v.y << " " << v.z;
    return o.str();
}
static std::string text3(const Farbe &f)
{
    std::ostringstream o;
    o.precision(12);
    o << f.r << " " << f.g << " " << f.b;
    return o.str();
}

static bool materialGleich(const Material &a, const Material &b)
{
    auto gleich = [](const Farbe &x, const Farbe &y)
    { return x.r == y.r && x.g == y.g && x.b == y.b; };
    return gleich(a.ambient, b.ambient) && gleich(a.diffuse, b.diffuse) && gleich(a.specular, b.specular) &&
           gleich(a.emission, b.emission) && gleich(a.absorption, b.absorption) && a.kA == b.kA && a.kD == b.kD &&
           a.shininess == b.shininess && a.specularStrength == b.specularStrength && a.roughness == b.roughness &&
           a.reflection == b.reflection && a.IndexOfRefraction == b.IndexOfRefraction && a.transparency == b.transparency;
}

static void materialSchreiben(std::ostream &out, const Material &m)
{
    out << " matAmbient=\"" << text3(m.ambient) << "\" matDiffuse=\"" << text3(m.diffuse) << "\" matSpecular=\""
        << text3(m.specular) << "\" matEmission=\"" << text3(m.emission) << "\" matAbsorption=\"" << text3(m.absorption)
        << "\" matWerte=\"" << m.kA << " " << m.kD << " " << m.shininess << " " << m.specularStrength << " " << m.roughness
        << " " << m.reflection << " " << m.IndexOfRefraction << " " << m.transparency << "\"";
}

using Attribute = std::map<std::string, std::string>;

static bool attrVektor(const Attribute &a, const char *name, Vector3D &v)
{
    auto it = a.find(name);
    if (it == a.end())
        return false;
    std::istringstream w(it->second);
    Vector3D r;
    if (!(w >> r.x >> r.y >> r.z))
        return false;
    v = r;
    return true;
}
static bool attrFarbe(const Attribute &a, const char *name, Farbe &f)
{
    Vector3D v;
    if (!attrVektor(a, name, v))
        return false;
    f = Farbe(v.x, v.y, v.z);
    return true;
}
static bool attrZahl(const Attribute &a, const char *name, double &z)
{
    auto it = a.find(name);
    if (it == a.end())
        return false;
    try
    {
        z = std::stod(it->second);
        return true;
    }
    catch (...)
    {
        return false;
    }
}

static void materialLesen(const Attribute &a, Material &m)
{
    attrFarbe(a, "matAmbient", m.ambient);
    attrFarbe(a, "matDiffuse", m.diffuse);
    attrFarbe(a, "matSpecular", m.specular);
    attrFarbe(a, "matEmission", m.emission);
    attrFarbe(a, "matAbsorption", m.absorption);
    auto it = a.find("matWerte");
    if (it != a.end())
    {
        std::istringstream w(it->second);
        double v[8];
        bool ok = true;
        for (double &x : v)
            ok = ok && bool(w >> x);
        if (ok)
        {
            m.kA = v[0], m.kD = v[1], m.shininess = v[2], m.specularStrength = v[3];
            m.roughness = v[4], m.reflection = v[5], m.IndexOfRefraction = v[6], m.transparency = v[7];
        }
    }
}

// ein Objekt eines Snapshots (jetzt = Stand beim Speichern, snap = Stand im Snapshot)
static void snapshotObjektSchreiben(std::ostream &out, const ObjektZustand &jetzt, const ObjektZustand &snap, size_t index,
                                    int tiefe)
{
    if (jetzt.art != snap.art)
        return;
    std::ostringstream a;
    a.precision(12);
    a << " index=\"" << index << "\" position=\"" << text3(snap.position) << "\" orientation=\"";
    for (int i = 0; i < 9; ++i)
        a << (i ? " " : "") << snap.ausrichtung[i];
    a << "\" min=\"" << text3(snap.minBound) << "\" max=\"" << text3(snap.maxBound) << "\" center=\""
      << text3(snap.boundingCenter) << "\" radius=\"" << snap.boundingRadius << "\"";
    if (!materialGleich(jetzt.mat, snap.mat))
    {
        a << " materialChanged=\"1\"";
        materialSchreiben(a, snap.mat);
    }

    switch (snap.art)
    {
    case 1: // Mesh
    case 2: // Baum (wird als Mesh gespeichert: Holz und Blaetter hintereinander)
    case 5: // Pfeil
    case 6: // Text
    {
        Affin abb;
        if (!affinAnpassen({&jetzt.d1, &jetzt.d2}, {&snap.d1, &snap.d2}, abb))
        {
            std::cout << "Snapshot: Objekt " << index << " ist nicht nur verschoben/gedreht/skaliert, wird nicht gespeichert\n";
            return;
        }
        a << " transform=\"" << abb.text() << "\"";
        if (snap.art == 5)
            a << " v1=\"" << text3(snap.v1) << "\" value=\"" << snap.zahl << "\"";
        if (snap.art == 6)
            a << " v1=\"" << text3(snap.v1) << "\" v2=\"" << text3(snap.v2) << "\" v3=\"" << text3(snap.v3) << "\"";
        break;
    }
    case 3: // Kugel
        a << " value=\"" << snap.zahl << "\"";
        break;
    case 9: // Instanz: ihre Abbildung
        a << " instance=\"" << snap.abb.text() << "\"";
        break;
    case 4: // Stab
        a << " v1=\"" << text3(snap.v1) << "\" v2=\"" << text3(snap.v2) << "\" value=\"" << snap.zahl << "\"";
        break;
    case 8: // Rauchbox/Wolkenbereich
        a << " v1=\"" << text3(snap.v1) << "\" v2=\"" << text3(snap.v2) << "\" volume=\""
          << static_cast<VolumenBox *>(snap.o)->rauchIndex << "\"";
        break;
    default:
        break;
    }

    const std::string einzug(size_t(8 + 4 * tiefe), ' ');
    if (snap.art == 7 && jetzt.kinder.size() == snap.kinder.size())
    {
        out << einzug << "<object" << a.str() << ">\n";
        for (size_t k = 0; k < snap.kinder.size(); ++k)
            snapshotObjektSchreiben(out, jetzt.kinder[k], snap.kinder[k], k, tiefe + 1);
        out << einzug << "</object>\n";
    }
    else
        out << einzug << "<object" << a.str() << "/>\n";
}

// Nummer des Objekts in der Datei (Platzhalter wie Rauchboxen zaehlen nicht); -1: nicht direkt in der Szene
static int dateiNummer(const object *o)
{
    int nummer = 0;
    for (auto &so : scene)
    {
        const bool platzhalter = dynamic_cast<VolumenBox *>(so.get()) != nullptr;
        if (so.get() == o)
            return platzhalter ? -1 : nummer;
        if (!platzhalter)
            ++nummer;
    }
    return -1;
}

static object *objektMitDateiNummer(int nr)
{
    int nummer = 0;
    for (auto &so : scene)
    {
        if (dynamic_cast<VolumenBox *>(so.get()))
            continue;
        if (nummer++ == nr)
            return so.get();
    }
    return nullptr;
}

static const char *const WIEDERHOLUNG_NAMEN[] = {"", "rotate", "swing", "bob", "roll"};

// Pfad eines Objekts in der Datei: "3" (Objekt 3) bzw. "3/1/0" (Teil 0 von Teil 1 der Gruppe 3); leer: nicht gefunden
static bool pfadIn(const Gruppe *g, const object *o, std::string &pfad)
{
    for (size_t i = 0; i < g->kinder.size(); ++i)
    {
        const object *k = g->kinder[i].get();
        std::string unter;
        if (k == o || (dynamic_cast<const Gruppe *>(k) && pfadIn(static_cast<const Gruppe *>(k), o, unter)))
        {
            pfad = "/" + std::to_string(i) + unter;
            return true;
        }
    }
    return false;
}
static std::string objektPfad(const object *o)
{
    for (auto &so : scene)
    {
        std::string unter;
        if (so.get() == o || (dynamic_cast<const Gruppe *>(so.get()) && pfadIn(static_cast<const Gruppe *>(so.get()), o, unter)))
        {
            const int nr = dateiNummer(so.get());
            return nr < 0 ? std::string() : std::to_string(nr) + unter;
        }
    }
    return std::string();
}
static object *objektAusPfad(const std::string &pfad)
{
    std::istringstream w(pfad);
    std::string teil;
    object *o = nullptr;
    bool erstes = true;
    while (std::getline(w, teil, '/'))
    {
        char *ende = nullptr;
        const long nr = std::strtol(teil.c_str(), &ende, 10);
        if (teil.empty() || *ende || nr < 0)
            return nullptr;
        if (erstes)
        {
            o = objektMitDateiNummer(int(nr));
            erstes = false;
        }
        else
        {
            auto *g = dynamic_cast<Gruppe *>(o);
            if (!g || size_t(nr) >= g->kinder.size())
                return nullptr;
            o = g->kinder[size_t(nr)].get();
        }
        if (!o)
            return nullptr;
    }
    return o;
}

// Ablaeufe in der XML: <motionTemplate> (gespeicherte Vorlagen) und <motion> (Einsaetze) in <animation>
static std::string xmlMaske(const std::string &x)
{
    std::string t;
    for (char c : x)
        t += c == '&' ? "&amp;" : c == '<' ? "&lt;" : c == '>' ? "&gt;" : c == '"' ? "&quot;" : std::string(1, c);
    return t;
}
static std::string xmlZurueck(const std::string &t)
{
    std::string r;
    for (size_t i = 0; i < t.size(); ++i)
    {
        static const std::pair<const char *, char> ent[] = {{"&amp;", '&'}, {"&lt;", '<'}, {"&gt;", '>'}, {"&quot;", '"'}};
        bool ersetzt = false;
        for (auto &e : ent)
            if (t.compare(i, std::strlen(e.first), e.first) == 0)
            {
                r += e.second;
                i += std::strlen(e.first) - 1;
                ersetzt = true;
                break;
            }
        if (!ersetzt)
            r += t[i];
    }
    return r;
}
static void ablaufGliederSchreiben(std::ostream &o, const AblaufVorlage &v, const char *einzug);

void animationSpeichern(std::ostream &out)
{
    if (snapshots.empty() && !animationAn)
        return;
    std::ostringstream o;
    o.precision(12);
    o << "    <animation fps=\"" << fps << "\" active=\"" << (animationAn ? 1 : 0) << "\" current=\"" << zeitJetzt << "\">\n";
    for (const Snapshot &s : snapshots)
    {
        o << "      <snapshot time=\"" << s.zeit << "\"";
        if (!s.label.empty())
        {
            std::string t; // fuer das XML-Attribut maskieren
            for (char c : s.label)
                t += c == '&' ? "&amp;" : c == '<' ? "&lt;" : c == '>' ? "&gt;" : c == '"' ? "&quot;" : std::string(1, c);
            o << " label=\"" << t << "\"";
        }
        o << ">\n";
        if (s.mitKamera)
            o << "        <camera position=\"" << text3(s.kameraPosition) << "\" target=\"" << text3(s.kameraZiel)
              << "\" fov=\"" << s.kameraFov << "\"/>\n";
        for (const LichtZustand &l : s.lichter)
        {
            int platz = lichtPlatz(l.l);
            if (platz < 0)
                continue;
            o << "        <light index=\"" << platz << "\" position=\"" << text3(l.position) << "\" color=\""
              << text3(l.farbe) << "\" direction=\"" << text3(l.richtung) << "\"/>\n";
        }
        for (const ObjektZustand &z : s.objekte)
        {
            // Nummer in der Datei: Platzhalter (Rauchbox/Wolken) werden nicht als Objekt gespeichert
            size_t nummer = 0;
            for (size_t i = 0; i < scene.size(); ++i)
            {
                const bool platzhalter = dynamic_cast<VolumenBox *>(scene[i].get()) != nullptr;
                if (scene[i].get() == z.o)
                {
                    snapshotObjektSchreiben(o, ZustandZugriff::lesen(scene[i].get()), z, nummer, 0);
                    break;
                }
                if (!platzhalter)
                    ++nummer;
            }
        }
        for (const Wiederholung &w : s.wiederholungen)
        {
            if (w.art <= WIEDERHOLUNG_KEINE || w.art > WIEDERHOLUNG_ABROLLEN || !objektVorhanden(w.o))
                continue;
            const std::string pfad = objektPfad(w.o);
            if (pfad.empty())
                continue;
            o << "        <repeat object=\"" << pfad << "\" type=\"" << WIEDERHOLUNG_NAMEN[w.art] << "\" axis=\"";
            if (w.achse == 3)
                o << text3(w.achsVektor);
            else
                o << "xyz"[std::clamp(w.achse, 0, 2)];
            o << "\" local=\"" << (w.lokal ? 1 : 0) << "\" period=\"" << w.periode << "\" amount=\"" << w.weite
              << "\" direction=\"" << (w.richtung < 0 ? -1 : 1) << "\"";
            if (w.phase != 0)
                o << " phase=\"" << w.phase << "\"";
            if (w.mittel != 0)
                o << " offset=\"" << w.mittel << "\"";
            if (w.mitGelenk)
                o << " pivot=\"" << text3(w.gelenk) << "\"";
            o << "/>\n";
        }
        o << "      </snapshot>\n";
    }
    for (const AblaufVorlage &v : gAblaufVorlagen)
    {
        o << "      <motionTemplate name=\"" << xmlMaske(v.name) << "\" speed=\"" << text3(v.geschwindigkeit) << "\" duration=\""
          << v.dauer << "\"";
        if (v.einlauf >= 0)
            o << " ramp=\"" << v.einlauf << "\"";
        o << ">\n";
        ablaufGliederSchreiben(o, v, "        ");
        o << "      </motionTemplate>\n";
    }
    for (const AblaufEinsatz &e : gAblaufEinsaetze)
    {
        if (!ablaufGueltig(e))
            continue;
        const std::string pfad = objektPfad(e.o);
        if (pfad.empty())
            continue;
        o << "      <motion object=\"" << pfad << "\" name=\"" << xmlMaske(e.v.name) << "\" start=\"" << e.start << "\" duration=\""
          << e.dauer << "\" tempo=\"" << e.tempo << "\" every=\"" << e.alle << "\" until=\"" << e.bis << "\" front=\""
          << text3(e.vorne) << "\" up=\"" << text3(e.oben) << "\" side=\"" << text3(e.quer) << "\" speed=\""
          << text3(e.v.geschwindigkeit) << "\" templateDuration=\"" << e.v.dauer << "\"";
        if (e.v.einlauf >= 0)
            o << " ramp=\"" << e.v.einlauf << "\"";
        o << ">\n";
        ablaufGliederSchreiben(o, e.v, "        ");
        o << "      </motion>\n";
    }
    o << "    </animation>\n\n";
    out << o.str();
}

// Snapshot-Eintrag auf den geladenen Zustand z anwenden (z = Stand nach dem Laden)
static void snapshotObjektLesen(const AnimationsKnoten &k, ObjektZustand &z)
{
    const Attribute &a = k.attr;
    attrVektor(a, "position", z.position);
    {
        auto it = a.find("orientation");
        if (it != a.end())
        {
            std::istringstream w(it->second);
            double m[9];
            bool ok = true;
            for (double &x : m)
                ok = ok && bool(w >> x);
            if (ok)
                std::copy(m, m + 9, z.ausrichtung);
        }
    }
    attrVektor(a, "min", z.minBound);
    attrVektor(a, "max", z.maxBound);
    attrVektor(a, "center", z.boundingCenter);
    attrZahl(a, "radius", z.boundingRadius);

    if (a.count("materialChanged"))
    {
        materialLesen(a, z.mat);
        for (auto *liste : {&z.d1, &z.d2})
            for (auto &d : *liste)
                d.mat = z.mat;
    }

    auto tr = a.find("transform");
    Affin abb;
    if (tr != a.end() && abb.lesen(tr->second))
    {
        affinAnwenden(abb, z.d1);
        affinAnwenden(abb, z.d2);
        if (z.art == 2)
            z.v1 = abb.punkt(z.v1); // eigene Position des Baums
    }
    switch (z.art)
    {
    case 9:
    {
        auto ins = a.find("instance");
        Affin ia;
        if (ins != a.end() && ia.lesen(ins->second))
            z.abb = ia;
        break;
    }
    case 3:
        attrZahl(a, "value", z.zahl);
        break;
    case 4:
        attrVektor(a, "v1", z.v1);
        attrVektor(a, "v2", z.v2);
        attrZahl(a, "value", z.zahl);
        break;
    case 5:
        attrVektor(a, "v1", z.v1);
        attrZahl(a, "value", z.zahl);
        break;
    case 6:
        attrVektor(a, "v1", z.v1);
        attrVektor(a, "v2", z.v2);
        attrVektor(a, "v3", z.v3);
        break;
    case 8:
        attrVektor(a, "v1", z.v1);
        attrVektor(a, "v2", z.v2);
        break;
    case 7:
        for (const auto &kind : k.kinder)
        {
            double nr = -1;
            if (kind.name == "object" && attrZahl(kind.attr, "index", nr) && nr >= 0 && size_t(nr) < z.kinder.size())
                snapshotObjektLesen(kind, z.kinder[size_t(nr)]);
        }
        break;
    default:
        break;
    }
}

static void ablaufGliederSchreiben(std::ostream &o, const AblaufVorlage &v, const char *einzug)
{
    for (const AblaufGlied &g : v.glieder)
    {
        o << einzug << "<part path=\"" << xmlMaske(g.pfad) << "\" type=\"" << WIEDERHOLUNG_NAMEN[std::clamp(g.art, 1, 4)] << "\" axis=\""
          << text3(g.achse) << "\" period=\"" << g.periode << "\" amount=\"" << g.weite << "\" offset=\"" << g.mittel << "\" phase=\""
          << g.phase << "\" direction=\"" << (g.richtung < 0 ? -1 : 1) << "\"";
        if (g.mitGelenk)
            o << " pivot=\"" << text3(g.gelenk) << "\"";
        o << "/>\n";
    }
    for (const AblaufPose &p : v.posen)
    {
        o << einzug << "<pose time=\"" << p.zeit << "\">\n";
        for (const auto &t : p.teile)
            o << einzug << "  <place path=\"" << xmlMaske(t.first) << "\" matrix=\"" << t.second.text() << "\"/>\n";
        o << einzug << "</pose>\n";
    }
}

static void ablaufGliederLesen(const AnimationsKnoten &k, AblaufVorlage &v)
{
    for (const auto &p : k.kinder)
    {
        if (p.name == "pose")
        {
            AblaufPose pose;
            attrZahl(p.attr, "time", pose.zeit);
            for (const auto &t : p.kinder)
            {
                Affin A;
                if (t.name != "place" || !t.attr.count("matrix") || !A.lesen(t.attr.at("matrix")))
                    continue;
                pose.teile.push_back({t.attr.count("path") ? xmlZurueck(t.attr.at("path")) : std::string(), A});
            }
            v.posen.push_back(pose);
            continue;
        }
        if (p.name != "part")
            continue;
        AblaufGlied g;
        g.pfad = p.attr.count("path") ? xmlZurueck(p.attr.at("path")) : std::string();
        const std::string typ = p.attr.count("type") ? p.attr.at("type") : std::string("swing");
        for (int i = 1; i <= WIEDERHOLUNG_ABROLLEN; ++i)
            if (typ == WIEDERHOLUNG_NAMEN[i])
                g.art = i;
        attrVektor(p.attr, "axis", g.achse);
        attrZahl(p.attr, "period", g.periode);
        attrZahl(p.attr, "amount", g.weite);
        attrZahl(p.attr, "offset", g.mittel);
        attrZahl(p.attr, "phase", g.phase);
        double r = 1;
        if (attrZahl(p.attr, "direction", r))
            g.richtung = r < 0 ? -1 : 1;
        g.mitGelenk = attrVektor(p.attr, "pivot", g.gelenk);
        v.glieder.push_back(g);
    }
}

void animationLaden(const AnimationsKnoten &k)
{
    mischSpeicherLeeren();
    grundZustaende.clear();
    snapshots.clear();
    double wert;
    if (attrZahl(k.attr, "fps", wert))
        bilderProSekundeSetzen(int(std::round(wert)));
    animationAn = k.attr.count("active") && k.attr.at("active") == "1";

    for (const auto &sk : k.kinder)
    {
        if (sk.name != "snapshot")
            continue;
        Snapshot s;
        attrZahl(sk.attr, "time", s.zeit);
        if (sk.attr.count("label"))
        {
            // Entities zuruecksetzen (falls der Parser sie stehen laesst)
            std::string t = sk.attr.at("label"), r;
            for (size_t i = 0; i < t.size(); ++i)
            {
                static const std::pair<const char *, char> ent[] = {{"&amp;", '&'}, {"&lt;", '<'}, {"&gt;", '>'}, {"&quot;", '"'}};
                bool ersetzt = false;
                for (auto &e : ent)
                    if (t.compare(i, std::strlen(e.first), e.first) == 0)
                    {
                        r += e.second;
                        i += std::strlen(e.first) - 1;
                        ersetzt = true;
                        break;
                    }
                if (!ersetzt)
                    r += t[i];
            }
            s.label = r;
        }
        for (const auto &e : sk.kinder)
        {
            double nr = -1;
            if (e.name == "camera" && !cam.empty())
            {
                s.mitKamera = true;
                s.kameraPosition = cam[0].position;
                s.kameraZiel = cam[0].lookAt;
                s.kameraFov = cam[0].fov;
                attrVektor(e.attr, "position", s.kameraPosition);
                attrVektor(e.attr, "target", s.kameraZiel);
                attrZahl(e.attr, "fov", s.kameraFov);
            }
            else if (e.name == "light" && attrZahl(e.attr, "index", nr) && nr >= 0 && size_t(nr) < lights.size())
            {
                LichtZustand l = lichtLesen(lights[size_t(nr)].get());
                attrVektor(e.attr, "position", l.position);
                attrFarbe(e.attr, "color", l.farbe);
                attrVektor(e.attr, "direction", l.richtung);
                s.lichter.push_back(l);
            }
            else if (e.name == "repeat" && e.attr.count("object"))
            {
                Wiederholung w;
                w.o = objektAusPfad(e.attr.at("object"));
                const std::string typ = e.attr.count("type") ? e.attr.at("type") : std::string();
                for (int i = 1; i <= WIEDERHOLUNG_ABROLLEN; ++i)
                    if (typ == WIEDERHOLUNG_NAMEN[i])
                        w.art = i;
                const std::string achse = e.attr.count("axis") ? e.attr.at("axis") : std::string("z");
                w.achse = (achse == "x" || achse == "X" || achse == "0") ? 0 : (achse == "y" || achse == "Y" || achse == "1") ? 1 : 2;
                if (achse.find(' ') != std::string::npos && attrVektor(e.attr, "axis", w.achsVektor))
                    w.achse = 3; // freie Achse "x y z"
                attrZahl(e.attr, "phase", w.phase);
                attrZahl(e.attr, "offset", w.mittel);
                w.mitGelenk = attrVektor(e.attr, "pivot", w.gelenk);
                w.lokal = e.attr.count("local") && e.attr.at("local") == "1";
                attrZahl(e.attr, "period", w.periode);
                attrZahl(e.attr, "amount", w.weite);
                double r = 1;
                if (attrZahl(e.attr, "direction", r))
                    w.richtung = r < 0 ? -1 : 1;
                if (w.o && w.art != WIEDERHOLUNG_KEINE)
                    s.wiederholungen.push_back(w);
            }
            else if (e.name == "object" && e.attr.count("volume"))
            {
                double box = 0;
                if (attrZahl(e.attr, "volume", box))
                    if (VolumenBox *vb = volumenBoxVon(int(std::round(box))))
                    {
                        ObjektZustand z = ZustandZugriff::lesen(vb);
                        snapshotObjektLesen(e, z);
                        s.objekte.push_back(std::move(z));
                    }
            }
            else if (e.name == "object" && attrZahl(e.attr, "index", nr) && nr >= 0)
            {
                // Nummer in der Datei: Platzhalter ueberspringen
                object *ziel = nullptr;
                size_t nummer = 0;
                for (auto &so : scene)
                {
                    if (dynamic_cast<VolumenBox *>(so.get()))
                        continue;
                    if (nummer++ == size_t(nr))
                    {
                        ziel = so.get();
                        break;
                    }
                }
                if (ziel)
                {
                    ObjektZustand z = ZustandZugriff::lesen(ziel);
                    snapshotObjektLesen(e, z);
                    s.objekte.push_back(std::move(z));
                }
            }
        }
        einsortieren(std::move(s));
    }
    gAblaufEinsaetze.clear();
    for (const auto &e : k.kinder)
    {
        if (e.name == "motionTemplate")
        {
            AblaufVorlage v;
            v.name = e.attr.count("name") ? xmlZurueck(e.attr.at("name")) : std::string("Ablauf");
            attrVektor(e.attr, "speed", v.geschwindigkeit);
            attrZahl(e.attr, "duration", v.dauer);
            attrZahl(e.attr, "ramp", v.einlauf);
            ablaufGliederLesen(e, v);
            bool ersetzt = false; // gleicher Name: die Vorlage aus der Datei gilt
            for (auto &x : gAblaufVorlagen)
                if (x.name == v.name)
                {
                    x = v;
                    ersetzt = true;
                }
            if (!ersetzt)
                gAblaufVorlagen.push_back(v);
        }
        else if (e.name == "motion" && e.attr.count("object"))
        {
            AblaufEinsatz a;
            a.o = objektAusPfad(e.attr.at("object"));
            if (!a.o)
                continue;
            a.v.name = e.attr.count("name") ? xmlZurueck(e.attr.at("name")) : std::string("Ablauf");
            attrZahl(e.attr, "start", a.start);
            attrZahl(e.attr, "duration", a.dauer);
            attrZahl(e.attr, "tempo", a.tempo);
            attrZahl(e.attr, "every", a.alle);
            attrZahl(e.attr, "until", a.bis);
            attrVektor(e.attr, "front", a.vorne);
            attrVektor(e.attr, "up", a.oben);
            attrVektor(e.attr, "side", a.quer);
            attrVektor(e.attr, "speed", a.v.geschwindigkeit);
            a.v.dauer = a.dauer;
            attrZahl(e.attr, "templateDuration", a.v.dauer);
            attrZahl(e.attr, "ramp", a.v.einlauf);
            ablaufGliederLesen(e, a.v);
            std::stable_sort(a.v.posen.begin(), a.v.posen.end(),
                             [](const AblaufPose &x, const AblaufPose &y) { return x.zeit < y.zeit; });
            gAblaufEinsaetze.push_back(a);
        }
    }
    zeitJetzt = 0;
    if (attrZahl(k.attr, "current", wert))
        zeitJetzt = wert;
}
