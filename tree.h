#pragma once

#include "dreieck.h"
#include "object.h"
#include "stab.h"
#include <vector>
#include "material.h"
#include "bvh.h"
#include "BVHNode.h"
#include "stats.h"
#include <deque>


// Alle Stellschrauben des Baums (Vorgaben = bisheriges Verhalten). Laengen in den Einheiten des Baums
// (die Toolwindow erzeugt Baeume mit Stammlaenge 5 und skaliert sie danach).
struct BaumParameter
{
    // Stamm und Aeste
    double dickenFaktor = 0.65;            // Radius je Stufe
    double laengeMin = 0.75, laengeMax = 0.9; // Laenge je Stufe (zufaellig dazwischen)
    int seitenMin = 2, seitenMax = 4;      // Seitenaeste je Glied
    double ansatzMin = 0.2;                // Seitenaeste setzen ab diesem Anteil der Astlaenge an
    double seitenLaenge = 1.0;             // Laenge der Seitenaeste im Verhaeltnis zum Hauptast (< 1: Tanne, Kegelform)
    double streuungHaupt = 0.3, streuungSeiten = 0.6; // seitliche Zufallsrichtung
    double schwereHaupt = 0.7, schwereSeiten = 0.6;   // Faktor auf die Steigung (kleiner = flacher)
    double spitzeMin = 0.6, spitzeMax = 0.9;          // Radius am Ende eines Glieds (Anteil vom Anfang)
    double mindestDicke = 0.01;            // duennere Aeste wachsen nicht mehr
    int segmente = 8;                      // Ecken des Astquerschnitts
    Farbe holzFarbe = Farbe(0.36, 0.22, 0.12);

    // Blaetter
    int blattStufen = 2;                   // Blaetter an den letzten N Stufen (0 = keine)
    double blattDichte = 50.0;             // Blaetter je Astende ~ Radius^1,5 * Dichte
    int blattBueschel = 3;                 // Faktor auf die Anzahl
    double blattMin = 0.1, blattMax = 0.2; // halbe Kantenlaenge eines Blatts
    double blattStreuung = 0.1;            // Radius, in dem die Blaetter ums Astende liegen
    Farbe blattFarbe = Farbe(0.20, 0.55, 0.20);

    // Schnee
    double schnee = 0.0;                   // 0 = kein Schnee, 1 = viel
    double schneeDicke = 0.3;              // Dicke der Schneeschicht auf den Aesten (Anteil des Astradius)
    Farbe schneeFarbe = Farbe(0.95, 0.95, 0.97);

    unsigned zufall = 0;                   // Startwert des Zufalls (0 = jedes Mal anders)
};

class Tree : public object
{
    friend class GpuAnsichten; // liest woodTriangles/leaves fuer die GPU-Ansichten
    friend class SceneSaver;   // exportiert woodTriangles/leaves beim Speichern
    friend bool szeneAlsOBJExportieren(const std::string &, std::string &); // Export OBJ
    friend struct ZustandZugriff; // Rueckgaengig (bearbeitung.cpp)

public:
    Tree(const Vector3D& pos,
         const Vector3D& richtung,
         double laenge,
         double radiusHolz,
         int tiefe,
         double wkeitWeiteresGlied,
         const BaumParameter &parameter = BaumParameter());

    ~Tree();

    //std::vector<object*> bvhObjects;

    const wchar_t* getTypeName() const override{return L"Tree";}

    void intersect(const Ray& ray, Hit& hit) const override;
    void drawFlat(Wireframe& wf, DrawMode mode) const override;
    void drawWireframePixels(Wireframe& wf, DrawMode mode) const override;

    void setPosition(const Vector3D &pos);

    // eigene Grenzen (Tree hat eigene minBound/maxBound, die von object blieben ungesetzt:
    // Fokus, Drehpunkt und Auswahlrahmen rechneten sonst mit einer falschen Box)
    void getWorldAABB(Vector3D &mn, Vector3D &mx) const override
    {
        mn = minBound;
        mx = maxBound;
    }

private:
    void build(const Vector3D& pos,
               const Vector3D& dir,
               double len,
               double radiusHolz,
               int depth,
               double wkeitWeiteresGlied);

