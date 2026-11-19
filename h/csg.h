#pragma once
// Mengenoperationen auf Dreiecksnetzen (CSG): Vereinigung (Addieren) und Differenz (Subtrahieren).
// Verfahren wie csg.js (Evan Wallace, MIT-Lizenz): jedes Netz wird zu einem BSP-Baum aus seinen Flaechen;
// die Flaechen des einen Koerpers werden am Baum des anderen zerschnitten und je nach innen/aussen
// behalten oder verworfen. Die Netze sollten geschlossen sein (Wuerfel, Wand, Rotationskoerper, Stein ...).
// Alle Schleifen ohne Rekursion (grosse Netze sprengen sonst den Stapel).
// "Manipulation" -> "Objekte addieren" / "Objekte subtrahieren" (toolwindow.cpp).

#include "dreieck.h"
#include "material.h"
#include "vector3d.h"
#include <algorithm>
#include <array>
#include <map>
#include <tuple>
#include <cmath>
#include <utility>
#include <vector>

namespace csg
{
struct Ecke
{
    Vector3D p;
    double u = 0, v = 0; // Texturkoordinaten (werden beim Zerschneiden mitgefuehrt)
};

struct Vieleck
{
    std::vector<Ecke> e;
    Vector3D n; // Ebene: n * p = w
    double w = 0;
    int mat = 0; // Nummer in der Materialliste
};

inline Ecke mischen(const Ecke &a, const Ecke &b, double t)
{
    return {a.p + (b.p - a.p) * t, a.u + (b.u - a.u) * t, a.v + (b.v - a.v) * t};
}

inline void umdrehen(Vieleck &v)
{
    std::reverse(v.e.begin(), v.e.end());
    v.n = -v.n;
    v.w = -v.w;
}

class Baum
{
  public:
    explicit Baum(double eps) : eps(eps) {}

    void bauen(std::vector<Vieleck> polys)
    {
        if (polys.empty())
            return;
        if (k.empty())
            k.emplace_back();
        std::vector<std::pair<int, std::vector<Vieleck>>> stapel;
        stapel.push_back({0, std::move(polys)});
        while (!stapel.empty())
        {
            auto [nr, liste] = std::move(stapel.back());
            stapel.pop_back();
            if (liste.empty())
                continue;
            if (!k[size_t(nr)].hatEbene)
            {
                k[size_t(nr)].hatEbene = true;
                k[size_t(nr)].n = liste.front().n;
                k[size_t(nr)].w = liste.front().w;
            }
            std::vector<Vieleck> vorne, hinten;
            for (auto &p : liste)
                teilen(k[size_t(nr)].n, k[size_t(nr)].w, p, k[size_t(nr)].polys, k[size_t(nr)].polys, vorne, hinten);
            if (!vorne.empty())
            {
                if (k[size_t(nr)].vorne < 0)
                {
                    k[size_t(nr)].vorne = int(k.size());
                    k.emplace_back();
                }
                stapel.push_back({k[size_t(nr)].vorne, std::move(vorne)});
            }
            if (!hinten.empty())
            {
                if (k[size_t(nr)].hinten < 0)
                {
                    k[size_t(nr)].hinten = int(k.size());
                    k.emplace_back();
                }
                stapel.push_back({k[size_t(nr)].hinten, std::move(hinten)});
            }
        }
    }

    // innen und aussen tauschen
    void umkehren()
    {
        for (auto &x : k)
        {
            for (auto &p : x.polys)
                umdrehen(p);
            x.n = -x.n;
            x.w = -x.w;
            std::swap(x.vorne, x.hinten);
        }
    }

    // Teile der Flaechen, die in diesem Koerper liegen, entfernen
    std::vector<Vieleck> beschneiden(std::vector<Vieleck> polys) const
    {
        std::vector<Vieleck> ergebnis;
        if (k.empty() || !k[0].hatEbene)
            return polys;
        std::vector<std::pair<int, std::vector<Vieleck>>> stapel;
        stapel.push_back({0, std::move(polys)});
        while (!stapel.empty())
        {
            auto [nr, liste] = std::move(stapel.back());
            stapel.pop_back();
            const Knoten &x = k[size_t(nr)];
            std::vector<Vieleck> vorne, hinten;
            for (auto &p : liste)
                teilen(x.n, x.w, p, vorne, hinten, vorne, hinten);
            if (x.vorne >= 0)
                stapel.push_back({x.vorne, std::move(vorne)});
            else
                for (auto &p : vorne)
                    ergebnis.push_back(std::move(p));
            if (x.hinten >= 0)
                stapel.push_back({x.hinten, std::move(hinten)});
            // ohne hinteren Teilbaum: hinten = innen, wird verworfen
        }
        return ergebnis;
    }

