#pragma once
// Rauchboxen und Wolkenbereich als Objekte in der Szene ("Platzhalter"): damit lassen sie sich in den
// Ansichten anklicken, verschieben und skalieren, stehen in der Objektliste, gehen mit Rueckgaengig,
// Knoepfen -X..+Z und Snapshots. Die eigentlichen Daten bleiben in smokeBoxes bzw. SkyMin/SkyMax/
// WolkeboxMin/WolkeboxMax (scene.cpp); der Platzhalter liest und schreibt sie.
//
// Fuer den Raytracer ist der Platzhalter unsichtbar: intersect() trifft nur beim Anklicken in den
// Ansichten (volumenBoxPicken, gesetzt in Wireframe::pickAll). Gespeichert wird er nicht (die Boxen
// stehen in <settings>); nach dem Laden und bei Aenderungen legt volumenBoxenAbgleichen() sie neu an.

#include "object.h"
#include "scene.h"
#include "smokeBox.h"
#include "ray.h"
#include "hit.h"
#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

// true nur waehrend Wireframe::pickAll (je Thread): dann treffen Strahlen die Platzhalter
inline thread_local bool volumenBoxPicken = false;
// Fangbreite fuer den Rahmen: fest (Ortho, Welteinheiten) + je Einheit Abstand (Perspektive)
inline thread_local double volumenBoxFang = 0.0, volumenBoxFangJeAbstand = 0.0;

// Wolkenbereich wie in render.cpp: Wolkenschicht (Hoehe) innerhalb des Himmelsbereichs;
// ueberschneiden sie sich nicht, der ganze Himmelsbereich
inline void wolkenBereich(Vector3D &mn, Vector3D &mx)
{
    mn = Vector3D(std::min(SkyMin.x, SkyMax.x), std::min(SkyMin.y, SkyMax.y), std::min(SkyMin.z, SkyMax.z));
    mx = Vector3D(std::max(SkyMin.x, SkyMax.x), std::max(SkyMin.y, SkyMax.y), std::max(SkyMin.z, SkyMax.z));
    double su = std::min(WolkeboxMin.y, WolkeboxMax.y), so = std::max(WolkeboxMin.y, WolkeboxMax.y);
    double u = std::max(mn.y, su), o = std::min(mx.y, so);
    if (o - u > 1e-9)
    {
        mn.y = u;
        mx.y = o;
    }
}

class VolumenBox : public object
{
public:
    int rauchIndex; // >= 0: smokeBoxes[rauchIndex]; -1: Wolkenbereich

    explicit VolumenBox(int index) : object(Material()), rauchIndex(index)
    {
        name = index >= 0 ? "Rauchbox " + std::to_string(index + 1) : std::string("Wolken");
        ausGlobal();
    }

    const wchar_t *getTypeName() const override { return rauchIndex >= 0 ? L"Rauchbox" : L"Wolkenbereich"; }
    bool istWolke() const { return rauchIndex < 0; }
    bool gueltig() const { return istWolke() || rauchIndex < int(smokeBoxes.size()); }

    void box(Vector3D &mn, Vector3D &mx) const
    {
        if (istWolke())
        {
            wolkenBereich(mn, mx);
            return;
        }
        if (!gueltig())
        {
            mn = mx = position;
            return;
        }
        const SmokeBox &b = smokeBoxes[size_t(rauchIndex)];
        mn = Vector3D(std::min(b.min.x, b.max.x), std::min(b.min.y, b.max.y), std::min(b.min.z, b.max.z));
        mx = Vector3D(std::max(b.min.x, b.max.x), std::max(b.min.y, b.max.y), std::max(b.min.z, b.max.z));
    }

