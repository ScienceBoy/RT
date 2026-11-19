#pragma once
// Kanten-Modus: einzelne Kanten eines Netzes waehlen (in einer Ansicht anklicken, Strg = mehrere) und
//   - loeschen: die Kante mit den angrenzenden Dreiecken entfernen (Loch)
//   - zusammenziehen: beide Endpunkte auf die Kantenmitte legen (Netz vereinfachen, Spitze bilden)
//   - umklappen: die beiden Dreiecke an der Kante ueber die andere Diagonale ihres Vierecks teilen
//     (in einem reinen Dreiecksnetz kann eine Kante zwischen zwei Dreiecken nicht einfach verschwinden)
// Zustand (gewaehlte Kanten) fuer Wireframe (Anklicken), GpuAnsichten (Markierung) und toolwindow (Bedienung).

#include "dreieck.h"
#include "vector3d.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <map>
#include <mutex>
#include <tuple>
#include <vector>

class object;

namespace kanten
{
struct Kante
{
    Vector3D a, b; // Endpunkte in Weltkoordinaten
};

inline bool gModus = false;           // Kanten-Modus an (Knopf "Kanten")
inline object *gObjekt = nullptr;     // Netz, dessen Kanten gewaehlt werden
inline std::vector<Kante> gGewaehlt;  // gewaehlte Kanten (unter gMutex)
inline std::mutex gMutex;
inline std::atomic<unsigned> gStand{0}; // aendert sich bei jeder Aenderung der Auswahl oder des Netzes

inline Vector3D ecke(const Dreieck &d, int i) { return (i == 0 ? d.a : i == 1 ? d.b : d.c) + d.position; }

// Toleranz fuer "gleiche Ecke" aus der Groesse des Netzes
inline double toleranz(const std::vector<std::vector<Dreieck> *> &listen)
{
    double g = 0;
    for (auto *l : listen)
        for (const Dreieck &d : *l)
            for (int i = 0; i < 3; ++i)
            {
                const Vector3D p = ecke(d, i);
                g = std::max({g, std::fabs(p.x), std::fabs(p.y), std::fabs(p.z)});
            }
    return std::max(1e-12, g * 1e-7);
}

inline bool gleich(const Vector3D &p, const Vector3D &q, double tol) { return (p - q).length() <= tol; }

// Index k (0..2), wenn das Dreieck die Kante zwischen Ecke k und Ecke k+1 hat (egal in welcher Richtung); sonst -1
inline int kanteIn(const Dreieck &d, const Kante &k, double tol)
{
    for (int i = 0; i < 3; ++i)
    {
        const Vector3D p = ecke(d, i), q = ecke(d, (i + 1) % 3);
        if ((gleich(p, k.a, tol) && gleich(q, k.b, tol)) || (gleich(p, k.b, tol) && gleich(q, k.a, tol)))
            return i;
    }
    return -1;
}

// alle Kanten eines Netzes, jede nur einmal (zum Anzeigen und Anklicken)
inline std::vector<Kante> alleKanten(const std::vector<std::vector<Dreieck> *> &listen, size_t hoechstens = 200000)
{
    const double tol = toleranz(listen);
    const double q = tol * 10.0;
    auto schluessel = [&](const Vector3D &p)
    { return std::make_tuple((long long)std::llround(p.x / q), (long long)std::llround(p.y / q), (long long)std::llround(p.z / q)); };
    std::map<std::pair<std::tuple<long long, long long, long long>, std::tuple<long long, long long, long long>>, bool> gesehen;
    std::vector<Kante> r;
    for (auto *l : listen)
        for (const Dreieck &d : *l)
            for (int i = 0; i < 3 && r.size() < hoechstens; ++i)
            {
                const Vector3D a = ecke(d, i), b = ecke(d, (i + 1) % 3);
                auto ka = schluessel(a), kb = schluessel(b);
                if (kb < ka)
                    std::swap(ka, kb);
                if (ka == kb || !gesehen.emplace(std::make_pair(ka, kb), true).second)
                    continue;
                r.push_back({a, b});
            }
    return r;
}

// Lage in die Ecken uebernehmen (position = 0), damit Ecken direkt verglichen und versetzt werden koennen
inline void lageEinrechnen(std::vector<Dreieck> &d)
{
    for (Dreieck &t : d)
        if (t.position.x != 0 || t.position.y != 0 || t.position.z != 0)
        {
            t.a += t.position;
            t.b += t.position;
            t.c += t.position;
            t.position = Vector3D(0, 0, 0);
        }
}

// Loch: alle Dreiecke an den Kanten entfernen; liefert die Anzahl entfernter Dreiecke
inline size_t loeschen(std::vector<Dreieck> &d, const std::vector<Kante> &k, double tol)
{
    const size_t vorher = d.size();
    d.erase(std::remove_if(d.begin(), d.end(),
                           [&](const Dreieck &t)
                           {
                               for (const Kante &e : k)
                                   if (kanteIn(t, e, tol) >= 0)
                                       return true;
                               return false;
                           }),
            d.end());
    return vorher - d.size();
}

// Zusammenziehen: beide Endpunkte jeder Kante auf ihre Mitte; entartete Dreiecke fallen weg.
// Liefert die Anzahl entfernter Dreiecke.
inline size_t zusammenziehen(std::vector<Dreieck> &d, std::vector<Kante> k, double tol)
{
    lageEinrechnen(d);
    const size_t vorher = d.size();
    for (size_t n = 0; n < k.size(); ++n)
    {
        const Vector3D a = k[n].a, b = k[n].b, m = (a + b) * 0.5;
        auto setzen = [&](Vector3D &p)
        {
            if (gleich(p, a, tol) || gleich(p, b, tol))
                p = m;
        };
        for (Dreieck &t : d)
        {
            const Vector3D alt[3] = {t.a, t.b, t.c};
            setzen(t.a);
            setzen(t.b);
            setzen(t.c);
            if (!gleich(alt[0], t.a, 0) || !gleich(alt[1], t.b, 0) || !gleich(alt[2], t.c, 0))
                t.rebuild();
        }
        // spaetere Kanten mit denselben Endpunkten wandern mit
        for (size_t j = n + 1; j < k.size(); ++j)
        {
            setzen(k[j].a);
            setzen(k[j].b);
        }
    }
    d.erase(std::remove_if(d.begin(), d.end(),
                           [&](const Dreieck &t)
                           { return gleich(t.a, t.b, tol) || gleich(t.b, t.c, tol) || gleich(t.c, t.a, tol) ||
                                    (t.b - t.a).cross(t.c - t.a).length() < tol * tol; }),
            d.end());
    return vorher - d.size();
}

// Umklappen: (u,v,c) und (v,u,d) -> (c,u,d) und (d,v,c); Umlauf, Material, Texturkoordinaten und
// Ecknormalen bleiben. Kanten, an denen nicht genau zwei Dreiecke liegen, werden uebersprungen.
// Liefert die Anzahl umgeklappter Kanten.
inline size_t umklappen(std::vector<Dreieck> &d, const std::vector<Kante> &k, double tol)
{
    lageEinrechnen(d);
    size_t anzahl = 0;
    for (const Kante &e : k)
    {
        int t1 = -1, t2 = -1, k1 = -1, k2 = -1, zahl = 0;
        for (size_t i = 0; i < d.size(); ++i)
        {
            const int ki = kanteIn(d[i], e, tol);
            if (ki < 0)
                continue;
            ++zahl;
            if (t1 < 0)
                t1 = int(i), k1 = ki;
            else
                t2 = int(i), k2 = ki;
        }
        if (zahl != 2)
            continue;
        // Ecken so drehen, dass die Kante vorn liegt: T1 = (u, v, c), T2 = (v, u, dd)
        struct E
        {
            Vector3D p, n;
            Vector2D uv;
        };
        auto ecken = [&](const Dreieck &t, int start)
        {
            const Vector3D p[3] = {t.a, t.b, t.c}, n[3] = {t.nA, t.nB, t.nC};
            const Vector2D uv[3] = {t.uvA, t.uvB, t.uvC};
            std::vector<E> r;
            for (int i = 0; i < 3; ++i)
                r.push_back({p[(start + i) % 3], n[(start + i) % 3], uv[(start + i) % 3]});
            return r;
        };
        const Dreieck A = d[size_t(t1)], B = d[size_t(t2)];
        std::vector<E> ea = ecken(A, k1), eb = ecken(B, k2);
        if (!gleich(ea[0].p, eb[1].p, tol)) // T2 muss die Kante gegenlaeufig haben (sonst uneinheitlicher Umlauf)
            continue;
        const E &u = ea[0], &v = ea[1], &c = ea[2], &dd = eb[2];
        auto bauen = [&](const Dreieck &vorlage, const E &x, const E &y, const E &z)
        {
            Dreieck t = vorlage;
            t.a = x.p, t.b = y.p, t.c = z.p;
            t.nA = x.n, t.nB = y.n, t.nC = z.n;
            t.uvA = x.uv, t.uvB = y.uv, t.uvC = z.uv;
            t.position = Vector3D(0, 0, 0);
            t.rebuild();
            return t;
        };
        d[size_t(t1)] = bauen(A, c, u, dd);
        d[size_t(t2)] = bauen(B, dd, v, c);
        ++anzahl;
    }
    return anzahl;
}

// Abstand eines Punktes (Pixel) von einer Strecke (Pixel)
inline double abstandZurStrecke(double px, double py, double ax, double ay, double bx, double by)
{
    const double dx = bx - ax, dy = by - ay, l2 = dx * dx + dy * dy;
    const double t = l2 > 0 ? std::clamp(((px - ax) * dx + (py - ay) * dy) / l2, 0.0, 1.0) : 0.0;
    return std::hypot(ax + t * dx - px, ay + t * dy - py);
}
} // namespace kanten
