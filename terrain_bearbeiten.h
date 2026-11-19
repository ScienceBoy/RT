#pragma once
// Terrain bearbeiten (Reiter "Manipulation"):
//  - unterObjektAbsenken: das Terrain unter einem oder mehreren Objekten (Wegbett, Teich, Fundament, Platz) bis knapp
//    unter deren Unterseite absenken - auf Wunsch auch anheben ("angleichen"), mit weichem Rand. Die Unterseite
//    liefert ein Strahltest von unten (also die echte Form, auch gedreht), nicht nur der Huellquader.
//  - pinsel: an einer Stelle anheben, absenken, glaetten oder ebnen (mit der Maus ueber das Terrain ziehen).
// Gearbeitet wird auf den Ecken der Dreiecke: gleiche Ecken (Raster) werden gemeinsam verschoben, das Netz bleibt
// geschlossen. Nur die Hoehe (y) aendert sich. Danach: Normalen/BVH neu (Aufrufer).
// Nur als Header (testbar ohne Oberflaeche).

#include "dreieck.h"
#include "ray.h"
#include "vector3d.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace terrainBearbeiten
{
// gemeinsame Ecken eines Netzes: Punkte und fuer jede Dreiecksecke der Index ihres Punkts
struct Ecken
{
    std::vector<Vector3D> punkt;
    std::vector<int> index; // 3 je Dreieck
};

inline Ecken eckenSammeln(const std::vector<Dreieck> &d)
{
    Ecken e;
    double g = 0;
    for (const Dreieck &t : d)
        for (const Vector3D &p : {t.a, t.b, t.c})
        {
            const Vector3D q = p + t.position;
            g = std::max({g, std::fabs(q.x), std::fabs(q.y), std::fabs(q.z)});
        }
    const double raster = std::max(1e-12, g * 1e-9);
    std::map<std::tuple<long long, long long, long long>, int> nummer;
    e.index.reserve(d.size() * 3);
    for (const Dreieck &t : d)
        for (const Vector3D &p : {t.a, t.b, t.c})
        {
            const Vector3D q = p + t.position;
            auto k = std::make_tuple((long long)std::llround(q.x / raster), (long long)std::llround(q.y / raster),
                                     (long long)std::llround(q.z / raster));
            auto it = nummer.find(k);
            if (it == nummer.end())
            {
                it = nummer.emplace(k, int(e.punkt.size())).first;
                e.punkt.push_back(q);
            }
            e.index.push_back(it->second);
        }
    return e;
}

// neue Hoehen zurueck in die Dreiecke (Lage eingerechnet, position = 0); nur geaenderte Dreiecke werden neu gebaut
inline void eckenZurueck(std::vector<Dreieck> &d, const Ecken &e, const std::vector<double> &hoehe)
{
    for (size_t i = 0; i < d.size(); ++i)
    {
        Dreieck &t = d[i];
        bool anders = false;
        Vector3D *p[3] = {&t.a, &t.b, &t.c};
        for (int k = 0; k < 3; ++k)
        {
            const int n = e.index[i * 3 + size_t(k)];
            Vector3D q = e.punkt[size_t(n)];
            q.y = hoehe[size_t(n)];
            const Vector3D alt = *p[k] + t.position;
            if (std::fabs(alt.y - q.y) > 0 || t.position.x != 0 || t.position.y != 0 || t.position.z != 0)
                anders = true;
            *p[k] = q;
        }
        t.position = Vector3D(0, 0, 0);
        if (anders)
            t.rebuild();
    }
}

inline double weich(double x) // 0..1 -> 0..1, sanft an beiden Enden (smoothstep)
{
    x = std::clamp(x, 0.0, 1.0);
    return x * x * (3.0 - 2.0 * x);
}

// Terrain unter Objekten absenken bzw. angleichen.
//  unterkante(x, z, y): Unterseite der Objekte an dieser Stelle (false: dort ist kein Objekt)
//  abstand: so weit unter der Unterseite liegt das Terrain danach
//  rand:    Breite des weichen Uebergangs um die Grundflaeche (0 = harte Kante)
//  angleichen: auch Mulden unter dem Objekt bis zur Unterseite auffuellen (sonst nur absenken)
// Liefert die Anzahl veraenderter Punkte.
inline size_t unterObjektAbsenken(std::vector<Dreieck> &d, const std::function<bool(double, double, double &)> &unterkante,
                                  double abstand, double rand, bool angleichen)
{
    if (d.empty())
        return 0;
    Ecken e = eckenSammeln(d);
    std::vector<double> h(e.punkt.size());
    for (size_t i = 0; i < h.size(); ++i)
        h[i] = e.punkt[i].y;

    // 1. Punkte unter den Objekten: Ziel = Unterseite - Abstand
    struct Innen
    {
        double x, z, ziel;
    };
    std::vector<Innen> innen;
    std::vector<char> drin(h.size(), 0);
    for (size_t i = 0; i < h.size(); ++i)
    {
        double u;
        if (!unterkante(e.punkt[i].x, e.punkt[i].z, u))
            continue;
        const double ziel = u - abstand;
        drin[i] = 1;
        innen.push_back({e.punkt[i].x, e.punkt[i].z, ziel});
        h[i] = angleichen ? ziel : std::min(h[i], ziel);
    }
    if (innen.empty())
        return 0;

    // 2. weicher Rand: Punkte ausserhalb bis 'rand' vom naechsten Punkt darunter (Raster fuer die Suche)
    if (rand > 0)
    {
        std::unordered_map<long long, std::vector<int>> zellen;
        auto zelle = [&](double x, double z)
        {
            const long long cx = (long long)std::floor(x / rand), cz = (long long)std::floor(z / rand);
            return std::make_pair(cx, cz);
        };
        auto schluessel = [](long long cx, long long cz) { return cx * 73856093LL ^ cz * 19349663LL; };
        for (size_t k = 0; k < innen.size(); ++k)
        {
            auto c = zelle(innen[k].x, innen[k].z);
            zellen[schluessel(c.first, c.second)].push_back(int(k));
        }
        for (size_t i = 0; i < h.size(); ++i)
        {
            if (drin[i])
                continue;
            const double x = e.punkt[i].x, z = e.punkt[i].z;
            auto c = zelle(x, z);
            double best = 1e300;
            int bestK = -1;
            for (long long dx = -1; dx <= 1; ++dx)
                for (long long dz = -1; dz <= 1; ++dz)
                {
                    auto it = zellen.find(schluessel(c.first + dx, c.second + dz));
                    if (it == zellen.end())
                        continue;
                    for (int k : it->second)
                    {
                        const double dd = std::hypot(innen[size_t(k)].x - x, innen[size_t(k)].z - z);
                        if (dd < best)
                            best = dd, bestK = k;
                    }
                }
            if (bestK < 0 || best >= rand)
                continue;
            const double w = weich(1.0 - best / rand); // 1 an der Kante, 0 am Ende des Rands
            const double ziel = innen[size_t(bestK)].ziel;
            if (angleichen || h[i] > ziel)
                h[i] += (ziel - h[i]) * w;
        }
    }

    size_t geaendert = 0;
    for (size_t i = 0; i < h.size(); ++i)
        geaendert += std::fabs(h[i] - e.punkt[i].y) > 0;
    if (geaendert)
        eckenZurueck(d, e, h);
    return geaendert;
}

enum PinselArt
{
    ANHEBEN = 0,
    ABSENKEN = 1,
    GLAETTEN = 2,
    EBNEN = 3
};

// Pinsel an der Stelle 'mitte' (x/z; y = Hoehe dort, fuer "Ebnen"), Radius r, Staerke 0..1.
// Gewicht je Punkt (1 - (d/r)^2)^2: in der Mitte voll, zum Rand weich auslaufend.
// Anheben/Absenken: bis 10 % des Radius je Schritt; Glaetten: zum gewichteten Mittel der Umgebung;
// Ebnen: zur Hoehe an der Mitte. Liefert die Anzahl veraenderter Punkte.
inline size_t pinsel(std::vector<Dreieck> &d, const Vector3D &mitte, double r, double staerke, int art)
{
    if (d.empty() || !(r > 0))
        return 0;
    staerke = std::clamp(staerke, 0.0, 1.0);
    Ecken e = eckenSammeln(d);
    std::vector<double> h(e.punkt.size());
    std::vector<std::pair<int, double>> im; // Punkt, Gewicht
    double summe = 0, summeW = 0;
    for (size_t i = 0; i < h.size(); ++i)
    {
        h[i] = e.punkt[i].y;
        const double dd = std::hypot(e.punkt[i].x - mitte.x, e.punkt[i].z - mitte.z);
        if (dd >= r)
            continue;
        const double q = dd / r, w = (1.0 - q * q) * (1.0 - q * q);
        im.push_back({int(i), w});
        summe += h[i] * w;
        summeW += w;
    }
    if (im.empty())
        return 0;
    const double mittel = summeW > 0 ? summe / summeW : mitte.y;
    for (auto [i, w] : im)
    {
        double &y = h[size_t(i)];
        switch (art)
        {
        case ANHEBEN:
            y += 0.1 * r * staerke * w;
            break;
        case ABSENKEN:
            y -= 0.1 * r * staerke * w;
            break;
        case GLAETTEN:
            y += (mittel - y) * staerke * w;
            break;
        case EBNEN:
            y += (mitte.y - y) * staerke * w;
            break;
        default:
            break;
        }
    }
    eckenZurueck(d, e, h);
    return im.size();
}
} // namespace terrainBearbeiten

// Pinsel-Modus in den Ansichten (Wireframe ruft diese Funktionen auf, toolwindow setzt sie)
namespace terrainPinsel
{
inline bool gAktiv = false;
inline std::function<bool(const Ray &)> gStart;  // Mausklick: true = Terrain getroffen, Strich beginnt
inline std::function<void(const Ray &)> gZiehen; // Maus mit gedrueckter Taste bewegt
inline std::function<void()> gEnde;              // Maustaste losgelassen
} // namespace terrainPinsel
