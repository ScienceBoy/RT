#include "wireframe.h"
#include "camera.h"
#include "scene.h"
#include "volumenbox.h"
#include "gruppe.h"
#include "kanten.h"
#include "terrain_bearbeiten.h"
#include "material_pinsel.h"
#include <functional>
#include "drehpunkt.h"

static Vector3D drehpunktEbenePunkt, drehpunktEbeneNormal(0, 1, 0), drehpunktVersatz; // Ziehen des Drehpunkts
#include "rotations.h"
#include "threading.h"
#include "fenster.h"
#include "light.h"
#include "material.h"
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <thread>
#include "mauspicking.h"
#include "mausray.h"
#include "object.h"
#include "wand.h"
#include "mesh.h"
#include "SceneLoader.h"
#include <mutex>
#include <climits>
#include <math.h>
#include "wireframe.h"
#include <unordered_set>
#include <set>
#include "wireframe.h"
#include "wireframe.h"
#include "gpu_aufzeichnung.h"
#include "drehung.h"
#include "bearbeitung.h"

// Camera* camera = nullptr;
extern Fenster winRay;
std::mutex sceneMutex;
thread_local GpuAufzeichnung *Wireframe::aufzeichnung = nullptr;
double scale = 0.5;
bool rendernRunning = false;
bool useFlat = true;
bool overObject = false;
extern bool FensterDirty;

// ======================================================================
// Konstruktor - zum übergebenen Fenster passende Werte setzen
// ======================================================================

Wireframe::Wireframe(Fenster &window)
    : fenster(window),
      w(window.breite()),
      h(window.hoehe())
{
    // Maus-Events auf die Instanz-Handler weiterleiten
    fenster.setMouseMoveHandler([this](int x, int y, Fenster &f)
                                { this->onMouseMove(x, y, f); });
    fenster.setMouseWheelHandler([this](int delta, Fenster &f)
                                 { this->onMouseWheel(delta, f); });
    fenster.setMouseDownHandler([this](int x, int y, Fenster &f)
                                { this->onMouseDown(x, y, f); });
    fenster.setMouseUpHandler([this]()
                              { this->onMouseUp(); });

    // Mittlere Maustaste: Ortho-Ansichten verschieben (die Perspektive setzt eigene Handler)
    fenster.setMiddleMouseDownHandler([this](int x, int y, Fenster &)
                                      { panAktiv = true; panX = x; panY = y; });
    fenster.setMiddleMouseUpHandler([this]()
                                    { panAktiv = false; });

    // Tastendrücke an onKey() weiterreichen
    fenster.setKeyHandler([this](WPARAM key)
                          { this->onKey(key); });
}

// ======================================================================
// Szene registrieren
// ======================================================================

// Mitte der drei Ortho-Ansichten. Vorher immer die Mitte der Szene: beim Verschieben eines
// Objekts wanderte die Szenenmitte mit, und die Ansichten zogen das Objekt in die Mitte zurueck.
// Jetzt wird sie einmal festgelegt (erstes Bild mit Objekten nach Laden/Importieren/Leeren, 'Z').
static std::mutex mitteMutex;
static bool mitteGesetzt = false;
static Vector3D mitteWert;

Vector3D ansichtsMitte(bool szeneLeer)
{
    std::lock_guard<std::mutex> lock(mitteMutex);
    if (!mitteGesetzt)
    {
        if (szeneLeer)
            return (sceneMin + sceneMax) * 0.5;
        mitteWert = (sceneMin + sceneMax) * 0.5;
        mitteGesetzt = true;
    }
    return mitteWert;
}

void ansichtenNeuZentrieren()
{
    std::lock_guard<std::mutex> lock(mitteMutex);
    mitteGesetzt = false;
}

void ansichtsMitteSetzen(const Vector3D &mitte)
{
    std::lock_guard<std::mutex> lock(mitteMutex);
    mitteWert = mitte;
    mitteGesetzt = true;
}

void ansichtsMitteVerschieben(const Vector3D &delta)
{
    std::lock_guard<std::mutex> lock(mitteMutex);
    if (!mitteGesetzt)
        mitteWert = (sceneMin + sceneMax) * 0.5;
    mitteWert += delta;
    mitteGesetzt = true;
}

MausPosition gMausPosition;

void Wireframe::setScene(const std::vector<std::unique_ptr<object>> &scene)
{
    ansichtenNeuZentrieren();

    // wird auch aus dem Import-Thread aufgerufen, waehrend der Renderthread liest
    std::lock_guard<std::mutex> lock(sceneMutex);
    sceneobjects.clear();

    for (const auto &obj : scene)
        sceneobjects.push_back(obj.get());
}

bool Wireframe::projectPoint(
    const Vector3D &p,
    ScreenPoint &sp)
{
    project(p, sp.x, sp.y, sp.z);

    return sp.x > -1000 &&
           sp.y > -1000 &&
           std::isfinite(sp.z);
}

bool Wireframe::draw3DLine(
    const Vector3D &a,
    const Vector3D &b,
    const Farbe &color)
{
    ScreenPoint pa;
    ScreenPoint pb;

    if (!projectPoint(a, pa))
        return false;

    if (!projectPoint(b, pb))
        return false;

    Material mat;
    mat.diffuse = color;

    drawLine(
        pa.x, pa.y, -1e12,
        pb.x, pb.y, -1e12,
        mat);

    return true;
}

void Wireframe::drawGrid()
{
    double viewWidth = w / scale;
    double viewHeight = h / scale;

    double extent = std::max(viewWidth, viewHeight) * 1.0;

    // Skalierung
    double spacing = pow(10.0, floor(log10(150.0 / scale)));

    double centerX = 0.0; // cam[0].lookAt.x;
    double centerY = 0.0; // cam[0].lookAt.y;
    double centerZ = 0.0; // cam[0].lookAt.z;

    double startX = floor(centerX / spacing) * spacing;
    double startY = floor(centerY / spacing) * spacing;
    double startZ = floor(centerZ / spacing) * spacing;

    int count = static_cast<int>(std::ceil(extent / spacing));

    constexpr double eps = 0.01;

    double base = 1.0;
    double fade = 1.0;

    Farbe farColor(0.0, 0.0, 0.2);
    farColor = invertColor(farColor);
    Farbe nearColor(0.8, 0.9, 1.0);
    nearColor = invertColor(nearColor);

    // Boden
    if (isPerspective())
    {
        constexpr double groundY = -0.1;
        const int tiles = 100;

        for (int ix = 0; ix < tiles; ++ix)
        {
            for (int iz = 0; iz < tiles; ++iz)
            {
                double cx = 0; // cam[0].position.x;
                double cz = 0; // cam[0].position.z;

                double x0 = cx - extent + ix * (2.0 * extent) / tiles;
                double x1 = cx - extent + (ix + 1) * (2.0 * extent) / tiles;

                double z0 = cz - extent + iz * (2.0 * extent) / tiles;
                double z1 = cz - extent + (iz + 1) * (2.0 * extent) / tiles;

                Vector3D A(x0, groundY, z0);
                Vector3D B(x1, groundY, z0);
                Vector3D C(x1, groundY, z1);
                Vector3D D(x0, groundY, z1);

                // Verlauf vorne -> hinten
                double t0 = double(iz) / tiles;
                double t1 = double(iz + 1) / tiles;

                Farbe color0(
                    farColor.r * (1.0 - t0) + nearColor.r * t0,
                    farColor.g * (1.0 - t0) + nearColor.g * t0,
                    farColor.b * (1.0 - t0) + nearColor.b * t0);

                Farbe color1(
                    farColor.r * (1.0 - t1) + nearColor.r * t1,
                    farColor.g * (1.0 - t1) + nearColor.g * t1,
                    farColor.b * (1.0 - t1) + nearColor.b * t1);

                // erstes Dreieck
                draw3DTriangle(
                    A, B, C,
                    color0,
                    color0,
                    color1);

                // zweites Dreieck
                draw3DTriangle(
                    A, C, D,
                    color0,
                    color1,
                    color1);
            }
        }
    }

    // zentrale Achsen
    draw3DLine(
        Vector3D(centerX - extent, 0, 0),
        Vector3D(centerX + extent, 0, 0),
        Farbe(0.2, 0.2, 0.8)); // X

    if (!isPerspective())
        draw3DLine(
            Vector3D(0, centerY - extent, 0),
            Vector3D(0, centerY + extent, 0),
            Farbe(0.2, 0.8, 0.2)); // Y

    draw3DLine(
        Vector3D(0, 0, centerZ - extent),
        Vector3D(0, 0, centerZ + extent),
        Farbe(0.8, 0.2, 0.2)); // Z

    // Zehner-Linien
    for (int i = -count; i <= count; ++i)
    {
        double pX = startX + i * spacing;
        double pY = startY + i * spacing;
        double pZ = startZ + i * spacing;

        // Nur Linien innerhalb der Bodenfläche zeichnen
        if (pX < centerX - extent || pX > centerX + extent)
            continue;

        if (pZ < centerZ - extent || pZ > centerZ + extent)
            continue;

        if (std::abs(pX) < eps ||
            std::abs(pY) < eps ||
            std::abs(pZ) < eps)
        {
            continue;
        }

        bool majorLine = (std::llround(pX / spacing) % 10 == 0); // Zentrum im Ursprung
        // bool majorLine = (std::abs(i) % 10 == 0); // Zentrum im LookAt-Punkt

        base = majorLine ? 0.10 : 0.40;

        Farbe c(
            base * fade,
            base * fade,
            base * fade);

        Vector3D camPos = cam[0].position;
        Vector3D viewDir = (cam[0].lookAt - camPos).normalized();

        Vector3D point(0.0, 0.0, pZ);

        // Projektion auf Blickrichtung
        double depth = extent - (point - camPos) * viewDir;

        double t = std::clamp(depth / extent, 0.0, 1.0);

        c = Farbe(
            nearColor.r + t * (farColor.r - nearColor.r),
            nearColor.g + t * (farColor.g - nearColor.g),
            nearColor.b + t * (farColor.b - nearColor.b));

        c = invertColor(c);

        c.r *= fade;
        c.g *= fade;
        c.b *= fade;

        // Oben-Ansicht (XZ)
        // Horizontal (X)
        draw3DLine(
            Vector3D(centerX - extent, 0, pZ),
            Vector3D(centerX + extent, 0, pZ),
            c);

        // Vertikal (Z)
        draw3DLine(
            Vector3D(pX, 0, centerZ - extent),
            Vector3D(pX, 0, centerZ + extent),
            c);

        if (!isPerspective())
        {
            // Seiten-Ansicht (YZ)
            // Horizontal (Z)
            draw3DLine(
                Vector3D(0, pY, centerZ - extent),
                Vector3D(0, pY, centerZ + extent),
                c);

            // Vertikal (Y)
            draw3DLine(
                Vector3D(0, centerY - extent, pZ),
                Vector3D(0, centerY + extent, pZ),
                c);

            // Front-Ansicht (XY)
            // Horizontal (X)
            draw3DLine(
                Vector3D(centerX - extent, pY, 0),
                Vector3D(centerX + extent, pY, 0),
                c);

            // Vertikal (Y)
            draw3DLine(
                Vector3D(pX, centerY - extent, 0),
                Vector3D(pX, centerY + extent, 0),
                c);
        }
    }
}

