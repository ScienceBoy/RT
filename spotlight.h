#pragma once
// Spotlicht: ein Punktlicht, das nur in einen Kegel leuchtet (Buehnenscheinwerfer, Taschenlampe, Schreibtischlampe).
//  - richtung: wohin der Kegel zeigt (normiert)
//  - winkel:   halber Oeffnungswinkel in Grad (Rand des Kegels)
//  - weich:    Anteil des Winkels, ueber den das Licht zum Rand hin weich ausblendet (0 = harte Kante, 1 = von der
//              Mitte an)
// Helligkeit wie beim Punktlicht (Leistung / (4 pi d^2)), mal Kegelfaktor = smoothstep(cos(Rand), cos(innen), cos(w)).
// Mit "beleuchtetem Nebel" wird der Kegel als Lichtstrahl sichtbar (lichtkegelImNebel in render.cpp).
// Fuer die Kamera ist die Lampe selbst unsichtbar (wie das Punktlicht).
// Nur als Header, damit keine neue .cpp-Datei in CMake noetig ist.

#include "pointlight.h"
#include "material.h"
#include "wireframe.h"
#include <algorithm>
#include <cmath>
#include <ostream>

class SpotLight : public PointLight
{
  public:
    Vector3D richtung = Vector3D(0, -1, 0);
    double winkel = 30.0; // halber Oeffnungswinkel (Grad)
    double weich = 0.25;  // weicher Rand (Anteil des Winkels)

    SpotLight(const Vector3D &pos, const Vector3D &dir, const Farbe &col, double power, double halbWinkel = 30.0,
              double weicherRand = 0.25)
        : PointLight(pos, col, power), richtung(dir.length() > 1e-12 ? dir.normalized() : Vector3D(0, -1, 0)),
          winkel(halbWinkel), weich(weicherRand)
    {
    }

    // 0 ausserhalb des Kegels, 1 im inneren Kegel, dazwischen weich
    double kegelFaktor(const Vector3D &p) const
    {
        Vector3D zuP = p - position;
        const double l = zuP.length();
        if (!(l > 1e-12))
            return 1.0;
        const double c = (zuP * (1.0 / l)) * richtung;
        const double rand = std::clamp(winkel, 0.1, 179.0) * M_PI / 180.0;
        const double innen = rand * (1.0 - std::clamp(weich, 0.0, 1.0));
        const double cosRand = std::cos(rand), cosInnen = std::cos(innen);
        if (c <= cosRand)
            return 0.0;
        if (c >= cosInnen || cosInnen - cosRand < 1e-12)
            return 1.0;
        const double x = (c - cosRand) / (cosInnen - cosRand);
        return x * x * (3.0 - 2.0 * x);
    }

    LightSample sample(const Vector3D &p) const override
    {
        LightSample s = PointLight::sample(p);
        s.radiance = s.radiance * kegelFaktor(p);
        return s;
    }

    Farbe intensityAt(const Vector3D &p) const override { return PointLight::intensityAt(p) * kegelFaktor(p); }

    void save(std::ostream &out) const override
    {
        out << "        <light type=\"spot\" power=\"" << power << "\" angle=\"" << winkel << "\" soft=\"" << weich << "\">\n";
        out << "            <position x=\"" << position.x << "\" y=\"" << position.y << "\" z=\"" << position.z << "\"/>\n";
        out << "            <color r=\"" << farbe.r << "\" g=\"" << farbe.g << "\" b=\"" << farbe.b << "\"/>\n";
        out << "            <direction x=\"" << richtung.x << "\" y=\"" << richtung.y << "\" z=\"" << richtung.z << "\"/>\n";
        out << "        </light>\n\n";
    }

    // Symbol: kleiner Stern wie beim Punktlicht, dazu der Kegel (8 Mantellinien und der Randkreis)
    void drawFlat(Wireframe &wf, DrawMode mode) const override { drawWireframePixels(wf, mode); }
    void drawWireframePixels(Wireframe &wf, DrawMode mode) const override
    {
        Farbe color = mode == DrawMode::HIGHLIGHT ? Farbe(1, 1, 0) : mode == DrawMode::HOVER ? Farbe(0, 1, 1) : farbe;
        const Material m = makeMaterialSimple(color);
        auto linie = [&](const Vector3D &a, const Vector3D &b)
        {
            int x0, y0, x1, y1;
            double z0, z1;
            wf.project(a, x0, y0, z0);
            wf.project(b, x1, y1, z1);
            wf.drawLine(x0, y0, z0, x1, y1, z1, m);
        };
        // kleiner Stern
        const double s = 0.4;
        linie(position - Vector3D(s, 0, 0), position + Vector3D(s, 0, 0));
        linie(position - Vector3D(0, s, 0), position + Vector3D(0, s, 0));
        linie(position - Vector3D(0, 0, s), position + Vector3D(0, 0, s));
        // Kegel: Laenge 2 (wie die Strahlen des Punktlichts), Radius aus dem Winkel (hoechstens 4)
        const Vector3D d = richtung.length() > 1e-12 ? richtung.normalized() : Vector3D(0, -1, 0);
        const double laenge = 2.0;
        const double r = std::min(4.0, laenge * std::tan(std::clamp(winkel, 1.0, 85.0) * M_PI / 180.0));
        Vector3D hilf = std::fabs(d.y) < 0.9 ? Vector3D(0, 1, 0) : Vector3D(1, 0, 0);
        const Vector3D e1 = d.cross(hilf).normalized(), e2 = d.cross(e1).normalized();
        const Vector3D mitte = position + d * laenge;
        const int schritte = 24;
        for (int i = 0; i < schritte; ++i)
        {
            const double a0 = 2.0 * M_PI * i / schritte, a1 = 2.0 * M_PI * (i + 1) / schritte;
            const Vector3D p0 = mitte + (e1 * std::cos(a0) + e2 * std::sin(a0)) * r;
            const Vector3D p1 = mitte + (e1 * std::cos(a1) + e2 * std::sin(a1)) * r;
            linie(p0, p1);
            if (i % 3 == 0)
                linie(position, p0);
        }
        linie(position, mitte); // Achse
    }
};