    void beschneidenMit(const Baum &anderer)
    {
        for (auto &x : k)
            x.polys = anderer.beschneiden(std::move(x.polys));
    }

    std::vector<Vieleck> alle() const
    {
        std::vector<Vieleck> r;
        for (auto &x : k)
            r.insert(r.end(), x.polys.begin(), x.polys.end());
        return r;
    }

  private:
    struct Knoten
    {
        bool hatEbene = false;
        Vector3D n;
        double w = 0;
        int vorne = -1, hinten = -1;
        std::vector<Vieleck> polys;
    };
    std::vector<Knoten> k;
    double eps;

    enum
    {
        AUF = 0,
        VORNE = 1,
        HINTEN = 2,
        BEIDE = 3
    };

    void teilen(const Vector3D &n, double w, const Vieleck &p, std::vector<Vieleck> &aufVorne,
                std::vector<Vieleck> &aufHinten, std::vector<Vieleck> &vorne, std::vector<Vieleck> &hinten) const
    {
        int art = 0;
        std::vector<int> arten(p.e.size());
        for (size_t i = 0; i < p.e.size(); ++i)
        {
            const double t = n * p.e[i].p - w;
            const int a = t < -eps ? HINTEN : t > eps ? VORNE : AUF;
            art |= a;
            arten[i] = a;
        }
        switch (art)
        {
        case AUF:
            (n * p.n > 0 ? aufVorne : aufHinten).push_back(p);
            break;
        case VORNE:
            vorne.push_back(p);
            break;
        case HINTEN:
            hinten.push_back(p);
            break;
        default:
        {
            Vieleck f, b;
            f.n = b.n = p.n;
            f.w = b.w = p.w;
            f.mat = b.mat = p.mat;
            for (size_t i = 0; i < p.e.size(); ++i)
            {
                const size_t j = (i + 1) % p.e.size();
                const int ti = arten[i], tj = arten[j];
                const Ecke &vi = p.e[i], &vj = p.e[j];
                if (ti != HINTEN)
                    f.e.push_back(vi);
                if (ti != VORNE)
                    b.e.push_back(vi);
                if ((ti | tj) == BEIDE)
                {
                    const double t = (w - n * vi.p) / (n * (vj.p - vi.p));
                    const Ecke m = mischen(vi, vj, t);
                    f.e.push_back(m);
                    b.e.push_back(m);
                }
            }
            if (f.e.size() >= 3)
                vorne.push_back(std::move(f));
            if (b.e.size() >= 3)
                hinten.push_back(std::move(b));
        }
        }
    }
};

// Dreiecke (Welt = a + position) -> Vielecke; Materialien werden in 'materialien' gesammelt
inline std::vector<Vieleck> ausDreiecken(const std::vector<const std::vector<Dreieck> *> &listen,
                                         std::vector<Material> &materialien)
{
    std::vector<Vieleck> r;
    for (auto *liste : listen)
        for (const Dreieck &d : *liste)
        {
            Vieleck v;
            v.e = {{d.a + d.position, d.uvA.x, d.uvA.y}, {d.b + d.position, d.uvB.x, d.uvB.y}, {d.c + d.position, d.uvC.x, d.uvC.y}};
            const Vector3D n = (v.e[1].p - v.e[0].p).cross(v.e[2].p - v.e[0].p);
            if (n.length() < 1e-300)
                continue; // entartet
            v.n = n * (1.0 / n.length());
            v.w = v.n * v.e[0].p;
            v.mat = int(materialien.size());
            materialien.push_back(d.mat);
            r.push_back(std::move(v));
        }
    return r;
}

// Vielecke -> Dreiecke (Faecher), flach schattiert
inline std::vector<Dreieck> zuDreiecken(const std::vector<Vieleck> &polys, const std::vector<Material> &materialien)
{
    std::vector<Dreieck> r;
    for (const Vieleck &v : polys)
        for (size_t i = 1; i + 1 < v.e.size(); ++i)
        {
            const Ecke &a = v.e[0], &b = v.e[i], &c = v.e[i + 1];
            if ((b.p - a.p).cross(c.p - a.p).length() < 1e-300)
                continue;
            Dreieck d(a.p, b.p, c.p, materialien[size_t(v.mat)]);
            d.uvA.x = a.u, d.uvA.y = a.v;
            d.uvB.x = b.u, d.uvB.y = b.v;
            d.uvC.x = c.u, d.uvC.y = c.v;
            r.push_back(d);
        }
    return r;
}

// Toleranz aus der Groesse der beteiligten Koerper
inline double toleranz(const std::vector<Vieleck> &a, const std::vector<Vieleck> &b)
{
    double m = 0;
    for (auto *l : {&a, &b})
        for (auto &v : *l)
            for (auto &e : v.e)
                m = std::max({m, std::fabs(e.p.x), std::fabs(e.p.y), std::fabs(e.p.z)});
    return std::max(1e-9, 1e-7 * m);
}

// A + B (Vereinigung)
inline std::vector<Vieleck> vereinigung(const std::vector<Vieleck> &A, const std::vector<Vieleck> &B)
{
    const double eps = toleranz(A, B);
    Baum a(eps), b(eps);
    a.bauen(A);
    b.bauen(B);
    a.beschneidenMit(b);
    b.beschneidenMit(a);
    b.umkehren();
    b.beschneidenMit(a);
    b.umkehren();
    a.bauen(b.alle());
    return a.alle();
}

// A - B (Differenz)
inline std::vector<Vieleck> differenz(const std::vector<Vieleck> &A, const std::vector<Vieleck> &B)
{
    const double eps = toleranz(A, B);
    Baum a(eps), b(eps);
    a.bauen(A);
    b.bauen(B);
    a.umkehren();
    a.beschneidenMit(b);
    b.beschneidenMit(a);
    b.umkehren();
    b.beschneidenMit(a);
    b.umkehren();
    a.bauen(b.alle());
    a.umkehren();
    return a.alle();
}

// ---------- Grundkoerper als geschlossene Netze (Kugel, Stab, Quader) ----------

inline std::vector<Dreieck> kugelNetz(const Vector3D &mitte, double r, const Material &mat, int segmente = 24)
{
    std::vector<Dreieck> d;
    const int ringe = std::max(4, segmente / 2);
    auto punkt = [&](int i, int j)
    {
        const double th = M_PI * i / ringe, ph = 2.0 * M_PI * j / segmente;
        return mitte + Vector3D(std::sin(th) * std::cos(ph), std::cos(th), std::sin(th) * std::sin(ph)) * r;
    };
    for (int i = 0; i < ringe; ++i)
        for (int j = 0; j < segmente; ++j)
        {
            const Vector3D a = punkt(i, j), b = punkt(i, j + 1), c = punkt(i + 1, j + 1), e = punkt(i + 1, j);
            if (i > 0)
                d.emplace_back(a, b, e, mat); // Umlauf nach aussen
            if (i + 1 < ringe)
                d.emplace_back(b, c, e, mat);
        }
    return d;
}

inline std::vector<Dreieck> zylinderNetz(const Vector3D &p1, const Vector3D &p2, double r, const Material &mat,
                                         int segmente = 24)
{
    std::vector<Dreieck> d;
    const Vector3D achse = p2 - p1;
    if (achse.length() < 1e-300)
        return d;
    const Vector3D w = achse * (1.0 / achse.length());
    const Vector3D hilf = std::fabs(w.y) < 0.9 ? Vector3D(0, 1, 0) : Vector3D(1, 0, 0);
    const Vector3D u = hilf.cross(w).normalized(), v = w.cross(u);
    auto rand = [&](const Vector3D &m, int j)
    {
        const double ph = 2.0 * M_PI * j / segmente;
        return m + (u * std::cos(ph) + v * std::sin(ph)) * r;
    };
    for (int j = 0; j < segmente; ++j)
    {
        const Vector3D a1 = rand(p1, j), b1 = rand(p1, j + 1), a2 = rand(p2, j), b2 = rand(p2, j + 1);
        d.emplace_back(a1, b1, b2, mat); // Mantel
        d.emplace_back(a1, b2, a2, mat);
        d.emplace_back(p1, b1, a1, mat); // Boden (zeigt nach -w)
        d.emplace_back(p2, a2, b2, mat); // Deckel (zeigt nach +w)
    }
    return d;
}

// Quader mit den Achsen ex, ey, ez (Halbachsen, beliebig gedreht) um 'mitte'
inline std::vector<Dreieck> quaderNetz(const Vector3D &mitte, const Vector3D &ex, const Vector3D &ey, const Vector3D &ez,
                                       const Material &mat)
{
    Vector3D P[8];
    for (int i = 0; i < 8; ++i)
        P[i] = mitte + ex * ((i & 1) ? 1.0 : -1.0) + ey * ((i & 2) ? 1.0 : -1.0) + ez * ((i & 4) ? 1.0 : -1.0);
    const bool rechts = ex.cross(ey) * ez > 0; // sonst Umlauf umdrehen
    const int q[6][4] = {{0, 2, 3, 1}, {4, 5, 7, 6}, {0, 1, 5, 4}, {2, 6, 7, 3}, {0, 4, 6, 2}, {1, 3, 7, 5}};
    std::vector<Dreieck> d;
    for (auto &f : q)
    {
        if (rechts)
        {
            d.emplace_back(P[f[0]], P[f[1]], P[f[2]], mat);
            d.emplace_back(P[f[0]], P[f[2]], P[f[3]], mat);
        }
        else
        {
            d.emplace_back(P[f[0]], P[f[2]], P[f[1]], mat);
            d.emplace_back(P[f[0]], P[f[3]], P[f[2]], mat);
        }
    }
    return d;
}

// Dreiecke einheitlich nach aussen drehen. Fuers Rendern ist der Umlaufsinn egal, fuer CSG nicht: ein Koerper,
// dessen Dreiecke nach innen zeigen, gilt als "alles ausser ihm" (Addieren ergaebe dann die Schnittmenge).
// 1. Nachbardreiecke (gemeinsame Kante) muessen die Kante gegenlaeufig durchlaufen -> einheitlich je Teilstueck
// 2. jedes zusammenhaengende Teilstueck mit negativem Rauminhalt ganz umdrehen
// Gibt die Anzahl umgedrehter Dreiecke zurueck.
inline size_t ausrichten(std::vector<Dreieck> &d)
{
    const size_t n = d.size();
    if (n == 0)
        return 0;
    // Ecken zusammenfassen (gleiche Lage bis auf Rundung)
    double gr = 0;
    for (const Dreieck &t : d)
        for (const Vector3D &p : {t.a, t.b, t.c})
            gr = std::max({gr, std::fabs(p.x), std::fabs(p.y), std::fabs(p.z)});
    const double q = std::max(1e-12, gr * 1e-9);
    std::map<std::tuple<long long, long long, long long>, int> ecken;
    auto nummer = [&](const Vector3D &p)
    {
        auto k = std::make_tuple((long long)std::llround(p.x / q), (long long)std::llround(p.y / q), (long long)std::llround(p.z / q));
        auto it = ecken.find(k);
        if (it != ecken.end())
            return it->second;
        const int id = int(ecken.size());
        ecken.emplace(k, id);
        return id;
    };
    std::vector<std::array<int, 3>> e(n);
    for (size_t i = 0; i < n; ++i)
        e[i] = {nummer(d[i].a), nummer(d[i].b), nummer(d[i].c)};
    // Kante (klein, gross) -> Dreiecke mit Richtung (true: klein -> gross)
    std::map<std::pair<int, int>, std::vector<std::pair<size_t, bool>>> kanten;
    for (size_t i = 0; i < n; ++i)
        for (int k = 0; k < 3; ++k)
        {
            const int u = e[i][size_t(k)], v = e[i][size_t((k + 1) % 3)];
            if (u == v)
                continue;
            kanten[{std::min(u, v), std::max(u, v)}].push_back({i, u < v});
        }
    std::vector<std::vector<std::pair<size_t, bool>>> nachbarn(n); // (Dreieck, gleiche Richtung?)
    for (auto &kv : kanten)
    {
        auto &l = kv.second;
        if (l.size() != 2)
            continue; // Rand oder mehrdeutig
        const bool gleich = l[0].second == l[1].second;
        nachbarn[l[0].first].push_back({l[1].first, gleich});
        nachbarn[l[1].first].push_back({l[0].first, gleich});
    }
    std::vector<int> dreh(n, -1); // 1 = umdrehen
    std::vector<size_t> stapel, teil;
    size_t umgedreht = 0;
    for (size_t start = 0; start < n; ++start)
    {
        if (dreh[start] >= 0)
            continue;
        teil.clear();
        dreh[start] = 0;
        stapel.push_back(start);
        while (!stapel.empty())
        {
            const size_t i = stapel.back();
            stapel.pop_back();
            teil.push_back(i);
            for (auto [j, gleich] : nachbarn[i])
                if (dreh[j] < 0)
                {
                    dreh[j] = gleich ? 1 - dreh[i] : dreh[i]; // gleiche Richtung: einer muss umgedreht werden
                    stapel.push_back(j);
                }
        }
        // Rauminhalt des Teilstuecks mit dieser Ausrichtung
        double vol = 0;
        for (size_t i : teil)
        {
            const Vector3D a = d[i].a + d[i].position, b = d[i].b + d[i].position, c = d[i].c + d[i].position;
            const double v = a * b.cross(c);
            vol += dreh[i] ? -v : v;
        }
        if (vol < 0)
            for (size_t i : teil)
                dreh[i] = 1 - dreh[i];
    }
    for (size_t i = 0; i < n; ++i)
        if (dreh[i] == 1)
        {
            Dreieck &t = d[i];
            std::swap(t.b, t.c);
            std::swap(t.nB, t.nC);
            std::swap(t.uvB, t.uvC);
            t.rebuild();
            ++umgedreht;
        }
    return umgedreht;
}

// Rauminhalt (Pruefung und Hinweis): Summe der Tetraeder zum Ursprung
inline double volumen(const std::vector<Dreieck> &d)
{
    double s = 0;
    for (const Dreieck &t : d)
        s += (t.a + t.position) * ((t.b + t.position).cross(t.c + t.position));
    return s / 6.0;
}
} // namespace csg