bool Wireframe::draw3DTriangle(
    const Vector3D &a,
    const Vector3D &b,
    const Vector3D &c,
    const Farbe &ca,
    const Farbe &cb,
    const Farbe &cc)
{
    ScreenPoint pa, pb, pc;

    if (!projectPoint(a, pa))
        return false;

    if (!projectPoint(b, pb))
        return false;

    if (!projectPoint(c, pc))
        return false;

    Vertex va{
        (double)pa.x,
        (double)pa.y,
        pa.z,
        ca};

    Vertex vb{
        (double)pb.x,
        (double)pb.y,
        pb.z,
        cb};

    Vertex vc{
        (double)pc.x,
        (double)pc.y,
        pc.z,
        cc};

    drawFilledTriangle(va, vb, vc);

    return true;
}

void Wireframe::drawFilledTriangle(
    const Vertex &v0,
    const Vertex &v1,
    const Vertex &v2)
{
    int minX = std::max(0, (int)std::floor(std::min({v0.x, v1.x, v2.x})));

    int maxX = std::min(w - 1, (int)std::ceil(std::max({v0.x, v1.x, v2.x})));

    int minY = std::max(0, (int)std::floor(std::min({v0.y, v1.y, v2.y})));

    int maxY = std::min(h - 1, (int)std::ceil(std::max({v0.y, v1.y, v2.y})));

    double area =
        (v1.x - v0.x) * (v2.y - v0.y) -
        (v2.x - v0.x) * (v1.y - v0.y);

    if (std::abs(area) < 1e-10)
        return;

    for (int y = minY; y <= maxY; ++y)
    {
        for (int x = minX; x <= maxX; ++x)
        {
            double w0 =
                ((v1.x - x) * (v2.y - y) -
                 (v2.x - x) * (v1.y - y)) /
                area;

            double w1 =
                ((v2.x - x) * (v0.y - y) -
                 (v0.x - x) * (v2.y - y)) /
                area;

            double w2 = 1.0 - w0 - w1;

            if (w0 >= 0 &&
                w1 >= 0 &&
                w2 >= 0)
            {
                double z =
                    w0 * v0.z +
                    w1 * v1.z +
                    w2 * v2.z;

                Farbe c(
                    w0 * v0.color.r +
                        w1 * v1.color.r +
                        w2 * v2.color.r,

                    w0 * v0.color.g +
                        w1 * v1.color.g +
                        w2 * v2.color.g,

                    w0 * v0.color.b +
                        w1 * v1.color.b +
                        w2 * v2.color.b);

                // setPixel(x, y, z, c);
                setPixelAlpha(x, y, z, c, 0.25);
            }
        }
    }
}

// ======================================================================
// Haupt-Renderloop
// ======================================================================

void Wireframe::run()
{
    if (rendernRunning || !FensterDirty)
        return;

    const int frameTime = 1000 / 30; // ~30 FPS

    // Key-Handler setzen
    fenster.setKeyHandler([this](WPARAM key)
                          { this->onKey(key); });

    while (fenster.isOpen())
    {
        DWORD start = GetTickCount();

        // Fenster-Events abarbeiten
        fenster.processEvents();

        // Bildschirm löschen
        // for (int y = 0; y < h; ++y)
        //    for (int x = 0; x < w; ++x)
        //        fenster.pixel(x, y, Farbe(0,0,0));

        // Fenstergröße ermitteln
        int width = fenster.breite();
        int height = fenster.hoehe();

        // Z-Buffer und Framebuffer für diesen Frame initialisieren
        initBuffers(width, height);

        // Szene zeichnen

        std::vector<object *> objectsCopy;
        {
            std::lock_guard<std::mutex> lock(sceneMutex);
            objectsCopy = sceneobjects;
        }

        for (auto &obj : objectsCopy)
        {
            if (!obj || obj->versteckt) // ausgeblendet (Objektliste)
                continue;

            DrawMode mode = DrawMode::NORMAL;

            if (selectedType == SelectedType::OBJECT && obj == selectedObject)
                // mode = DrawMode::HIGHLIGHT;
                continue;
            else if (hoverType == SelectedType::OBJECT && obj == hoverObject)
                // mode = DrawMode::HOVER;
                continue;

            if (useFlat)
            {
                obj->drawFlat(*this, mode);
                // obj->drawWireframePixels(*this, mode);
            }
            else
            {
                obj->drawWireframePixels(*this, mode);
            }
        }

        for (auto &obj : objectsCopy)
        {
            // nochmals durch alle Objekte und die hover oder selektierten nochmals darüber zeichnen
            if (!obj)
                continue;

            if (!(selectedType == SelectedType::OBJECT && obj == selectedObject) && !(hoverType == SelectedType::OBJECT && obj == hoverObject))
                continue;

            DrawMode mode = DrawMode::NORMAL;

            if (selectedType == SelectedType::OBJECT && obj == selectedObject)
                mode = DrawMode::HIGHLIGHT;
            else if (hoverType == SelectedType::OBJECT && obj == hoverObject)
                mode = DrawMode::HOVER;

            if (useFlat)
            {
                obj->drawFlat(*this, mode);
                // obj->drawWireframePixels(*this, mode);
            }
            else
                obj->drawWireframePixels(*this, mode);
            surrounding(obj->mat, width, height);
        }

        for (auto &light : lights)
        {
            DrawMode mode = DrawMode::NORMAL;

            if (selectedType == SelectedType::LIGHT &&
                light.get() == selectedLight)
                mode = DrawMode::HIGHLIGHT;

            else if (hoverType == SelectedType::LIGHT &&
                     light.get() == hoverLight)
                mode = DrawMode::HOVER;

            if (useFlat)
            {
                light->drawFlat(*this, mode);
            }
            else
            {
                light->drawWireframePixels(*this, mode);
            }
            lichtMarkierungZeichnen(*light, mode);
        }

        // Kamera
        DrawMode mode = DrawMode::NORMAL;
        if (selectedType == SelectedType::CAMERA)
            mode = DrawMode::HIGHLIGHT;
        else if (hoverType == SelectedType::CAMERA || hoverType == SelectedType::LOOKAT)
            mode = DrawMode::HOVER;

        if (useFlat)
        {
            cam[0].drawFlat(*this, mode);
            // cam[0].drawWireframePixels(*this, mode);
        }
        else
            cam[0].drawWireframePixels(*this, mode);

        // Update des Fensters
        /*for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
                fenster.pixel(x, y, frameBuffer[y * w + x]);*/

        POINT p;
        GetCursorPos(&p);

        HWND hwndUnderMouse = WindowFromPoint(p);

        if (hwndUnderMouse == fenster.getHWND())
        {

            Farbe cross(1, 0, 0);

            if (overObject)
                cross = Farbe(0, 1, 1);

            // horizontale Linie
            for (int dx = -10; dx <= 10; dx++)
            {
                setPixel(mouseX + dx, mouseY, -1e9, cross);
            }

            // vertikale Linie
            for (int dy = -10; dy <= 10; dy++)
            {
                setPixel(mouseX, mouseY + dy, -1e9, cross);
            }
        }

        drawGrid();

        fenster.copyBuffer(frameBuffer.data());

        fenster.update();

        // Frame-Time anpassen für ~60 FPS
        DWORD elapsed = GetTickCount() - start;
        if (elapsed < frameTime)
            Sleep(frameTime - elapsed);
    }
}

bool Wireframe::sameColor(
    const Farbe& a,
    const Farbe& b)
{
    constexpr double eps = 0.05;

    return
        std::abs(a.r - b.r) < eps &&
        std::abs(a.g - b.g) < eps &&
        std::abs(a.b - b.b) < eps;
}

void Wireframe::surrounding(Material mat, int w, int h)
{
    const Farbe base  = mat.diffuse;
    const Farbe base1 = invertColorHighlight(base);
    const Farbe base2 = invertColor(base);

    // Original lesen, in Kopie schreiben
    std::vector<Farbe> newBuffer = frameBuffer;

    for (int y = 1; y < h - 1; ++y)
    {
        for (int x = 1; x < w - 1; ++x)
        {
            const int index = y * w + x;

            const Farbe& pixel = frameBuffer[index];

            // Nur Pixel betrachten, die base1 oder base2 sind
            bool isOutlinePixel =
                sameColor(pixel, base1) ||
                sameColor(pixel, base2);

            if (!isOutlinePixel)
                continue;

            bool edge = false;

            for (int dy = -1; dy <= 1 && !edge; ++dy)
            {
                for (int dx = -1; dx <= 1; ++dx)
                {
                    if (dx == 0 && dy == 0)
                        continue;

                    const int neighborIndex =
                        (y + dy) * w + (x + dx);

                    const Farbe& neighbor =
                        frameBuffer[neighborIndex];

                    bool neighborIsOutline =
                        sameColor(neighbor, base1) ||
                        sameColor(neighbor, base2);

                    if (!neighborIsOutline)
                    {
                        edge = true;
                        break;
                    }
                }
            }

            // Randpixel einfärben
            if (!edge)
            {
                newBuffer[index] = base;
            }
        }
    }

    frameBuffer.swap(newBuffer);
}

Vector3D Wireframe::getPixelCenter(const std::vector<Vector3D> &pixels)
{
    Vector3D center(0, 0, 0);
    if (!pixels.empty())
    {
        for (const auto &p : pixels)
        {
            center += Vector3D(p);
        }

        center = center * (1.0 / static_cast<float>(pixels.size()));
    }
    return center;
}

// ======================================================================
// Projektion und Maus-Strahl – fuer alle Ansichten aus ansicht() berechnet
// ======================================================================

void Wireframe::project(const Vector3D &p, int &x, int &y)
{
    double z;
    project(p, x, y, z);
}

void Wireframe::project(const Vector3D &p, int &x, int &y, double &z)
{
    // GPU-Aufzeichnung: Weltpunkt merken, x enthaelt den Index darauf
    if (aufzeichnung)
    {
        x = aufzeichnung->punkt(p);
        y = 0;
        z = aufzeichnung->zWert;
        return;
    }

    AnsichtParameter a = ansicht();
    Vector3D rel = p - a.auge;

    double px = rel * a.rechts;
    double py = rel * a.oben;
    double pz = rel * a.vorne; // Tiefe: kleiner = naeher am Betrachter

    if (!a.perspektivisch)
    {
        x = int(w * 0.5 + px * a.pixelProEinheit);
        y = int(h * 0.5 - py * a.pixelProEinheit);
        z = pz;
        return;
    }

    if (pz <= 0.0)
    {
        x = y = -10000;
        z = std::numeric_limits<double>::infinity();
        return;
    }

    double ndcX = px / (pz * a.tanHalbX);
    double ndcY = py / (pz * a.tanHalbY);

    x = int((ndcX + 1.0) * 0.5 * w);
    y = int((1.0 - ndcY) * 0.5 * h);
    z = pz;
}

Ray Wireframe::createRay(int x, int y)
{
    AnsichtParameter a = ansicht();

    if (a.perspektivisch)
    {
        double ndcX = 2.0 * x / w - 1.0;
        double ndcY = 1.0 - 2.0 * y / h;

        Vector3D dir = a.vorne + a.rechts * (ndcX * a.tanHalbX) + a.oben * (ndcY * a.tanHalbY);
        return Ray(a.auge, dir.normalized());
    }

    // Orthogonal: Strahl parallel zur Blickrichtung, Start vor der ganzen Szene
    double sx = (x - w * 0.5) / a.pixelProEinheit;
    double sy = -(y - h * 0.5) / a.pixelProEinheit;
    double zurueck = szenenRadius(a.auge) + 10.0;

    Vector3D origin = a.auge + a.rechts * sx + a.oben * sy - a.vorne * zurueck;

    // winzige Neigung wie bisher, damit keine Richtungskomponente exakt 0 ist
    Vector3D dir = a.vorne + Vector3D(1e-9, 1e-9, 1e-9);
    return Ray(origin, dir.normalized());
}

