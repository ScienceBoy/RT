#include "camera.h"
#include "wireframe.h"
#include "fenster.h"
#include <cmath>
#include <iostream>
#include <math.h>
#include <csignal>
#include <iomanip>
#include "camera.h"

// Konstruktor
Camera::Camera(const Vector3D &pos, const Vector3D &lookAt, double fovDeg, double aspect, const Vector3D &upVec)
    : position(pos), lookAt(lookAt), up(upVec), fov(fovDeg), aspectRatio(aspect)
{
    w = (position - lookAt).normalized();
    u = up.cross(w).normalized();
    v = w.cross(u);

    double theta = fov * M_PI / 180.0;
    halfHeight = tan(theta / 2.0);
    halfWidth = aspectRatio * halfHeight;

    target = lookAt;
    
    // WICHTIG: yaw und pitch initialisieren basierend auf der aktuellen Position
    // So dass updateOrbit() mit diesen Werten die gleiche Position berechnet
    Vector3D delta = position - target;
    distance = delta.length();
    
    // pitch = arcsin(y / distance)
    pitch = std::asin(std::clamp(delta.y / distance, -1.0, 1.0));
    
    // yaw = atan2(x, z)
    yaw = std::atan2(delta.x, delta.z);
}

// Ray für Pixel
Ray Camera::makeRay(double x, double y, Fenster &f) const
{
    double nx = (x + 0.5) / f.breite();
    double ny = (y + 0.5) / f.hoehe();

    double sx = -(2 * nx - 1);
    double sy = -(2 * ny - 1);

    Vector3D direction = (-w) + u * (sx * halfWidth) + v * (sy * halfHeight);
    return Ray(position, direction.normalized());
}

void Camera::updateOrbit()
{
    position.x =
        target.x +
        distance * cos(pitch) * sin(yaw);

    position.y =
        target.y +
        distance * sin(pitch);

    position.z =
        target.z +
        distance * cos(pitch) * cos(yaw);

    updateDirection();
}



