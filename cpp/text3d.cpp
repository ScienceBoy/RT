#include "text3d.h"
#include "wireframe.h"
#include "camera.h"
#include <algorithm>
#include <cctype>
#include "mesh_factory.h"
#include "mesh.h"

// ============================================================
// KONSTRUKTOR
// ============================================================

Text3D::Text3D(const std::string& text,
               const Vector3D& pos,
               double size,
               double depth,
               const Material& mat)
    : object(mat),
      text(text),
      size(size),
      depth(depth)
{
    position = pos;

    buildFont();
    buildText();

    // ========================================
    // Bounding Sphere berechnen
    // ========================================

    boundingCenter = Vector3D(0,0,0);

    int count = 0;

    for(const auto& tri : tris)
    {
        boundingCenter += tri.a;
        boundingCenter += tri.b;
        boundingCenter += tri.c;

        count += 3;
    }

    if(count > 0)
        boundingCenter *= (1.0 / count);

    boundingRadius = 0.0;

    for(const auto& tri : tris)
    {
        boundingRadius =
            std::max(
                boundingRadius,
                (tri.a - boundingCenter).length());

        boundingRadius =
            std::max(
                boundingRadius,
                (tri.b - boundingCenter).length());

        boundingRadius =
            std::max(
                boundingRadius,
                (tri.c - boundingCenter).length());
    }
}

// ============================================================
// GLYPH GENERATION
// ============================================================

Glyph Text3D::makeGlyph(char c, double s)
{
    Glyph g;

    switch(std::toupper(static_cast<unsigned char>(c)))
    {
        case 'A': g.rects={{0,0,0.2,1},{0.8,0,1,1},{0.2,0.8,0.8,1},{0.2,0.45,0.8,0.6}}; break;
        case 'B': g.rects={{0,0,0.2,1},{0.2,0.8,0.8,1},{0.2,0.4,0.8,0.6},{0.2,0,0.8,0.2},{0.8,0.6,1,0.8},{0.8,0.2,1,0.4}}; break;
        case 'C': g.rects={{0,0,0.2,1},{0.2,0.8,1,1},{0.2,0,1,0.2}}; break;
        case 'D': g.rects={{0,0,0.2,1},{0.2,0.8,0.8,1},{0.2,0,0.8,0.2},{0.8,0.2,1,0.8}}; break;
        case 'E': g.rects={{0,0,0.2,1},{0.2,0.8,1,1},{0.2,0.4,0.8,0.6},{0.2,0,1,0.2}}; break;
        case 'F': g.rects={{0,0,0.2,1},{0.2,0.8,1,1},{0.2,0.4,0.8,0.6}}; break;
        case 'G': g.rects={{0,0,0.2,1},{0.2,0.8,1,1},{0.2,0,1,0.2},{0.8,0,1,0.6},{0.5,0.4,1,0.6}}; break;
        case 'H': g.rects={{0,0,0.2,1},{0.8,0,1,1},{0.2,0.4,0.8,0.6}}; break;
        case 'I': g.rects={{0,0.8,1,1},{0.4,0,0.6,1},{0,0,1,0.2}}; break;
        case 'J': g.rects={{0,0.8,1,1},{0.4,0,0.6,1},{0,0,0.5,0.2},{0,0,0.2,0.4}}; break;
        case 'K': g.rects={{0,0,0.2,1},{0.2,0.4,1,0.6},{0.6,0.6,1,1},{0.6,0,1,0.4}}; break;
        case 'L': g.rects={{0,0,0.2,1},{0.2,0,1,0.2}}; break;
        case 'M': g.rects={{0,0,0.2,1},{0.8,0,1,1},{0.35,0.5,0.5,1},{0.5,0.5,0.65,1}}; break;
        case 'N': g.rects={{0,0,0.2,1},{0.8,0,1,1},{0.2,0.4,0.8,0.6}}; break;
        case 'O': g.rects={{0,0,0.2,1},{0.8,0,1,1},{0.2,0.8,0.8,1},{0.2,0,0.8,0.2}}; break;
        case 'P': g.rects={{0,0,0.2,1},{0.2,0.8,1,1},{0.2,0.4,1,0.6},{0.8,0.6,1,0.8}}; break;
        case 'Q': g.rects={{0,0,0.2,1},{0.8,0,1,1},{0.2,0.8,0.8,1},{0.2,0,0.8,0.2},{0.6,0,1,0.4}}; break;
        case 'R': g.rects={{0,0,0.2,1},{0.2,0.8,1,1},{0.2,0.4,1,0.6},{0.8,0.6,1,0.8},{0.6,0,1,0.4}}; break;
        case 'S': g.rects={{0,0.8,1,1},{0,0.4,0.2,0.8},{0,0.4,1,0.6},{0.8,0,1,0.4},{0,0,1,0.2}}; break;
        case 'T': g.rects={{0,0.8,1,1},{0.4,0,0.6,0.8}}; break;
        case 'U': g.rects={{0,0,0.2,1},{0.8,0,1,1},{0.2,0,0.8,0.2}}; break;
        case 'V': g.rects={{0,0.4,0.2,1},{0.8,0.4,1,1},{0.4,0,0.6,0.5}}; break;
        case 'W': g.rects={{0,0,0.2,1},{0.8,0,1,1},{0.3,0,0.45,0.5},{0.55,0,0.7,0.5}}; break;
        case 'X': g.rects={{0,0,0.2,0.4},{0,0.6,0.2,1},{0.8,0,1,0.4},{0.8,0.6,1,1},{0.35,0.35,0.65,0.65}}; break;
        case 'Y': g.rects={{0,0.6,0.2,1},{0.8,0.6,1,1},{0.4,0,0.6,0.7},{0.2,0.4,0.8,0.6}}; break;
        case 'Z': g.rects={{0,0.8,1,1},{0,0,1,0.2},{0.2,0.4,0.8,0.6}}; break;
        case ' ': default: break;
    }

    return g;
}