double Wireframe::szenenRadius(const Vector3D &mitte) const
{
    double r2 = 0.0;
    auto beruecksichtigen = [&](const Vector3D &p)
    {
        Vector3D d = p - mitte;
        r2 = std::max(r2, d * d);
    };

    beruecksichtigen(sceneMin);
    beruecksichtigen(sceneMax);
    beruecksichtigen(Vector3D(sceneMin.x, sceneMin.y, sceneMax.z));
    beruecksichtigen(Vector3D(sceneMin.x, sceneMax.y, sceneMin.z));
    beruecksichtigen(Vector3D(sceneMax.x, sceneMin.y, sceneMin.z));
    beruecksichtigen(Vector3D(sceneMax.x, sceneMax.y, sceneMin.z));
    beruecksichtigen(Vector3D(sceneMax.x, sceneMin.y, sceneMax.z));
    beruecksichtigen(Vector3D(sceneMin.x, sceneMax.y, sceneMax.z));

    for (const auto &l : lights)
        beruecksichtigen(l->getPosition());

    for (const auto &c : cam)
    {
        beruecksichtigen(c.position);
        beruecksichtigen(c.lookAt);
    }

    return std::sqrt(r2) + 100.0;
}

void Wireframe::setPixelAlpha(int x, int y, double z, const Farbe &color, double alpha)
{
    if (x < 0 || x >= w || y < 0 || y >= h)
        return;

    int index = y * w + x;

    if (z < zBuffer[index])
    {
        // Farbe, die bereits im Framebuffer liegt
        Farbe background = frameBuffer[index];

        // Alpha-Blending
        Farbe result(
            color.r * alpha + background.r * (1.0 - alpha),
            color.g * alpha + background.g * (1.0 - alpha),
            color.b * alpha + background.b * (1.0 - alpha));

        zBuffer[index] = z;
        frameBuffer[index] = result;
    }
}

// ======================================================================
// Linien zeichnen
// ======================================================================

void Wireframe::drawLine(int x0, int y0, double z0,
                         int x1, int y1, double z1,
                         const Material &mat)
{
    if (aufzeichnung)
    {
        aufzeichnung->linie(x0, x1, mat.diffuse);
        return;
    }

    int dx = abs(x1 - x0);
    int dy = abs(y1 - y0);
    int steps = std::max(dx, dy);

    if (steps == 0)
    {
        if (x0 >= 0 && x0 < w && y0 >= 0 && y0 < h)
            setPixel(x0, y0, z0, mat.diffuse);
        return;
    }

    for (int i = 0; i <= steps; ++i)
    {
        double t = (double)i / steps;

        int x = int(x0 + (x1 - x0) * t);
        int y = int(y0 + (y1 - y0) * t);
        double z = z0 + t * (z1 - z0);
        z -= std::max(1e-4, z * 1e-5); // kleiner Bias, damit Linie vor Fläche liegt

        if (x >= 0 && x < w && y >= 0 && y < h)
        {
            /*Farbe col = (i % 2 == 0)
                            ? mat.diffuse + Farbe(0.3, 0.3, 0.3)
                            : mat.diffuse;

            setPixel(x, y, z, col);*/
            setPixel(x, y, z, mat.diffuse);
        }
    }
}

Vector3D rotateAroundAxis(
    const Vector3D &v,
    const Vector3D &axis,
    double angleDeg)
{
    double a = angleDeg * M_PI / 180.0;
    Vector3D k = axis.normalized();

    return v * cos(a) + k.cross(v) * sin(a) + k * (k * v) * (1 - cos(a));
}

void Wireframe::onKey(WPARAM key)
{
    /*    HWND foreground = GetForegroundWindow();

        if (foreground != fenster.getHWND())
        {
            return;
        }
        if (key == VK_ADD || (key == VK_OEM_PLUS && (GetKeyState(VK_SHIFT) & 0x8000))) {
            scale *= 1.1;
            std::cout << "Zoom In, scale = " << scale << std::endl;
        }
        else if (key == VK_SUBTRACT || key == VK_OEM_MINUS) {
            scale /= 1.1;
            std::cout << "Zoom Out, scale = " << scale << std::endl;
        }

        if (scale < 0.001) scale = 0.001;

        if (key == VK_ESCAPE)
        {
            selectedType = SelectedType::NONE;
            selectedObject = nullptr;
            selectedLight = nullptr;
            selectedCamera = nullptr;

            Mausdrag.active = false;
        }*/

    /*if (key == VK_LEFT) {
        // 🔁 Kamera drehen
        double angle = 5.0; // 5 Grad pro Tastendruck

        Vector3D pos = cam[0].position;
        Vector3D look = cam[0].lookAt;

        Vector3D dir = look - pos;
        dir = rotateY(dir, -angle);

        cam[0].lookAt = pos + dir;
        cam[0].update();
    }

    else if (key == VK_RIGHT) {
        // 🔁 Kamera drehen
        double angle = 5.0; // 5 Grad pro Tastendruck

        Vector3D pos = cam[0].position;
        Vector3D look = cam[0].lookAt;

        Vector3D dir = look - pos;
        dir = rotateY(dir, angle);

        cam[0].lookAt = pos + dir;
        cam[0].update();
    }*/

    double step = 0.5;
    double sensitivity = 1;

    // Wenn SHIFT gedrückt, 10x schneller
    if (GetAsyncKeyState(VK_SHIFT) & 0x8000)
    {
        sensitivity *= 10.0;
    }

    // Wenn CTRL gedrückt, 100x schneller
    if (GetAsyncKeyState(VK_CONTROL) & 0x8000)
    {
        sensitivity *= 100.0;
    }

    double rotSpeed = 2.0; // Grad pro Tastendruck

    Vector3D look = cam[0].lookAt;
    Vector3D pos = cam[0].position;

    // Vektor von LookAt → Kamera
    Vector3D offset = pos - look;

    // Yaw: LINKS / RECHTS (um Welt-Up)
    if (key == VK_LEFT)
    {
        offset = rotateAroundAxis(offset, Vector3D(0, 1, 0), +rotSpeed);
    }
    else if (key == VK_RIGHT)
    {
        offset = rotateAroundAxis(offset, Vector3D(0, 1, 0), -rotSpeed);
    }

    // Pitch: HOCH / RUNTER (um Kamera-Rechtsvektor)
    Vector3D right = cam[0].u;

    if (key == VK_UP)
    {
        offset = rotateAroundAxis(offset, right, +rotSpeed);
    }
    else if (key == VK_DOWN)
    {
        offset = rotateAroundAxis(offset, right, -rotSpeed);
    }

    // Neue Kameraposition
    cam[0].position = look + offset;
    cam[0].update();
}

PickResult Wireframe::pick(const Ray &ray)
{
    PickResult best;

    // ===== Objekte =====
    // Pick-Radius sollte sich mit scale anpassen, damit Objekte auch bei geändertem Zoom still pickbar sind
    // Bei scale > 1.0 (zoom in): Objekte sind weiter auseinander → größerer Radius nötig
    // Bei scale < 1.0 (zoom out): Objekte sind näher beieinander → kleinerer Radius nötig
    const double BASE_PICK_RADIUS = 6.0;
    double PICK_RADIUS_PIXELS = 6.0;

    for (auto &obj : sceneobjects)
    {
        if (obj->versteckt) // ausgeblendet: nicht anklickbar
            continue;
        // ===== 1. Screen-Space Test =====
        int sx, sy;
        double sz;

        project(obj->position, sx, sy, sz);

        int dx = sx - mouseX;
        int dy = sy - mouseY;

        double dist2D = std::sqrt(dx * dx + dy * dy);

        if (dist2D > PICK_RADIUS_PIXELS)
            continue;

        // ===== 2. Feiner Test =====
        Hit hit;
        obj->intersect(ray, hit);

        double finalT = 1e20;
        Vector3D finalPos;

        if (hit.hit && hit.t > 0)
        {
            finalT = hit.t;
            finalPos = hit.position;
        }
        else
        {
            // fallback: Tiefe aus Projektion schätzen
            Vector3D toObj = obj->position - ray.origin;
            finalT = toObj * ray.direction;
            finalPos = obj->position;
        }

        // ===== 3. Bestes Objekt =====
        if (finalT > 0 && finalT < best.t)
        {
            best.t = finalT;
            best.type = SelectedType::OBJECT;
            best.ptr = obj;
            best.position = finalPos;
        }
    }

    // ===== Licht =====
    for (auto &light : lights)
    {
        int sx, sy;
        // project(light.position, sx, sy);
        project(light->getPosition(), sx, sy);

        int dx = sx - mouseX;
        int dy = sy - mouseY;

        double dist2D = std::sqrt(dx * dx + dy * dy);
        if (dist2D > PICK_RADIUS_PIXELS)
            continue;

        // Tiefe berechnen
        // Vector3D toL = light.position - ray.origin;
        Vector3D toL = light->getPosition() - ray.origin;
        double t = toL * ray.direction;

        if (t > 0 && t < best.t)
        {
            best.t = t;
            best.type = SelectedType::LIGHT;
            best.ptr = &light;
            // best.position = light.position;
            best.position = light->getPosition();
        }
    }

    // ===== Kamera =====
    {
        int sx, sy;
        project(cam[0].position, sx, sy);

        int dx = sx - mouseX;
        int dy = sy - mouseY;

        double dist2D = std::sqrt(dx * dx + dy * dy);
        if (dist2D <= PICK_RADIUS_PIXELS)
        {
            Vector3D toC = cam[0].position - ray.origin;
            double t = toC * ray.direction;

            if (t > 0 && t < best.t)
            {
                best.t = t;
                best.type = SelectedType::CAMERA;
                best.ptr = &cam[0];
                best.position = cam[0].position;
            }
        }
    }

    // ===== LookAt =====
    {
        int sx, sy;
        project(cam[0].lookAt, sx, sy);

        int dx = sx - mouseX;
        int dy = sy - mouseY;

        double dist2D = std::sqrt(dx * dx + dy * dy);
        if (dist2D <= PICK_RADIUS_PIXELS)
        {
            Vector3D toL = cam[0].lookAt - ray.origin;
            double t = toL * ray.direction;

            if (t > 0 && t < best.t)
            {
                best.t = t;
                best.type = SelectedType::LOOKAT;
                best.ptr = &cam[0];
                best.position = cam[0].lookAt;
            }
        }
    }

    return best;
}

void Wireframe::clearSelection()
{
    selectedObject = nullptr;
    selectedLight = nullptr;
    selectedCamera = nullptr;

    hoverObject = nullptr;
    hoverLight = nullptr;
    hoverCamera = nullptr;

    selectedType = SelectedType::NONE;
    hoverType = SelectedType::NONE;
}

void Wireframe::clearScene()
{
    std::lock_guard<std::mutex> lock(sceneMutex);
    sceneobjects.clear();
    clearSelection(); // jetzt kein Lock in clearSelection
}

