#pragma once
// Textur-Abbildung: Trefferpunkt -> Bildkoordinaten (u, v). Gemeinsam fuer Raytracer (texture.cpp) und
// GPU-Ansichten (gpu_ansichten.cpp), damit beide dasselbe zeigen.
//
// Ablauf:
//  1. Textur-Raum (texturRaum): Punkt q und Normale n in den Koordinaten, in denen projiziert wird, dazu die
//     Bezugsbox lmin..lmax. "Haftet am Objekt": bei Netzen ueber die Ruhelage der Dreiecksecken (dreieck.h),
//     bei Kugeln ueber die gemerkte Drehung (ausrichtung), bei Staeben ueber die Achse, bei Instanzen ueber die
//     Umkehrabbildung in die Vorlage. Sonst Weltkoordinaten und Welt-Box wie bisher.
//  2. Projektion (texturRoh): je nach Art Kugel, Ebene, UV, Zylinder, Wuerfel oder Wuerfel weich (triplanar,
//     drei Ebenen nach der Normale gemischt). Mit Kachelgroesse in Metern statt "einmal ueber die Box".
//  3. Nachbearbeitung (texturNach): Versatz, freie Drehung, 90-Grad-Schritte, Spiegeln.
// Nur als Header.

#include "affin.h"
#include "dreieck.h"
#include "instanz.h"
#include "kugel.h"
#include "object.h"
#include "rotations.h"
#include "stab.h"
#include "textur_param.h"
#include "vector3d.h"
#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

struct TexturRaum
{
    Vector3D q, n;      // Punkt und Normale im Textur-Raum
    Vector3D lmin, lmax; // Bezugsbox im Textur-Raum
    bool uvOk = false;  // fuer die Art UV: Bildkoordinaten aus dem Netz
    double uu = 0, vv = 0;
    double massstab = 1.0; // Meter in der Welt je Einheit im Textur-Raum (Skalierung seit dem Festmachen)
};

// bis zu drei Abtastungen (triplanar), Gewichte summieren sich zu 1
struct TexturProbe
{
    int n = 0;
    double u[3] = {0, 0, 0}, v[3] = {0, 0, 0}, w[3] = {0, 0, 0};
    double meterJeU = 1.0;  // Weltmeter je Bildbreite (fuer Mipmap-Stufe und Relief)
    double periodeU = 0.0;  // Kugel/Zylinder: Sprung von u an der Naht (fuer die Ansichten)
};

// ---------------------------------------------------------------------------------------------------------
// Ruhelage der Dreiecke (Textur haftet am Objekt)
// ---------------------------------------------------------------------------------------------------------

// Ruhelage aus der jetzigen Lage (optional ueber 'abb' umgerechnet, z.B. beim Laden die gespeicherte Abbildung)
inline void texturRuheSetzen(std::vector<Dreieck> &d, const Affin *abb = nullptr)
{
    if (d.empty())
        return;
    Vector3D mn(1e300, 1e300, 1e300), mx(-1e300, -1e300, -1e300);
    auto ecke = [&](const Dreieck &t, int k)
    {
        Vector3D p = (k == 0 ? t.a : k == 1 ? t.b : t.c) + t.position;
        return abb ? abb->punkt(p) : p;
    };
    for (const Dreieck &t : d)
        for (int k = 0; k < 3; ++k)
        {
            const Vector3D p = ecke(t, k);
            mn = Vector3D(std::min(mn.x, p.x), std::min(mn.y, p.y), std::min(mn.z, p.z));
            mx = Vector3D(std::max(mx.x, p.x), std::max(mx.y, p.y), std::max(mx.z, p.z));
        }
    const Vector3D c = (mn + mx) * 0.5;
    for (Dreieck &t : d)
    {
        for (int k = 0; k < 3; ++k)
        {
            const Vector3D p = ecke(t, k) - c;
            t.ruhe[k * 3 + 0] = float(p.x);
            t.ruhe[k * 3 + 1] = float(p.y);
            t.ruhe[k * 3 + 2] = float(p.z);
        }
        t.ruheGesetzt = true;
        t.texturBox = true;
        t.texturMin = mn - c;
        t.texturMax = mx - c;
    }
}

inline Vector3D ruheEcke(const Dreieck &t, int k)
{
    return Vector3D(t.ruhe[k * 3], t.ruhe[k * 3 + 1], t.ruhe[k * 3 + 2]);
}

