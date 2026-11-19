#pragma once
// Drehen von Objekten um Weltachsen und Drehwinkel fuer die Anzeige.
//
// Die Objekte drehen sich mit rotate(Grad um X, dann Y, dann Z) selbst um die Mitte ihrer
// Bounding-Box. Sie merken sich aber keine Ausrichtung (ein Mesh dreht einfach seine Dreiecke).
// Deshalb fuehrt object::ausrichtung die Drehung seit dem Erstellen/Laden mit, solange ueber
// diese Funktionen gedreht wird (Maus, Ziffernblock, Eingabefelder).
//
// Winkel: Grad, Rechte-Hand-Regel, angewandt erst um X, dann Y, dann Z (R = Rz * Ry * Rx).

#include "object.h"
#include "rotations.h"
#include "vector3d.h"
#include <algorithm>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

inline Vector3D drehAchse(int achse)
{
    return achse == 0 ? Vector3D(1, 0, 0) : achse == 1 ? Vector3D(0, 1, 0) : Vector3D(0, 0, 1);
}

// v um die Achse k um 'grad' drehen (Rechte-Hand-Regel)
inline Vector3D drehenUmAchse(const Vector3D &v, const Vector3D &k, double grad)
{
    double a = grad * M_PI / 180.0;
    return v * std::cos(a) + k.cross(v) * std::sin(a) + k * (k * v) * (1 - std::cos(a));
}

// rotateX/Y/Z (benutzt von Mesh::rotate usw.) gegenueber der Rechte-Hand-Regel: +1 oder -1, einmal ausgemessen
inline double drehVorzeichen(int achse)
{
    static double vorzeichen[3] = {0, 0, 0};
    if (vorzeichen[achse] == 0)
    {
        const Vector3D a = drehAchse(achse);
        const Vector3D v = drehAchse((achse + 1) % 3);
        const double w = 0.3;
        Vector3D r = achse == 0 ? rotateX(v, w) : achse == 1 ? rotateY(v, w) : rotateZ(v, w);
        double plus = (r - drehenUmAchse(v, a, w * 180.0 / M_PI)).length();
        double minus = (r - drehenUmAchse(v, a, -w * 180.0 / M_PI)).length();
        vorzeichen[achse] = plus <= minus ? 1.0 : -1.0;
    }
    return vorzeichen[achse];
}

// ---------- 3x3-Matrizen (zeilenweise) ----------

inline void matrixMal(const double a[9], const double b[9], double ergebnis[9])
{
    double r[9];
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            r[i * 3 + j] = a[i * 3 + 0] * b[0 * 3 + j] + a[i * 3 + 1] * b[1 * 3 + j] + a[i * 3 + 2] * b[2 * 3 + j];
    std::copy(r, r + 9, ergebnis);
}

inline void achsMatrix(int achse, double grad, double m[9])
{
    double a = grad * M_PI / 180.0, c = std::cos(a), s = std::sin(a);
    double x[9] = {1, 0, 0, 0, c, -s, 0, s, c};
    double y[9] = {c, 0, s, 0, 1, 0, -s, 0, c};
    double z[9] = {c, -s, 0, s, c, 0, 0, 0, 1};
    const double *q = achse == 0 ? x : achse == 1 ? y : z;
    std::copy(q, q + 9, m);
}

// R = Rz * Ry * Rx
inline void matrixAusWinkeln(const Vector3D &grad, double m[9])
{
    double rx[9], ry[9], rz[9];
    achsMatrix(0, grad.x, rx);
    achsMatrix(1, grad.y, ry);
    achsMatrix(2, grad.z, rz);
    matrixMal(ry, rx, m);
    matrixMal(rz, m, m);
}