// ============================================================
// FONT BUILD
// ============================================================

void Text3D::buildFont()
{
    font.clear();

    std::string chars =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ ";

    for(char c : chars)
    {
        font[c] = makeGlyph(c, size);
    }
}

// ============================================================
// TEXT BUILD
// ============================================================

void Text3D::buildText()
{
    tris.clear();

    double spacing = size * 1.2;

    for(size_t i = 0; i < text.size(); i++)
    {
        Vector3D offset(
            i * spacing,
            0,
            0);

        addGlyphMesh(text[i], offset);
    }
}

// ============================================================
// GLYPH → MESH
// ============================================================

void Text3D::addGlyphMesh(
    char c,
    const Vector3D& offset)
{
    char key =
        std::toupper(
            static_cast<unsigned char>(c));

    auto it = font.find(key);

    if(it == font.end())
        return;

    extrudeGlyph(it->second, offset);
}

// ============================================================
// GLYPH EXTRUSION VIA createWall()
// ============================================================

void Text3D::extrudeGlyph(
    const Glyph& g,
    const Vector3D& offset)
{
    for(const auto& r : g.rects)
    {
        Vector3D a(
            r.x0 * size,
            r.y0 * size,
            0);

        Vector3D b(
            r.x1 * size,
            r.y0 * size,
            0);

        Vector3D c(
            r.x1 * size,
            r.y1 * size,
            0);

        Vector3D d(
            r.x0 * size,
            r.y1 * size,
            0);

        a += offset;
        b += offset;
        c += offset;
        d += offset;

        scene.push_back(createWall(
            a,
            b,
            c,
            d,
            mat,
            50
        ));
    }
}


// ============================================
// POSITION UPDATE
// ============================================

void Text3D::setPosition(const Vector3D& pos)
{
    Vector3D delta = pos - position;
    position = pos;

    /*for(auto& t : tris)
    {
        t.a += delta;
        t.b += delta;
        t.c += delta;
    }*/

    minBound += delta;
    maxBound += delta;
}

// ============================================
// INTERSECTION
// ============================================

// ============================================
// Intersection (Picking)
// ============================================

void Text3D::intersect(const Ray& ray, Hit& hit) const
{    
    // BVH / AABB Early-Out
    if (!intersectBounding(ray))
        return;

    Ray localRay;
    localRay.origin = ray.origin - position;
    localRay.direction = ray.direction;

    for(const auto& t : tris)
    {
        t.intersect(localRay, hit);
    }

    if(hit.t > 0)
        hit.position = localRay.origin + localRay.direction * hit.t + position;
}