bool Wireframe::intersectRaySphere(const Ray &ray,
                                   const Vector3D &center,
                                   double radius,
                                   double &tHit)
{
    Vector3D oc = ray.origin - center;

    double a = ray.direction * ray.direction;
    double b = 2.0 * (oc * ray.direction);
    double c = (oc * oc) - radius * radius;

    double discriminant = b * b - 4 * a * c;

    if (discriminant < 0)
        return false;

    double sqrtD = sqrt(discriminant);

    double t1 = (-b - sqrtD) / (2.0 * a);
    double t2 = (-b + sqrtD) / (2.0 * a);

    if (t1 > 0)
        tHit = t1;
    else if (t2 > 0)
        tHit = t2;
    else
        return false;

    return true;
}

std::vector<PickResult> Wireframe::pickAll(const Ray &ray)
{
    std::vector<PickResult> hits;
    std::vector<PickResult> gizmoHits;

    // ===== Objekte =====
    // Radius sollte sich mit scale anpassen für konsistentes Picking bei Zoom
    const double BASE_RADIUS = 6.0;

    // Bei orthogonalen Views: Screen-Space-Test zusätzlich zur Ray-Intersection
    const double SCREEN_PICK_RADIUS = 6.0;

    // Fangradius fuer Lichter/Kamera/LookAt: BASE_RADIUS Pixel, umgerechnet in Welteinheiten
    // im Abstand t vom Strahlursprung (vorher 6 * scale: wuchs beim Heranzoomen)
    AnsichtParameter a = ansicht();
    auto fangRadius = [&](double t)
    {
        return a.perspektivisch ? BASE_RADIUS * t * 2.0 * a.tanHalbY / std::max(h, 1)
                                : BASE_RADIUS / a.pixelProEinheit;
    };

    // Rauchboxen/Wolkenbereich (VolumenBox) treffen nur hier, nicht im Raytracer
    struct PickModus
    {
        PickModus(double fest, double jeAbstand)
        {
            volumenBoxPicken = true;
            volumenBoxFang = fest;
            volumenBoxFangJeAbstand = jeAbstand;
        }
        ~PickModus() { volumenBoxPicken = false; }
    } pickModus(a.perspektivisch ? 0.0 : BASE_RADIUS / std::max(a.pixelProEinheit, 1e-300),
                a.perspektivisch ? BASE_RADIUS * 2.0 * a.tanHalbY / std::max(h, 1) : 0.0);

    for (auto &obj : sceneobjects)
    {
        if (obj->versteckt) // ausgeblendet: nicht anklickbar
            continue;
        Hit hit;
        obj->intersect(ray, hit);
        if (hit.hit)
            overObject = true;
        else
            overObject = false;

        if (hit.hit && hit.t > 0.0)
        {
            PickResult r;
            r.t = hit.t;
            r.type = SelectedType::OBJECT;
            r.ptr = obj;
            r.position = hit.position;

            hits.push_back(r);
        }
        // Fallback für orthogonale Views: Screen-Space-basiertes Picking
        /*else if (!isPerspective())
        {
            int sx, sy;
            double sz;

            project(obj->position, sx, sy, sz);

            int dx = sx - mouseX;
            int dy = sy - mouseY;
            double dist2D = std::sqrt(dx * dx + dy * dy);

            if (dist2D <= SCREEN_PICK_RADIUS)
            {
                PickResult r;
                r.t = (obj->position - ray.origin).length(); // Tiefe schätzen
                r.type = SelectedType::OBJECT;
                r.ptr = obj;
                r.position = obj->position;

                hits.push_back(r);
            }
        }*/
    }

    // ===== Licht =====
    for (auto &light : lights)
    {
        double dist = distanceRayToPoint(ray, light->getPosition());
        Vector3D toL = light->getPosition() - ray.origin;
        double t = toL * ray.direction;

        if (t <= 0)
            continue;

        double radius = fangRadius(t) * 2.0; // so gross wie die Markierung (lichtMarkierungZeichnen)

        if (dist > radius)
            continue;

        PickResult r;
        r.t = t;
        r.type = SelectedType::LIGHT;
        r.ptr = light.get();
        r.position = light->getPosition();

        overObject = true;
        gizmoHits.push_back(r);
    }

    // ===== Kamera =====
    {
        Vector3D toC = cam[0].position - ray.origin;
        double t = toC * ray.direction;

        Vector3D closest = ray.origin + ray.direction * t;
        double dist = (cam[0].position - closest).length();

        if (t > 0 && dist < fangRadius(t))
        {
            PickResult r;
            r.t = t;
            r.type = SelectedType::CAMERA;
            r.ptr = &cam[0];
            r.position = cam[0].position;

            overObject = true;
            gizmoHits.push_back(r);
        }
    }

    // ===== LookAt =====
    {
        Vector3D toL = cam[0].lookAt - ray.origin;
        double t = toL * ray.direction;

        Vector3D closest = ray.origin + ray.direction * t;
        double dist = (cam[0].lookAt - closest).length();

        if (t > 0 && dist < fangRadius(t))
        {
            PickResult r;
            r.t = t;
            r.type = SelectedType::LOOKAT;
            r.ptr = &cam[0];
            r.position = cam[0].lookAt;

            overObject = true;
            gizmoHits.push_back(r);
        }
    }

    // ===== Sortieren =====
    std::sort(gizmoHits.begin(), gizmoHits.end(), [](auto &a, auto &b)
              { return a.t < b.t; });
    std::sort(hits.begin(), hits.end(), [](auto &a, auto &b)
              { return a.t < b.t; });

    // Gizmos zuerst, danach Objekte
    gizmoHits.insert(gizmoHits.end(), hits.begin(), hits.end());

    return gizmoHits;
}

static bool pinselZieht = false; // Terrain-Pinsel: Strich laeuft (eine Maus fuer alle Ansichten)

// Kette vom getroffenen Objekt der Szene bis zum innersten getroffenen Teil (Gruppen werden durchlaufen).
// ohneFell: Huellen der Fell-Imitation zaehlen nicht (der Pinsel soll den Koerper darunter treffen).
static std::vector<object *> trefferKette(object *oben, const Ray &ray, bool ohneFell)
{
    std::vector<object *> kette;
    if (!oben || (ohneFell && oben->texturPar().fellHuelle))
        return kette;
    kette.push_back(oben);
    while (auto *gr = dynamic_cast<Gruppe *>(kette.back()))
    {
        object *best = nullptr;
        double bestT = 1e300;
        for (auto &k : gr->kinder)
        {
            if (!k || k->versteckt || (ohneFell && k->texturPar().fellHuelle))
                continue;
            Hit h;
            k->intersect(ray, h);
            if (h.hit && h.t > 0 && h.t < bestT)
                bestT = h.t, best = k.get();
        }
        if (!best)
            break;
        kette.push_back(best);
    }
    if (ohneFell && kette.size() == 1 && dynamic_cast<Gruppe *>(kette[0]))
        kette.clear(); // Gruppe nur aus Fell-Huellen getroffen
    return kette;
}

std::vector<object *> Wireframe::objektKetteUnter(int x, int y, bool ohneFellHuellen)
{
    Ray ray = createRay(x, y);
    for (const PickResult &p : pickAll(ray)) // vorderstes Objekt (ohne Fell: das erste mit festem Teil)
    {
        if (p.type != SelectedType::OBJECT)
            continue;
        std::vector<object *> k = trefferKette(static_cast<object *>(p.ptr), ray, ohneFellHuellen);
        if (!k.empty())
            return k;
        if (!ohneFellHuellen)
            break;
    }
    return {};
}