void Camera::drawFlat(Wireframe &wf, DrawMode mode) const
{
    int x0, y0, x1, y1, x2, y2;
    double z0, z1, z2;

    // ===== Farbe bestimmen =====
    Farbe baseColor = Farbe(1, 1, 1);

    if (mode == DrawMode::HIGHLIGHT)
        baseColor = Farbe(1, 1, 0);
    else if (mode == DrawMode::HOVER)
        baseColor = Farbe(0, 0, 1);
    else
        baseColor = Farbe(1, 0, 0);

    Material matHere = makeMaterialSimple(baseColor);

    // ===== Frustum berechnen =====
    // Groesse in Pixeln statt Welteinheiten (vorher fest 5: in grossen Szenen unsichtbar)
    const double jePixel = wf.weltJePixel(position); // 0: Ansicht schaut aus dieser Kamera (Frustum entfaellt)
    double d = 45.0 * jePixel;
    Vector3D center = position - w * d;

    Vector3D topLeft = center + v * (halfHeight * d) - u * (halfWidth * d);
    Vector3D topRight = center + v * (halfHeight * d) + u * (halfWidth * d);
    Vector3D bottomLeft = center - v * (halfHeight * d) - u * (halfWidth * d);
    Vector3D bottomRight = center - v * (halfHeight * d) + u * (halfWidth * d);

    // ===== Projektion =====
    int px, py, tlx, tly, trx, try_, blx, bly, brx, bry;
    double pz, tlz, trz, blz, brz;

    wf.project(position, px, py, pz);
    wf.project(topLeft, tlx, tly, tlz);
    wf.project(topRight, trx, try_, trz);
    wf.project(bottomLeft, blx, bly, blz);
    wf.project(bottomRight, brx, bry, brz);
    pz = -pz;
    tlz = -tlz;
    trz = -trz;
    blz = -blz;
    brz = -brz;

    if (pz > 0 && tlz > 0 && trz > 0 && blz > 0 && brz > 0)
    {
        // ===== Frustum füllen (4 Dreiecke) =====
        wf.fillTriangle(px, py, pz, tlx, tly, tlz, trx, try_, trz, matHere);
        wf.fillTriangle(px, py, pz, trx, try_, trz, brx, bry, brz, matHere);
        wf.fillTriangle(px, py, pz, brx, bry, brz, blx, bly, blz, matHere);
        wf.fillTriangle(px, py, pz, blx, bly, blz, tlx, tly, tlz, matHere);

        // ===== Rechteck vorne =====
        wf.fillTriangle(tlx, tly, tlz, trx, try_, trz, brx, bry, brz, matHere);
        wf.fillTriangle(tlx, tly, tlz, brx, bry, brz, blx, bly, blz, matHere);
    }
    // =========================================================
    // 🎯 LookAt-Würfel
    // =========================================================

    double size = 6.0 * (wf.weltJePixel(lookAt) > 0 ? wf.weltJePixel(lookAt) : jePixel); // 6 Pixel
    Vector3D c = lookAt;

    Vector3D p000 = c + Vector3D(-size, -size, -size);
    Vector3D p001 = c + Vector3D(-size, -size, size);
    Vector3D p010 = c + Vector3D(-size, size, -size);
    Vector3D p011 = c + Vector3D(-size, size, size);
    Vector3D p100 = c + Vector3D(size, -size, -size);
    Vector3D p101 = c + Vector3D(size, -size, size);
    Vector3D p110 = c + Vector3D(size, size, -size);
    Vector3D p111 = c + Vector3D(size, size, size);

    int x[8], y[8];
    double z[8];
    Vector3D pts[8] = {p000, p001, p010, p011, p100, p101, p110, p111};

    for (int i = 0; i < 8; i++)
        wf.project(pts[i], x[i], y[i], z[i]);

    // 6 Flächen (je 2 Dreiecke)
    auto quad = [&](int a, int b, int c, int d)
    {
        // wf.fillTriangle(x[a],y[a],z[a], x[b],y[b],z[b], x[c],y[c],z[c], matHere);
        // wf.fillTriangle(x[a],y[a],z[a], x[c],y[c],z[c], x[d],y[d],z[d], matHere);
    };

    quad(0, 1, 3, 2); // links
    quad(4, 5, 7, 6); // rechts
    quad(0, 1, 5, 4); // unten
    quad(2, 3, 7, 6); // oben
    quad(0, 2, 6, 4); // hinten
    quad(1, 3, 7, 5); // vorne

    // =========================================================
    // ➖ Linien behalten
    // =========================================================

    // Achsen + gestrichelte Linie NICHT füllen → weiterhin drawWireframe nutzen
    drawWireframePixels(wf, mode);
}

