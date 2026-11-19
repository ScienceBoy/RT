#pragma once
// Affine Abbildung (3x3-Matrix + Verschiebung) zwischen zwei Dreieckslisten gleicher Anzahl:
// Verschieben, Drehen, Skalieren (auch je Achse) und Kombinationen davon sind affin.
// Gebraucht fuer
//  - eigene OBJ-Dateien: die Lage "Datei -> Szene" wird beim Speichern als transform="..." festgehalten
//    (vorher gingen Verschieben und Skalieren beim Laden verloren);
//  - Snapshots der Animation in der XML: Lage je Snapshot relativ zur gespeicherten Geometrie.

#include "vector3d.h"
#include "dreieck.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

struct Affin
{
    double m[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    Vector3D t = Vector3D(0, 0, 0);

    Vector3D richtung(const Vector3D &v) const // nur der lineare Teil
    {
        return Vector3D(m[0][0] * v.x + m[0][1] * v.y + m[0][2] * v.z,
                        m[1][0] * v.x + m[1][1] * v.y + m[1][2] * v.z,
                        m[2][0] * v.x + m[2][1] * v.y + m[2][2] * v.z);
    }
    Vector3D punkt(const Vector3D &p) const { return richtung(p) + t; }

    double determinante() const
    {
        return m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
               m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
    }

    // Normalen: mit der inversen Transponierten (Kofaktoren), danach normiert
    Vector3D normale(const Vector3D &n) const
    {
        double k[3][3];
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
            {
                int i1 = (i + 1) % 3, i2 = (i + 2) % 3, j1 = (j + 1) % 3, j2 = (j + 2) % 3;
                k[i][j] = m[i1][j1] * m[i2][j2] - m[i1][j2] * m[i2][j1];
            }
        Vector3D r(k[0][0] * n.x + k[0][1] * n.y + k[0][2] * n.z,
                   k[1][0] * n.x + k[1][1] * n.y + k[1][2] * n.z,
                   k[2][0] * n.x + k[2][1] * n.y + k[2][2] * n.z);
        if (determinante() < 0)
            r = -r;
        double l = r.length();
        return l > 1e-300 ? r * (1.0 / l) : n;
    }

    bool istEins(double toleranz = 1e-9) const
    {
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
                if (std::fabs(m[i][j] - (i == j ? 1.0 : 0.0)) > toleranz)
                    return false;
        return t.length() <= toleranz * (1.0 + 0.0);
    }

    // erst b, dann diese Abbildung
    Affin nach(const Affin &b) const
    {
        Affin r;
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
                r.m[i][j] = m[i][0] * b.m[0][j] + m[i][1] * b.m[1][j] + m[i][2] * b.m[2][j];
        r.t = punkt(b.t);
        return r;
    }

    std::string text() const // "m00 m01 m02 tx  m10 m11 m12 ty  m20 m21 m22 tz"
    {
        std::ostringstream o;
        o.precision(12);
        double tv[3] = {t.x, t.y, t.z};
        for (int i = 0; i < 3; ++i)
        {
            if (i)
                o << "  ";
            o << m[i][0] << " " << m[i][1] << " " << m[i][2] << " " << tv[i];
        }
        return o.str();
    }

    bool lesen(const std::string &s)
    {
        std::istringstream w(s);
        double tv[3];
        for (int i = 0; i < 3; ++i)
            if (!(w >> m[i][0] >> m[i][1] >> m[i][2] >> tv[i]))
                return false;
        t = Vector3D(tv[0], tv[1], tv[2]);
        return true;
    }
};

// alle Ecken (und Vertex-Normalen) einer Dreiecksliste abbilden
inline void affinAnwenden(const Affin &a, std::vector<Dreieck> &tris)
{
    for (auto &d : tris)
    {
        d.a = a.punkt(d.a);
        d.b = a.punkt(d.b);
        d.c = a.punkt(d.c);
        if (d.hasVertexNormals)
        {
            d.nA = a.normale(d.nA);
            d.nB = a.normale(d.nB);
            d.nC = a.normale(d.nC);
        }
        d.rebuild();
    }
}

// Abbildung "von -> nach" fuer Dreieckslisten gleicher Laenge (mehrere Listen hintereinander,
// z.B. Holz und Blaetter eines Baums). Kleinste Quadrate ueber die Ecken; false, wenn die Listen
// nicht zusammenpassen oder die Aenderung nicht affin ist (z.B. Baum neu gewachsen).
inline bool affinAnpassen(const std::vector<const std::vector<Dreieck> *> &von,
                          const std::vector<const std::vector<Dreieck> *> &nach, Affin &ergebnis)
{
    std::vector<const Dreieck *> a, b;
    for (auto *l : von)
        for (auto &d : *l)
            a.push_back(&d);
    for (auto *l : nach)
        for (auto &d : *l)
            b.push_back(&d);
    if (a.empty() || a.size() != b.size())
        return false;

    // hoechstens ~4000 Dreiecke auswerten (gleichmaessig verteilt)
    const size_t schritt = std::max<size_t>(1, a.size() / 4000);
    std::vector<Vector3D> p, q;
    for (size_t i = 0; i < a.size(); i += schritt)
    {
        p.push_back(a[i]->a), p.push_back(a[i]->b), p.push_back(a[i]->c);
        q.push_back(b[i]->a), q.push_back(b[i]->b), q.push_back(b[i]->c);
    }

    auto mitte = [](const std::vector<Vector3D> &v)
    {
        Vector3D s(0, 0, 0);
        for (auto &x : v)
            s += x;
        return s * (1.0 / double(v.size()));
    };
    auto ausdehnung = [](const std::vector<Vector3D> &v)
    {
        Vector3D mn = v[0], mx = v[0];
        for (auto &x : v)
        {
            mn = Vector3D(std::min(mn.x, x.x), std::min(mn.y, x.y), std::min(mn.z, x.z));
            mx = Vector3D(std::max(mx.x, x.x), std::max(mx.y, x.y), std::max(mx.z, x.z));
        }
        return (mx - mn).length();
    };
    const Vector3D cp = mitte(p), cq = mitte(q);
    const double groesse = std::max(ausdehnung(p), 1e-12);

    double S[3][3] = {}, C[3][3] = {};
    auto dazu = [&](const Vector3D &pp, const Vector3D &qq)
    {
        double x[3] = {pp.x, pp.y, pp.z}, y[3] = {qq.x, qq.y, qq.z};
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
            {
                S[i][j] += x[i] * x[j];
                C[i][j] += y[i] * x[j];
            }
    };
    for (size_t i = 0; i < p.size(); ++i)
        dazu(p[i] - cp, q[i] - cq);

    auto det3 = [](double M[3][3])
    {
        return M[0][0] * (M[1][1] * M[2][2] - M[1][2] * M[2][1]) - M[0][1] * (M[1][0] * M[2][2] - M[1][2] * M[2][0]) +
               M[0][2] * (M[1][0] * M[2][1] - M[1][1] * M[2][0]);
    };

    // flache Objekte (Wand, Boden): die Richtung senkrecht dazu ueber die Flaechennormalen festlegen
    double spur = S[0][0] + S[1][1] + S[2][2];
    if (spur <= 0)
        return false;
    if (det3(S) < 1e-9 * std::pow(spur / 3.0, 3))
    {
        for (size_t i = 0; i + 2 < p.size(); i += 3)
        {
            Vector3D np = (p[i + 1] - p[i]).cross(p[i + 2] - p[i]);
            Vector3D nq = (q[i + 1] - q[i]).cross(q[i + 2] - q[i]);
            double lp = np.length(), lq = nq.length();
            if (lp < 1e-300 || lq < 1e-300)
                continue;
            double s = std::sqrt(lq / lp); // Massstab wie die Flaeche
            Vector3D mp = (p[i] + p[i + 1] + p[i + 2]) * (1.0 / 3.0);
            Vector3D mq = (q[i] + q[i + 1] + q[i + 2]) * (1.0 / 3.0);
            dazu(mp + np * (groesse / lp) - cp, mq + nq * (groesse * s / lq) - cq);
        }
        if (det3(S) <= 0)
            return false;
    }

    // L = C * S^-1
    double d = det3(S);
    double inv[3][3];
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
        {
            int i1 = (j + 1) % 3, i2 = (j + 2) % 3, j1 = (i + 1) % 3, j2 = (i + 2) % 3;
            inv[i][j] = (S[i1][j1] * S[i2][j2] - S[i1][j2] * S[i2][j1]) / d;
        }
    Affin r;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            r.m[i][j] = C[i][0] * inv[0][j] + C[i][1] * inv[1][j] + C[i][2] * inv[2][j];
    r.t = cq - r.richtung(cp);

    // Probe: passt jede Ecke?
    const double toleranz = 1e-4 * std::max(groesse, ausdehnung(q)) + 1e-9;
    for (size_t i = 0; i < p.size(); ++i)
        if ((r.punkt(p[i]) - q[i]).length() > toleranz)
            return false;
    ergebnis = r;
    return true;
}
