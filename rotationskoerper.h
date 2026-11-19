#pragma once
// Rotationskoerper: ein Profil (Radius r >= 0, Hoehe y) wird um die senkrechte Achse (Y) gedreht.
// "+ Neu" -> "Rotationsobjekt ..." (Dialog mit Profil-Editor in toolwindow.cpp).
//
// Je Profilstrecke und Segment ein Viereck aus zwei Dreiecken; Punkte auf der Achse (r = 0) ergeben
// Spitzen bzw. geschlossene Boeden. Normalen zeigen nach aussen, egal in welcher Richtung das Profil
// gezeichnet wurde. Glatt: Normalen je Profilpunkt gemittelt, ausser an Knicken ueber 'knickGrad'
// (dort bleibt die Kante scharf, z.B. Boden einer Vase).

#include "dreieck.h"
#include "material.h"
#include "vector3d.h"
#include <cmath>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace rotation
{
struct ProfilPunkt
{
    double r = 0, y = 0;
};

struct Normale2D
{
    double r = 0, y = 0;
};

inline std::vector<Dreieck> koerper(const std::vector<ProfilPunkt> &p, int segmente, bool glatt, const Material &mat,
                                    double knickGrad = 40.0)
{
    std::vector<Dreieck> d;
    const size_t n = p.size();
    if (n < 2 || segmente < 3)
        return d;

    // Normale je Profilstrecke: senkrecht zur Strecke
    std::vector<Normale2D> sn(n - 1);
    std::vector<bool> gueltig(n - 1, false);
    double richtung = 0; // > 0: Normalen zeigen nach aussen
    for (size_t k = 0; k + 1 < n; ++k)
    {
        const double dr = p[k + 1].r - p[k].r, dy = p[k + 1].y - p[k].y;
        const double l = std::sqrt(dr * dr + dy * dy);
        if (l < 1e-12)
            continue;
        sn[k] = {dy / l, -dr / l};
        gueltig[k] = true;
        // nach aussen = von der Achse weg, gewichtet mit der Flaeche, die die Strecke ueberstreicht
        richtung += sn[k].r * l * (p[k].r + p[k + 1].r) * 0.5;
    }
    if (richtung < 0)
        for (auto &m : sn)
            m = {-m.r, -m.y};

    // Normale am Punkt i fuer die Strecke k (k = i - 1 oder i)
    const double knickCos = std::cos(knickGrad * M_PI / 180.0);
    auto normaleAm = [&](size_t i, size_t k) -> Normale2D
    {
        Normale2D eigen = sn[k];
        if (!glatt)
            return eigen;
        const size_t andere = (k == i) ? (i >= 1 ? i - 1 : size_t(-1)) : (i + 1 < n ? i : size_t(-1));
        if (andere == size_t(-1) || andere >= sn.size() || !gueltig[andere])
            return eigen;
        const Normale2D o = sn[andere];
        if (eigen.r * o.r + eigen.y * o.y < knickCos)
            return eigen; // Knick: scharfe Kante
        Normale2D m{eigen.r + o.r, eigen.y + o.y};
        const double l = std::sqrt(m.r * m.r + m.y * m.y);
        return l > 1e-12 ? Normale2D{m.r / l, m.y / l} : eigen;
    };
    auto punkt = [&](const ProfilPunkt &q, double w) { return Vector3D(q.r * std::cos(w), q.y, q.r * std::sin(w)); };
    auto normale3D = [&](const Normale2D &m, double w)
    { return Vector3D(m.r * std::cos(w), m.y, m.r * std::sin(w)).normalized(); };

    for (size_t k = 0; k + 1 < n; ++k)
    {
        if (!gueltig[k])
            continue;
        const Normale2D n0 = normaleAm(k, k), n1 = normaleAm(k + 1, k);
        for (int j = 0; j < segmente; ++j)
        {
            const double w0 = 2.0 * M_PI * j / segmente, w1 = 2.0 * M_PI * (j + 1) / segmente;
            const Vector3D A = punkt(p[k], w0), B = punkt(p[k], w1), C = punkt(p[k + 1], w1), D = punkt(p[k + 1], w0);
            const Vector3D nA = normale3D(n0, w0), nB = normale3D(n0, w1), nC = normale3D(n1, w1), nD = normale3D(n1, w0);
            auto dazu = [&](Vector3D a, Vector3D b, Vector3D c, Vector3D na, Vector3D nb, Vector3D nc)
            {
                Vector3D g = (b - a).cross(c - a);
                if (g.length() < 1e-14)
                    return; // entartet (zwei Ecken auf der Achse)
                if (g * (na + nb + nc) < 0) // Umlaufsinn wie die Normalen (nach aussen)
                {
                    std::swap(b, c);
                    std::swap(nb, nc);
                }
                if (glatt)
                    d.emplace_back(a, b, c, na, nb, nc, mat);
                else
                    d.emplace_back(a, b, c, mat);
            };
            dazu(A, B, C, nA, nB, nC);
            dazu(A, C, D, nA, nC, nD);
        }
    }
    return d;
}
} // namespace rotation