inline Vector3D winkelAusMatrix(const double m[9])
{
    const double grad = 180.0 / M_PI;
    double sy = -std::clamp(m[6], -1.0, 1.0); // m[6] = -sin(y)
    double y = std::asin(sy);

    if (std::fabs(sy) > 0.999999) // Kardansperre: Drehung um X und Z fallen zusammen
        return Vector3D(0, y * grad, std::atan2(-m[1], m[4]) * grad);

    double x = std::atan2(m[7], m[8]);
    double z = std::atan2(m[3], m[0]);

    auto sauber = [](double w)
    {
        w = std::round(w * 1e6) / 1e6; // -0.000001 nicht als -0.0 anzeigen
        return w == 0.0 ? 0.0 : w;
    };
    return Vector3D(sauber(x * grad), sauber(y * grad), sauber(z * grad));
}

// ---------- Objekte ----------

// Mitte der Bounding-Box: um diesen Punkt drehen/skalieren die Objekte selbst
inline Vector3D drehMitte(const object *o)
{
    Vector3D mn = o->position, mx = o->position;
    o->getWorldAABB(mn, mx);
    return (mn + mx) * 0.5;
}

// Um die Weltachse 'achse' (0=X, 1=Y, 2=Z) um 'grad' drehen, um den festen Punkt 'pivot'
inline void objektDrehen(object *o, int achse, double grad, const Vector3D &pivot)
{
    Vector3D mitte = drehMitte(o);

    double w = grad * drehVorzeichen(achse);
    o->rotate(achse == 0 ? Vector3D(w, 0, 0) : achse == 1 ? Vector3D(0, w, 0) : Vector3D(0, 0, w));

    // Das Objekt dreht um seine aktuelle Mitte; die verschiebt sich bei unsymmetrischen Objekten
    // mit jeder Drehung. Ausgleich, damit es um den festen Punkt dreht.
    Vector3D r = mitte - pivot;
    Vector3D korrektur = drehenUmAchse(r, drehAchse(achse), grad) - r;
    if (korrektur.length() > 1e-12)
        o->translate(korrektur);

    double m[9];
    achsMatrix(achse, grad, m);
    matrixMal(m, o->ausrichtung, o->ausrichtung);
}

inline void objektDrehen(object *o, int achse, double grad)
{
    objektDrehen(o, achse, grad, drehMitte(o));
}

// Je Achse um den festen Punkt 'pivot' skalieren (die Objekte skalieren selbst um ihre Mitte)
inline void objektSkalieren(object *o, const Vector3D &faktor, const Vector3D &pivot)
{
    Vector3D mitte = drehMitte(o);
    o->scale(faktor);

    Vector3D d = mitte - pivot;
    Vector3D korrektur(d.x * (faktor.x - 1.0), d.y * (faktor.y - 1.0), d.z * (faktor.z - 1.0));
    if (korrektur.length() > 1e-12)
        o->translate(korrektur);
}

// gemerkte Drehwinkel (Grad, X dann Y dann Z)
inline Vector3D objektDrehwinkel(const object *o)
{
    return winkelAusMatrix(o->ausrichtung);
}

// auf die angegebenen Drehwinkel drehen (um die eigene Mitte)
inline void objektDrehwinkelSetzen(object *o, const Vector3D &grad)
{
    double ziel[9], rueck[9], delta[9];
    matrixAusWinkeln(grad, ziel);

    // delta = ziel * ausrichtung^T (Drehmatrix: transponiert = invers)
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            rueck[i * 3 + j] = o->ausrichtung[j * 3 + i];
    matrixMal(ziel, rueck, delta);

    Vector3D d = winkelAusMatrix(delta);
    if (d.length() > 1e-9)
        o->rotate(Vector3D(d.x * drehVorzeichen(0), d.y * drehVorzeichen(1), d.z * drehVorzeichen(2)));

    std::copy(ziel, ziel + 9, o->ausrichtung);
}

// nur merken, ohne die Geometrie zu drehen (beim Laden: Geometrie ist schon gedreht gespeichert)
inline void objektAusrichtungMerken(object *o, const Vector3D &grad)
{
    matrixAusWinkeln(grad, o->ausrichtung);
}
