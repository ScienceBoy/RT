#pragma once
// Fell-Imitation (Reiter Manipulation): Fell aus mehreren Huellen um ein Koerperteil.
// Jede Huelle ist eine Kopie der Oberflaeche, um k/N * Laenge entlang der gemittelten Eckennormalen nach aussen
// geschoben (dazu etwas nach unten: "Haengen"). Sie traegt ein Alpha-Bild mit Haarspitzen (nach aussen weniger und
// duenner) und die Fellfarbe (nach innen dunkler). Die Huellen haben dieselbe Textur-Ruhelage wie das Koerperteil,
// damit die Haare aller Huellen genau uebereinander liegen.
// Die Huellen sind Begleiter ihres Koerperteils: nach jeder Aenderung, nach Rueckgaengig und in jedem Bild der
// Animation werden sie aus dem Koerperteil neu berechnet (nachfuehren) - so laufen sie auch bei gehenden Tieren mit.
// Nur als Header (testbar ohne Oberflaeche).

#include "csg.h"
#include "dreieck.h"
#include "gruppe.h"
#include "kugel.h"
#include "mesh.h"
#include "stab.h"
#include "textur_abbildung.h"
#include "vector3d.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <map>
#include <random>
#include <string>
#include <tuple>
#include <unordered_set>
#include <vector>

