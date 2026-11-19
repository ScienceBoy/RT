#pragma once
// Instanz (verknuepfte Kopie): verweist auf eine gemeinsame Vorlage und hat nur ihre eigene Lage, Drehung und
// Groesse (affine Abbildung "Vorlage -> Welt"). 100 Instanzen eines Baums brauchen den Speicher eines Baums.
//  - Raytracing: Strahl in die Koordinaten der Vorlage umrechnen, dort treffen (deren BVH), Treffer zurueck
//  - Ansichten: die Vorlage wird einmal hochgeladen und je Instanz mit ihrer Matrix gezeichnet (gpu_ansichten)
//  - Verschieben/Drehen/Skalieren aendern nur die Abbildung; Material/Netz-Operationen wirken auf die Vorlage
//    (also auf alle Instanzen); "Instanz lösen" macht eine eigenstaendige Kopie.
// Die Vorlage liegt nicht in der Szene; sie lebt, solange eine Instanz (oder ein Rueckgaengig-Schritt) sie haelt.
// Nur als Header (wie gruppe.h), damit keine neue .cpp-Datei in CMake noetig ist.

#include "affin.h"
#include "gruppe.h"
#include "object.h"
#include "rotations.h"
#include <algorithm>
#include <cmath>
#include <memory>

// Umkehrabbildung (bei entarteter Abbildung: Einheit)
inline Affin affinInvers(const Affin &A)
{
    Affin inv;
    const double det = A.determinante();
    if (!(std::fabs(det) > 1e-300))
        return inv;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
        {
            const int i1 = (j + 1) % 3, i2 = (j + 2) % 3, j1 = (i + 1) % 3, j2 = (i + 2) % 3;
            inv.m[i][j] = (A.m[i1][j1] * A.m[i2][j2] - A.m[i1][j2] * A.m[i2][j1]) / det;
        }
    inv.t = Vector3D(0, 0, 0);
    inv.t = -inv.richtung(A.t);
    return inv;
}

class Instanz : public object
{
  public:
    std::shared_ptr<object> vorlage; // gemeinsame Geometrie (nicht in der Szene)
    Affin abb;                       // Vorlage -> Welt
    Affin inv;                       // Welt -> Vorlage

    Instanz(std::shared_ptr<object> v, const Affin &a) : object(v ? v->mat : Material()), vorlage(std::move(v))
    {
        abbSetzen(a);
    }

    const wchar_t *getTypeName() const override { return L"Instanz"; }

    void abbSetzen(const Affin &a)
    {
        abb = a;
        inv = affinInvers(a);
        grenzenAktualisieren();
    }

    // Grenzen: die acht Ecken der Vorlagen-Box abgebildet
    void grenzenAktualisieren()
    {
        if (!vorlage)
            return;
        if (auto *g = dynamic_cast<Gruppe *>(vorlage.get()))
            g->grenzenAktualisieren();
        Vector3D mn = vorlage->position, mx = vorlage->position;
        vorlage->getWorldAABB(mn, mx);
        bool erstes = true;
        for (int i = 0; i < 8; ++i)
        {
            const Vector3D e((i & 1) ? mx.x : mn.x, (i & 2) ? mx.y : mn.y, (i & 4) ? mx.z : mn.z);
            const Vector3D p = abb.punkt(e);
            if (erstes)
            {
                minBound = maxBound = p;
                erstes = false;
                continue;
            }
            minBound = Vector3D(std::min(minBound.x, p.x), std::min(minBound.y, p.y), std::min(minBound.z, p.z));
            maxBound = Vector3D(std::max(maxBound.x, p.x), std::max(maxBound.y, p.y), std::max(maxBound.z, p.z));
        }
        texturUebernehmen();
        boundingCenter = (minBound + maxBound) * 0.5;
        boundingRadius = (maxBound - minBound).length() * 0.5;
        position = boundingCenter;
    }

    void getWorldAABB(Vector3D &mn, Vector3D &mx) const override
    {
        mn = minBound;
        mx = maxBound;
    }