    void boxSetzen(const Vector3D &a, const Vector3D &b)
    {
        Vector3D mn(std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z));
        Vector3D mx(std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z));
        if (istWolke())
        {
            // Himmelsbereich und Wolkenschicht werden eins (gerechnet wird ohnehin nur in der Schnittmenge)
            SkyMin = mn;
            SkyMax = mx;
            WolkeboxMin.y = mn.y;
            WolkeboxMax.y = mx.y;
        }
        else if (gueltig())
        {
            smokeBoxes[size_t(rauchIndex)].min = mn;
            smokeBoxes[size_t(rauchIndex)].max = mx;
        }
        ausGlobal();
    }

    // Felder von object nachfuehren. minBound/maxBound zaehlen in computeSceneBounds (scene.cpp):
    // beim riesigen Wolkenbereich nur ein Punkt (Mitte unten), sonst passte "Einpassen" den ganzen Himmel ein
    void ausGlobal()
    {
        Vector3D mn, mx;
        box(mn, mx);
        position = (mn + mx) * 0.5;
        boundingCenter = position;
        boundingRadius = (mx - mn).length() * 0.5;
        if (istWolke())
            minBound = maxBound = Vector3D(position.x, mn.y, position.z);
        else
        {
            minBound = mn;
            maxBound = mx;
        }
    }

    void getWorldAABB(Vector3D &mn, Vector3D &mx) const override { box(mn, mx); }

    // Nur beim Anklicken: Rauchbox ganz, Wolkenbereich nur nahe am Rahmen (sonst saesse er ueberall im Weg)
    void intersect(const Ray &ray, Hit &hit) const override
    {
        if (!volumenBoxPicken)
            return;
        Vector3D mn, mx;
        box(mn, mx);
        double t0 = -1e300, t1 = 1e300;
        for (int k = 0; k < 3; ++k)
        {
            double o = k == 0 ? ray.origin.x : k == 1 ? ray.origin.y : ray.origin.z;
            double d = k == 0 ? ray.direction.x : k == 1 ? ray.direction.y : ray.direction.z;
            double a = k == 0 ? mn.x : k == 1 ? mn.y : mn.z, b = k == 0 ? mx.x : k == 1 ? mx.y : mx.z;
            if (std::fabs(d) < 1e-300)
            {
                if (o < a || o > b)
                    return;
                continue;
            }
            double ta = (a - o) / d, tb = (b - o) / d;
            t0 = std::max(t0, std::min(ta, tb));
            t1 = std::min(t1, std::max(ta, tb));
        }
        if (t1 < t0 || t1 <= 0)
            return;

        auto nahAmRand = [&](double t)
        {
            Vector3D p = ray.origin + ray.direction * t;
            double fang = volumenBoxFang + volumenBoxFangJeAbstand * std::max(t, 0.0);
            double q[3] = {p.x, p.y, p.z}, a[3] = {mn.x, mn.y, mn.z}, b[3] = {mx.x, mx.y, mx.z};
            // Achse der getroffenen Flaeche: die mit dem kleinsten Abstand zu einer Seite
            int flaeche = 0;
            double best = 1e300;
            for (int k = 0; k < 3; ++k)
            {
                double d = std::min(std::fabs(q[k] - a[k]), std::fabs(q[k] - b[k]));
                if (d < best)
                    best = d, flaeche = k;
            }
            for (int k = 0; k < 3; ++k)
                if (k != flaeche && std::min(q[k] - a[k], b[k] - q[k]) <= std::max(fang, (b[k] - a[k]) * 0.01))
                    return true;
            return false;
        };

        double t = t0 > 0 ? t0 : t1;
        if (istWolke() && !nahAmRand(t))
        {
            if (t0 > 0 && nahAmRand(t1)) // Rueckseite des Rahmens
                t = t1;
            else
                return;
        }
        if (t <= 0 || t >= hit.t)
            return;
        hit.hit = true;
        hit.t = t;
        hit.position = ray.origin + ray.direction * t;
    }

    void translate(const Vector3D &delta) override
    {
        Vector3D mn, mx;
        box(mn, mx);
        boxSetzen(mn + delta, mx + delta);
    }
    void scale(const Vector3D &f) override // um die eigene Mitte
    {
        Vector3D mn, mx;
        box(mn, mx);
        Vector3D m = (mn + mx) * 0.5, h = (mx - mn) * 0.5;
        Vector3D neu(std::fabs(h.x * f.x), std::fabs(h.y * f.y), std::fabs(h.z * f.z));
        boxSetzen(m - neu, m + neu);
    }
    void rotate(const Vector3D &) override {} // Boxen sind achsenparallel
    void drawWireframePixels(Wireframe &, DrawMode) const override {} // Rahmen zeichnet GpuAnsichten
    void drawFlat(Wireframe &, DrawMode) const override {}
};

// Platzhalter in der Szene passen zu den Einstellungen (Rauch an: einer je Box, Wolken an: einer)?
inline bool volumenBoxenStimmen()
{
    size_t rauch = 0;
    bool wolke = false;
    for (auto &o : scene)
        if (auto *v = dynamic_cast<VolumenBox *>(o.get()))
        {
            if (v->istWolke())
            {
                if (wolke || !wolkeVolumeVorhanden)
                    return false;
                wolke = true;
            }
            else if (!rauchVorhanden || v->rauchIndex != int(rauch++) || !v->gueltig())
                return false;
        }
    return rauch == (rauchVorhanden ? smokeBoxes.size() : 0) && wolke == wolkeVolumeVorhanden;
}

// Platzhalter neu anlegen (am Ende der Szene). Aufrufer sperrt die Szene (gSzeneAendern bzw. Laden)
inline void volumenBoxenAbgleichen()
{
    scene.erase(std::remove_if(scene.begin(), scene.end(),
                               [](const std::unique_ptr<object> &o)
                               { return dynamic_cast<VolumenBox *>(o.get()) != nullptr; }),
                scene.end());
    if (rauchVorhanden)
        for (size_t i = 0; i < smokeBoxes.size(); ++i)
            scene.push_back(std::make_unique<VolumenBox>(int(i)));
    if (wolkeVolumeVorhanden)
        scene.push_back(std::make_unique<VolumenBox>(-1));
}

// nach Aenderungen in den Render-Einstellungen: Felder der Platzhalter nachfuehren
inline void volumenBoxenNachfuehren()
{
    for (auto &o : scene)
        if (auto *v = dynamic_cast<VolumenBox *>(o.get()))
            v->ausGlobal();
}

// Platzhalter zu einer Rauchbox (index >= 0) bzw. den Wolken (-1); nullptr, wenn keiner
inline VolumenBox *volumenBoxVon(int index)
{
    for (auto &o : scene)
        if (auto *v = dynamic_cast<VolumenBox *>(o.get()))
            if (v->rauchIndex == index)
                return v;
    return nullptr;
}
