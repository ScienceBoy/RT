#pragma once
// Regen, Wellenringe auf Wasser und nasse Flaechen (nur im gerenderten Bild).
//
// Regen: keine Objekte, sondern Streifen, die der Kamerastrahl im Raum findet. Der Raum ist in Wuerfel der Kantenlaenge
// 'abstand' geteilt; in jedem steckt (mit Wahrscheinlichkeit 'dichte') ein fallender Tropfen. Lage und Takt kommen aus
// einer Zufallszahl der Zellnummer (kein Speicher, immer gleich). Ein Tropfen ist ein duenner Strich der Laenge
// 'laenge' (Bewegungsunschaerfe: Weg in der Belichtungszeit), schraeg nach dem Wind, und faellt mit 'geschwindigkeit':
//     y(t) = Zellboden + L/2 + frac(phase - v t / abstand) * (abstand - L)
// Jeder Durchlauf durch die Zelle ist ein neuer Tropfen (andere Lage). Der Strahl laeuft Zelle fuer Zelle (wie beim
// Gitter-Durchlauf) bis zum ersten Objekt bzw. bis 'sichtweite'; je Zelle wird der Abstand Strahl - Strich gerechnet.
// Duenner als ein Bildpunkt: breiter, dafuer blasser (gleiche Menge Licht, kein Flimmern).
//
// Ringe: Auf Flaechen mit "Regenringe" > 0 (Wasser) schlagen Tropfen ein: Raster 'ringAbstand' auf der Flaeche (x, z),
// je Zelle ein Einschlag je 'ringTakt' Sekunden. Hoehe eines Rings (r = Abstand zum Einschlag, tau = Alter):
//     h = A exp(-tau/T) exp(-(r - c tau)^2 / w^2) cos(k (r - c tau))
// Nur die Normale wird gekippt (Gradient von h); Spiegelung und Brechung zeigen die Ringe dann von selbst.
// In den ersten Hundertstelsekunden: ein heller Spritzer an der Einschlagstelle.
//
// Nass: Flaechen werden dunkler (Wasser im Material) und glaenzender (Wasserfilm).
// Nur als Header (testbar ohne Oberflaeche).

