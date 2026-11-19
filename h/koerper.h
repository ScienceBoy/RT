#pragma once
// Organische Grundkoerper als Dreiecksnetze:
//  - Ellipsoid (drei Radien), glatt oder facettiert          "+ Neu -> Ellipsoid ..."
//  - Roehre entlang eines Pfads mit Radius je Punkt          "+ Neu -> Röhre entlang Punkten ..."
//    (Schwanz, Hals, Schlange, Henkel, Rohr, Ast); der Pfad wird mit einer Catmull-Rom-Kurve geglaettet,
//    die Ringe folgen ihm ohne Verdrehen (parallel transportierter Rahmen), Enden rund geschlossen.
// Beide sind geschlossen und nach aussen orientiert (fuer "Objekte addieren/subtrahieren").

#include "dreieck.h"
#include "material.h"
#include "vector3d.h"
#include <algorithm>
#include <cmath>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace koerper
{
// Rauminhalt mit Vorzeichen (> 0: nach aussen orientiert)
inline double volumen(const std::vector<Dreieck> &d)
{
    double s = 0;
    for (const Dreieck &t : d)
        s += t.a * (t.b.cross(t.c));
    return s / 6.0;
}

// Umlaufsinn aller Dreiecke umdrehen (die Ecknormalen zeigen schon nach aussen und bleiben)
inline void umdrehen(std::vector<Dreieck> &d)
{
    for (Dreieck &t : d)
    {
        std::swap(t.b, t.c);
        std::swap(t.nB, t.nC);
        std::swap(t.uvB, t.uvC);
        t.rebuild();
    }
}

inline void dreieckDazu(std::vector<Dreieck> &d, const Vector3D &a, const Vector3D &b, const Vector3D &c, const Vector3D &na,
                        const Vector3D &nb, const Vector3D &nc, bool glatt, const Material &mat)
{
    if ((b - a).cross(c - a).length() < 1e-14)
        return; // entartet (Pol)
    if (glatt)
        d.emplace_back(a, b, c, na, nb, nc, mat);
    else
        d.emplace_back(a, b, c, mat);
}

// Ellipsoid um 'mitte' mit den Radien rx, ry, rz (Halbachsen entlang X, Y, Z)
inline std::vector<Dreieck> ellipsoid(const Vector3D &mitte, double rx, double ry, double rz, int segmente, bool glatt,
                                      const Material &mat)
{
    std::vector<Dreieck> d;
    segmente = std::max(6, segmente);
    const int ringe = std::max(3, segmente / 2);
    auto punkt = [&](int i, int j)
    {
        const double th = M_PI * i / ringe, ph = 2.0 * M_PI * j / segmente;
        const Vector3D e(std::sin(th) * std::cos(ph), std::cos(th), std::sin(th) * std::sin(ph));
        return e;
    };
    auto lage = [&](const Vector3D &e) { return mitte + Vector3D(e.x * rx, e.y * ry, e.z * rz); };
    auto normale = [&](const Vector3D &e) // Gradient der Ellipsoid-Gleichung
    {
        Vector3D n(e.x / rx, e.y / ry, e.z / rz);
        return n.length() > 1e-300 ? n.normalized() : Vector3D(0, 1, 0);
    };
    for (int i = 0; i < ringe; ++i)
        for (int j = 0; j < segmente; ++j)
        {
            const Vector3D a = punkt(i, j), b = punkt(i, j + 1), c = punkt(i + 1, j + 1), e = punkt(i + 1, j);
            if (i > 0)
                dreieckDazu(d, lage(a), lage(e), lage(b), normale(a), normale(e), normale(b), glatt, mat);
            if (i + 1 < ringe)
                dreieckDazu(d, lage(b), lage(e), lage(c), normale(b), normale(e), normale(c), glatt, mat);
        }
    if (volumen(d) < 0)
        umdrehen(d);
    return d;
}

// ---------- Roehre ----------

struct PfadPunkt
{
    Vector3D p;
    double r; // Radius an diesem Punkt
};

// Catmull-Rom: 'schritte' Zwischenpunkte je Strecke (Lage und Radius)
inline std::vector<PfadPunkt> glaetten(const std::vector<PfadPunkt> &pk, int schritte)
{
    if (pk.size() < 3 || schritte <= 1)
        return pk;
    std::vector<PfadPunkt> r;
    const size_t n = pk.size();
    for (size_t i = 0; i + 1 < n; ++i)
    {
        const PfadPunkt &p0 = pk[i == 0 ? 0 : i - 1], &p1 = pk[i], &p2 = pk[i + 1], &p3 = pk[std::min(n - 1, i + 2)];
        for (int s = 0; s < schritte; ++s)
        {
            const double t = double(s) / schritte, t2 = t * t, t3 = t2 * t;
            auto cr = [&](double a, double b, double c, double e)
            { return 0.5 * (2 * b + (-a + c) * t + (2 * a - 5 * b + 4 * c - e) * t2 + (-a + 3 * b - 3 * c + e) * t3); };
            PfadPunkt q;
            q.p = Vector3D(cr(p0.p.x, p1.p.x, p2.p.x, p3.p.x), cr(p0.p.y, p1.p.y, p2.p.y, p3.p.y), cr(p0.p.z, p1.p.z, p2.p.z, p3.p.z));
            q.r = std::max(1e-6, p1.r + (p2.r - p1.r) * t); // Radius linear (keine Ueberschwinger)
            r.push_back(q);
        }
    }
    r.push_back(pk.back());
    return r;
}

// Roehre entlang der Punkte; 'unterteilung' Zwischenpunkte je Strecke, 'segmente' rundherum.
// kappen: Enden mit einer runden Kuppe schliessen (sonst offen).
inline std::vector<Dreieck> roehre(const std::vector<PfadPunkt> &punkte, int segmente, int unterteilung, bool glatt, bool kappen,
                                   const Material &mat)
{
    std::vector<Dreieck> d;
    // doppelte Punkte entfernen
    std::vector<PfadPunkt> pk;
    for (const PfadPunkt &q : punkte)
        if (pk.empty() || (q.p - pk.back().p).length() > 1e-9)
            pk.push_back(q);
    if (pk.size() < 2)
        return d;
    pk = glaetten(pk, std::max(1, unterteilung));
    segmente = std::max(3, segmente);
    const size_t n = pk.size();

    // Tangenten und parallel transportierte Rahmen (kein Verdrehen entlang des Pfads)
    std::vector<Vector3D> T(n), N(n), B(n);
    for (size_t i = 0; i < n; ++i)
    {
        Vector3D t = pk[std::min(n - 1, i + 1)].p - pk[i == 0 ? 0 : i - 1].p;
        T[i] = t.length() > 1e-300 ? t.normalized() : (i ? T[i - 1] : Vector3D(1, 0, 0));
    }
    {
        Vector3D hilf = std::fabs(T[0].z) < 0.9 ? Vector3D(0, 0, 1) : Vector3D(1, 0, 0);
        N[0] = (hilf - T[0] * (hilf * T[0])).normalized();
    }
    for (size_t i = 1; i < n; ++i)
    {
        Vector3D v = N[i - 1] - T[i] * (N[i - 1] * T[i]);
        N[i] = v.length() > 1e-12 ? v.normalized() : N[i - 1];
    }
    for (size_t i = 0; i < n; ++i)
        B[i] = T[i].cross(N[i]);

    auto ring = [&](size_t i, int j) { const double ph = 2.0 * M_PI * j / segmente; return N[i] * std::cos(ph) + B[i] * std::sin(ph); };
    auto lage = [&](size_t i, int j) { return pk[i].p + ring(i, j) * pk[i].r; };
    // Normale: radial, geneigt durch die Aenderung des Radius
    auto normale = [&](size_t i, int j)
    {
        const double dr = (pk[std::min(n - 1, i + 1)].r - pk[i == 0 ? 0 : i - 1].r);
        const double dl = (pk[std::min(n - 1, i + 1)].p - pk[i == 0 ? 0 : i - 1].p).length();
        const double neig = dl > 1e-12 ? -dr / dl : 0.0;
        return (ring(i, j) + T[i] * neig).normalized();
    };
    for (size_t i = 0; i + 1 < n; ++i)
        for (int j = 0; j < segmente; ++j)
        {
            dreieckDazu(d, lage(i, j), lage(i, j + 1), lage(i + 1, j + 1), normale(i, j), normale(i, j + 1), normale(i + 1, j + 1), glatt, mat);
            dreieckDazu(d, lage(i, j), lage(i + 1, j + 1), lage(i + 1, j), normale(i, j), normale(i + 1, j + 1), normale(i + 1, j), glatt, mat);
        }
    if (kappen)
    {
        // runde Kuppe: drei Ringe zur Spitze hin (Viertelkreis), Spitze 0,8 r ueber das Ende hinaus
        for (int ende = 0; ende < 2; ++ende)
        {
            const size_t i = ende == 0 ? 0 : n - 1;
            const Vector3D aussen = ende == 0 ? -T[i] : T[i];
            const double r = pk[i].r;
            const int stufen = 3;
            auto kp = [&](int k, int j) // k = 0: Rand der Roehre, k = stufen: Spitze
            {
                const double w = 0.5 * M_PI * k / stufen;
                return pk[i].p + ring(i, j) * (r * std::cos(w)) + aussen * (0.8 * r * std::sin(w));
            };
            auto kn = [&](int k, int j)
            {
                const double w = 0.5 * M_PI * k / stufen;
                Vector3D v = ring(i, j) * std::cos(w) / 1.0 + aussen * (std::sin(w) / 0.8);
                return v.normalized();
            };
            for (int k = 0; k < stufen; ++k)
                for (int j = 0; j < segmente; ++j)
                {
                    if (ende == 1) // Ende: gleicher Umlauf wie die Roehre
                    {
                        dreieckDazu(d, kp(k, j), kp(k, j + 1), kp(k + 1, j + 1), kn(k, j), kn(k, j + 1), kn(k + 1, j + 1), glatt, mat);
                        dreieckDazu(d, kp(k, j), kp(k + 1, j + 1), kp(k + 1, j), kn(k, j), kn(k + 1, j + 1), kn(k + 1, j), glatt, mat);
                    }
                    else // Anfang: zeigt in die Gegenrichtung, Umlauf andersherum
                    {
                        dreieckDazu(d, kp(k, j), kp(k + 1, j + 1), kp(k, j + 1), kn(k, j), kn(k + 1, j + 1), kn(k, j + 1), glatt, mat);
                        dreieckDazu(d, kp(k, j), kp(k + 1, j), kp(k + 1, j + 1), kn(k, j), kn(k + 1, j), kn(k + 1, j + 1), glatt, mat);
                    }
                }
        }
    }
    // nach aussen orientieren (Umlauf haengt von der Pfadrichtung ab)
    if (kappen && volumen(d) < 0)
        umdrehen(d);
    else if (!kappen && !d.empty())
    {
        // offene Roehre: Dreiecksnormale soll wie die Ecknormale nach aussen zeigen
        const Dreieck &t = d.front();
        if ((t.b - t.a).cross(t.c - t.a) * normale(0, 0) < 0)
            umdrehen(d);
    }
    return d;
}
} // namespace koerper