// Abbildung jetzige Lage -> Ruhelage (kleinste Quadrate), zum Speichern; false ohne Ruhelage
inline bool texturRuheAbbildung(const std::vector<Dreieck> &d, Affin &abb)
{
    std::vector<Dreieck> ruhe;
    ruhe.reserve(d.size());
    for (const Dreieck &t : d)
    {
        if (!t.ruheGesetzt)
            return false;
        ruhe.emplace_back(ruheEcke(t, 0), ruheEcke(t, 1), ruheEcke(t, 2), t.mat);
    }
    std::vector<Dreieck> jetzt;
    jetzt.reserve(d.size());
    for (const Dreieck &t : d)
        jetzt.emplace_back(t.a + t.position, t.b + t.position, t.c + t.position, t.mat);
    return affinAnpassen({&jetzt}, {&ruhe}, abb);
}

// ---------------------------------------------------------------------------------------------------------
// 1. Textur-Raum
// ---------------------------------------------------------------------------------------------------------

inline Vector3D texturDrehT(const double R[9], const Vector3D &v) // R^T * v
{
    return Vector3D(R[0] * v.x + R[3] * v.y + R[6] * v.z, R[1] * v.x + R[4] * v.y + R[7] * v.z,
                    R[2] * v.x + R[5] * v.y + R[8] * v.z);
}

inline Vector3D texturNormiert(const Vector3D &v, const Vector3D &ersatz)
{
    const double l = v.length();
    return l > 1e-300 ? v * (1.0 / l) : ersatz;
}

// baryzentrische Gewichte von p im Dreieck abc (p wird in die Ebene projiziert)
inline void texturBary(const Vector3D &a, const Vector3D &b, const Vector3D &c, const Vector3D &p, double &w0,
                       double &w1, double &w2)
{
    const Vector3D v0 = b - a, v1 = c - a, v2 = p - a;
    const double d00 = v0 * v0, d01 = v0 * v1, d11 = v1 * v1, d20 = v2 * v0, d21 = v2 * v1;
    const double den = d00 * d11 - d01 * d01;
    if (!(std::fabs(den) > 1e-300))
    {
        w0 = 1, w1 = w2 = 0;
        return;
    }
    w1 = (d11 * d20 - d01 * d21) / den;
    w2 = (d00 * d21 - d01 * d20) / den;
    w0 = 1.0 - w1 - w2;
}