void Text3D::drawFlat(Wireframe& wf, DrawMode mode) const
{
    int x0,y0,x1,y1,x2,y2;
    double z0, z1, z2;
    //Material matHere = mat;

    // Material je nach DrawMode
    /*if (mode == DrawMode::HIGHLIGHT)
        matHere = makeMaterialSimple(Farbe(1,1,0));  // Gelb
    else if (mode == DrawMode::HOVER)
        matHere = makeMaterialSimple(Farbe(0,1,1));  // Cyan
    else
        matHere = makeMaterialSimple(mat.diffuse);   // Standardfarbe

    */

    // Alle Dreiecke füllen
    for(const auto& tri : tris)
    {
        Vector3D a = tri.a + position;
        Vector3D b = tri.b + position;
        Vector3D c = tri.c + position;

        Vector3D M = (a+b+c) * (1.0 / 3.0);

        Vector3D normale = (b-a).cross(c-a).normalized();
        Vector3D viewDir = (M - cam[0].position).normalized();
        Vector3D N = (normale * viewDir < 0) ? normale : -normale;

        // Licht (Weltlicht)
        //Vector3D L = (lights[0].position - M).normalized();
        Vector3D L(0,0,0);

        for (const auto& light : lights)
        {
            L += light->directionFrom(M);
        }

        L = L.normalized();

        double intensity = std::max(0.0, N * L);

        Farbe base =
            (mode == DrawMode::HIGHLIGHT) ? Farbe(1,1,0) :
            (mode == DrawMode::HOVER)     ? Farbe(0,1,1) :
                                            mat.diffuse;

        Farbe shaded(
            base.r * intensity,
            base.g * intensity,
            base.b * intensity
        );

        Material matHere = makeMaterialSimple(shaded);

        
        // 3D → 2D Projektion
        wf.project(a, x0, y0, z0);
        wf.project(b, x1, y1, z1);
        wf.project(c, x2, y2, z2);

        // Fläche füllen
        wf.fillTriangle(x0, y0, z0, x1, y1, z1, x2, y2, z2, matHere);

        wf.drawLine(x0,y0,z0,x1,y1,z1,matHere);
        wf.drawLine(x1,y1,z1,x2,y2,z2,matHere);
        wf.drawLine(x2,y2,z2,x0,y0,z0, matHere);
    }
}


// ============================================
// Wireframe zeichnen
// ============================================

void Text3D::drawWireframePixels(Wireframe& wf, DrawMode mode) const
{
    int x0,y0,x1,y1;
    double z0,z1;
    Material matHere = mat;

    if (mode == DrawMode::HIGHLIGHT)
        matHere = makeMaterialSimple(Farbe(1,1,0));  // Gelb für selektiertes Objekt
    else if (mode == DrawMode::HOVER)
        matHere = makeMaterialSimple(Farbe(0,1,1));  // Cyan für Hover
    else
        matHere = makeMaterialSimple(mat.diffuse);         // Standardfarbe

    for(const auto& tri : tris)
    {
        Vector3D a = tri.a + position;
        Vector3D b = tri.b + position;
        Vector3D c = tri.c + position;

        wf.project(a,x0,y0,z0); wf.project(b,x1,y1,z1); wf.drawLine(x0,y0,z0,x1,y1,z1,matHere);
        wf.project(b,x0,y0,z0); wf.project(c,x1,y1,z1); wf.drawLine(x0,y0,z0,x1,y1,z1,matHere);
        wf.project(c,x0,y0,z0); wf.project(a,x1,y1,z1); wf.drawLine(x0,y0,z0,x1,y1,z1,matHere);
    }
}

static std::vector<Vector3D> rect(
    double x0, double y0,
    double x1, double y1,
    double s)
{
    return {
        Vector3D(x0*s, y0*s, 0),
        Vector3D(x0*s, y1*s, 0),
        Vector3D(x1*s, y1*s, 0),
        Vector3D(x1*s, y0*s, 0),
        Vector3D(x0*s, y0*s, 0)
    };
    }

void Text3D::getWorldAABB(Vector3D& minOut, Vector3D& maxOut) const
{
    if (tris.empty()) return;

    minOut = tris[0].a + position;
    maxOut = tris[0].a + position;

    auto expand = [&](const Vector3D& p)
    {
        minOut.x = std::min(minOut.x, p.x);
        minOut.y = std::min(minOut.y, p.y);
        minOut.z = std::min(minOut.z, p.z);

        maxOut.x = std::max(maxOut.x, p.x);
        maxOut.y = std::max(maxOut.y, p.y);
        maxOut.z = std::max(maxOut.z, p.z);
    };

    for (const auto& t : tris)
    {
        expand(t.a + position);
        expand(t.b + position);
        expand(t.c + position);
    }
}