// Wireframe-Darstellung
void Camera::drawWireframePixels(Wireframe &wf, DrawMode mode) const
{
    int x0, y0, x1, y1;
    double z0, z1;
    // Groesse in Pixeln statt Welteinheiten (vorher fest 30: in grossen Szenen unsichtbar)
    const double jePixel = wf.weltJePixel(position);
    double d = 45.0 * jePixel;
    Vector3D center = position - w * d;

    Vector3D topLeft = center + v * (halfHeight * d) - u * (halfWidth * d);
    Vector3D topRight = center + v * (halfHeight * d) + u * (halfWidth * d);
    Vector3D bottomLeft = center - v * (halfHeight * d) - u * (halfWidth * d);
    Vector3D bottomRight = center - v * (halfHeight * d) + u * (halfWidth * d);

    auto drawEdge = [&](const Vector3D &a, const Vector3D &b)
    {
        Farbe col = mode == DrawMode::HIGHLIGHT ? Farbe(1, 1, 0)
                 : mode == DrawMode::HOVER ? Farbe(0, 0, 1)
                                           : Farbe(1, 1, 1);

        wf.project(a, x0, y0, z0);
        wf.project(b, x1, y1, z1);
        wf.drawLine(x0, y0, z0, x1, y1, z1, makeMaterialSimple(col));
    };

    auto drawEdgeColored = [&](const Vector3D &a, const Vector3D &b, const Farbe &col)
    {
        Farbe color;

        if (mode == DrawMode::HIGHLIGHT)
            color = Farbe(1, 1, 0); // Gelb für Auswahl
        else if (mode == DrawMode::HOVER)
            color = Farbe(0, 0, 1); // Cyan für Hover
        else
            color = col; // Standardfarbecol;

        wf.project(a, x0, y0, z0);
        wf.project(b, x1, y1, z1);
        wf.drawLine(x0, y0, z0, x1, y1, z1, makeMaterialSimple(color));
    };

    drawEdge(position, topLeft);
    drawEdge(position, topRight);
    drawEdge(position, bottomLeft);
    drawEdge(position, bottomRight);

    drawEdge(topLeft, topRight);
    drawEdge(topRight, bottomRight);
    drawEdge(bottomRight, bottomLeft);
    drawEdge(bottomLeft, topLeft);

    const double lookJePixel = wf.weltJePixel(lookAt);
    double size = 6.0 * lookJePixel; // Groesse des Wuerfels: 6 Pixel

    Vector3D c = lookAt;

    // 8 Eckpunkte
    Vector3D p000 = c + Vector3D(-size, -size, -size);
    Vector3D p001 = c + Vector3D(-size, -size, size);
    Vector3D p010 = c + Vector3D(-size, size, -size);
    Vector3D p011 = c + Vector3D(-size, size, size);
    Vector3D p100 = c + Vector3D(size, -size, -size);
    Vector3D p101 = c + Vector3D(size, -size, size);
    Vector3D p110 = c + Vector3D(size, size, -size);
    Vector3D p111 = c + Vector3D(size, size, size);

    // Kanten zeichnen von lookAt-Punkt
    Farbe lookCol = mode == DrawMode::HIGHLIGHT ? Farbe(1, 1, 0)
                  : mode == DrawMode::HOVER ? Farbe(0, 0, 1)
                                            : Farbe(1, 0, 0);

    if (!wf.isPerspective())
    {
        drawEdgeColored(p000, p001, lookCol);
        drawEdgeColored(p000, p010, lookCol);
        drawEdgeColored(p000, p100, lookCol);

        drawEdgeColored(p111, p110, lookCol);
        drawEdgeColored(p111, p101, lookCol);
        drawEdgeColored(p111, p011, lookCol);

        drawEdgeColored(p001, p011, lookCol);
        drawEdgeColored(p001, p101, lookCol);

        drawEdgeColored(p010, p011, lookCol);
        drawEdgeColored(p010, p110, lookCol);

        drawEdgeColored(p100, p101, lookCol);
        drawEdgeColored(p100, p110, lookCol);

        double s = 10.0;

        drawEdgeColored(c + Vector3D(-s, 0, 0), c + Vector3D(s, 0, 0), Farbe(1, 0, 0));
        drawEdgeColored(c + Vector3D(0, -s, 0), c + Vector3D(0, s, 0), Farbe(1, 0, 0));
        drawEdgeColored(c + Vector3D(0, 0, -s), c + Vector3D(0, 0, s), Farbe(1, 0, 0));
    }

    auto drawDashedLine = [&](const Vector3D &a, const Vector3D &b)
    {
        int segments = std::max(4, (int)(20 * scale));     // Anzahl Stücke
        double dashRatio = 0.5; // 50% sichtbar, 50% Lücke

        for (int i = 0; i < segments; i++)
        {
            double t0 = (double)i / segments;
            double t1 = (double)(i + dashRatio) / segments;
            if (t1 > 1.0)
                t1 = 1.0;

            if (i % 2 == 0) // jedes zweite Segment zeichnen
            {
                Vector3D p0 = a + (b - a) * t0;
                Vector3D p1 = a + (b - a) * t1;

                wf.project(p0, x0, y0, z0);
                wf.project(p1, x1, y1, z1);
                wf.drawLine(x0, y0, z0, x1, y1, z1, makeMaterialSimple(Farbe(0.7, 0.7, 0.7)));
            }
        }
    };

    if (wf.showLookAtHelper())
        drawDashedLine(position, position + (lookAt - position) * 2.0);
}

// Basisvektoren aktualisieren
void Camera::update()
{
    w = (position - lookAt).normalized();
    u = up.cross(w).normalized();
    v = w.cross(u);

    double theta = fov * M_PI / 180.0;
    halfHeight = tan(theta / 2);
    halfWidth = aspectRatio * halfHeight;

    target = lookAt;
}

// Richtung aus yaw/pitch
void Camera::updateDirection()
{
    Vector3D dir = position - target;

    w = dir.normalized();

    u = up.cross(w).normalized();
    v = w.cross(u).normalized();

    lookAt = target;
}




void Camera::setPosition(const Vector3D &pos)
{
    position = pos;
    update(); // Basisvektoren neu berechnen
}