// Textur-Raum fuer einen Punkt p mit geometrischer Normale n (Welt) auf dem Objekt o
inline TexturRaum texturRaum(const object &o, const Vector3D &p, const Vector3D &n, double hitU = 0, double hitV = 0,
                             bool hitUv = false)
{
    TexturRaum r;
    const TexturParameter &par = o.texturPar();
    const bool fest = par.objektfest;
    r.q = p;
    r.n = n;
    r.lmin = o.texturBox ? o.texturMin : o.minBound;
    r.lmax = o.texturBox ? o.texturMax : o.maxBound;
    r.uvOk = hitUv;
    r.uu = hitU;
    r.vv = hitV;

    if (const auto *t = dynamic_cast<const Dreieck *>(&o))
    {
        const Vector3D A = t->a + t->position, B = t->b + t->position, C = t->c + t->position;
        double w0, w1, w2;
        texturBary(A, B, C, p, w0, w1, w2);
        r.uvOk = true;
        r.uu = w0 * t->uvA.x + w1 * t->uvB.x + w2 * t->uvC.x;
        r.vv = w0 * t->uvA.y + w1 * t->uvB.y + w2 * t->uvC.y;
        if (fest && t->ruheGesetzt)
        {
            const Vector3D R0 = ruheEcke(*t, 0), R1 = ruheEcke(*t, 1), R2 = ruheEcke(*t, 2);
            r.q = R0 * w0 + R1 * w1 + R2 * w2;
            const Vector3D nr = (R1 - R0).cross(R2 - R0);
            const Vector3D nw = (B - A).cross(C - A);
            // Normale der Ruhelage, auf derselben Seite wie die uebergebene (Flaechen-Vorderseite)
            Vector3D nn = texturNormiert(nr, n);
            if ((nw * n) < 0)
                nn = -nn;
            r.n = nn;
            const double fr = nr.length(), fw = nw.length();
            r.massstab = fr > 1e-300 ? std::sqrt(fw / fr) : 1.0;
            r.lmin = t->texturMin;
            r.lmax = t->texturMax;
        }
        return r;
    }
    if (const auto *k = dynamic_cast<const Kugel *>(&o))
    {
        const Vector3D d = p - k->position;
        r.q = fest ? texturDrehT(k->ausrichtung, d) : d;
        r.n = fest ? texturDrehT(k->ausrichtung, n) : n;
        r.lmin = Vector3D(-k->radius, -k->radius, -k->radius);
        r.lmax = Vector3D(k->radius, k->radius, k->radius);
        return r;
    }
    if (const auto *s = dynamic_cast<const Stab *>(&o))
    {
        if (!fest)
            return r;
        const Vector3D P1 = s->p1 + s->position, P2 = s->p2 + s->position;
        const Vector3D ey = texturNormiert(P2 - P1, Vector3D(0, 1, 0));
        const Vector3D hilf = std::fabs(ey.z) < 0.9 ? Vector3D(0, 0, 1) : Vector3D(1, 0, 0);
        const Vector3D ex = texturNormiert(ey.cross(hilf), Vector3D(1, 0, 0));
        const Vector3D ez = ex.cross(ey);
        const Vector3D d = p - P1;
        r.q = Vector3D(d * ex, d * ey, d * ez);
        r.n = Vector3D(n * ex, n * ey, n * ez);
        r.lmin = Vector3D(-s->radius, 0, -s->radius);
        r.lmax = Vector3D(s->radius, (P2 - P1).length(), s->radius);
        return r;
    }
    if (const auto *in = dynamic_cast<const Instanz *>(&o))
    {
        if (!fest || !in->vorlage)
            return r;
        r.q = in->inv.punkt(p);
        const auto &A = in->abb.m; // Normale in die Vorlage: A^T * n
        r.n = texturNormiert(Vector3D(A[0][0] * n.x + A[1][0] * n.y + A[2][0] * n.z,
                                      A[0][1] * n.x + A[1][1] * n.y + A[2][1] * n.z,
                                      A[0][2] * n.x + A[1][2] * n.y + A[2][2] * n.z),
                             n);
        Vector3D mn = in->vorlage->minBound, mx = in->vorlage->maxBound;
        in->vorlage->getWorldAABB(mn, mx);
        r.lmin = mn;
        r.lmax = mx;
        r.massstab = std::cbrt(std::max(1e-300, std::fabs(in->abb.determinante())));
        return r;
    }
    // andere Objekte: um die Boxmitte, mit der gemerkten Drehung
    if (fest)
    {
        const Vector3D c = (o.minBound + o.maxBound) * 0.5;
        r.q = texturDrehT(o.ausrichtung, p - c);
        r.n = texturDrehT(o.ausrichtung, n);
        Vector3D mn(1e300, 1e300, 1e300), mx(-1e300, -1e300, -1e300);
        for (int i = 0; i < 8; ++i)
        {
            const Vector3D e((i & 1) ? o.maxBound.x : o.minBound.x, (i & 2) ? o.maxBound.y : o.minBound.y,
                             (i & 4) ? o.maxBound.z : o.minBound.z);
            const Vector3D l = texturDrehT(o.ausrichtung, e - c);
            mn = Vector3D(std::min(mn.x, l.x), std::min(mn.y, l.y), std::min(mn.z, l.z));
            mx = Vector3D(std::max(mx.x, l.x), std::max(mx.y, l.y), std::max(mx.z, l.z));
        }
        r.lmin = mn;
        r.lmax = mx;
    }
    return r;
}

// ---------------------------------------------------------------------------------------------------------
// 2. Projektion
// ---------------------------------------------------------------------------------------------------------

// Drehung der Kugel-Abbildung zurueckrechnen (wie bisher: erst X, dann Y, dann Z, jeweils mit -Winkel)
inline Vector3D texturKugelDrehen(const Vector3D &d, const Vector3D &grad)
{
    if (grad.x == 0 && grad.y == 0 && grad.z == 0)
        return d;
    Vector3D r = rotateX(d, -grad.x * M_PI / 180.0);
    r = rotateY(r, -grad.y * M_PI / 180.0);
    return rotateZ(r, -grad.z * M_PI / 180.0);
}

