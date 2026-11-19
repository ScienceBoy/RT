#pragma once
// Stein aus zufaelligen Schnitten: ein Wuerfel wird mehrmals mit zufaellig gedrehten Ebenen beschnitten
// (wie Abschlaege mit dem Hammer). Ergebnis: ein konvexer, kantiger Koerper mit grossen Bruchflaechen.
// "+ Neu" -> "Stein ..." (toolwindow.cpp).
//
// Jede Flaeche ist ein konvexes Vieleck (gegen den Uhrzeigersinn von aussen gesehen). Ein Schnitt mit der
// Ebene n * p = d schneidet jedes Vieleck ab (Sutherland-Hodgman) und schliesst die Schnittstelle mit einem
// neuen Vieleck (Deckel). Der Ursprung liegt immer innen, der Stein zerfaellt also nie.

#include "dreieck.h"
#include "material.h"
#include "vector3d.h"
#include <algorithm>
#include <cmath>
#include <random>
#include <vector>

namespace stein
{
using Vieleck = std::vector<Vector3D>;

inline void schneiden(std::vector<Vieleck> &flaechen, const Vector3D &n, double d)
{
    std::vector<Vieleck> neu;
    std::vector<Vector3D> schnitt;
    for (const Vieleck &f : flaechen)
    {
        Vieleck aus;
        for (size_t i = 0; i < f.size(); ++i)
        {
            const Vector3D &a = f[i], &b = f[(i + 1) % f.size()];
            const double da = a * n - d, db = b * n - d;
            if (da <= 0)
                aus.push_back(a);
            if ((da < 0 && db > 0) || (da > 0 && db < 0))
            {
                Vector3D p = a + (b - a) * (da / (da - db));
                aus.push_back(p);
                schnitt.push_back(p);
            }
        }
        if (aus.size() >= 3)
            neu.push_back(aus);
    }
    // Deckel: Schnittpunkte (doppelte entfernen) nach Winkel um ihre Mitte, gegen den Uhrzeigersinn um n
    std::vector<Vector3D> punkte;
    for (const Vector3D &p : schnitt)
    {
        bool doppelt = false;
        for (const Vector3D &q : punkte)
            doppelt = doppelt || (p - q).length() < 1e-9;
        if (!doppelt)
            punkte.push_back(p);
    }
    if (punkte.size() >= 3)
    {
        Vector3D mitte(0, 0, 0);
        for (const Vector3D &p : punkte)
            mitte += p;
        mitte = mitte * (1.0 / double(punkte.size()));
        Vector3D hilf = std::fabs(n.y) < 0.9 ? Vector3D(0, 1, 0) : Vector3D(1, 0, 0);
        Vector3D u = hilf.cross(n).normalized(), v = n.cross(u); // u x v = n
        std::sort(punkte.begin(), punkte.end(), [&](const Vector3D &a, const Vector3D &b)
                  { return std::atan2((a - mitte) * v, (a - mitte) * u) < std::atan2((b - mitte) * v, (b - mitte) * u); });
        neu.push_back(punkte);
    }
    flaechen.swap(neu);
}

// groesse: Kantenlaenge des Ausgangswuerfels; schnitte: Anzahl Abschlaege; tiefe: wie tief jeder Schnitt
// geht (1 = normal, groesser = tiefer, runder); hoehe: Faktor fuer die Hoehe (< 1 = flacher Stein);
// zufall: Startwert (gleicher Wert = gleicher Stein). Unterseite auf y = 0, Mitte auf x = z = 0.
inline std::vector<Dreieck> erzeugen(double groesse, int schnitte, double tiefe, double hoehe, unsigned zufall,
                                     const Material &mat)
{
    const double h = groesse * 0.5;
    Vector3D P[8];
    for (int i = 0; i < 8; ++i)
        P[i] = Vector3D((i & 1) ? h : -h, (i & 2) ? h : -h, (i & 4) ? h : -h);
    const int q[6][4] = {{0, 1, 3, 2}, {4, 6, 7, 5}, {0, 4, 5, 1}, {2, 3, 7, 6}, {0, 2, 6, 4}, {1, 5, 7, 3}};
    std::vector<Vieleck> flaechen;
    for (auto &f : q)
        flaechen.push_back({P[f[0]], P[f[1]], P[f[2]], P[f[3]]});

    std::mt19937 rng(zufall);
    std::normal_distribution<double> gauss(0.0, 1.0);
    std::uniform_real_distribution<double> anteil(0.15, 0.38);
    for (int s = 0; s < schnitte; ++s)
    {
        Vector3D n(gauss(rng), gauss(rng), gauss(rng));
        if (n.length() < 1e-9)
            continue;
        n = n.normalized();
        double weitester = 0;
        for (const Vieleck &f : flaechen)
            for (const Vector3D &p : f)
                weitester = std::max(weitester, p * n);
        // Ebene zwischen Mitte und weitestem Punkt: tiefe 1 schneidet 15..38 % ab
        const double d = weitester * std::max(0.05, 1.0 - std::max(0.0, tiefe) * anteil(rng));
        schneiden(flaechen, n, d);
    }

    // auf die gewuenschte Groesse bringen (die Schnitte machen den Wuerfel deutlich kleiner):
    // laengste Ausdehnung = groesse; dann flacher/hoeher und auf den Boden stellen
    Vector3D mn(1e300, 1e300, 1e300), mx(-1e300, -1e300, -1e300);
    for (const Vieleck &f : flaechen)
        for (const Vector3D &p : f)
        {
            mn = Vector3D(std::min(mn.x, p.x), std::min(mn.y, p.y), std::min(mn.z, p.z));
            mx = Vector3D(std::max(mx.x, p.x), std::max(mx.y, p.y), std::max(mx.z, p.z));
        }
    const double ausdehnung = std::max({mx.x - mn.x, mx.y - mn.y, mx.z - mn.z});
    const double faktor = ausdehnung > 1e-12 ? groesse / ausdehnung : 1.0;
    const Vector3D mitte = (mn + mx) * 0.5;
    double unten = 1e300;
    for (Vieleck &f : flaechen)
        for (Vector3D &p : f)
        {
            p = (p - mitte) * faktor;
            p.y *= hoehe;
            unten = std::min(unten, p.y);
        }
    std::vector<Dreieck> d;
    for (const Vieleck &f : flaechen)
        for (size_t i = 1; i + 1 < f.size(); ++i)
        {
            Vector3D a = f[0], b = f[i], c = f[i + 1];
            a.y -= unten;
            b.y -= unten;
            c.y -= unten;
            if (((b - a).cross(c - a)).length() < 1e-14)
                continue; // entartet
            d.emplace_back(a, b, c, mat);
        }
    return d;
}

// Zufaellige Drehung um alle drei Achsen (Winkel je 0..360 Grad, Reihenfolge X, Y, Z), danach wieder mit
// der Unterseite auf y = 0 und der Mitte auf x = z = 0. Gibt den waagrechten Radius (halbe Diagonale der
// Grundflaeche) zurueck, damit mehrere Steine ohne Ueberlappung verteilt werden koennen.
inline double zufaelligDrehen(std::vector<Dreieck> &d, std::mt19937 &rng)
{
    std::uniform_real_distribution<double> winkel(0.0, 2.0 * 3.14159265358979323846);
    const double ax = winkel(rng), ay = winkel(rng), az = winkel(rng);
    const double cx = std::cos(ax), sx = std::sin(ax), cy = std::cos(ay), sy = std::sin(ay);
    const double cz = std::cos(az), sz = std::sin(az);
    auto drehen = [&](const Vector3D &v)
    {
        Vector3D r(v.x, cx * v.y - sx * v.z, sx * v.y + cx * v.z);          // um X
        r = Vector3D(cy * r.x + sy * r.z, r.y, -sy * r.x + cy * r.z);       // um Y
        return Vector3D(cz * r.x - sz * r.y, sz * r.x + cz * r.y, r.z);     // um Z
    };
    Vector3D mn(1e300, 1e300, 1e300), mx(-1e300, -1e300, -1e300);
    auto grenze = [&](const Vector3D &p)
    {
        mn = Vector3D(std::min(mn.x, p.x), std::min(mn.y, p.y), std::min(mn.z, p.z));
        mx = Vector3D(std::max(mx.x, p.x), std::max(mx.y, p.y), std::max(mx.z, p.z));
    };
    for (Dreieck &t : d)
    {
        t.a = drehen(t.a);
        t.b = drehen(t.b);
        t.c = drehen(t.c);
        t.nA = drehen(t.nA);
        t.nB = drehen(t.nB);
        t.nC = drehen(t.nC);
        grenze(t.a);
        grenze(t.b);
        grenze(t.c);
    }
    const Vector3D verschiebung(-(mn.x + mx.x) * 0.5, -mn.y, -(mn.z + mx.z) * 0.5);
    for (Dreieck &t : d)
    {
        t.a += verschiebung;
        t.b += verschiebung;
        t.c += verschiebung;
    }
    const double bx = (mx.x - mn.x) * 0.5, bz = (mx.z - mn.z) * 0.5;
    return std::sqrt(bx * bx + bz * bz);
}
} // namespace stein