void Wireframe::onMouseDown(int x, int y, Fenster &f)
{
    mouseDownX = x;
    mouseDownY = y;
    this->mouseX = x;
    this->mouseY = y;
    mousePressed = true;

    Mausdrag.active = false;

    // Drehpunkt (Rotate/Scale) angeklickt? Ziehen verschiebt ihn, Shift+Klick: zurueck zur Mitte.
    // Die Auswahl bleibt dabei, wie sie ist.
    if (drehpunktSichtbar())
    {
        const Vector3D dp = drehpunkt();
        int px, py;
        project(dp, px, py);
        if (std::hypot(double(x - px), double(y - py)) <= 9.0)
        {
            mousePressed = false;
            if (GetKeyState(VK_SHIFT) & 0x8000)
            {
                drehpunktZuruecksetzen();
                return;
            }
            drehpunktZiehen = true;
            drehpunktEbeneNormal = getDragPlaneNormal();
            drehpunktVersatz = Vector3D(0, 0, 0);
            Ray r = createRay(x, y);
            double t;
            if (intersect(r, dp, drehpunktEbeneNormal, t))
                drehpunktVersatz = dp - (r.origin + r.direction * t);
            drehpunktEbenePunkt = dp;
            return;
        }
    }

    // Terrain-Pinsel: Klick auf das Terrain beginnt einen Strich (daneben: normale Auswahl)
    if (terrainPinsel::gAktiv && terrainPinsel::gStart && terrainPinsel::gStart(createRay(x, y)))
    {
        pinselZieht = true;
        mousePressed = false; // kein Verschieben
        return;
    }

    // Material-Pinsel: Klick uebertraegt Material und Textur auf das innerste Teil (Umschalt: ganzes Objekt)
    if (materialPinsel::gAktiv && materialPinsel::gAnwenden)
    {
        std::vector<object *> kette = objektKetteUnter(x, y, true);
        if (!kette.empty())
        {
            mousePressed = false; // kein Verschieben
            materialPinsel::gAnwenden((GetKeyState(VK_SHIFT) & 0x8000) ? kette.front() : kette.back());
            return;
        }
    }

    // Kanten-Modus: Kante des Netzes anklicken (Strg: zur Auswahl dazu bzw. wieder weg)
    if (kanten::gModus)
    {
        mousePressed = false; // kein Ziehen
        kanteAnklicken(x, y, (GetKeyState(VK_CONTROL) & 0x8000) != 0);
        return;
    }

    Ray ray = createRay(x, y);
    auto hits = pickAll(ray);

    // ---------- Teile von Gruppen direkt in der Ansicht ----------
    // Alt+Klick: das innerste getroffene Teil; Doppelklick: eine Ebene tiefer als die jetzige Auswahl;
    // Strg+Alt+Klick: Teil zur Auswahl dazu bzw. weg. Ist ein Teil gewaehlt, zieht ein Klick darauf nur dieses Teil.
    {
        static ULONGLONG letzterKlick = 0;
        static int letztesX = -100000, letztesY = -100000;
        const ULONGLONG jetzt = GetTickCount64();
        const bool doppel = jetzt - letzterKlick <= ULONGLONG(GetDoubleClickTime()) && std::abs(x - letztesX) <= 4 &&
                            std::abs(y - letztesY) <= 4;
        letzterKlick = doppel ? 0 : jetzt; // ein dritter Klick zaehlt wieder als erster
        letztesX = x;
        letztesY = y;
        const bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
        if (!hits.empty() && hits[0].type == SelectedType::OBJECT)
        {
            // Kette vom getroffenen Objekt der Szene bis zum innersten getroffenen Teil
            std::vector<object *> kette{static_cast<object *>(hits[0].ptr)};
            while (auto *gr = dynamic_cast<Gruppe *>(kette.back()))
            {
                object *best = nullptr;
                double bestT = 1e300;
                for (auto &k : gr->kinder)
                {
                    if (!k || k->versteckt)
                        continue;
                    Hit h;
                    k->intersect(ray, h);
                    if (h.hit && h.t > 0 && h.t < bestT)
                        bestT = h.t, best = k.get();
                }
                if (!best)
                    break;
                kette.push_back(best);
            }
            std::vector<object *> vorher = ausgewaehlteObjekte();
            auto inKette = [&](object *o) { return std::find(kette.begin() + 1, kette.end(), o) != kette.end(); };
            object *ziel = nullptr;
            if (kette.size() > 1 && alt)
                ziel = kette.back();
            else if (kette.size() > 1 && doppel)
            {
                // eine Ebene unter der bisher gewaehlten Stufe dieser Kette (sonst die erste Ebene)
                size_t stufe = 0;
                for (size_t i = 0; i < kette.size(); ++i)
                    if (std::find(vorher.begin(), vorher.end(), kette[i]) != vorher.end())
                        stufe = i;
                ziel = kette[std::min(stufe + 1, kette.size() - 1)];
            }
            if (ziel)
            {
                rahmenMoeglich = rahmenAktiv = false;
                if ((GetKeyState(VK_CONTROL) & 0x8000))
                {
                    auswahlUmschalten(ziel);
                    mousePressed = false;
                }
                else
                    auswahlSetzen({ziel});
                lastMouseX = lastMouseY = -100000;
                return;
            }
            // gewaehltes Teil angeklickt: nur die gewaehlten Teile ziehen (die Gruppe bleibt)
            if (!(GetKeyState(VK_CONTROL) & 0x8000))
                for (object *v : vorher)
                    if (inKette(v))
                    {
                        std::vector<object *> alle = vorher;
                        alle.erase(std::find(alle.begin(), alle.end(), v));
                        alle.insert(alle.begin(), v);
                        auswahlSetzen(alle);
                        rahmenMoeglich = rahmenAktiv = false;
                        lastMouseX = x;
                        lastMouseY = y;
                        return;
                    }
        }
    }

    // Strg+Klick auf ein Objekt: zur Auswahl hinzufuegen bzw. wieder entfernen
    if ((GetKeyState(VK_CONTROL) & 0x8000) && !hits.empty() && hits[0].type == SelectedType::OBJECT)
    {
        auswahlUmschalten(static_cast<object *>(hits[0].ptr));
        mousePressed = false; // kein Ziehen
        lastMouseX = lastMouseY = -100000; // naechster Klick an derselben Stelle: kein Durchschalten
        return;
    }

    bool sameClickPos = (abs(x - lastMouseX) < 10 && abs(y - lastMouseY) < 10);

    //  Prüfen ob aktuelles Objekt angeklickt wurde
    bool clickedSelected = false;

    if (!hits.empty())
    {
        PickResult &front = hits[0];

        if (selectedType == front.type)
        {
            if (selectedType == SelectedType::OBJECT && front.ptr == selectedObject)
                clickedSelected = true;

            else if (selectedType == SelectedType::LIGHT && front.ptr == selectedLight)
                clickedSelected = true;

            else if ((selectedType == SelectedType::CAMERA || selectedType == SelectedType::LOOKAT) && front.ptr == selectedCamera)
                clickedSelected = true;
        }
    }

    //  Wenn auf aktuelles Objekt geklickt, KEIN Cycling
    /*if (clickedSelected)
    {
        return; //  wichtig!
    }*/

    //  Normales Picking / Cycling
    if (!sameClickPos)
    {
        lastHits = hits;
        pickIndex = 0;
    }
    else
    {
        pickIndex++;
    }

    // Reset (Mehrfachauswahl merken: Klick auf eines ihrer Objekte behaelt sie)
    std::vector<object *> ausgewaehlteObjekteVorKlick = ausgewaehlteObjekte();
    auswahlSetzen({});

    // Klick ins Leere: Ziehen spannt einen Auswahlrahmen auf (Strg: bisherige Auswahl bleibt)
    rahmenMoeglich = hits.empty();
    rahmenAktiv = false;
    rahmenX0 = rahmenX1 = x;
    rahmenY0 = rahmenY1 = y;
    rahmenVorher.clear();
    if (rahmenMoeglich && (GetKeyState(VK_CONTROL) & 0x8000))
        rahmenVorher = ausgewaehlteObjekteVorKlick;
    selectedObject = nullptr;
    selectedLight = nullptr;
    selectedCamera = nullptr;
    selectedType = SelectedType::NONE;

    if (!lastHits.empty())
    {
        pickIndex %= lastHits.size();
        PickResult &hit = lastHits[pickIndex];

        selectedType = hit.type;

        if (hit.type == SelectedType::OBJECT)
        {
            // Klick auf ein Objekt einer Mehrfachauswahl: alle zusammen ziehen
            auto alle = ausgewaehlteObjekteVorKlick;
            object *o = (object *)hit.ptr;
            if (std::find(alle.begin(), alle.end(), o) != alle.end() && !sameClickPos)
            {
                alle.erase(std::find(alle.begin(), alle.end(), o));
                alle.insert(alle.begin(), o);
                auswahlSetzen(alle);
            }
            else
                auswahlSetzen({o});
        }

        else if (hit.type == SelectedType::LIGHT)
            selectedLight = static_cast<light *>(hit.ptr);

        else
            selectedCamera = (Camera *)hit.ptr;
    }

    lastMouseX = x;
    lastMouseY = y;
}

void Wireframe::onMouseMove(int x, int y, Fenster &f)
{
    // Aktuelle Maus-Position speichern (wichtig für korrektes Picking bei geändertem Scale)
    this->mouseX = x;
    this->mouseY = y;

    // Terrain-Pinsel: Strich fortsetzen, solange die linke Maustaste gedrueckt ist
    if (pinselZieht)
    {
        if (!(GetKeyState(VK_LBUTTON) & 0x8000) || !terrainPinsel::gAktiv)
        {
            pinselZieht = false;
            if (terrainPinsel::gEnde)
                terrainPinsel::gEnde();
            return;
        }
        if (terrainPinsel::gZiehen)
            terrainPinsel::gZiehen(createRay(x, y));
        return;
    }

    // Ortho-Ansichten mit gedrueckter mittlerer Maustaste verschieben
    if (panAktiv && !isPerspective())
    {
        AnsichtParameter a = ansicht();
        int dx = x - panX, dy = y - panY;
        panX = x;
        panY = y;
        if (a.pixelProEinheit > 0)
            ansichtsMitteVerschieben((a.rechts * double(-dx) + a.oben * double(dy)) * (1.0 / a.pixelProEinheit));
        return;
    }

    // Drehpunkt ziehen: in der Ebene der Ansicht durch seine bisherige Lage
    if (drehpunktZiehen)
    {
        Ray r = createRay(x, y);
        double t;
        if (intersect(r, drehpunktEbenePunkt, drehpunktEbeneNormal, t))
            drehpunktSetzen(r.origin + r.direction * t + drehpunktVersatz);
        return;
    }

    // ------------------------------
    // 1. Hover-Erkennung (ohne Drag)
    // ------------------------------
    Ray ray = createRay(x, y);
    auto hits = pickAll(ray);

    // Weltpunkt unter der Maus fuer die Statusleiste
    {
        AnsichtParameter a = ansicht();
        MausPosition m;
        m.ansicht = a.perspektivisch ? L"Kamerasicht"
                    : std::fabs(a.vorne.y) > 0.5 ? L"Top"
                    : std::fabs(a.vorne.z) > 0.5 ? L"Front"
                                                 : L"Side";
        if (!hits.empty())
        {
            m.gueltig = true;
            m.punkt = hits[0].position;
            m.achse[0] = m.achse[1] = m.achse[2] = true;
        }
        else if (!a.perspektivisch)
        {
            // ohne Treffer nur die beiden Koordinaten der Bildebene
            m.gueltig = true;
            m.punkt = ray.origin;
            m.achse[0] = std::fabs(a.vorne.x) < 0.5;
            m.achse[1] = std::fabs(a.vorne.y) < 0.5;
            m.achse[2] = std::fabs(a.vorne.z) < 0.5;
        }
        gMausPosition = m;
    }

    if (!hits.empty() && Mausdrag.active == false)
    {
        PickResult &front = hits[0]; // Vorderstes Element
        hoverType = front.type;

        hoverObject = (front.type == SelectedType::OBJECT) ? (object *)front.ptr : nullptr;
        hoverLight = (front.type == SelectedType::LIGHT) ? (light *)front.ptr : nullptr;
        hoverCamera = (front.type == SelectedType::CAMERA || front.type == SelectedType::LOOKAT) ? (Camera *)front.ptr : nullptr;
    }
    else
    {
        hoverType = SelectedType::NONE;
        hoverObject = nullptr;
        hoverLight = nullptr;
        hoverCamera = nullptr;
    }

    // ------------------------------
    // 2. Drag-Code
    // ------------------------------
    if (!mousePressed)
        return;

    int dx = x - mouseDownX;
    int dy = y - mouseDownY;
    double dist = std::sqrt(dx * dx + dy * dy);

    // Auswahlrahmen (Maus wurde auf freier Flaeche gedrueckt)
    if (rahmenMoeglich)
    {
        if (!rahmenAktiv && dist < DISTANCE_THRESHOLD)
            return;
        rahmenAktiv = true;
        rahmenX1 = x;
        rahmenY1 = y;
        return;
    }

    // Rotate/Scale: nur Objekte, eigene Behandlung
    if (selectedAction != SelectedAction::MOVE)
    {
        drehenOderSkalieren(x, y, dist);
        return;
    }

    if (!Mausdrag.active)
    {
        if (dist < DISTANCE_THRESHOLD)
            return;
        if (selectedType == SelectedType::NONE)
            return;

        Mausdrag.active = true;
        Mausdrag.hasLastWorldPos = false;
        Mausdrag.planeNormal = getDragPlaneNormal();

        if (selectedType == SelectedType::OBJECT)
            aenderungBeginnen("Verschieben", ausgewaehlteObjekte());
        else if (selectedType == SelectedType::LIGHT && selectedLight)
            aenderungBeginnen("Licht verschieben", {}, {selectedLight});
        Vector3D pos = getSelectedPosition();
        Mausdrag.planePoint = pos;

        double t;
        Ray ray2 = createRay(x, y);
        if (!intersect(ray2, Mausdrag.planePoint, Mausdrag.planeNormal, t))
            return;

        Mausdrag.offset = pos - (ray2.origin + ray2.direction * t);
        Mausdrag.lastWorldPos = pos;
        Mausdrag.hasLastWorldPos = true;
        Mausdrag.startPos = pos;
        Mausdrag.startX = mouseDownX;
        Mausdrag.startY = mouseDownY;
        einschnappenVorbereiten();
    }

    if (!Mausdrag.active)
        return;

    double t;
    if (!intersect(ray, Mausdrag.planePoint, Mausdrag.planeNormal, t))
        return;

    Vector3D hit = ray.origin + ray.direction * t;
    Vector3D newPos = hit + Mausdrag.offset;

    // Shift: nur in eine Richtung – waagrecht oder senkrecht auf dem Bildschirm, je nachdem, in welche
    // Richtung die Maus seit dem Druecken weiter gezogen wurde (jederzeit umschaltbar durch Loslassen von Shift)
    if (GetKeyState(VK_SHIFT) & 0x8000)
    {
        const Vector3D d = newPos - Mausdrag.startPos;
        const Vector3D n = Mausdrag.planeNormal.normalized();
        Vector3D waagrecht = ansicht().rechts;
        waagrecht = waagrecht - n * (waagrecht * n); // Bildschirm-Rechts in der Zugebene
        if (waagrecht.length() < 1e-9)
            waagrecht = ansicht().oben.cross(n);
        waagrecht = waagrecht.normalized();
        const bool horizontal = std::abs(x - Mausdrag.startX) >= std::abs(y - Mausdrag.startY);
        const Vector3D entlang = waagrecht * (d * waagrecht);
        newPos = Mausdrag.startPos + (horizontal ? entlang : d - entlang);
    }

    // Einschnappen an Kanten anderer Objekte (und am Boden y = 0); ALT gedrueckt: frei
    if (selectedType == SelectedType::OBJECT && Mausdrag.snapBereit && !(GetKeyState(VK_MENU) & 0x8000))
        newPos = Mausdrag.startPos + einschnappen(newPos - Mausdrag.startPos, (GetKeyState(VK_SHIFT) & 0x8000) != 0);

    // ------------------------------
    // 3. Verschiebe ausgewähltes Objekt oder Mesh
    // ------------------------------
    if (selectedType == SelectedType::OBJECT && selectedObject)
    {
        // alle ausgewaehlten Objekte um dasselbe Stueck (translate verschiebt auch Mesh-Dreiecke)
        Vector3D delta = newPos - Mausdrag.lastWorldPos;
        {
            std::lock_guard<std::mutex> lock(sceneMutex); // GPU-Ansichten lesen die Geometrie
            for (object *o : ausgewaehlteObjekte())
                o->translate(delta);
        }
        Mausdrag.lastWorldPos = newPos;
    }
    else
    {
        setSelectedPosition(newPos);
        Mausdrag.lastWorldPos = newPos;
    }

    computeSceneBounds();
}