// Rohe Bildkoordinaten (vor Versatz, Drehung, Spiegeln). einfach = true: triplanar wie Wuerfel (Ansichten)
inline TexturProbe texturRoh(TextureMode modus, const TexturParameter &par, const TexturRaum &r, bool einfach = false)
{
    TexturProbe pr;
    const Vector3D g = r.lmax - r.lmin;
    const Vector3D m = (r.lmin + r.lmax) * 0.5;
    const double k = par.kachel > 0 ? par.kachel / std::max(1e-12, r.massstab) : 0.0; // Kachel im Textur-Raum
    auto anteil = [](double wert, double von, double bereich) { return bereich > 1e-12 ? (wert - von) / bereich : 0.5; };
    // Strecke s (ab 'von') auf der Achse mit Laenge 'bereich': Kachel oder ganze Box
    auto achse = [&](double wert, double von, double bereich) { return k > 0 ? (wert - von) / k : anteil(wert, von, bereich); };
    const double meter = std::max(1e-12, r.massstab);
    auto eins = [&](double u, double v, double meterJeU)
    {
        pr.n = 1;
        pr.u[0] = u, pr.v[0] = v, pr.w[0] = 1.0;
        pr.meterJeU = meterJeU;
    };
    const double groessteSeite = std::max({g.x, g.y, g.z, 1e-12});

    // eine Wuerfelseite nach der Achse 0 (X), 1 (Y), 2 (Z) und dem Vorzeichen der Normale
    auto seite = [&](int ax, double vorzeichen, double &u, double &v)
    {
        const Vector3D &p = r.q;
        if (ax == 1) // oben / unten
        {
            u = achse(p.x, r.lmin.x, g.x);
            const double z = achse(p.z, r.lmin.z, g.z);
            v = vorzeichen > 0 ? (k > 0 ? -z : 1.0 - z) : z;
        }
        else if (ax == 2) // vorne (-Z) / hinten (+Z)
        {
            const double x = achse(p.x, r.lmin.x, g.x);
            u = vorzeichen < 0 ? x : (k > 0 ? -x : 1.0 - x);
            v = achse(p.y, r.lmin.y, g.y);
        }
        else // links (-X) / rechts (+X)
        {
            const double z = achse(p.z, r.lmin.z, g.z);
            u = vorzeichen < 0 ? (k > 0 ? -z : 1.0 - z) : z;
            v = achse(p.y, r.lmin.y, g.y);
        }
    };

    switch (modus)
    {
    case TextureMode::SPHERE:
    {
        Vector3D d = texturNormiert(r.q - m, Vector3D(0, 0, -1));
        d = texturKugelDrehen(d, par.kugelDrehung);
        const double phi = std::atan2(d.x, d.z);
        const double theta = std::asin(std::clamp(d.y, -1.0, 1.0));
        double u = 0.5 - phi / (2.0 * M_PI), v = 0.5 + theta / M_PI;
        const double radius = std::max(1e-12, 0.5 * groessteSeite);
        double nu = 1.0, nv = 1.0;
        if (k > 0)
        {
            nu = std::max(1.0, std::round(2.0 * M_PI * radius / k)); // ganze Zahl: keine Naht
            nv = std::max(1.0, std::round(M_PI * radius / k));
        }
        eins(u * nu, v * nv, 2.0 * M_PI * radius * meter / nu);
        pr.periodeU = nu;
        return pr;
    }
    case TextureMode::PLANAR:
    {
        double u, v, laenge;
        if (g.y <= g.x && g.y <= g.z)
        {
            u = achse(r.q.x, r.lmin.x, g.x);
            const double z = achse(r.q.z, r.lmin.z, g.z);
            v = k > 0 ? -z : 1.0 - z; // Boden: oberes Bildende bei -Z
            laenge = g.x;
        }
        else if (g.z <= g.x)
        {
            u = achse(r.q.x, r.lmin.x, g.x);
            v = achse(r.q.y, r.lmin.y, g.y);
            laenge = g.x;
        }
        else
        {
            const double z = achse(r.q.z, r.lmin.z, g.z);
            u = k > 0 ? -z : 1.0 - z;
            v = achse(r.q.y, r.lmin.y, g.y);
            laenge = g.z;
        }
        eins(u, v, (k > 0 ? k : laenge) * meter);
        return pr;
    }
    case TextureMode::UV:
        eins(r.uvOk ? r.uu : 0.0, r.uvOk ? r.vv : 0.0, groessteSeite * meter);
        return pr;
    case TextureMode::CYLINDER:
    {
        const Vector3D p = r.q - m;
        const double radius = std::max(1e-12, 0.5 * std::max(g.x, g.z));
        const double eps = std::max(1e-4, g.y * 1e-4);
        const bool deckel = std::fabs(r.q.y - r.lmax.y) < eps || std::fabs(r.q.y - r.lmin.y) < eps ||
                            std::fabs(r.n.y) > 0.9;
        if (deckel)
        {
            if (k > 0)
                eins(p.x / k, p.z / k, k * meter);
            else
                eins(0.5 + p.x / (2 * radius), 0.5 + p.z / (2 * radius), 2 * radius * meter);
            return pr;
        }
        const double phi = std::atan2(p.x, p.z);
        double u = 0.5 - phi / (2 * M_PI);
        double nu = 1.0;
        if (k > 0)
            nu = std::max(1.0, std::round(2.0 * M_PI * radius / k));
        eins(u * nu, achse(r.q.y, r.lmin.y, g.y), 2.0 * M_PI * radius * meter / nu);
        pr.periodeU = nu;
        return pr;
    }
    case TextureMode::BOX:
    case TextureMode::TRIPLANAR:
    {
        const double ax = std::fabs(r.n.x), ay = std::fabs(r.n.y), az = std::fabs(r.n.z);
        const double meterJeU = (k > 0 ? k : groessteSeite) * meter;
        if (modus == TextureMode::BOX || einfach)
        {
            double u, v;
            if (ay >= ax && ay >= az)
                seite(1, r.n.y, u, v);
            else if (az >= ax)
                seite(2, r.n.z, u, v);
            else
                seite(0, r.n.x, u, v);
            eins(u, v, meterJeU);
            return pr;
        }
        // weich: drei Seiten, Gewicht ~ |n|^4 (schmale Uebergaenge an den Kanten)
        double w[3] = {ax * ax * ax * ax, ay * ay * ay * ay, az * az * az * az};
        const double summe = std::max(1e-300, w[0] + w[1] + w[2]);
        const double vz[3] = {r.n.x, r.n.y, r.n.z};
        pr.n = 0;
        for (int i = 0; i < 3; ++i)
        {
            if (w[i] / summe < 1e-3)
                continue;
            double u, v;
            seite(i, vz[i], u, v);
            pr.u[pr.n] = u, pr.v[pr.n] = v, pr.w[pr.n] = w[i];
            ++pr.n;
        }
        double s = 0;
        for (int i = 0; i < pr.n; ++i)
            s += pr.w[i];
        for (int i = 0; i < pr.n; ++i)
            pr.w[i] /= s;
        pr.meterJeU = meterJeU;
        return pr;
    }
    default:
        return pr; // keine Abbildung
    }
}

