#pragma once
// Stabile Ruhelage eines Koerpers (fuer "Auf Oberfläche absetzen" -> stabil hinlegen):
// Liegt der Koerper mit der Seite in Richtung n (nach unten zeigend) auf dem Boden, so ist die Hoehe seines
// Schwerpunkts h(n) = max_i (p_i * n) - s * n. Ruhelagen sind die lokalen Minima von h: dort liegt der Koerper
// auf einer Flaeche seiner Huelle, und der Schwerpunkt liegt ueber dieser Flaeche (kein Kippen mehr).
// Ausgehend von der jetzigen Lage (n = nach unten) wird bergab gesucht - wie ein Stein, der von der Spitze
// auf die naechste flache Seite kippt.

#include "dreieck.h"
#include "vector3d.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace stabil
{
// Schwerpunkt eines (geschlossenen) Netzes; bei offenem/entartetem Netz der Mittelwert der Ecken
inline Vector3D schwerpunkt(const std::vector<Dreieck> &d)
{
    Vector3D m(0, 0, 0);
    size_t n = 0;
    for (const Dreieck &t : d)
        for (const Vector3D &p : {t.a, t.b, t.c})
        {
            m = m + p + t.position;
            ++n;
        }
    if (n == 0)
        return m;
    m = m * (1.0 / double(n));
    double vol = 0;
    Vector3D s(0, 0, 0);
    for (const Dreieck &t : d)
    {
        const Vector3D a = t.a + t.position - m, b = t.b + t.position - m, c = t.c + t.position - m;
        const double v = a * b.cross(c) / 6.0;
        vol += v;
        s = s + (a + b + c) * (v / 4.0);
    }
    double groesse = 0;
    for (const Dreieck &t : d)
        groesse = std::max(groesse, (t.a + t.position - m).length());
    if (std::fabs(vol) > 1e-9 * groesse * groesse * groesse && std::fabs(vol) > 1e-300)
        return m + s * (1.0 / vol);
    return m;
}

// Ecken (Lage eingerechnet), hoechstens 'max' Stueck (gleichmaessig ausgewaehlt)
inline std::vector<Vector3D> ecken(const std::vector<Dreieck> &d, size_t max = 1500)
{
    std::vector<Vector3D> p;
    p.reserve(d.size() * 3);
    for (const Dreieck &t : d)
        for (const Vector3D &q : {t.a, t.b, t.c})
            p.push_back(q + t.position);
    if (p.size() > max)
    {
        // aeusserste Punkte zuerst behalten waere besser; gleichmaessig reicht fuer Steine (viele Ecken)
        std::vector<Vector3D> r;
        r.reserve(max);
        const double schritt = double(p.size()) / double(max);
        for (size_t i = 0; i < max; ++i)
            r.push_back(p[size_t(double(i) * schritt)]);
        p.swap(r);
    }
    return p;
}

inline double hoehe(const std::vector<Vector3D> &p, const Vector3D &s, const Vector3D &n)
{
    double m = -1e300;
    for (const Vector3D &q : p)
        m = std::max(m, q * n);
    return m - s * n;
}

// lokale Suche bergab auf der Kugel, Start: 'start' (nach unten zeigend, z.B. (0,-1,0))
inline Vector3D ruheRichtung(const std::vector<Vector3D> &p, const Vector3D &s, Vector3D start = Vector3D(0, -1, 0))
{
    Vector3D n = start.normalized();
    if (p.size() < 4)
        return n;
    double h = hoehe(p, s, n);
    double schritt = 0.35; // Bogenmass
    for (int iter = 0; iter < 600 && schritt > 1e-3; ++iter)
    {
        // zwei Richtungen quer zu n
        Vector3D hilf = std::fabs(n.x) < 0.9 ? Vector3D(1, 0, 0) : Vector3D(0, 1, 0);
        Vector3D e1 = n.cross(hilf).normalized(), e2 = n.cross(e1).normalized();
        double bestH = h;
        Vector3D bestN = n;
        for (int k = 0; k < 12; ++k)
        {
            const double w = 2.0 * M_PI * k / 12.0;
            Vector3D q = n * std::cos(schritt) + (e1 * std::cos(w) + e2 * std::sin(w)) * std::sin(schritt);
            q = q.normalized();
            const double hq = hoehe(p, s, q);
            if (hq < bestH - 1e-12)
            {
                bestH = hq;
                bestN = q;
            }
        }
        if (bestH < h)
        {
            h = bestH;
            n = bestN;
        }
        else
            schritt *= 0.5;
    }
    return n;
}
} // namespace stabil