// ======================================================================
// Kanten-Modus: naechste Kante unter der Maus waehlen
// ======================================================================

void Wireframe::kanteAnklicken(int x, int y, bool dazu)
{
    // Pixel einer Weltposition (wie project(), aber ohne die GPU-Aufzeichnung)
    const AnsichtParameter a = ansicht();
    auto bild = [&](const Vector3D &p, double &px, double &py, double &tiefe)
    {
        const Vector3D rel = p - a.auge;
        const double rx = rel * a.rechts, ry = rel * a.oben;
        tiefe = rel * a.vorne;
        if (!a.perspektivisch)
        {
            px = w * 0.5 + rx * a.pixelProEinheit;
            py = h * 0.5 - ry * a.pixelProEinheit;
            return true;
        }
        if (tiefe <= 1e-9)
            return false;
        px = (rx / (tiefe * a.tanHalbX) + 1.0) * 0.5 * w;
        py = (1.0 - ry / (tiefe * a.tanHalbY)) * 0.5 * h;
        return true;
    };

    object *o = kanten::gObjekt;
    std::vector<std::vector<Dreieck> *> listen;
    const std::vector<object *> auswahl = ausgewaehlteObjekte(); // nur ein ausgewaehltes (also vorhandenes) Netz
    if (o && std::find(auswahl.begin(), auswahl.end(), o) == auswahl.end())
        o = nullptr;
    if (o)
        listen = objektDreiecke(o);
    double besterAbstand = 9.0, besteTiefe = 1e300;
    kanten::Kante beste{};
    bool gefunden = false;
    if (!listen.empty())
        for (const kanten::Kante &k : kanten::alleKanten(listen))
        {
            double ax, ay, ta, bx, by, tb;
            if (!bild(k.a, ax, ay, ta) || !bild(k.b, bx, by, tb))
                continue;
            const double d = kanten::abstandZurStrecke(x, y, ax, ay, bx, by);
            const double tiefe = 0.5 * (ta + tb);
            // naeher an der Maus; bei fast gleichem Abstand die vordere Kante
            if (d < besterAbstand - 1.0 || (d < besterAbstand + 1.0 && tiefe < besteTiefe && d < 9.0))
            {
                besterAbstand = std::min(besterAbstand, d);
                besteTiefe = tiefe;
                beste = k;
                gefunden = true;
            }
        }

    if (!gefunden)
    {
        // keine Kante: ein anderes Netz anklicken wechselt das Objekt, ins Leere leert die Auswahl
        auto hits = pickAll(createRay(x, y));
        std::lock_guard<std::mutex> lock(kanten::gMutex);
        if (!hits.empty() && hits[0].type == SelectedType::OBJECT && hits[0].ptr != o)
        {
            kanten::gObjekt = static_cast<object *>(hits[0].ptr);
            auswahlSetzen({kanten::gObjekt});
        }
        if (!dazu)
            kanten::gGewaehlt.clear();
        ++kanten::gStand;
        return;
    }
    std::lock_guard<std::mutex> lock(kanten::gMutex);
    const double tol = kanten::toleranz(listen);
    auto gleicheKante = [&](const kanten::Kante &k)
    {
        return (kanten::gleich(k.a, beste.a, tol) && kanten::gleich(k.b, beste.b, tol)) ||
               (kanten::gleich(k.a, beste.b, tol) && kanten::gleich(k.b, beste.a, tol));
    };
    auto it = std::find_if(kanten::gGewaehlt.begin(), kanten::gGewaehlt.end(), gleicheKante);
    if (dazu)
    {
        if (it != kanten::gGewaehlt.end())
            kanten::gGewaehlt.erase(it); // Strg: abwaehlen
        else
            kanten::gGewaehlt.push_back(beste);
    }
    else
        kanten::gGewaehlt = {beste};
    ++kanten::gStand;
}

// ======================================================================
// Einschnappen beim Verschieben
// ======================================================================

// Beim Start des Ziehens: Grenzen der Auswahl und aller anderen Objekte (auch der Teile von Gruppen, ohne die
// Gruppen, in denen die Auswahl selbst steckt)
void Wireframe::einschnappenVorbereiten()
{
    Mausdrag.snapZiele.clear();
    Mausdrag.snapBereit = false;
    std::vector<object *> auswahl = ausgewaehlteObjekte();
    if (auswahl.empty() || !auswahlGrenzen(auswahl, Mausdrag.startMin, Mausdrag.startMax))
        return;
    std::vector<object *> ausgeschlossen = auswahl; // die Auswahl und alle Gruppen um sie herum
    for (object *o : auswahl)
        for (Gruppe *g = gruppeVon(o); g; g = gruppeVon(g))
            ausgeschlossen.push_back(g);
    std::function<void(object *)> sammeln = [&](object *o)
    {
        if (Mausdrag.snapZiele.size() > 20000 || o->versteckt)
            return;
        if (std::find(auswahl.begin(), auswahl.end(), o) != auswahl.end())
            return; // bewegt sich mit
        const bool umgibtAuswahl = std::find(ausgeschlossen.begin(), ausgeschlossen.end(), o) != ausgeschlossen.end();
        if (!umgibtAuswahl && !dynamic_cast<VolumenBox *>(o))
        {
            Vector3D mn = o->position, mx = o->position;
            o->getWorldAABB(mn, mx);
            Mausdrag.snapZiele.push_back({mn, mx});
        }
        if (auto *g = dynamic_cast<Gruppe *>(o))
            for (auto &k : g->kinder)
                sammeln(k.get());
    };
    {
        std::lock_guard<std::mutex> lock(sceneMutex);
        for (auto &o : scene)
            sammeln(o.get());
    }
    Mausdrag.snapBereit = true;
}

// Verschiebung d (seit dem Start) so anpassen, dass eine Kante der Auswahl auf einer Kante eines anderen
// Objekts (oder dem Boden) liegt, wenn sie auf dem Bildschirm weniger als etwa 10 Pixel davon entfernt ist.
// Nur in Richtungen, in denen gerade verschoben wird; andere Objekte zaehlen nur, wenn sie in der zweiten
// Bewegungsrichtung in der Naehe liegen (sonst rastet es an weit entfernten Objekten ein).
Vector3D Wireframe::einschnappen(Vector3D d, bool nurBewegteAchsen) const
{
    const Vector3D mn = Mausdrag.startMin + d, mx = Mausdrag.startMax + d;
    const double proPixel = weltJePixel((mn + mx) * 0.5);
    if (!(proPixel > 0))
        return d;
    const double fang = 10.0 * proPixel;
    const Vector3D n = Mausdrag.planeNormal.normalized();
    const double nk[3] = {std::fabs(n.x), std::fabs(n.y), std::fabs(n.z)};
    double groesse = 0;
    for (int k = 0; k < 3; ++k)
        groesse = std::max(groesse, mx[k] - mn[k]);
    const double nah = std::max(groesse, 40.0 * proPixel);
    for (int k = 0; k < 3; ++k)
    {
        if (nk[k] > 0.7)
            continue; // quer zur Zugebene: dort wird nicht verschoben
        if (nurBewegteAchsen && std::fabs(d[k]) < 1e-12)
            continue; // Shift: gesperrte Richtung bleibt
        double beste = fang, korrektur = 0;
        auto pruefen = [&](double c)
        {
            for (double kante : {mn[k], mx[k]})
                if (std::fabs(kante - c) < beste)
                {
                    beste = std::fabs(kante - c);
                    korrektur = c - kante;
                }
        };
        if (k == 1)
            pruefen(0.0); // Boden
        for (const auto &z : Mausdrag.snapZiele)
        {
            bool inDerNaehe = true;
            for (int j = 0; j < 3 && inDerNaehe; ++j)
                if (j != k && nk[j] <= 0.7) // nur die zweite Bewegungsrichtung (nicht die Bildtiefe)
                {
                    const double luecke = std::max({0.0, z.first[j] - mx[j], mn[j] - z.second[j]});
                    inDerNaehe = luecke <= nah;
                }
            if (!inDerNaehe)
                continue;
            pruefen(z.first[k]);
            pruefen(z.second[k]);
        }
        if (beste < fang)
        {
            if (k == 0)
                d.x += korrektur;
            else if (k == 1)
                d.y += korrektur;
            else
                d.z += korrektur;
        }
    }
    return d;
}

// ======================================================================
// Drehen und Skalieren mit der Maus
// ======================================================================


