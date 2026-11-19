#pragma once
// Operationen auf Dreiecksnetzen (Reiter "Manipulation" im Objektfenster).
//
// "Flaeche" = zusammenhaengende Dreiecke in derselben Ebene (gemeinsame Kanten, gleiche Normale),
// z.B. eine Wuerfelseite aus zwei Dreiecken. Gerechnet wird in Weltkoordinaten (a + position);
// die neuen Dreiecke haben position = 0 und uebernehmen Material, Vertex-Normalen und UV.
//
//  - flaechenZerlegen:   Dreiecke je Flaeche (fuer "Objekte in 3D-Ebenen umwandeln")
//  - delaunayFlippen:    Delaunay-Flipping je Flaeche (nach R. Porath 2022, CC BY 4.0)
//  - porathTriangulieren: Faecher vom Mittelpunkt + rekursive Teilung (nach R. Porath)
//  - loopUnterteilen:    ein Schritt Loop-Subdivision (glaettet das ganze Netz)

#include "dreieck.h"
#include "material.h"
#include "vector3d.h"
#include "vector2d.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <tuple>
#include <utility>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace netz
{
// ---------- Grundlagen ----------

// Ecke mit allem, was ein Dreieck an ihr traegt
struct Ecke
{
    Vector3D p, n;
    Vector2D uv;
};

inline Vector3D eckePos(const Dreieck &t, int k) { return (k == 0 ? t.a : k == 1 ? t.b : t.c) + t.position; }
inline Vector3D eckeNormale(const Dreieck &t, int k) { return k == 0 ? t.nA : k == 1 ? t.nB : t.nC; }
inline Vector2D eckeUV(const Dreieck &t, int k) { return k == 0 ? t.uvA : k == 1 ? t.uvB : t.uvC; }
inline Ecke ecke(const Dreieck &t, int k) { return {eckePos(t, k), eckeNormale(t, k), eckeUV(t, k)}; }

inline Vector2D uv(double x, double y)
{
    Vector2D v;
    v.x = x;
    v.y = y;
    return v;
}
inline Vector2D uvMittel(const Vector2D &a, const Vector2D &b, double fa = 0.5)
{
    return uv(a.x * fa + b.x * (1 - fa), a.y * fa + b.y * (1 - fa));
}

// neues Dreieck aus drei Ecken, mit Material (und Vertex-Normalen, wenn die Vorlage welche hatte)
inline Dreieck dreieckAus(const Ecke &a, const Ecke &b, const Ecke &c, const Dreieck &vorlage)
{
    Dreieck d = vorlage.hasVertexNormals ? Dreieck(a.p, b.p, c.p, a.n, b.n, c.n, vorlage.mat) : Dreieck(a.p, b.p, c.p, vorlage.mat);
    d.uvA = a.uv;
    d.uvB = b.uv;
    d.uvC = c.uv;
    return d;
}

inline Vector3D flaechenNormale(const Dreieck &t)
{
    return (eckePos(t, 1) - eckePos(t, 0)).cross(eckePos(t, 2) - eckePos(t, 0));
}

// Ecken nach Position zusammenfassen (gleiche Stelle = gleicher Index)
struct Verschweissen
{
    double raster;
    std::map<std::tuple<long long, long long, long long>, int> index;
    std::vector<Vector3D> punkte;

    explicit Verschweissen(double r) : raster(r > 0 ? r : 1e-9) {}
    int operator()(const Vector3D &p)
    {
        auto k = std::make_tuple((long long)std::llround(p.x / raster), (long long)std::llround(p.y / raster),
                                 (long long)std::llround(p.z / raster));
        auto it = index.find(k);
        if (it != index.end())
            return it->second;
        int i = int(punkte.size());
        index[k] = i;
        punkte.push_back(p);
        return i;
    }
};

// Rastergroesse aus der Netzgroesse (Rundungsfehler der Eckpunkte gelten als gleich)
inline double rasterFuer(const std::vector<Dreieck> &d)
{
    if (d.empty())
        return 1e-9;
    Vector3D mn = eckePos(d[0], 0), mx = mn;
    for (auto &t : d)
        for (int k = 0; k < 3; ++k)
        {
            Vector3D p = eckePos(t, k);
            mn = Vector3D(std::min(mn.x, p.x), std::min(mn.y, p.y), std::min(mn.z, p.z));
            mx = Vector3D(std::max(mx.x, p.x), std::max(mx.y, p.y), std::max(mx.z, p.z));
        }
    return std::max((mx - mn).length() * 1e-7, 1e-12);
}

using Kante = std::pair<int, int>;
inline Kante kante(int a, int b) { return a < b ? Kante(a, b) : Kante(b, a); }

// ---------- Flaechen finden ----------

// Dreiecke (Indizes) je Flaeche: verbunden ueber gemeinsame Kanten, gleiche Normale
inline std::vector<std::vector<int>> flaechenFinden(const std::vector<Dreieck> &d)
{
    Verschweissen w(rasterFuer(d));
    std::vector<std::array<int, 3>> idx(d.size());
    std::vector<Vector3D> n(d.size());
    std::map<Kante, std::vector<int>> anKante;
    for (size_t i = 0; i < d.size(); ++i)
    {
        for (int k = 0; k < 3; ++k)
            idx[i][k] = w(eckePos(d[i], k));
        n[i] = flaechenNormale(d[i]).normalized();
        for (int k = 0; k < 3; ++k)
            anKante[kante(idx[i][k], idx[i][(k + 1) % 3])].push_back(int(i));
    }
    std::vector<int> flaecheVon(d.size(), -1);
    std::vector<std::vector<int>> flaechen;
    for (size_t s = 0; s < d.size(); ++s)
    {
        if (flaecheVon[s] >= 0)
            continue;
        const int f = int(flaechen.size());
        flaechen.push_back({int(s)});
        flaecheVon[s] = f;
        for (size_t q = 0; q < flaechen[f].size(); ++q)
        {
            int t = flaechen[f][q];
            for (int k = 0; k < 3; ++k)
                for (int u : anKante[kante(idx[t][k], idx[t][(k + 1) % 3])])
                    if (flaecheVon[u] < 0 && n[u] * n[s] > 0.99999) // gleiche Ebene (unter 0,26 Grad)
                    {
                        flaecheVon[u] = f;
                        flaechen[f].push_back(u);
                    }
        }
    }
    return flaechen;
}

// Dreiecke je Flaeche (Kopien)
inline std::vector<std::vector<Dreieck>> flaechenZerlegen(const std::vector<Dreieck> &d)
{
    std::vector<std::vector<Dreieck>> ergebnis;
    for (auto &f : flaechenFinden(d))
    {
        ergebnis.emplace_back();
        for (int i : f)
            ergebnis.back().push_back(d[size_t(i)]);
    }
    return ergebnis;
}

// ---------- ebene Flaeche in 2D ----------

struct Flaeche2D
{
    Vector3D normale, u, v, ursprung;
    std::vector<Ecke> ecken;               // Ecken der Flaeche (verschweisst)
    std::vector<std::array<int, 3>> dreiecke;
    std::vector<int> vorlage;              // Index des Original-Dreiecks (Material, Normalen)

    double x(int i) const { return (ecken[size_t(i)].p - ursprung) * u; }
    double y(int i) const { return (ecken[size_t(i)].p - ursprung) * v; }
    // > 0: gegen den Uhrzeigersinn, von der Vorderseite (Normale) gesehen
    double orient(int a, int b, int c) const
    {
        return (x(b) - x(a)) * (y(c) - y(a)) - (y(b) - y(a)) * (x(c) - x(a));
    }
};

inline Flaeche2D flaeche2D(const std::vector<Dreieck> &d, const std::vector<int> &dreiecke, double raster)
{
    Flaeche2D f;
    Vector3D summe(0, 0, 0);
    for (int i : dreiecke)
        summe += flaechenNormale(d[size_t(i)]);
    f.normale = summe.normalized();
    Vector3D hilf = std::fabs(f.normale.y) < 0.9 ? Vector3D(0, 1, 0) : Vector3D(1, 0, 0);
    f.u = hilf.cross(f.normale).normalized();
    f.v = f.normale.cross(f.u);
    f.ursprung = eckePos(d[size_t(dreiecke[0])], 0);
    Verschweissen w(raster);
    for (int i : dreiecke)
    {
        std::array<int, 3> e;
        for (int k = 0; k < 3; ++k)
        {
            Vector3D p = eckePos(d[size_t(i)], k);
            size_t vorher = w.punkte.size();
            e[k] = w(p);
            if (w.punkte.size() > vorher)
                f.ecken.push_back(ecke(d[size_t(i)], k));
        }
        f.dreiecke.push_back(e);
        f.vorlage.push_back(i);
    }
    return f;
}

// Dreiecke der Flaeche wieder als Dreieck-Objekte; Umlaufsinn wie die Flaeche (Normale nach aussen)
inline void flaecheAusgeben(const Flaeche2D &f, const std::vector<Dreieck> &d, std::vector<Dreieck> &aus)
{
    for (size_t i = 0; i < f.dreiecke.size(); ++i)
    {
        auto e = f.dreiecke[i];
        if (f.orient(e[0], e[1], e[2]) < 0)
            std::swap(e[1], e[2]);
        if (std::fabs(f.orient(e[0], e[1], e[2])) < 1e-300)
            continue; // entartet
        const Dreieck &v = d[size_t(f.vorlage[i])];
        aus.push_back(dreieckAus(f.ecken[size_t(e[0])], f.ecken[size_t(e[1])], f.ecken[size_t(e[2])], v));
    }
}

// ---------- Delaunay-Flipping ----------
// Eine gemeinsame Kante zweier Dreiecke wird umgeklappt, wenn die gegenueberliegende Ecke des
// Nachbarn im Umkreis des Dreiecks liegt (wie LegalizeEdge). Zusaetzlich nur bei konvexem Viereck.
// Bis keine Kante mehr umklappt oder hoechstens grenze Flips.

inline bool imUmkreis(const Flaeche2D &f, int a, int b, int c, int p)
{
    // Umkreismittelpunkt (wie MeanPoint mit der Cramerschen Regel, hier direkt)
    const double ax = f.x(a), ay = f.y(a), bx = f.x(b), by = f.y(b), cx = f.x(c), cy = f.y(c);
    const double dd = 2.0 * (ax * (by - cy) + bx * (cy - ay) + cx * (ay - by));
    if (std::fabs(dd) < 1e-300)
        return false;
    const double a2 = ax * ax + ay * ay, b2 = bx * bx + by * by, c2 = cx * cx + cy * cy;
    const double mx = (a2 * (by - cy) + b2 * (cy - ay) + c2 * (ay - by)) / dd;
    const double my = (a2 * (cx - bx) + b2 * (ax - cx) + c2 * (bx - ax)) / dd;
    const double r2 = (ax - mx) * (ax - mx) + (ay - my) * (ay - my);
    const double px = f.x(p), py = f.y(p);
    const double d2 = (px - mx) * (px - mx) + (py - my) * (py - my);
    return d2 < r2 * (1.0 - 1e-9); // knapp auf dem Kreis: nicht klappen (sonst endloses Hin und Her)
}

inline int flaecheFlippen(Flaeche2D &f, int grenze)
{
    int flips = 0;
    bool geaendert = true;
    while (geaendert && flips < grenze)
    {
        geaendert = false;
        std::map<Kante, std::vector<int>> anKante;
        for (size_t i = 0; i < f.dreiecke.size(); ++i)
            for (int k = 0; k < 3; ++k)
                anKante[kante(f.dreiecke[i][k], f.dreiecke[i][(k + 1) % 3])].push_back(int(i));
        for (auto &[k, ts] : anKante)
        {
            if (ts.size() != 2)
                continue; // Rand oder mehrfach benutzte Kante
            const int t1 = ts[0], t2 = ts[1];
            const int i = k.first, j = k.second;
            auto dritte = [&](int t)
            {
                for (int e : f.dreiecke[size_t(t)])
                    if (e != i && e != j)
                        return e;
                return -1;
            };
            const int pk = dritte(t1), ph = dritte(t2);
            if (pk < 0 || ph < 0 || pk == ph)
                continue;
            if (!imUmkreis(f, pk, i, j, ph))
                continue;
            // konvexes Viereck: die neue Diagonale pk-ph kreuzt die alte i-j
            const double o1 = f.orient(pk, ph, i), o2 = f.orient(pk, ph, j);
            const double o3 = f.orient(i, j, pk), o4 = f.orient(i, j, ph);
            if (!(o1 * o2 < 0 && o3 * o4 < 0))
                continue;
            f.dreiecke[size_t(t1)] = {pk, i, ph};
            f.dreiecke[size_t(t2)] = {pk, ph, j};
            ++flips;
            geaendert = true;
            break; // Nachbarschaft hat sich geaendert: neu aufbauen
        }
    }
    return flips;
}

// alle Flaechen eines Netzes; liefert die Zahl der Flips
inline int delaunayFlippen(std::vector<Dreieck> &d)
{
    const double raster = rasterFuer(d);
    std::vector<Dreieck> neu;
    int flips = 0;
    for (auto &fl : flaechenFinden(d))
    {
        if (fl.size() < 2)
        {
            neu.push_back(d[size_t(fl[0])]);
            continue;
        }
        Flaeche2D f = flaeche2D(d, fl, raster);
        flips += flaecheFlippen(f, 100 * int(fl.size()) + 100);
        flaecheAusgeben(f, d, neu);
    }
    d.swap(neu);
    return flips;
}

// ---------- Porath-Triangulation ----------
// Je Flaeche: Mittelpunkt der Randecken, Ecken nach Winkel um den Mittelpunkt sortiert, Faecher vom
// Mittelpunkt zu je zwei benachbarten Ecken (createTriangles). Dann tiefe-mal jedes Dreieck vom eigenen
// Mittelpunkt aus in drei geteilt (PorathTriangulation). Nicht konvexe Flaechen oder Flaechen mit
// Loechern: nur die rekursive Teilung der vorhandenen Dreiecke (ein Faecher waere dort falsch).

inline bool randFinden(const Flaeche2D &f, std::vector<int> &rand)
{
    std::map<Kante, int> zaehler;
    std::map<Kante, std::pair<int, int>> richtung;
    for (auto e : f.dreiecke)
    {
        if (f.orient(e[0], e[1], e[2]) < 0)
            std::swap(e[1], e[2]);
        for (int k = 0; k < 3; ++k)
        {
            Kante kk = kante(e[k], e[(k + 1) % 3]);
            ++zaehler[kk];
            richtung[kk] = {e[k], e[(k + 1) % 3]};
        }
    }
    std::map<int, int> weiter;
    int anzahl = 0;
    for (auto &[kk, z] : zaehler)
        if (z == 1)
        {
            weiter[richtung[kk].first] = richtung[kk].second;
            ++anzahl;
        }
    if (anzahl < 3)
        return false;
    int start = weiter.begin()->first, p = start;
    rand.clear();
    do
    {
        rand.push_back(p);
        auto it = weiter.find(p);
        if (it == weiter.end())
            return false;
        p = it->second;
    } while (p != start && int(rand.size()) <= anzahl);
    return int(rand.size()) == anzahl; // genau ein geschlossener Rand (keine Loecher)
}

inline bool konvex(const Flaeche2D &f, const std::vector<int> &rand)
{
    const size_t n = rand.size();
    for (size_t i = 0; i < n; ++i)
        if (f.orient(rand[i], rand[(i + 1) % n], rand[(i + 2) % n]) < -1e-12)
            return false;
    return true;
}

inline void dreiTeilen(Flaeche2D &f, std::vector<int> &vorlage)
{
    std::vector<std::array<int, 3>> neu;
    std::vector<int> neuVorlage;
    for (size_t i = 0; i < f.dreiecke.size(); ++i)
    {
        const auto e = f.dreiecke[i];
        const Ecke &a = f.ecken[size_t(e[0])], &b = f.ecken[size_t(e[1])], &c = f.ecken[size_t(e[2])];
        Ecke m;
        m.p = (a.p + b.p + c.p) * (1.0 / 3.0);
        m.n = (a.n + b.n + c.n) * (1.0 / 3.0);
        m.uv = uv((a.uv.x + b.uv.x + c.uv.x) / 3.0, (a.uv.y + b.uv.y + c.uv.y) / 3.0);
        const int mi = int(f.ecken.size());
        f.ecken.push_back(m);
        neu.push_back({mi, e[0], e[1]});
        neu.push_back({mi, e[1], e[2]});
        neu.push_back({mi, e[2], e[0]});
        for (int k = 0; k < 3; ++k)
            neuVorlage.push_back(vorlage[i]);
    }
    f.dreiecke.swap(neu);
    vorlage.swap(neuVorlage);
}

inline void porathTriangulieren(std::vector<Dreieck> &d, int tiefe)
{
    const double raster = rasterFuer(d);
    std::vector<Dreieck> neu;
    for (auto &fl : flaechenFinden(d))
    {
        Flaeche2D f = flaeche2D(d, fl, raster);
        std::vector<int> rand;
        if (randFinden(f, rand) && konvex(f, rand))
        {
            // Mittelpunkt der Randecken, Ecken nach Winkel sortiert, Faecher
            Ecke m;
            m.p = Vector3D(0, 0, 0);
            m.n = Vector3D(0, 0, 0);
            double su = 0, sv = 0;
            for (int i : rand)
            {
                m.p += f.ecken[size_t(i)].p;
                m.n += f.ecken[size_t(i)].n;
                su += f.ecken[size_t(i)].uv.x;
                sv += f.ecken[size_t(i)].uv.y;
            }
            const double n = double(rand.size());
            m.p = m.p * (1.0 / n);
            m.n = m.n * (1.0 / n);
            m.uv = uv(su / n, sv / n);
            const int mi = int(f.ecken.size());
            f.ecken.push_back(m);
            std::vector<int> sortiert = rand;
            const double mx = f.x(mi), my = f.y(mi);
            std::sort(sortiert.begin(), sortiert.end(), [&](int a, int b)
                      { return std::atan2(f.y(a) - my, f.x(a) - mx) < std::atan2(f.y(b) - my, f.x(b) - mx); });
            const int vorlageDreieck = f.vorlage.empty() ? fl[0] : f.vorlage[0];
            f.dreiecke.clear();
            f.vorlage.clear();
            for (size_t i = 0; i < sortiert.size(); ++i)
            {
                f.dreiecke.push_back({mi, sortiert[i], sortiert[(i + 1) % sortiert.size()]});
                f.vorlage.push_back(vorlageDreieck);
            }
        }
        for (int s = 0; s < tiefe; ++s)
            dreiTeilen(f, f.vorlage);
        flaecheAusgeben(f, d, neu);
    }
    d.swap(neu);
}

// ---------- Loop-Subdivision (ein Schritt) ----------
// Neue Kantenpunkte: innen 3/8 (a + b) + 1/8 (c + d), am Rand die Mitte.
// Alte Ecken: (1 - n beta) v + beta * Summe der Nachbarn, beta = (5/8 - (3/8 + 1/4 cos(2 pi / n))^2) / n
// (gleich alpha = 3/8 + (3/8 + 1/4 cos(2 pi / n))^2 wie im Original); Randecken 3/4 v + 1/8 (Nachbarn am Rand).
// Jedes Dreieck wird zu vier. Danach glatte Vertex-Normalen.

inline void loopUnterteilen(std::vector<Dreieck> &d)
{
    if (d.empty())
        return;
    Verschweissen w(rasterFuer(d));
    std::vector<std::array<int, 3>> idx(d.size());
    for (size_t i = 0; i < d.size(); ++i)
        for (int k = 0; k < 3; ++k)
            idx[i][k] = w(eckePos(d[i], k));
    const std::vector<Vector3D> &p = w.punkte;
    const size_t nPunkte = p.size();

    // Kanten mit ihren gegenueberliegenden Ecken
    std::map<Kante, std::vector<int>> gegenueber;
    for (auto &e : idx)
        for (int k = 0; k < 3; ++k)
            gegenueber[kante(e[k], e[(k + 1) % 3])].push_back(e[(k + 2) % 3]);

    // neue Kantenpunkte
    std::map<Kante, Vector3D> kantenPunkt;
    std::vector<std::vector<int>> nachbarn(nPunkte), randNachbarn(nPunkte);
    for (auto &[k, opp] : gegenueber)
    {
        const Vector3D &a = p[size_t(k.first)], &b = p[size_t(k.second)];
        if (opp.size() == 2)
            kantenPunkt[k] = (a + b) * (3.0 / 8.0) + (p[size_t(opp[0])] + p[size_t(opp[1])]) * (1.0 / 8.0);
        else
        {
            kantenPunkt[k] = (a + b) * 0.5; // Rand (oder mehrfach benutzte Kante)
            randNachbarn[size_t(k.first)].push_back(k.second);
            randNachbarn[size_t(k.second)].push_back(k.first);
        }
        nachbarn[size_t(k.first)].push_back(k.second);
        nachbarn[size_t(k.second)].push_back(k.first);
    }

    // neue Lage der alten Ecken
    std::vector<Vector3D> neuP(nPunkte);
    for (size_t v = 0; v < nPunkte; ++v)
    {
        if (!randNachbarn[v].empty())
        {
            if (randNachbarn[v].size() == 2)
                neuP[v] = p[v] * 0.75 + (p[size_t(randNachbarn[v][0])] + p[size_t(randNachbarn[v][1])]) * 0.125;
            else
                neuP[v] = p[v]; // Ecke an einer Spitze des Rands: bleibt
            continue;
        }
        const double n = double(nachbarn[v].size());
        if (n < 3)
        {
            neuP[v] = p[v];
            continue;
        }
        const double c = 3.0 / 8.0 + 0.25 * std::cos(2.0 * M_PI / n);
        const double beta = (5.0 / 8.0 - c * c) / n;
        Vector3D summe(0, 0, 0);
        for (int q : nachbarn[v])
            summe += p[size_t(q)];
        neuP[v] = p[v] * (1.0 - n * beta) + summe * beta;
    }

    // vier neue Dreiecke je altem; UV je Dreieck (Naehte bleiben erhalten)
    struct Neu
    {
        int punkt[3];      // >= 0: alte Ecke, < 0: Kantenpunkt -(Nummer + 1)
        Vector2D uv[3];
        int vorlage;
    };
    std::vector<Vector3D> kp;
    std::map<Kante, int> kpIndex;
    auto kantenNr = [&](int a, int b)
    {
        Kante k = kante(a, b);
        auto it = kpIndex.find(k);
        if (it != kpIndex.end())
            return it->second;
        int i = int(kp.size());
        kp.push_back(kantenPunkt[k]);
        kpIndex[k] = i;
        return i;
    };
    std::vector<Neu> neu;
    neu.reserve(d.size() * 4);
    for (size_t i = 0; i < d.size(); ++i)
    {
        const auto &e = idx[i];
        const Vector2D u0 = d[i].uvA, u1 = d[i].uvB, u2 = d[i].uvC;
        const int m01 = -(kantenNr(e[0], e[1]) + 1), m12 = -(kantenNr(e[1], e[2]) + 1), m20 = -(kantenNr(e[2], e[0]) + 1);
        const Vector2D w01 = uvMittel(u0, u1), w12 = uvMittel(u1, u2), w20 = uvMittel(u2, u0);
        neu.push_back({{e[0], m01, m20}, {u0, w01, w20}, int(i)});
        neu.push_back({{e[1], m12, m01}, {u1, w12, w01}, int(i)});
        neu.push_back({{e[2], m20, m12}, {u2, w20, w12}, int(i)});
        neu.push_back({{m01, m12, m20}, {w01, w12, w20}, int(i)});
    }
    auto lage = [&](int pkt) { return pkt >= 0 ? neuP[size_t(pkt)] : kp[size_t(-pkt - 1)]; };

    // glatte Normalen: Flaechennormalen je Punkt aufsummieren
    std::vector<Vector3D> nAlt(nPunkte, Vector3D(0, 0, 0)), nKante(kp.size(), Vector3D(0, 0, 0));
    auto normaleDazu = [&](int pkt, const Vector3D &n)
    {
        if (pkt >= 0)
            nAlt[size_t(pkt)] += n;
        else
            nKante[size_t(-pkt - 1)] += n;
    };
    for (auto &t : neu)
    {
        Vector3D n = (lage(t.punkt[1]) - lage(t.punkt[0])).cross(lage(t.punkt[2]) - lage(t.punkt[0]));
        for (int k = 0; k < 3; ++k)
            normaleDazu(t.punkt[k], n);
    }
    auto normale = [&](int pkt)
    {
        Vector3D n = pkt >= 0 ? nAlt[size_t(pkt)] : nKante[size_t(-pkt - 1)];
        return n.length() > 1e-300 ? n.normalized() : Vector3D(0, 1, 0);
    };

    std::vector<Dreieck> ergebnis;
    ergebnis.reserve(neu.size());
    for (auto &t : neu)
    {
        Dreieck dd(lage(t.punkt[0]), lage(t.punkt[1]), lage(t.punkt[2]), normale(t.punkt[0]), normale(t.punkt[1]),
                   normale(t.punkt[2]), d[size_t(t.vorlage)].mat);
        dd.uvA = t.uv[0];
        dd.uvB = t.uv[1];
        dd.uvC = t.uv[2];
        ergebnis.push_back(dd);
    }
    d.swap(ergebnis);
}

// ---------- Dicke fuer Flaechen (Solidify) ----------
// Eine zweite Lage im Abstand 'dicke' in Normalenrichtung (Ecknormale = Mittel der Flaechennormalen,
// so verlaengert, dass die Wand ueberall gleich dick ist), die Rueckseite umgedreht, offene Raender mit
// Seitenwaenden geschlossen. dicke > 0: nach aussen (in Normalenrichtung), < 0: nach innen.
// Geschlossene Koerper (Wuerfel) werden hohl: Aussen- und Innenhaut.
inline void dickeGeben(std::vector<Dreieck> &d, double dicke)
{
    if (d.empty() || std::fabs(dicke) < 1e-300)
        return;
    Verschweissen w(rasterFuer(d));
    std::vector<std::array<int, 3>> idx(d.size());
    std::vector<Vector3D> einheit(d.size());
    for (size_t i = 0; i < d.size(); ++i)
    {
        for (int k = 0; k < 3; ++k)
            idx[i][k] = w(eckePos(d[i], k));
        einheit[i] = flaechenNormale(d[i]).normalized();
    }
    const size_t n = w.punkte.size();
    std::vector<Vector3D> summe(n, Vector3D(0, 0, 0));
    std::vector<std::vector<int>> anPunkt(n);
    for (size_t i = 0; i < d.size(); ++i)
        for (int k = 0; k < 3; ++k)
        {
            Vector3D a = eckePos(d[i], (k + 1) % 3) - eckePos(d[i], k), b = eckePos(d[i], (k + 2) % 3) - eckePos(d[i], k);
            double la = a.length(), lb = b.length();
            double winkel = (la > 0 && lb > 0) ? std::acos(std::clamp((a * b) / (la * lb), -1.0, 1.0)) : 0.0;
            summe[size_t(idx[i][k])] += einheit[i] * winkel;
            anPunkt[size_t(idx[i][k])].push_back(int(i));
        }
    std::vector<Vector3D> versatz(n);
    for (size_t v = 0; v < n; ++v)
    {
        Vector3D nv = summe[v].length() > 1e-300 ? summe[v].normalized() : Vector3D(0, 1, 0);
        double kleinster = 1.0; // gleich dicke Wand: an Kanten/Ecken entsprechend weiter hinaus
        for (int t : anPunkt[v])
            kleinster = std::min(kleinster, nv * einheit[size_t(t)]);
        versatz[v] = nv * (dicke / std::max(kleinster, 0.33));
    }

    // Kanten zaehlen: einmal benutzt = offener Rand
    std::map<Kante, int> zaehler;
    for (auto &e : idx)
        for (int k = 0; k < 3; ++k)
            ++zaehler[kante(e[k], e[(k + 1) % 3])];

    std::vector<Dreieck> neu;
    neu.reserve(d.size() * 2 + 16);
    auto mitNormalen = [&](Dreieck t, const Vector3D &na, const Vector3D &nb, const Vector3D &nc, bool hat)
    {
        t.nA = na;
        t.nB = nb;
        t.nC = nc;
        t.hasVertexNormals = hat;
        return t;
    };
    for (size_t i = 0; i < d.size(); ++i)
    {
        const Dreieck &q = d[i];
        Ecke e[3], o[3];
        for (int k = 0; k < 3; ++k)
        {
            e[k] = ecke(q, k);
            o[k] = e[k];
            o[k].p = e[k].p + versatz[size_t(idx[i][k])];
        }
        const bool hat = q.hasVertexNormals;
        // die Lage auf der Seite der Dicke zeigt nach aussen, die andere wird umgedreht
        Dreieck aussen = dreieckAus(dicke > 0 ? o[0] : e[0], dicke > 0 ? o[1] : e[1], dicke > 0 ? o[2] : e[2], q);
        Dreieck innen = dreieckAus(dicke > 0 ? e[0] : o[0], dicke > 0 ? e[2] : o[2], dicke > 0 ? e[1] : o[1], q);
        neu.push_back(mitNormalen(aussen, e[0].n, e[1].n, e[2].n, hat));
        neu.push_back(mitNormalen(innen, -e[0].n, -e[2].n, -e[1].n, hat));
        // Seitenwaende an offenen Raendern
        for (int k = 0; k < 3; ++k)
        {
            const int a = idx[i][k], b = idx[i][(k + 1) % 3];
            if (zaehler[kante(a, b)] != 1)
                continue;
            const Vector3D pa = e[k].p, pb = e[(k + 1) % 3].p;
            const Vector3D qa = pa + versatz[size_t(a)], qb = pb + versatz[size_t(b)];
            const Vector3D aussenRichtung = (pb - pa).cross(einheit[i]); // vom Dreieck weg
            auto wand = [&](Vector3D x, Vector3D y, Vector3D z)
            {
                if (((y - x).cross(z - x)) * aussenRichtung < 0)
                    std::swap(y, z);
                Dreieck t(x, y, z, q.mat);
                t.hasVertexNormals = false;
                neu.push_back(t);
            };
            wand(pa, pb, qb);
            wand(pa, qb, qa);
        }
    }
    d.swap(neu);
}

// ---------- Faechern fuer Flaechen ----------
// Je ebene Flaeche ein neuer Punkt in der Mitte (flaechengewichteter Schwerpunkt), um 'hoehe' in
// Normalenrichtung versetzt; die Flaeche wird von dort aus zu jeder Randkante in Dreiecke aufgefaechert.
// hoehe > 0: Spitze nach aussen (Pyramide), < 0: nach innen (Mulde), 0: flacher Faecher.
inline void faechern(std::vector<Dreieck> &d, double hoehe)
{
    const double raster = rasterFuer(d);
    std::vector<Dreieck> neu;
    for (auto &fl : flaechenFinden(d))
    {
        Flaeche2D f = flaeche2D(d, fl, raster);
        // Schwerpunkt der Flaeche
        Vector3D schwer(0, 0, 0);
        double flaeche = 0, su = 0, sv = 0;
        for (auto &e : f.dreiecke)
        {
            const Vector3D a = f.ecken[size_t(e[0])].p, b = f.ecken[size_t(e[1])].p, c = f.ecken[size_t(e[2])].p;
            const double A = 0.5 * (b - a).cross(c - a).length();
            schwer += (a + b + c) * (A / 3.0);
            su += (f.ecken[size_t(e[0])].uv.x + f.ecken[size_t(e[1])].uv.x + f.ecken[size_t(e[2])].uv.x) * (A / 3.0);
            sv += (f.ecken[size_t(e[0])].uv.y + f.ecken[size_t(e[1])].uv.y + f.ecken[size_t(e[2])].uv.y) * (A / 3.0);
            flaeche += A;
        }
        if (flaeche <= 1e-300)
            continue;
        Ecke spitze;
        spitze.p = schwer * (1.0 / flaeche) + f.normale * hoehe;
        spitze.n = f.normale;
        spitze.uv = uv(su / flaeche, sv / flaeche);
        // Randkanten in Umlaufrichtung der Flaeche (gegen den Uhrzeigersinn um die Normale)
        std::map<Kante, int> zaehler;
        std::vector<std::pair<int, int>> gerichtet;
        for (auto e : f.dreiecke)
        {
            if (f.orient(e[0], e[1], e[2]) < 0)
                std::swap(e[1], e[2]);
            for (int k = 0; k < 3; ++k)
            {
                ++zaehler[kante(e[k], e[(k + 1) % 3])];
                gerichtet.push_back({e[k], e[(k + 1) % 3]});
            }
        }
        const Dreieck &vorlage = d[size_t(fl[0])];
        for (auto &[a, b] : gerichtet)
            if (zaehler[kante(a, b)] == 1)
            {
                Dreieck t = dreieckAus(f.ecken[size_t(a)], f.ecken[size_t(b)], spitze, vorlage);
                t.hasVertexNormals = false; // flach: die Seiten der Spitze sollen kantig bleiben
                neu.push_back(t);
            }
    }
    d.swap(neu);
}

// ---------- Glatt schattieren (Normalen interpolieren) ----------
// Je Ecke eines Dreiecks: Mittel der Flaechennormalen aller Dreiecke an dieser Stelle (verschweisst),
// gewichtet mit dem Eckwinkel; Dreiecke, deren Normale um mehr als knickGrad abweicht, zaehlen nicht
// (scharfe Kante bleibt). Der Renderer interpoliert diese Ecknormalen baryzentrisch (Phong-Schattierung).
// knickGrad <= 0: Vertex-Normalen entfernen (flach schattiert).
// Rueckgabe: Zahl der Dreiecksecken, deren Normale jetzt von der Flaechennormale abweicht (= geglaettet)
inline size_t normalenGlaetten(std::vector<Dreieck> &d, double knickGrad)
{
    if (knickGrad <= 0)
    {
        for (auto &t : d)
            t.hasVertexNormals = false;
        return 0;
    }
    Verschweissen w(rasterFuer(d));
    std::vector<std::array<int, 3>> idx(d.size());
    std::vector<Vector3D> einheit(d.size());
    std::vector<std::array<double, 3>> winkel(d.size());
    for (size_t i = 0; i < d.size(); ++i)
    {
        for (int k = 0; k < 3; ++k)
            idx[i][k] = w(eckePos(d[i], k));
        einheit[i] = flaechenNormale(d[i]).normalized();
        for (int k = 0; k < 3; ++k)
        {
            Vector3D a = eckePos(d[i], (k + 1) % 3) - eckePos(d[i], k), b = eckePos(d[i], (k + 2) % 3) - eckePos(d[i], k);
            double la = a.length(), lb = b.length();
            winkel[i][k] = (la > 0 && lb > 0) ? std::acos(std::clamp((a * b) / (la * lb), -1.0, 1.0)) : 0.0;
        }
    }
    std::vector<std::vector<std::pair<int, int>>> anEcke(w.punkte.size()); // (Dreieck, Ecke)
    for (size_t i = 0; i < d.size(); ++i)
        for (int k = 0; k < 3; ++k)
            anEcke[size_t(idx[i][k])].push_back({int(i), k});
    const double grenze = std::cos(std::min(knickGrad, 180.0) * M_PI / 180.0) - 1e-9;
    size_t geglaettet = 0;
    for (size_t i = 0; i < d.size(); ++i)
    {
        Vector3D n[3];
        for (int k = 0; k < 3; ++k)
        {
            Vector3D summe(0, 0, 0);
            for (auto [t, e] : anEcke[size_t(idx[i][k])])
                if (einheit[size_t(t)] * einheit[i] >= grenze)
                    summe += einheit[size_t(t)] * winkel[size_t(t)][e];
            n[k] = summe.length() > 1e-300 ? summe.normalized() : einheit[i];
            if (n[k] * einheit[i] < 1.0 - 1e-9)
                ++geglaettet;
        }
        d[i].nA = n[0];
        d[i].nB = n[1];
        d[i].nC = n[2];
        d[i].hasVertexNormals = true;
    }
    return geglaettet;
}
} // namespace netz