#include "vector3d.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace regen
{
struct Einstellungen
{
    bool an = false;
    double dichte = 0.35;         // Anteil der Zellen mit einem Tropfen (0..1)
    double abstand = 0.25;        // Zellgroesse (Einheiten): mittlerer Abstand der Tropfen
    double geschwindigkeit = 7.0; // Fallgeschwindigkeit (Einheiten/s)
    double laenge = 0.12;         // Laenge eines Strichs (Einheiten)
    double dicke = 0.002;         // Radius eines Strichs (Einheiten)
    double helligkeit = 0.6;      // Helligkeit der Striche (Anteil Himmelsfarbe)
    double deckkraft = 0.5;       // wie stark ein getroffener Strich deckt
    double windX = 0.0, windZ = 0.0; // Wind (Einheiten/s): Striche schraeg
    double sichtweite = 25.0;     // weiter weg keine Striche (Einheiten)
    // Ringe
    double ringAbstand = 0.3; // Raster der Einschlaege (Einheiten)
    double ringTakt = 0.9;    // Sekunden zwischen zwei Einschlaegen je Zelle
    double ringGroesse = 1.0; // Massstab der Ringe (1 = Meter-Szene)
    bool spritzer = true;
    // Naesse
    bool allesNass = true;  // bei Regen: alle Flaechen nass
    double nassStaerke = 0.7;
};
inline Einstellungen gRegen;

// ---------- Zufall aus Zellnummern ----------
inline uint64_t mischen(uint64_t x)
{
    x ^= x >> 33;
    x *= 0xff51afd7ed558ccdULL;
    x ^= x >> 33;
    x *= 0xc4ceb9fe1a85ec53ULL;
    x ^= x >> 33;
    return x;
}
inline double zufall(int64_t a, int64_t b, int64_t c, int64_t d, uint64_t salz)
{
    uint64_t h = mischen(uint64_t(a) * 0x9E3779B97F4A7C15ULL ^ salz);
    h = mischen(h ^ uint64_t(b) * 0xC2B2AE3D27D4EB4FULL);
    h = mischen(h ^ uint64_t(c) * 0x165667B19E3779F9ULL);
    h = mischen(h ^ uint64_t(d) * 0x27D4EB2F165667C5ULL);
    return double(h >> 11) * (1.0 / 9007199254740992.0); // [0, 1)
}

// kleinster Abstand zwischen Strahlstueck o + s d (s in [s0, s1], d normiert) und Strecke a..b; s: Stelle am Strahl
inline double strahlStrecke(const Vector3D &o, const Vector3D &d, double s0, double s1, const Vector3D &a, const Vector3D &b,
                            double &s)
{
    const Vector3D e = b - a, w = o - a;
    const double ee = e * e, de = d * e, dw = d * w, ew = e * w;
    const double nenner = ee - de * de; // |d| = 1
    double u = 0.0;
    if (ee < 1e-18)
        s = -dw;
    else if (std::fabs(nenner) < 1e-12) // parallel
        s = -dw;
    else
        s = (de * ew - ee * dw) / nenner;
    s = std::clamp(s, s0, s1);
    if (ee > 1e-18)
        u = std::clamp(((o + d * s) - a) * e / ee, 0.0, 1.0);
    // nach dem Begrenzen von u das s noch einmal passend waehlen
    const Vector3D q = a + e * u;
    s = std::clamp((q - o) * d, s0, s1);
    return ((o + d * s) - q).length();
}

// Regen entlang des Strahls bis tMax vor die Farbe 'hinten' legen. himmel(richtung) -> Farbe des Himmels.
// pixelWinkel: Oeffnungswinkel eines Bildpunkts (rad), fuer die Mindestbreite der Striche.
template <class Farbe, class Himmel>
Farbe entlang(const Vector3D &ursprung, const Vector3D &richtung, double tMax, const Farbe &hinten, double zeit,
              double pixelWinkel, Himmel himmel)
{
    const Einstellungen &g = gRegen;
    const double laengeR = richtung.length();
    if (!g.an || g.dichte <= 0.0 || g.abstand <= 1e-6 || !(laengeR > 1e-12))
        return hinten;
    const Vector3D d = richtung * (1.0 / laengeR);
    const double ende = std::min(std::isfinite(tMax) ? tMax * laengeR : 1e30, g.sichtweite);
    if (ende <= 0.0)
        return hinten;
    const double Z = g.abstand;
    const double L = std::min(g.laenge, 0.8 * Z);
    const double nah = std::max(0.4, 2.0 * Z); // naeher an der Kamera keine Tropfen
    // Fallrichtung (mit Wind) und Farbe der Striche
    Vector3D fall(g.windX, -std::max(0.1, g.geschwindigkeit), g.windZ);
    fall = fall.normalized();
    const Farbe tropfen = himmel(Vector3D(d.x, std::fabs(d.y) + 0.2, d.z).normalized()) * g.helligkeit;

    // Gitter-Durchlauf (Amanatides/Woo)
    int64_t ix = int64_t(std::floor(ursprung.x / Z)), iy = int64_t(std::floor(ursprung.y / Z)),
            iz = int64_t(std::floor(ursprung.z / Z));
    const int sx = d.x > 0 ? 1 : -1, sy = d.y > 0 ? 1 : -1, sz = d.z > 0 ? 1 : -1;
    auto ersteGrenze = [&](double p, double r, int64_t i, int s) -> double
    {
        if (std::fabs(r) < 1e-12)
            return 1e30;
        const double grenze = (s > 0 ? double(i + 1) : double(i)) * Z;
        return (grenze - p) / r;
    };
    double tx = ersteGrenze(ursprung.x, d.x, ix, sx), ty = ersteGrenze(ursprung.y, d.y, iy, sy),
           tz = ersteGrenze(ursprung.z, d.z, iz, sz);
    const double dtx = std::fabs(d.x) < 1e-12 ? 1e30 : Z / std::fabs(d.x);
    const double dty = std::fabs(d.y) < 1e-12 ? 1e30 : Z / std::fabs(d.y);
    const double dtz = std::fabs(d.z) < 1e-12 ? 1e30 : Z / std::fabs(d.z);

    Farbe summe = hinten * 0.0;
    double durch = 1.0; // Anteil, der noch durchkommt
    double s0 = 0.0;
    const double fallZellen = zeit * g.geschwindigkeit / std::max(1e-6, Z - L);
    for (int schritt = 0; schritt < 4000 && s0 < ende && durch > 0.02; ++schritt)
    {
        const double s1 = std::min({tx, ty, tz, ende});
        // Tropfen dieser Zelle (neu bei jedem Durchlauf n)
        const double phase = zufall(ix, iy, iz, 0, 11) - fallZellen;
        const double n = std::floor(phase);
        const double frac = phase - n;
        if (zufall(ix, iy, iz, int64_t(n), 23) < g.dichte)
        {
            const double rand = 0.1 * Z + std::fabs(fall.x) * L + std::fabs(fall.z) * L;
            const double hx = zufall(ix, iy, iz, int64_t(n), 37), hz = zufall(ix, iy, iz, int64_t(n), 41);
            const Vector3D mitte((double(ix) * Z) + rand + hx * std::max(0.0, Z - 2 * rand),
                                 double(iy) * Z + 0.5 * L + frac * (Z - L),
                                 (double(iz) * Z) + rand + hz * std::max(0.0, Z - 2 * rand));
            const Vector3D a = mitte - fall * (0.5 * L), b = mitte + fall * (0.5 * L);
            double s = 0.0;
            double abst = strahlStrecke(ursprung, d, std::max(s0, nah), std::max(s1, nah), a, b, s);
            if (s1 <= nah)
                abst = 1e30; // Tropfen direkt vor der Kamera: wuerden als dicke Striche das Bild durchziehen
            const double radius = g.dicke * (0.6 + 0.8 * zufall(ix, iy, iz, int64_t(n), 53));
            const double breit = std::max(radius, 0.6 * s * pixelWinkel); // mindestens etwa ein halber Bildpunkt
            if (abst < breit)
            {
                const double q = abst / breit;
                const double alpha = std::clamp(g.deckkraft * (radius / breit) * (1.0 - q * q), 0.0, 1.0);
                summe = summe + tropfen * (durch * alpha);
                durch *= 1.0 - alpha;
            }
        }
        if (s1 >= ende)
            break;
        s0 = s1;
        if (tx <= ty && tx <= tz)
        {
            ix += sx;
            tx += dtx;
        }
        else if (ty <= tz)
        {
            iy += sy;
            ty += dty;
        }
        else
        {
            iz += sz;
            tz += dtz;
        }
    }
    return summe + hinten * durch;
}

// Wellenringe an (x, z): Steigung (gx, gz) der Wasserhoehe; Rueckgabe: Helligkeit eines Spritzers (0..1)
inline double ringe(double x, double z, double zeit, double staerke, double &gx, double &gz)
{
    gx = gz = 0.0;
    const Einstellungen &g = gRegen;
    if (staerke <= 0.0 || g.ringAbstand <= 1e-6 || g.ringTakt <= 1e-6)
        return 0.0;
    const double R = g.ringAbstand, P = g.ringTakt, m = std::max(1e-3, g.ringGroesse);
    const double c = 0.25 * m;                    // Ausbreitung (Einheiten/s)
    const double T = 0.55;                        // Abklingen (s)
    const double w = 0.035 * m;                   // Breite des Wellenzugs
    const double k = 2.0 * 3.14159265358979 / (0.045 * m); // Wellenzahl
    const double A = 0.35 * staerke / k;          // groesste Steigung etwa 0,35 * Staerke
    const int64_t cx = int64_t(std::floor(x / R)), cz = int64_t(std::floor(z / R));
    const double dichte = std::clamp(0.4 + g.dichte, 0.0, 1.0);
    double spritz = 0.0;
    for (int64_t ax = cx - 1; ax <= cx + 1; ++ax)
        for (int64_t az = cz - 1; az <= cz + 1; ++az)
        {
            const double s = zeit / P + zufall(ax, az, 0, 0, 101);
            const double n = std::floor(s);
            for (int alt = 0; alt < 2; ++alt) // dieser und der vorige Einschlag
            {
                const int64_t nr = int64_t(n) - alt;
                const double tau = (s - double(nr)) * P;
                if (zufall(ax, az, nr, 1, 103) >= dichte)
                    continue;
                const double px = (double(ax) + zufall(ax, az, nr, 2, 107)) * R;
                const double pz = (double(az) + zufall(ax, az, nr, 3, 109)) * R;
                const double dx = x - px, dz = z - pz;
                const double r = std::sqrt(dx * dx + dz * dz);
                if (g.spritzer && tau < 0.09)
                {
                    const double rs = 0.012 * m;
                    spritz = std::max(spritz, (1.0 - tau / 0.09) * std::exp(-(r * r) / (rs * rs)));
                }
                const double u = r - c * tau;
                if (std::fabs(u) > 3.0 * w || r < 1e-9)
                    continue;
                const double huelle = A * std::exp(-tau / T) * std::exp(-(u * u) / (w * w));
                const double dhdr = huelle * (-2.0 * u / (w * w) * std::cos(k * u) - k * std::sin(k * u));
                gx += dhdr * dx / r;
                gz += dhdr * dz / r;
            }
        }
    return std::min(1.0, spritz * staerke);
}

// Normale mit Ringen (nur nach oben zeigende Flaechen); spritz: Helligkeit eines Spritzers
inline Vector3D ringNormale(const Vector3D &p, const Vector3D &N, double zeit, double staerke, double &spritz)
{
    spritz = 0.0;
    if (N.y < 0.3)
        return N;
    double gx, gz;
    spritz = ringe(p.x, p.z, zeit, staerke, gx, gz) * std::clamp((N.y - 0.3) / 0.4, 0.0, 1.0);
    return Vector3D(N.x - gx, N.y, N.z - gz).normalized();
}
} // namespace regen
