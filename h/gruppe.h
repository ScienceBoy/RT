#pragma once
// Gruppe: mehrere Objekte als ein Objekt (Strg+G gruppieren, Strg+Umschalt+G aufheben).
// Fuer Raytracing, Auswahl und Ziehen ist sie ein ganz normales Objekt; alles wird an die
// enthaltenen Objekte weitergegeben. Gruppen duerfen Gruppen enthalten.
// Nur als Header, damit keine neue .cpp-Datei in CMake noetig ist.

#include "object.h"
#include "rotations.h"
#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

class Gruppe : public object
{
public:
    std::vector<std::unique_ptr<object>> kinder;

    Gruppe() : object(Material()) {}

    const wchar_t *getTypeName() const override { return L"Gruppe"; }

    // Bounding-Box, Mitte und Radius aus den enthaltenen Objekten (nach jeder Aenderung aufrufen)
    void grenzenAktualisieren()
    {
        bool erstes = true;
        for (auto &k : kinder)
        {
            if (auto *g = dynamic_cast<Gruppe *>(k.get()))
                g->grenzenAktualisieren();
            Vector3D mn = k->position, mx = k->position;
            k->getWorldAABB(mn, mx);
            if (erstes)
            {
                minBound = mn;
                maxBound = mx;
                erstes = false;
                continue;
            }
            minBound = Vector3D(std::min(minBound.x, mn.x), std::min(minBound.y, mn.y), std::min(minBound.z, mn.z));
            maxBound = Vector3D(std::max(maxBound.x, mx.x), std::max(maxBound.y, mx.y), std::max(maxBound.z, mx.z));
        }
        if (erstes)
            minBound = maxBound = position;
        boundingCenter = (minBound + maxBound) * 0.5;
        boundingRadius = (maxBound - minBound).length() * 0.5;
        position = boundingCenter;
    }

    void getWorldAABB(Vector3D &mn, Vector3D &mx) const override
    {
        mn = minBound;
        mx = maxBound;
    }

    void intersect(const Ray &ray, Hit &hit) const override
    {
        if (!intersectBounding(ray))
            return;
        for (auto &k : kinder)
            k->intersect(ray, hit);
    }

    void drawFlat(Wireframe &wf, DrawMode mode) const override
    {
        for (auto &k : kinder)
            k->drawFlat(wf, mode);
    }

    void drawWireframePixels(Wireframe &wf, DrawMode mode) const override
    {
        for (auto &k : kinder)
            k->drawWireframePixels(wf, mode);
    }

    int getTriangleCount() const override
    {
        int n = 0;
        for (auto &k : kinder)
            n += k->getTriangleCount();
        return n;
    }

    void translate(const Vector3D &delta) override
    {
        for (auto &k : kinder)
            k->translate(delta);
        grenzenAktualisieren();
    }

    // Drehen um die Mitte der Gruppe: jedes Objekt dreht um seine eigene Mitte (wie rotate() es tut)
    // und wird danach auf die gedrehte Lage seiner Mitte verschoben
    void rotate(const Vector3D &rotation) override
    {
        const Vector3D mitte = (minBound + maxBound) * 0.5;
        const double rx = rotation.x * M_PI / 180.0, ry = rotation.y * M_PI / 180.0, rz = rotation.z * M_PI / 180.0;
        for (auto &k : kinder)
        {
            Vector3D mn = k->position, mx = k->position;
            k->getWorldAABB(mn, mx);
            Vector3D c = (mn + mx) * 0.5;
            k->rotate(rotation);
            Vector3D r = rotateZ(rotateY(rotateX(c - mitte, rx), ry), rz);
            Vector3D d = mitte + r - c;
            if (d.length() > 1e-12)
                k->translate(d);
        }
        grenzenAktualisieren();
    }

    // Skalieren um die Mitte der Gruppe (je Achse)
    void scale(const Vector3D &f) override
    {
        const Vector3D mitte = (minBound + maxBound) * 0.5;
        for (auto &k : kinder)
        {
            Vector3D mn = k->position, mx = k->position;
            k->getWorldAABB(mn, mx);
            Vector3D c = (mn + mx) * 0.5;
            k->scale(f);
            Vector3D d((c.x - mitte.x) * (f.x - 1.0), (c.y - mitte.y) * (f.y - 1.0), (c.z - mitte.z) * (f.z - 1.0));
            if (d.length() > 1e-12)
                k->translate(d);
        }
        grenzenAktualisieren();
    }

    void collectPrimitives(std::vector<object *> &out) override
    {
        for (auto &k : kinder)
            k->collectPrimitives(out);
    }

    void setTexture(std::shared_ptr<Texture> tex) override
    {
        texture = tex;
        for (auto &k : kinder)
            k->setTexture(tex);
    }

    void setTextureMode(TextureMode mode) override
    {
        textureMode = mode;
        for (auto &k : kinder)
            k->setTextureMode(mode);
    }
};

// alle Objekte ohne Gruppen (Gruppen werden aufgeloest, auch verschachtelte)
inline void gruppenAufloesen(object *o, std::vector<object *> &ziel)
{
    if (auto *g = dynamic_cast<Gruppe *>(o))
    {
        for (auto &k : g->kinder)
            gruppenAufloesen(k.get(), ziel);
    }
    else if (o)
        ziel.push_back(o);
}