    // Strahl in Vorlagen-Koordinaten; die Richtung wird normiert, t entsprechend umgerechnet
    void intersect(const Ray &ray, Hit &hit) const override
    {
        if (!vorlage || !intersectBounding(ray))
            return;
        const Vector3D d = inv.richtung(ray.direction);
        const double laenge = d.length();
        if (!(laenge > 1e-300))
            return;
        const Ray lokal(inv.punkt(ray.origin), d * (1.0 / laenge));
        Hit h = hit;
        const double bisher = hit.t;
        h.t = std::isfinite(bisher) ? bisher * laenge : bisher;
        const double vorher = h.t;
        vorlage->intersect(lokal, h);
        if (!(h.t < vorher))
            return; // nichts Naeheres getroffen
        const double tWelt = h.t / laenge;
        if (!(tWelt < bisher))
            return;
        hit = h;
        hit.t = tWelt;
        hit.position = abb.punkt(h.position);
        hit.normale = abb.normale(h.normale);
        hit.geomNormale = abb.normale(h.geomNormale);
        // Textur der Vorlage, aber auf die Box dieser Instanz bezogen (Trefferpunkt ist in Weltkoordinaten)
        if (h.obj && (h.obj->texture || h.obj->texPar))
            hit.obj = this;
    }

    // Drehen um die eigene Mitte (Grad um X, dann Y, dann Z wie die anderen Objekte)
    void rotate(const Vector3D &rotation) override
    {
        const double rx = rotation.x * M_PI / 180.0, ry = rotation.y * M_PI / 180.0, rz = rotation.z * M_PI / 180.0;
        auto drehen = [&](const Vector3D &v) { return rotateZ(rotateY(rotateX(v, rx), ry), rz); };
        Affin R;
        const Vector3D sx = drehen(Vector3D(1, 0, 0)), sy = drehen(Vector3D(0, 1, 0)), sz = drehen(Vector3D(0, 0, 1));
        R.m[0][0] = sx.x, R.m[1][0] = sx.y, R.m[2][0] = sx.z;
        R.m[0][1] = sy.x, R.m[1][1] = sy.y, R.m[2][1] = sy.z;
        R.m[0][2] = sz.x, R.m[1][2] = sz.y, R.m[2][2] = sz.z;
        umMitte(R);
    }

    // Skalieren um die eigene Mitte (je Achse der Welt)
    void scale(const Vector3D &f) override
    {
        Affin S;
        S.m[0][0] = f.x, S.m[1][1] = f.y, S.m[2][2] = f.z;
        umMitte(S);
    }

    void translate(const Vector3D &delta) override
    {
        Affin a = abb;
        a.t = a.t + delta;
        abbSetzen(a);
    }

    int getTriangleCount() const override { return vorlage ? vorlage->getTriangleCount() : 0; }

    // nur fuer den alten CPU-Zeichenweg: der Umriss (die GPU-Ansichten zeichnen die Vorlage mit der Matrix)
    void drawFlat(Wireframe &wf, DrawMode mode) const override { umriss(wf, mode); }
    void drawWireframePixels(Wireframe &wf, DrawMode mode) const override { umriss(wf, mode); }

    // Textur-Einstellungen der Vorlage (oder des ersten Teils mit Textur) uebernehmen
    void texturUebernehmen()
    {
        const object *q = vorlage.get();
        if (q && !q->texture && !q->texPar)
            if (auto *g = dynamic_cast<const Gruppe *>(q))
                for (const auto &k : g->kinder)
                    if (k && (k->texture || k->texPar))
                    {
                        q = k.get();
                        break;
                    }
        if (!q)
            return;
        texture = q->texture;
        textureMode = q->textureMode;
        textureImg = q->textureImg;
        textureName = q->textureName;
        texturSpiegelU = q->texturSpiegelU;
        texturSpiegelV = q->texturSpiegelV;
        texturDrehung = q->texturDrehung;
        texPar = q->texPar; // Kachel, Versatz, weitere Bilder (objektfest: in den Koordinaten der Vorlage)
        texturBox = false;
        mat = q->mat;
    }

  private:
    void umMitte(const Affin &M) // abb' = T(c) * M * T(-c) * abb
    {
        const Vector3D c = (minBound + maxBound) * 0.5;
        Affin um = M;
        um.t = c - M.richtung(c);
        abbSetzen(um.nach(abb));
    }
    void umriss(Wireframe &, DrawMode) const {}
};