namespace fell
{
struct Huelle
{
    object *huelle = nullptr; // Mesh
    object *basis = nullptr;  // Koerperteil (Mesh, Kugel, Stab)
    int k = 1, n = 8;         // Nummer der Huelle (1 = innen) und Anzahl
    double laenge = 0.02;     // Haarlaenge (Einheiten der Szene)
    double haengen = 0.0;     // 0..1: Spitzen fallen um haengen * Laenge nach unten
    int id = 0;               // gemeinsame Nummer aller Huellen eines Koerperteils (Speichern)
    double pruef = 0;         // Pruefsumme der Basis bei der letzten Berechnung
};

inline std::vector<Huelle> gHuellen; // alle Huellen der Szene

// Dreiecke der Oberflaeche eines Koerperteils (Kugel/Stab als Netz; feste Teilung: gleiche Reihenfolge je Aufruf)
inline bool basisDreiecke(const object *o, std::vector<Dreieck> &d)
{
    if (auto *m = dynamic_cast<const Mesh *>(o))
    {
        d = m->triangles;
        return !d.empty();
    }
    if (auto *k = dynamic_cast<const Kugel *>(o))
    {
        d = csg::kugelNetz(k->position, k->radius, k->mat, 40);
        return true;
    }
    if (auto *s = dynamic_cast<const Stab *>(o))
    {
        d = csg::zylinderNetz(s->p1 + s->position, s->p2 + s->position, s->radius, s->mat);
        return true;
    }
    return false;
}

inline double pruefsumme(const std::vector<Dreieck> &d)
{
    double s = double(d.size());
    for (size_t i = 0; i < d.size(); ++i)
    {
        const Dreieck &t = d[i];
        const Vector3D p = t.a + t.b * 1.618 + t.c * 2.414 + t.position * 3.0;
        s += (p.x * 1.0001 + p.y * 1.3 + p.z * 1.7) * double(i % 97 + 1);
    }
    return s;
}

// Huelle k von n aus den Basis-Dreiecken: gemeinsame Ecken (Raster) bekommen eine gemittelte Normale; jede
// Flaechennormale wird dafuer nach aussen gedreht (weg von der Mitte des Teils), weil die Dreiecke nicht einheitlich
// orientiert sein muessen. 'ziel' behaelt Material, Textur und Ruhelage, wenn die Anzahl passt.
inline void huelleBerechnen(const std::vector<Dreieck> &basis, std::vector<Dreieck> &ziel, int k, int n, double laenge,
                            double haengen)
{
    if (basis.empty())
    {
        ziel.clear();
        return;
    }
    // Mitte und Raster
    Vector3D mn(1e300, 1e300, 1e300), mx(-1e300, -1e300, -1e300);
    for (const Dreieck &t : basis)
        for (const Vector3D &p : {t.a + t.position, t.b + t.position, t.c + t.position})
        {
            mn = Vector3D(std::min(mn.x, p.x), std::min(mn.y, p.y), std::min(mn.z, p.z));
            mx = Vector3D(std::max(mx.x, p.x), std::max(mx.y, p.y), std::max(mx.z, p.z));
        }
    const Vector3D mitte = (mn + mx) * 0.5;
    const double raster = std::max(1e-12, (mx - mn).length() * 1e-6);
    std::map<std::tuple<long long, long long, long long>, int> nummer;
    std::vector<int> index(basis.size() * 3);
    std::vector<Vector3D> normale;
    for (size_t i = 0; i < basis.size(); ++i)
    {
        const Dreieck &t = basis[i];
        const Vector3D P[3] = {t.a + t.position, t.b + t.position, t.c + t.position};
        Vector3D nf = (P[1] - P[0]).cross(P[2] - P[0]); // Laenge = doppelte Flaeche (Gewicht)
        if (nf * ((P[0] + P[1] + P[2]) * (1.0 / 3.0) - mitte) < 0)
            nf = -nf;
        for (int e = 0; e < 3; ++e)
        {
            auto key = std::make_tuple((long long)std::llround(P[e].x / raster), (long long)std::llround(P[e].y / raster),
                                       (long long)std::llround(P[e].z / raster));
            auto it = nummer.find(key);
            if (it == nummer.end())
            {
                it = nummer.emplace(key, int(normale.size())).first;
                normale.push_back(Vector3D(0, 0, 0));
            }
            index[i * 3 + size_t(e)] = it->second;
            normale[size_t(it->second)] = normale[size_t(it->second)] + nf;
        }
    }
    for (Vector3D &v : normale)
    {
        const double l = v.length();
        v = l > 1e-300 ? v * (1.0 / l) : Vector3D(0, 1, 0);
    }
    const double t = double(k) / double(std::max(1, n));
    const double abstand = laenge * t;
    const Vector3D fallen = Vector3D(0, -1, 0) * (std::clamp(haengen, 0.0, 1.0) * laenge * t * t);

    const bool passt = ziel.size() == basis.size();
    if (!passt)
        ziel.clear();
    for (size_t i = 0; i < basis.size(); ++i)
    {
        const Dreieck &b = basis[i];
        Vector3D P[3] = {b.a + b.position, b.b + b.position, b.c + b.position};
        for (int e = 0; e < 3; ++e)
            P[e] = P[e] + normale[size_t(index[i * 3 + size_t(e)])] * abstand + fallen;
        if (passt)
        {
            Dreieck &z = ziel[i];
            z.a = P[0], z.b = P[1], z.c = P[2];
            z.position = Vector3D(0, 0, 0);
            z.rebuild();
        }
        else
        {
            Dreieck z(P[0], P[1], P[2], b.mat);
            z.uvA = b.uvA, z.uvB = b.uvB, z.uvC = b.uvC;
            ziel.push_back(z);
        }
    }
}

// Ruhelage der Textur von der Basis auf die Huelle uebernehmen (gleiche Reihenfolge der Dreiecke). Hat die Basis
// keine, wird sie aus der jetzigen Lage der Basis bestimmt.
inline void ruheUebernehmen(const std::vector<Dreieck> &basis, std::vector<Dreieck> &huelle)
{
    if (basis.size() != huelle.size())
        return;
    bool alle = true;
    for (const Dreieck &t : basis)
        alle = alle && t.ruheGesetzt;
    std::vector<Dreieck> kopie;
    const std::vector<Dreieck> *quelle = &basis;
    if (!alle)
    {
        kopie = basis;
        texturRuheSetzen(kopie);
        quelle = &kopie;
    }
    for (size_t i = 0; i < huelle.size(); ++i)
    {
        const Dreieck &q = (*quelle)[i];
        Dreieck &z = huelle[i];
        std::copy(q.ruhe, q.ruhe + 9, z.ruhe);
        z.ruheGesetzt = true;
        z.texturBox = true;
        z.texturMin = q.texturMin;
        z.texturMax = q.texturMax;
    }
}

// Alpha-Bild der Huelle k von n: dieselben Haarpositionen in allen Huellen (fester Zufall), nach aussen duenner;
// kurze Haare enden frueher. Graustufen 0/255, Groesse s x s (kachelbar).
inline std::vector<uint8_t> haarBild(int s, int haare, int k, int n, unsigned saat = 12345)
{
    std::vector<uint8_t> bild(size_t(s) * s, 0);
    std::mt19937 zufall(saat);
    std::uniform_real_distribution<double> gleich(0.0, 1.0);
    const double abstand = s / std::sqrt(std::max(1, haare)); // mittlerer Abstand der Haare in Bildpunkten
    const double t = double(k) / double(std::max(1, n));
    for (int i = 0; i < haare; ++i)
    {
        const double x = gleich(zufall) * s, y = gleich(zufall) * s;
        const double laenge = std::min(1.0, 0.5 + 0.55 * gleich(zufall)); // etwa jedes zehnte Haar reicht bis aussen
        const double dicke = 0.8 + 0.4 * gleich(zufall);
        if (t > laenge + 1e-9)
            continue;
        const double r = std::max(0.6, abstand * dicke * (0.30 * std::pow(std::max(0.0, 1.0 - t / laenge), 0.6) + 0.08));
        const int ri = int(std::ceil(r));
        for (int dy = -ri; dy <= ri; ++dy)
            for (int dx = -ri; dx <= ri; ++dx)
            {
                if (dx * dx + dy * dy > r * r)
                    continue;
                const int px = ((int(x) + dx) % s + s) % s, py = ((int(y) + dy) % s + s) % s;
                bild[size_t(py) * s + px] = 255;
            }
    }
    return bild;
}

// Bild als 24-Bit-BMP schreiben (Zeile 0 = oben; BMP speichert von unten nach oben)
inline bool bmpSchreiben(const std::string &datei, int b, int h, const std::vector<uint32_t> &argb)
{
    const int zeile = ((24 * b + 31) / 32) * 4;
    const uint32_t groesse = 54 + uint32_t(zeile) * uint32_t(h);
    std::vector<uint8_t> d(groesse, 0);
    auto u16 = [&](size_t o, uint16_t v) { d[o] = uint8_t(v), d[o + 1] = uint8_t(v >> 8); };
    auto u32 = [&](size_t o, uint32_t v)
    {
        for (int i = 0; i < 4; ++i)
            d[o + size_t(i)] = uint8_t(v >> (8 * i));
    };
    d[0] = 'B', d[1] = 'M';
    u32(2, groesse);
    u32(10, 54);
    u32(14, 40);
    u32(18, uint32_t(b));
    u32(22, uint32_t(h));
    u16(26, 1);
    u16(28, 24);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < b; ++x)
        {
            const uint32_t p = argb[size_t(h - 1 - y) * b + x];
            const size_t o = 54 + size_t(y) * zeile + size_t(x) * 3;
            d[o] = uint8_t(p), d[o + 1] = uint8_t(p >> 8), d[o + 2] = uint8_t(p >> 16);
        }
    std::ofstream f(datei, std::ios::binary);
    f.write((const char *)d.data(), std::streamsize(d.size()));
    return bool(f);
}