// ---------------------------------------------------------------------------------------------------------
// 3. Nachbearbeitung: Versatz, freie Drehung (um die Bildmitte), 90-Grad-Schritte, Spiegeln
// ---------------------------------------------------------------------------------------------------------
inline void texturNach(const object &o, const TexturParameter &par, double &u, double &v)
{
    u += par.versatzU;
    v += par.versatzV;
    if (std::fabs(par.winkel) > 1e-12)
    {
        const double a = par.winkel * M_PI / 180.0, c = std::cos(a), s = std::sin(a);
        const double du = u - 0.5, dv = v - 0.5;
        u = 0.5 + c * du - s * dv;
        v = 0.5 + s * du + c * dv;
    }
    for (int k = 0; k < (o.texturDrehung & 3); ++k)
    {
        const double t = u;
        u = v;
        v = 1.0 - t;
    }
    if (o.texturSpiegelU)
        u = 1.0 - u;
    if (o.texturSpiegelV)
        v = 1.0 - v;
}

// alles zusammen: Abtastungen fuer den Punkt p (Welt) mit geometrischer Normale n
inline TexturProbe texturAbbilden(const object &o, const Vector3D &p, const Vector3D &n, double hitU = 0,
                                  double hitV = 0, bool hitUv = false, bool einfach = false)
{
    const TexturParameter &par = o.texturPar();
    TexturProbe pr = texturRoh(o.textureMode, par, texturRaum(o, p, n, hitU, hitV, hitUv), einfach);
    for (int i = 0; i < pr.n; ++i)
        texturNach(o, par, pr.u[i], pr.v[i]);
    return pr;
}

// Einstellungen eines Objekts aendern: Kopie anlegen, aendern, setzen (Netze: auf die Dreiecke weitergeben)
template <class F> inline void texturParAendern(object *o, F &&aendern)
{
    auto neu = std::make_shared<TexturParameter>(o->texturPar());
    aendern(*neu);
    o->texPar = neu;
}