// Rotate: Ortho-Ansichten drehen um die Achse aus dem Bildschirm, das Objekt folgt der Maus
//         wie ein Drehknopf. Perspektive: waagrecht ziehen um Y, senkrecht um die waagrechte
//         Weltachse (X oder Z), die am ehesten quer zur Blickrichtung liegt.
//         Shift: 15-Grad-Raster.
// Scale:  von der Mitte weg ziehen vergroessert, zur Mitte hin verkleinert (gleichmaessig).
void Wireframe::drehenOderSkalieren(int x, int y, double dist)
{
    if (selectedType != SelectedType::OBJECT || !selectedObject)
        return;

    object *o = selectedObject;
    const std::vector<object *> objekte = ausgewaehlteObjekte();
    const bool raster = (GetKeyState(VK_SHIFT) & 0x8000) != 0;

    if (!Mausdrag.active)
    {
        if (dist < DISTANCE_THRESHOLD)
            return;

        Mausdrag = DragState();
        Mausdrag.active = true;

        // Drehpunkt: Mitte aller ausgewaehlten Objekte
        Vector3D mn, mx;
        // Drehpunkt: eigener Punkt (verschiebbare Markierung) bzw. Mitte aller ausgewaehlten Objekte
        Mausdrag.pivot = auswahlGrenzen(objekte, mn, mx) ? drehpunkt() : drehMitte(o);
        aenderungBeginnen(selectedAction == SelectedAction::SCALE ? "Skalieren" : "Drehen", objekte);
        Mausdrag.letztesX = mouseDownX;
        Mausdrag.letztesY = mouseDownY;

        int px, py;
        project(Mausdrag.pivot, px, py);
        Mausdrag.letzterAbstand = std::hypot(double(mouseDownX - px), double(mouseDownY - py));
        Mausdrag.letzterAbstandX = std::fabs(double(mouseDownX - px));
        Mausdrag.letzterAbstandY = std::fabs(double(mouseDownY - py));

        // Ortho: Startrichtung vom Punkt, an dem die Maus gedrueckt wurde
        if (!isPerspective())
        {
            Vector3D n = getDragPlaneNormal();
            Ray start = createRay(mouseDownX, mouseDownY);
            double t;
            if (std::hypot(double(mouseDownX - px), double(mouseDownY - py)) > 8.0 &&
                intersect(start, Mausdrag.pivot, n, t))
            {
                Mausdrag.letzteRichtung = start.origin + start.direction * t - Mausdrag.pivot;
                Mausdrag.hatRichtung = true;
            }
        }
    }

    const Vector3D pivot = Mausdrag.pivot;
    int px, py;
    project(pivot, px, py);

    // Hilfslinien fuer diese Ansicht (gezeichnet in GpuAnsichten)
    griffArt = selectedAction == SelectedAction::SCALE ? 2 : 1;
    griffX = px;
    griffY = py;
    griffStartX = mouseDownX;
    griffStartY = mouseDownY;
    griffJeRichtung = selectedAction == SelectedAction::SCALE && !isPerspective() && !raster;
    const double abstand = std::hypot(double(x - px), double(y - py));

    // Winkelsumme auf das Objekt anwenden (mit Shift im 15-Grad-Raster)
    auto anwenden = [&](int i, int achse)
    {
        double ziel = raster ? std::round(Mausdrag.winkelSumme[i] / 15.0) * 15.0 : Mausdrag.winkelSumme[i];
        double schritt = ziel - Mausdrag.winkelAngewandt[i];
        if (std::fabs(schritt) > 1e-9)
        {
            for (object *ob : objekte)
                objektDrehen(ob, achse, schritt, pivot);
            Mausdrag.winkelAngewandt[i] = ziel;
        }
    };

    {
        std::lock_guard<std::mutex> lock(sceneMutex); // GPU-Ansichten lesen die Geometrie

        if (selectedAction == SelectedAction::SCALE && !isPerspective() && !raster)
        {
            // Ortho: waagrecht ziehen skaliert entlang der Bild-Rechts-Achse, senkrecht entlang
            // der Bild-Oben-Achse (z.B. Top: X und Z); die Achse aus dem Bild bleibt
            AnsichtParameter a = ansicht();
            double ax = std::fabs(double(x - px)), ay = std::fabs(double(y - py));
            double fx = std::clamp(std::exp((ax - Mausdrag.letzterAbstandX) / 150.0), 0.5, 2.0);
            double fy = std::clamp(std::exp((ay - Mausdrag.letzterAbstandY) / 150.0), 0.5, 2.0);
            Mausdrag.letzterAbstandX = ax;
            Mausdrag.letzterAbstandY = ay;

            auto achseVon = [](const Vector3D &d)
            {
                return std::fabs(d.x) > 0.5 ? 0 : std::fabs(d.y) > 0.5 ? 1 : 2;
            };
            double f[3] = {1.0, 1.0, 1.0};
            f[achseVon(a.rechts)] = fx;
            f[achseVon(a.oben)] = fy;
            Vector3D faktor(f[0], f[1], f[2]);

            if ((faktor - Vector3D(1, 1, 1)).length() > 1e-9)
                for (object *ob : objekte)
                    objektSkalieren(ob, faktor, pivot);
        }
        else if (selectedAction == SelectedAction::SCALE)
        {
            // Perspektive, oder mit Shift: gleichmaessig in alle Richtungen
            double faktor = std::exp((abstand - Mausdrag.letzterAbstand) / 150.0);
            faktor = std::clamp(faktor, 0.5, 2.0);
            Mausdrag.letzterAbstand = abstand;
            if (std::fabs(faktor - 1.0) > 1e-9)
                for (object *ob : objekte)
                    objektSkalieren(ob, Vector3D(faktor, faktor, faktor), pivot);
        }
        else if (!isPerspective())
        {
            // Achse aus dem Bildschirm: Top Y, Front Z, Side X
            Vector3D n = getDragPlaneNormal();
            int achse = std::fabs(n.x) > 0.5 ? 0 : std::fabs(n.y) > 0.5 ? 1 : 2;
            Vector3D a = drehAchse(achse);

            Ray ray = createRay(x, y);
            double t;
            if (abstand > 8.0 && intersect(ray, pivot, a, t)) // zu nah an der Mitte: Winkel springt
            {
                Vector3D v = ray.origin + ray.direction * t - pivot;
                if (Mausdrag.hatRichtung)
                {
                    Vector3D u = Mausdrag.letzteRichtung;
                    Mausdrag.winkelSumme[0] += std::atan2(a * u.cross(v), u * v) * 180.0 / M_PI;
                }
                Mausdrag.letzteRichtung = v;
                Mausdrag.hatRichtung = true;
                anwenden(0, achse);
            }
        }
        else
        {
            int dx = x - Mausdrag.letztesX;
            int dy = y - Mausdrag.letztesY;
            Mausdrag.letztesX = x;
            Mausdrag.letztesY = y;

            Vector3D blick = !cam.empty() ? (pivot - cam[0].position).normalized() : Vector3D(0, 0, 1);

            // Bildschirm-"unten" in Weltkoordinaten (Ebene durch die Mitte quer zur Blickrichtung)
            Vector3D unten(0, -1, 0);
            {
                Ray r0 = createRay(px, py), r1 = createRay(px, py + 20);
                double t0, t1;
                if (intersect(r0, pivot, blick, t0) && intersect(r1, pivot, blick, t1))
                {
                    Vector3D d = (r1.origin + r1.direction * t1) - (r0.origin + r0.direction * t0);
                    if (d.length() > 1e-12)
                        unten = d.normalized();
                }
            }

            Vector3D mn = pivot, mx = pivot;
            auswahlGrenzen(objekte, mn, mx);
            double groesse = std::max((mx - mn).length() * 0.5, 1e-6);

            // Vorzeichen ausmessen: der vordere/untere Punkt des Objekts soll der Maus folgen
            auto vorzeichen = [&](int achse, bool waagrecht)
            {
                Vector3D a = drehAchse(achse);
                Vector3D d = unten - blick;
                d = d - a * (a * d);
                if (d.length() < 1e-9)
                    return 1.0;
                Vector3D q = d.normalized() * groesse;
                int x0, y0, x1, y1;
                project(pivot + q, x0, y0);
                project(pivot + drehenUmAchse(q, a, 5.0), x1, y1);
                int bewegung = waagrecht ? x1 - x0 : y1 - y0;
                return bewegung >= 0 ? 1.0 : -1.0;
            };

            const double gradProPixel = 0.5;

            if (dx != 0)
            {
                Mausdrag.winkelSumme[0] += dx * gradProPixel * vorzeichen(1, true);
                anwenden(0, 1);
            }
            if (dy != 0)
            {
                Vector3D quer = blick.cross(Vector3D(0, 1, 0));
                int achse = std::fabs(quer.x) >= std::fabs(quer.z) ? 0 : 2;
                Mausdrag.winkelSumme[1] += dy * gradProPixel * vorzeichen(achse, false);
                anwenden(1, achse);
            }
        }
    }

    computeSceneBounds();
}

void Wireframe::onMouseUp()
{
    if (pinselZieht)
    {
        pinselZieht = false;
        mousePressed = false;
        if (terrainPinsel::gEnde)
            terrainPinsel::gEnde();
        return;
    }
    if (drehpunktZiehen)
    {
        drehpunktZiehen = false;
        mousePressed = false;
        return;
    }
    if (rahmenAktiv)
        rahmenAuswaehlen();
    else if (rahmenMoeglich && !rahmenVorher.empty())
        auswahlSetzen(rahmenVorher); // Strg+Klick ins Leere: Auswahl behalten
    rahmenMoeglich = false;
    rahmenAktiv = false;
    rahmenVorher.clear();
    griffArt = 0;

    if (Mausdrag.active)
        aenderungAbschliessen(); // Rueckgaengig: Zustand nach dem Ziehen
    mousePressed = false;
    Mausdrag.active = false;
}

Vector3D Wireframe::getSelectedPosition()
{
    if (selectedType == SelectedType::OBJECT && selectedObject)
        return selectedObject->position;

    if (selectedType == SelectedType::LIGHT && selectedLight)
        return selectedLight->getPosition();

    if (selectedType == SelectedType::CAMERA && selectedCamera)
        return selectedCamera->position;

    if (selectedType == SelectedType::LOOKAT && selectedCamera)
        return selectedCamera->lookAt;

    return Vector3D();
}

void Wireframe::setSelectedPosition(const Vector3D &pos)
{
    if (selectedType == SelectedType::OBJECT && selectedObject)
    {
        Vector3D delta = pos - selectedObject->position;
        selectedObject->translate(delta);
    }

    else if (selectedType == SelectedType::LIGHT && selectedLight)
        selectedLight->translate(selectedLight->getPosition() - pos);

    else if (selectedType == SelectedType::CAMERA && selectedCamera)
    {
        selectedCamera->position = pos;
        selectedCamera->update();
    }

    else if (selectedType == SelectedType::LOOKAT && selectedCamera)
    {
        selectedCamera->lookAt = pos;
        selectedCamera->update();
    }
}

// Ortho-Ansichten: Mausrad zoomt wie '+'/'-' (globales scale).
// Vorher wurde hier cam[0].fov veraendert, also das Sichtfeld der Raytracing-Kamera.
void Wireframe::onMouseWheel(int delta, Fenster &f)
{
    double zoomStep = 0.1;

    // SHIFT = feiner
    if (GetAsyncKeyState(VK_SHIFT) & 0x8000)
        zoomStep = 0.02;

    // CTRL = schneller
    if (GetAsyncKeyState(VK_CONTROL) & 0x8000)
        zoomStep = 0.3;

    double rasten = delta / double(WHEEL_DELTA);
    scale *= std::pow(1.0 + zoomStep, rasten);

    if (scale < 0.001)
        scale = 0.001;
}

void Wireframe::initBuffers(int width, int height)
{
    w = width;
    h = height;

    zBuffer.assign(
        w * h,
        std::numeric_limits<double>::infinity());

    frameBuffer.resize(w * h);

    if (isPerspective())
    {
        // Unten hellgrau, oben dunkler
        for (int y = 0; y < h; y++)
        {
            double t = double(y) / double(h - 1);

            double c =
                0.18 * (1.0 - t * t) + // oben
                0.35 * t * t;          // unten

            Farbe col(c, c, c);

            for (int x = 0; x < w; x++)
            {
                setPixel(x, y, 100000, col);
                // frameBuffer[y * w + x] = col;
            }
        }
    }
    else
    {
        // Orthogonale Ansichten
        frameBuffer.assign(
            w * h,
            Farbe(0.22, 0.22, 0.22));
    }
}

void Wireframe::setPixel(
    int x,
    int y,
    double z,
    const Farbe &color)
{
    if (x < 0 || x >= w || y < 0 || y >= h)
        return;

    int index = y * w + x;

    if (z < zBuffer[index])
    {
        zBuffer[index] = z;
        frameBuffer[index] = color;
    }
}