// alle Objekte der Szene (auch in Gruppen), fuer die Gueltigkeit der Zeiger im Register
inline void alleObjekte(object *o, std::unordered_set<const object *> &menge)
{
    if (!o)
        return;
    menge.insert(o);
    if (auto *g = dynamic_cast<Gruppe *>(o))
        for (auto &k : g->kinder)
            alleObjekte(k.get(), menge);
}

// Huellen aus ihren Koerperteilen neu berechnen (unter der Szenensperre aufrufen). Huellen ohne Koerperteil oder
// nicht mehr in der Szene werden aus dem Register genommen (geloeschte Huellen kommen mit Rueckgaengig zurueck und
// bleiben dann stehen, bis "Fell-Imitation" erneut ausgefuehrt wird).
template <class Szene> inline void nachfuehren(Szene &szene)
{
    if (gHuellen.empty())
        return;
    std::unordered_set<const object *> da;
    for (auto &o : szene)
        alleObjekte(o.get(), da);
    std::vector<Dreieck> basis;
    const object *letzteBasis = nullptr;
    double summe = 0;
    for (Huelle &h : gHuellen)
    {
        if (!da.count(h.huelle) || !da.count(h.basis))
            continue;
        auto *m = dynamic_cast<Mesh *>(h.huelle);
        if (!m)
            continue;
        if (h.basis != letzteBasis)
        {
            letzteBasis = h.basis;
            if (!basisDreiecke(h.basis, basis))
            {
                basis.clear();
                continue;
            }
            summe = pruefsumme(basis);
        }
        if (basis.empty() || (summe == h.pruef && m->triangles.size() == basis.size()))
            continue;
        h.pruef = summe;
        const bool neuAufgebaut = m->triangles.size() != basis.size();
        huelleBerechnen(basis, m->triangles, h.k, h.n, h.laenge, h.haengen);
        if (neuAufgebaut) // andere Anzahl Dreiecke (Netz-Operation am Koerperteil): Material, Textur, Ruhelage neu
        {
            for (Dreieck &d : m->triangles)
                d.mat = m->mat;
            ruheUebernehmen(basis, m->triangles);
            m->setTexture(m->texture);
            m->setTextureMode(m->textureMode);
        }
        m->recomputeMeshBoundsFromTriangles();
        m->buildBVH();
        ++m->geometrieZaehler;
    }
}

// nur Huellen, die gerade in der Szene sind (geloeschte bleiben fuer Rueckgaengig im Register)
inline bool istHuelle(const object *o, const std::unordered_set<const object *> &da)
{
    for (const Huelle &h : gHuellen)
        if (h.huelle == o && da.count(h.huelle))
            return true;
    return false;
}

inline bool hatFell(const object *o, const std::unordered_set<const object *> &da)
{
    for (const Huelle &h : gHuellen)
        if (h.basis == o && da.count(h.huelle))
            return true;
    return false;
}

// Teile, die nie Fell bekommen (Namen wie bei "+ Neu -> Tier"; Gross-/Kleinschreibung egal, auch "Auge links" usw.)
inline bool ohneFell(const std::string &name)
{
    std::string n = name;
    for (char &c : n)
        c = char(std::tolower((unsigned char)c));
    static const char *const LISTE[] = {"auge", "pupille", "iris", "glanzpunkt", "nase", "schnurrhaar", "bart",
                                        "kralle", "zahn", "zunge", "lid", "halsband", "glocke"};
    for (const char *w : LISTE)
        if (n.rfind(w, 0) == 0) // beginnt damit
            return true;
    return false;
}

inline int naechsteId()
{
    int m = 0;
    for (const Huelle &h : gHuellen)
        m = std::max(m, h.id);
    return m + 1;
}
} // namespace fell
