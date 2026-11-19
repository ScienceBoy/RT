#pragma once
// Zusaetzliche Grundkoerper als Dreiecke (fuer Mesh): Ringe, Pyramide, Zylinder, Tetraeder, Oktaeder,
// Kreisflaeche, Kegel, Torus, Kapsel, Rohr. Alle um den Ursprung erzeugt, Achse = Y (senkrecht);
// verschoben wird danach. Runde Flaechen mit Vertex-Normalen (glatte Schattierung), Kanten und
// ebene Flaechen mit Flaechennormalen.

#include "dreieck.h"
#include "material.h"
#include <algorithm>
#include <cmath>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace formen
{
struct Netz
{
    const Material &mat;
    std::vector<Dreieck> d;

    explicit Netz(const Material &m) : mat(m) {}

    // ebenes Dreieck (Normale aus der Reihenfolge: gegen den Uhrzeigersinn von aussen gesehen)
    void flach(const Vector3D &a, const Vector3D &b, const Vector3D &c)
    {
        Vector3D n = (b - a).cross(c - a);
        if (n.length() < 1e-300)
            return; // entartet (z.B. Pol einer Kugel)
        n = n.normalized();
        d.emplace_back(a, b, c, n, n, n, mat);
    }
    void viereck(const Vector3D &a, const Vector3D &b, const Vector3D &c, const Vector3D &e)
    {
        flach(a, b, c);
        flach(a, c, e);
    }
    // gewoelbtes Dreieck mit Normalen je Ecke
    void glatt(const Vector3D &a, const Vector3D &b, const Vector3D &c, const Vector3D &na, const Vector3D &nb,
               const Vector3D &nc)
    {
        if ((b - a).cross(c - a).length() < 1e-300)
            return;
        d.emplace_back(a, b, c, na.normalized(), nb.normalized(), nc.normalized(), mat);
    }
};

inline Vector3D kreis(double winkel, double r, double y) { return Vector3D(r * std::cos(winkel), y, r * std::sin(winkel)); }
inline double winkel(int i, int n) { return 2.0 * M_PI * double(i) / double(n); }

// Kreisflaeche in der Hoehe y; oben = Normale nach +Y
inline void scheibe(Netz &n, double r, double y, int seg, bool oben)
{
    Vector3D m(0, y, 0);
    for (int i = 0; i < seg; ++i)
    {
        Vector3D a = kreis(winkel(i, seg), r, y), b = kreis(winkel(i + 1, seg), r, y);
        if (oben)
            n.flach(m, b, a);
        else
            n.flach(m, a, b);
    }
}

// Kreisring (flach) in der Hoehe y zwischen ri und ra
inline void ringflaeche(Netz &n, double ri, double ra, double y, int seg, bool oben)
{
    for (int i = 0; i < seg; ++i)
    {
        double w0 = winkel(i, seg), w1 = winkel(i + 1, seg);
        Vector3D i0 = kreis(w0, ri, y), i1 = kreis(w1, ri, y), a0 = kreis(w0, ra, y), a1 = kreis(w1, ra, y);
        if (oben)
            n.viereck(i0, i1, a1, a0);
        else
            n.viereck(i0, a0, a1, i1);
    }
}

// Mantel eines Zylinders (Radius r, von y0 bis y1); aussen = Normalen nach aussen
inline void mantel(Netz &n, double r, double y0, double y1, int seg, bool aussen)
{
    for (int i = 0; i < seg; ++i)
    {
        double w0 = winkel(i, seg), w1 = winkel(i + 1, seg);
        Vector3D n0(std::cos(w0), 0, std::sin(w0)), n1(std::cos(w1), 0, std::sin(w1));
        Vector3D u0 = kreis(w0, r, y0), u1 = kreis(w1, r, y0), o0 = kreis(w0, r, y1), o1 = kreis(w1, r, y1);
        if (aussen)
        {
            n.glatt(u0, o0, o1, n0, n0, n1);
            n.glatt(u0, o1, u1, n0, n1, n1);
        }
        else
        {
            n.glatt(u0, o1, o0, -n0, -n1, -n0);
            n.glatt(u0, u1, o1, -n0, -n1, -n1);
        }
    }
}

// ---------------- Koerper ----------------

// Zylinder, mittig um den Ursprung
inline std::vector<Dreieck> zylinder(double r, double h, int seg, bool deckel, const Material &mat)
{
    Netz n(mat);
    mantel(n, r, -h / 2, h / 2, seg, true);
    if (deckel)
    {
        scheibe(n, r, h / 2, seg, true);
        scheibe(n, r, -h / 2, seg, false);
    }
    return n.d;
}

// Hohlzylinder (3D-Ring, Rohr): innen ri, aussen ra, Hoehe h
inline std::vector<Dreieck> hohlzylinder(double ri, double ra, double h, int seg, const Material &mat)
{
    Netz n(mat);
    mantel(n, ra, -h / 2, h / 2, seg, true);
    mantel(n, ri, -h / 2, h / 2, seg, false);
    ringflaeche(n, ri, ra, h / 2, seg, true);
    ringflaeche(n, ri, ra, -h / 2, seg, false);
    return n.d;
}

// 2D-Ring: flacher Kreisring, waagrecht
inline std::vector<Dreieck> ring2d(double ri, double ra, int seg, const Material &mat)
{
    Netz n(mat);
    ringflaeche(n, ri, ra, 0.0, seg, true);
    return n.d;
}

// Kreisflaeche, waagrecht
inline std::vector<Dreieck> kreisflaeche(double r, int seg, const Material &mat)
{
    Netz n(mat);
    scheibe(n, r, 0.0, seg, true);
    return n.d;
}

// Kegel: Grundflaeche Radius r, Hoehe h (Spitze oben), mittig um den Ursprung
inline std::vector<Dreieck> kegel(double r, double h, int seg, bool boden, const Material &mat)
{
    Netz n(mat);
    const double y0 = -h / 2, y1 = h / 2;
    const double neigung = r / std::max(h, 1e-300); // Normale: (cos, r/h, sin) normiert
    Vector3D spitze(0, y1, 0);
    for (int i = 0; i < seg; ++i)
    {
        double w0 = winkel(i, seg), w1 = winkel(i + 1, seg), wm = 0.5 * (w0 + w1);
        Vector3D n0(std::cos(w0), neigung, std::sin(w0)), n1(std::cos(w1), neigung, std::sin(w1));
        Vector3D nm(std::cos(wm), neigung, std::sin(wm)); // an der Spitze: Mitte des Segments
        n.glatt(kreis(w0, r, y0), spitze, kreis(w1, r, y0), n0, nm, n1);
    }
    if (boden)
        scheibe(n, r, y0, seg, false);
    return n.d;
}

// Pyramide mit quadratischer Grundflaeche (Kante a), Hoehe h, mittig um den Ursprung
inline std::vector<Dreieck> pyramide(double a, double h, const Material &mat)
{
    Netz n(mat);
    const double k = a / 2, y0 = -h / 2;
    Vector3D p00(-k, y0, -k), p10(k, y0, -k), p11(k, y0, k), p01(-k, y0, k), s(0, h / 2, 0);
    n.flach(p00, s, p10);
    n.flach(p10, s, p11);
    n.flach(p11, s, p01);
    n.flach(p01, s, p00);
    n.viereck(p00, p10, p11, p01); // Boden (Normale nach unten)
    return n.d;
}

// regelmaessiger Tetraeder mit Kantenlaenge a, Grundflaeche waagrecht, Schwerpunkt im Ursprung
inline std::vector<Dreieck> tetraeder(double a, const Material &mat)
{
    Netz n(mat);
    const double ru = a / std::sqrt(3.0);        // Umkreis der Grundflaeche
    const double h = a * std::sqrt(2.0 / 3.0);   // Hoehe
    const double y0 = -h / 4, y1 = 3 * h / 4;    // Schwerpunkt bei 1/4 der Hoehe
    Vector3D b0 = kreis(M_PI / 2, ru, y0), b1 = kreis(M_PI / 2 + 2 * M_PI / 3, ru, y0),
             b2 = kreis(M_PI / 2 + 4 * M_PI / 3, ru, y0), s(0, y1, 0);
    // jede Flaeche so ausrichten, dass ihre Normale vom Schwerpunkt (Ursprung) weg zeigt
    auto aussen = [&](const Vector3D &p, const Vector3D &q, const Vector3D &r)
    {
        Vector3D nor = (q - p).cross(r - p);
        if (nor * (p + q + r) < 0)
            n.flach(p, r, q);
        else
            n.flach(p, q, r);
    };
    aussen(b0, b1, b2);
    aussen(b0, b1, s);
    aussen(b1, b2, s);
    aussen(b2, b0, s);
    return n.d;
}

// Oktaeder: Ecken im Abstand r vom Mittelpunkt (auf den Achsen)
inline std::vector<Dreieck> oktaeder(double r, const Material &mat)
{
    Netz n(mat);
    Vector3D o(0, r, 0), u(0, -r, 0);
    Vector3D ring[4] = {Vector3D(r, 0, 0), Vector3D(0, 0, r), Vector3D(-r, 0, 0), Vector3D(0, 0, -r)};
    for (int i = 0; i < 4; ++i)
    {
        Vector3D a = ring[i], b = ring[(i + 1) % 4];
        n.flach(a, o, b);
        n.flach(a, b, u);
    }
    return n.d;
}

// Torus: Mittelkreis Radius R (waagrecht), Rohrradius r
inline std::vector<Dreieck> torus(double R, double r, int segRing, int segRohr, const Material &mat)
{
    Netz n(mat);
    auto punkt = [&](int i, int j, Vector3D &p, Vector3D &nor)
    {
        double u = winkel(i, segRing), v = winkel(j, segRohr);
        Vector3D aussen(std::cos(u), 0, std::sin(u));
        nor = aussen * std::cos(v) + Vector3D(0, std::sin(v), 0);
        p = aussen * R + nor * r;
    };
    for (int i = 0; i < segRing; ++i)
        for (int j = 0; j < segRohr; ++j)
        {
            Vector3D p00, p10, p01, p11, n00, n10, n01, n11;
            punkt(i, j, p00, n00);
            punkt(i + 1, j, p10, n10);
            punkt(i, j + 1, p01, n01);
            punkt(i + 1, j + 1, p11, n11);
            n.glatt(p00, p01, p11, n00, n01, n11);
            n.glatt(p00, p11, p10, n00, n11, n10);
        }
    return n.d;
}

// Kapsel: Zylinder (Laenge l zwischen den Halbkugel-Mittelpunkten) mit Halbkugeln, Radius r, Achse Y
inline std::vector<Dreieck> kapsel(double r, double l, int seg, const Material &mat)
{
    Netz n(mat);
    const int breiten = std::max(2, seg / 4); // Breitenkreise je Halbkugel
    mantel(n, r, -l / 2, l / 2, seg, true);
    for (int seite = -1; seite <= 1; seite += 2) // unten, oben
    {
        const double my = seite * l / 2;
        for (int b = 0; b < breiten; ++b)
        {
            double t0 = 0.5 * M_PI * b / breiten, t1 = 0.5 * M_PI * (b + 1) / breiten; // 0 = Aequator
            for (int i = 0; i < seg; ++i)
            {
                double w0 = winkel(i, seg), w1 = winkel(i + 1, seg);
                auto nor = [&](double w, double t)
                { return Vector3D(std::cos(w) * std::cos(t), seite * std::sin(t), std::sin(w) * std::cos(t)); };
                Vector3D n00 = nor(w0, t0), n10 = nor(w1, t0), n01 = nor(w0, t1), n11 = nor(w1, t1);
                Vector3D m(0, my, 0);
                Vector3D p00 = m + n00 * r, p10 = m + n10 * r, p01 = m + n01 * r, p11 = m + n11 * r;
                if (seite > 0)
                {
                    n.glatt(p00, p01, p11, n00, n01, n11);
                    n.glatt(p00, p11, p10, n00, n11, n10);
                }
                else
                {
                    n.glatt(p00, p11, p01, n00, n11, n01);
                    n.glatt(p00, p10, p11, n00, n10, n11);
                }
            }
        }
    }
    return n.d;
}

// alle Dreiecke verschieben bzw. Achse Y -> X drehen (liegendes Rohr)
inline void verschieben(std::vector<Dreieck> &d, const Vector3D &v)
{
    for (auto &t : d)
    {
        t.a += v, t.b += v, t.c += v;
        t.rebuild();
    }
}
inline void liegendInX(std::vector<Dreieck> &d)
{
    auto dreh = [](const Vector3D &p) { return Vector3D(p.y, -p.x, p.z); }; // Y-Achse -> X-Achse
    for (auto &t : d)
    {
        t.a = dreh(t.a), t.b = dreh(t.b), t.c = dreh(t.c);
        t.nA = dreh(t.nA), t.nB = dreh(t.nB), t.nC = dreh(t.nC);
        t.rebuild();
    }
}
} // namespace formen