    void updateBounds();

    void addLeaf(const Vector3D &pos, const Vector3D &dir);

    bool intersectAABB(const Ray &ray, double tMax) const;

    double randRange(double a, double b);

    int getTriangleCount() const override;

    Vector3D randomInSphere();

    //std::vector<Stab> stabs;       // Baumstruktur
    //std::vector<Dreieck> leaves;   // Blätter
    //std::vector<Dreieck> woodTriangles; // Baumstruktur aus Dreiecken
    std::vector<Dreieck> woodTriangles;
    std::vector<Dreieck> leaves;

    Vector3D position;

    Vector3D minBound;
    Vector3D maxBound;
    double boundingRadius;
    Vector3D boundingCenter;

    object* bvhRoot = nullptr;
    std::vector<object*> bvhObjects;

    void addBranchMesh(const Vector3D &start, const Vector3D &end, double r1, double r2, int segments, const Material &mat);
    void addSchnee(const Vector3D &start, const Vector3D &end, double r1, double r2);
    BaumParameter param;
    Material holzMaterial, blattMaterial, schneeMaterial;
    void buildBVH();
    void translate(const Vector3D &delta) override;
    void rotate(const Vector3D &rotation);
    void scale(const Vector3D &scaleVec);
    void recomputeBounds();
    void safeDeleteBVH();
};

class StabObject : public object {
public:
    const Stab* s;

    StabObject(const Stab* stab)
        : object(stab->getMaterial()), s(stab)
    {
        Vector3D minB = s->getMinBound();
        Vector3D maxB = s->getMaxBound();
        boundingCenter = (minB + maxB) * 0.5;
    }

    const wchar_t* getTypeName() const override{return L"StabObject";}

    void intersect(const Ray& ray, Hit& hit) const override {
        s->intersect(ray, hit);
    }

    void getWorldAABB(Vector3D& min, Vector3D& max) const override {
        min = s->getMinBound();
        max = s->getMaxBound();
    }

    void drawFlat(Wireframe& wf, DrawMode mode) const override {
        s->drawFlat(wf, mode);
    }

    void drawWireframePixels(Wireframe& wf, DrawMode mode) const override {
        s->drawWireframePixels(wf, mode);
    }
};

class TriangleObject  : public object {
public:
    const Dreieck* t;

    TriangleObject(const Dreieck* tri)
        : object(tri->getMaterial()), t(tri)
    {
        Vector3D minB = t->getMinBound();
        Vector3D maxB = t->getMaxBound();
        boundingCenter = (minB + maxB) * 0.5;
    }

    const wchar_t* getTypeName() const override{return L"TriangleObject";}

    void intersect(const Ray& ray, Hit& hit) const override {
        t->intersect(ray, hit);
    }

    void getWorldAABB(Vector3D& min, Vector3D& max) const override {
        min = t->getMinBound();
        max = t->getMaxBound();
    }

    void drawFlat(Wireframe& wf, DrawMode mode) const override {
        t->drawFlat(wf, mode);
    }

    void drawWireframePixels(Wireframe& wf, DrawMode mode) const override {
        t->drawWireframePixels(wf, mode);
    }
};

class LeafObject : public object {
public:
    const Dreieck* t;

    LeafObject(const Dreieck* tri)
        : object(tri->getMaterial()), t(tri)
    {
        Vector3D minB = t->getMinBound();
        Vector3D maxB = t->getMaxBound();
        boundingCenter = (minB + maxB) * 0.5;
    }

    const wchar_t* getTypeName() const override{return L"LeafObject";}

    void intersect(const Ray& ray, Hit& hit) const override {
        t->intersect(ray, hit);
    }

    void getWorldAABB(Vector3D& min, Vector3D& max) const override {
        min = t->getMinBound();
        max = t->getMaxBound();
    }

    void drawFlat(Wireframe& wf, DrawMode mode) const override {
        t->drawFlat(wf, mode);
    }

    void drawWireframePixels(Wireframe& wf, DrawMode mode) const override {
        t->drawWireframePixels(wf, mode);
    }

};