void Wireframe::fillTriangleZ(
    int x0, int y0, double z0,
    int x1, int y1, double z1,
    int x2, int y2, double z2,
    const Material &mat)
{
    // Bounding box
    int minX = std::max(0, std::min({x0, x1, x2}));
    int maxX = std::min(w - 1, std::max({x0, x1, x2}));
    int minY = std::max(0, std::min({y0, y1, y2}));
    int maxY = std::min(h - 1, std::max({y0, y1, y2}));
    minX = std::max(0, minX);
    maxX = std::min(w - 1, maxX);

    // Vektor-Koordinaten
    double det = (y1 - y2) * (x0 - x2) + (x2 - x1) * (y0 - y2);
    if (det == 0)
        return; // Degeneriertes Dreieck

    int step = 1;
    for (int y = minY; y <= maxY; y = y + step)
    {
        for (int x = minX; x <= maxX; x = x + step)
        {
            // Baryzentrische Koordinaten
            double px = x + 0.5;
            double py = y + 0.5;

            double w0 = ((y1 - y2) * (px - x2) + (x2 - x1) * (py - y2)) / det;
            double w1 = ((y2 - y0) * (px - x2) + (x0 - x2) * (py - y2)) / det;
            double w2 = 1.0 - w0 - w1;

            const double EPS = -1e-6;
            if (w0 > EPS && w1 > EPS && w2 > EPS)
            {
                double z = w0 * z0 + w1 * z1 + w2 * z2; // lineare Interpolation
                setPixel(x, y, z, mat.diffuse);
            }
        }
    }
}

void Wireframe::clear()
{
    zBuffer.assign(w * h, std::numeric_limits<double>::infinity());
}

void Wireframe::fillTriangle(
    int x0, int y0, double z0,
    int x1, int y1, double z1,
    int x2, int y2, double z2,
    const Material &mat)
{
    if (aufzeichnung)
    {
        aufzeichnung->dreieck(x0, x1, x2, mat.diffuse);
        return;
    }

    // =========================================================
    // Bounding box
    // =========================================================
    int minX = std::max(0, std::min({x0, x1, x2}));
    int maxX = std::min(w - 1, std::max({x0, x1, x2}));
    int minY = std::max(0, std::min({y0, y1, y2}));
    int maxY = std::min(h - 1, std::max({y0, y1, y2}));

    auto edge = [](double ax, double ay,
                   double bx, double by,
                   double px, double py)
    {
        return (bx - ax) * (py - ay) - (by - ay) * (px - ax);
    };

    // =========================================================
    // Triangle area
    // =========================================================
    double area = edge(x0, y0, x1, y1, x2, y2);

    if (area == 0.0)
        return;

    // CCW fix
    if (area < 0.0)
    {
        std::swap(x1, x2);
        std::swap(y1, y2);
        std::swap(z1, z2);
        area = -area;
    }

    double invArea = 1.0 / area;

    // =========================================================
    // Edge coefficients (incremental)
    // =========================================================
    const double A01 = y0 - y1;
    const double A12 = y1 - y2;
    const double A20 = y2 - y0;

    const double B01 = x1 - x0;
    const double B12 = x2 - x1;
    const double B20 = x0 - x2;

    // =========================================================
    // Pixel start (center)
    // =========================================================
    double px = minX + 0.5;
    double py = minY + 0.5;

    // =========================================================
    // Initial edge values
    // =========================================================
    double w0_row = edge(x1, y1, x2, y2, px, py);
    double w1_row = edge(x2, y2, x0, y0, px, py);
    double w2_row = edge(x0, y0, x1, y1, px, py);

    // =========================================================
    // Pointer optimization (IMPORTANT)
    // =========================================================
    Farbe *fb = frameBuffer.data();
    double *zb = zBuffer.data();

    int width = w;

    // =========================================================
    // Raster loop
    // =========================================================
    for (int y = minY; y <= maxY; y++)
    {
        double w0 = w0_row;
        double w1 = w1_row;
        double w2 = w2_row;

        int idx = y * width + minX;

        Farbe *fbRow = fb + idx;
        double *zRow = zb + idx;

        for (int x = minX; x <= maxX; x++)
        {
            // =================================================
            // Inside test (unchanged)
            // =================================================
            if (w0 >= 0.0 && w1 >= 0.0 && w2 >= 0.0)
            {
                double alpha = w0 * invArea;
                double beta = w1 * invArea;
                double gamma = w2 * invArea;

                double z =
                    alpha * z0 +
                    beta * z1 +
                    gamma * z2;

                // =================================================
                // Branch optimized Z-test (safe version)
                // =================================================
                if (z < *zRow)
                {
                    *zRow = z;
                    *fbRow = mat.diffuse;
                }
            }

            // =================================================
            // Incremental edge stepping
            // =================================================
            w0 += A12;
            w1 += A20;
            w2 += A01;

            fbRow++;
            zRow++;
        }

        // =====================================================
        // next row
        // =====================================================
        w0_row += B12;
        w1_row += B20;
        w2_row += B01;
    }
}

double Wireframe::distanceRayToPoint(const Ray &ray, const Vector3D &p)
{
    Vector3D toP = p - ray.origin;
    double t = toP * ray.direction;

    Vector3D closest = ray.origin + ray.direction * t;
    return (p - closest).length();
}

// ======================================================================
// Lichtmarkierung: Kreis mit Strahlen in der Bildebene, Radius LICHT_PIXEL auf dem Bildschirm
// (die Lichter selbst zeichnen sich mit festem Weltradius und waren weit herausgezoomt kaum sichtbar)
// ======================================================================

double Wireframe::weltJePixel(const Vector3D &p) const
{
    AnsichtParameter a = ansicht();
    if (!a.perspektivisch)
        return 1.0 / std::max(a.pixelProEinheit, 1e-12);
    double t = (p - a.auge) * a.vorne;
    if (t <= 1e-6)
        return 0.0; // hinter oder in der Kamera
    return t * 2.0 * a.tanHalbY / std::max(h, 1);
}

void Wireframe::lichtMarkierungZeichnen(const light &l, DrawMode mode)
{
    const double LICHT_PIXEL = 10.0;
    AnsichtParameter a = ansicht();
    Vector3D p = l.getPosition();

    double proPixel;
    if (a.perspektivisch)
    {
        double t = (p - a.auge) * a.vorne;
        if (t <= 1e-6)
            return; // hinter der Kamera
        proPixel = t * 2.0 * a.tanHalbY / std::max(h, 1);
    }
    else
        proPixel = 1.0 / std::max(a.pixelProEinheit, 1e-12);
    const double r = LICHT_PIXEL * proPixel;

    // Farbe: Auswahl gelb, Hover cyan, sonst Lichtfarbe auf volle Helligkeit gebracht
    Farbe f = l.farbe;
    double hell = std::max({f.r, f.g, f.b});
    f = hell > 1e-6 ? Farbe(f.r / hell, f.g / hell, f.b / hell) : Farbe(1, 1, 1);
    if (mode == DrawMode::HIGHLIGHT)
        f = Farbe(1, 1, 0);
    else if (mode == DrawMode::HOVER)
        f = Farbe(0, 1, 1);
    const Material m = makeMaterialSimple(f);

    auto linie = [&](const Vector3D &u, const Vector3D &v)
    {
        int x0, y0, x1, y1;
        double z0, z1;
        project(u, x0, y0, z0);
        project(v, x1, y1, z1);
        drawLine(x0, y0, z0, x1, y1, z1, m);
    };
    auto richtung = [&](double w)
    {
        return a.rechts * std::cos(w) + a.oben * std::sin(w);
    };

    const int n = 24;
    for (int i = 0; i < n; ++i)
        linie(p + richtung(2 * M_PI * i / n) * r, p + richtung(2 * M_PI * (i + 1) / n) * r);

    for (int i = 0; i < 8; ++i) // Strahlen
    {
        Vector3D d = richtung(2 * M_PI * i / 8 + M_PI / 8);
        linie(p + d * (r * 1.3), p + d * (r * 1.8));
    }
}

// ======================================================================
// Auswahlrahmen auswerten: Objekte, deren Bounding-Box (auf den Bildschirm projiziert)
// ganz im Rahmen liegt (nach rechts gezogen) bzw. ihn beruehrt (nach links gezogen)
// ======================================================================

// Auswahlrahmen. Von links nach rechts gezogen: was ganz im Rahmen liegt. Liegt ein Objekt nur teilweise darin und
// ist es eine Gruppe, werden ihre Teile geprueft, die ganz darin liegen (auch tiefer verschachtelt) - so lassen sich
// z.B. die vier Pfoten einer Katze (auch mehrerer Katzen) waehlen, ohne die ganze Katze zu waehlen.
// Von rechts nach links: alles, was den Rahmen beruehrt (ganze Objekte).
void Wireframe::rahmenAuswaehlen()
{
    const int x0 = std::min(rahmenX0, rahmenX1), x1 = std::max(rahmenX0, rahmenX1);
    const int y0 = std::min(rahmenY0, rahmenY1), y1 = std::max(rahmenY0, rahmenY1);
    const bool ganz = rahmenX1 >= rahmenX0;
    const AnsichtParameter a = ansicht();

    std::vector<object *> treffer = rahmenVorher;

    // 0 = ausserhalb/unsichtbar, 1 = beruehrt den Rahmen, 2 = ganz darin
    auto lage = [&](object *o) -> int
    {
        Vector3D mn = o->position, mx = o->position;
        o->getWorldAABB(mn, mx);
        int sx0 = INT_MAX, sy0 = INT_MAX, sx1 = INT_MIN, sy1 = INT_MIN;
        bool alleSichtbar = true, eineSichtbar = false;
        for (int i = 0; i < 8; ++i)
        {
            Vector3D ecke((i & 1) ? mx.x : mn.x, (i & 2) ? mx.y : mn.y, (i & 4) ? mx.z : mn.z);
            if (a.perspektivisch && (ecke - a.auge) * a.vorne <= 1e-9)
            {
                alleSichtbar = false; // hinter der Kamera
                continue;
            }
            int px, py;
            project(ecke, px, py);
            sx0 = std::min(sx0, px);
            sy0 = std::min(sy0, py);
            sx1 = std::max(sx1, px);
            sy1 = std::max(sy1, py);
            eineSichtbar = true;
        }
        if (!eineSichtbar || !(sx1 >= x0 && sx0 <= x1 && sy1 >= y0 && sy0 <= y1))
            return 0;
        return (alleSichtbar && sx0 >= x0 && sx1 <= x1 && sy0 >= y0 && sy1 <= y1) ? 2 : 1;
    };
    auto dazu = [&](object *o)
    {
        if (std::find(treffer.begin(), treffer.end(), o) == treffer.end())
            treffer.push_back(o);
    };
    std::function<void(object *, bool)> pruefen = [&](object *o, bool teil)
    {
        if (!o || o->versteckt) // ausgeblendet: nicht im Auswahlrahmen
            return;
        if (teil && o->texturPar().fellHuelle)
            return; // Fell-Huellen folgen ihrem Koerperteil von selbst
        const int l = lage(o);
        if (l == 0)
            return;
        if (!ganz)
        {
            dazu(o);
            return;
        }
        if (l == 2)
        {
            dazu(o);
            return;
        }
        if (auto *g = dynamic_cast<Gruppe *>(o)) // teilweise darin: Teile, die ganz darin liegen
            for (auto &k : g->kinder)
                pruefen(k.get(), true);
    };
    for (object *o : sceneobjects)
        pruefen(o, false);

    auswahlSetzen(treffer);
}
