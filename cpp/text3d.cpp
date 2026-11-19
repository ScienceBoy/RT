#include "text3d.h"
#include "wireframe.h"
#include "camera.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include "rotations.h"
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
    name = text;

    buildFont();
    buildText();
    grenzenBerechnen();
}

// Bounding-Box in Weltkoordinaten, Bounding-Sphere (fuer Picking und Raytracing)
void Text3D::grenzenBerechnen()
{
    if (tris.empty())
    {
        minBound = maxBound = boundingCenter = position;
        boundingRadius = 0.0;
        return;
    }

    Vector3D lmin = tris[0].a, lmax = tris[0].a;
    auto erweitern = [&](const Vector3D& p)
    {
        lmin = Vector3D(std::min(lmin.x, p.x), std::min(lmin.y, p.y), std::min(lmin.z, p.z));
        lmax = Vector3D(std::max(lmax.x, p.x), std::max(lmax.y, p.y), std::max(lmax.z, p.z));
    };
    for (const auto& tri : tris)
    {
        erweitern(tri.a);
        erweitern(tri.b);
        erweitern(tri.c);
    }

    minBound = position + lmin;
    maxBound = position + lmax;
    boundingCenter = (minBound + maxBound) * 0.5;
    boundingRadius = (lmax - lmin).length() * 0.5;
}

void Text3D::setText(const std::string& neuerText)
{
    text = neuerText;
    name = text;
    buildText();
    grenzenBerechnen();
}

void Text3D::setAchsen(const Vector3D& x, const Vector3D& y, const Vector3D& z)
{
    achseX = x;
    achseY = y;
    achseZ = z;
    buildText();
    grenzenBerechnen();
}

void Text3D::rotate(const Vector3D& rotation)
{
    Vector3D mitte = (minBound + maxBound) * 0.5;

    double rx = rotation.x * M_PI / 180.0;
    double ry = rotation.y * M_PI / 180.0;
    double rz = rotation.z * M_PI / 180.0;

    auto rotP = [&](Vector3D p)
    {
        p = rotateX(p, rx);
        p = rotateY(p, ry);
        p = rotateZ(p, rz);
        return p;
    };

    position = mitte + rotP(position - mitte);
    setAchsen(rotP(achseX), rotP(achseY), rotP(achseZ));
}

void Text3D::scale(const Vector3D& scaleVec)
{
    Vector3D mitte = (minBound + maxBound) * 0.5;

    auto sclP = [&](const Vector3D& p)
    {
        return Vector3D(p.x * scaleVec.x, p.y * scaleVec.y, p.z * scaleVec.z);
    };

    position = mitte + sclP(position - mitte);
    setAchsen(sclP(achseX), sclP(achseY), sclP(achseZ));
}

void Text3D::setMaterial(const Material& m)
{
    mat = m;
    for (auto& t : tris)
        t.mat = m;
}

void Text3D::setFarbe(const Farbe& f)
{
    mat.diffuse = f;
    for (auto& t : tris)
        t.mat.diffuse = f;
}

// ============================================================
// SCHRIFT: 5x7-Pixelschrift (Kleinbuchstaben mit Unterlaenge bis 9 Zeilen)
// '#' = Pixel. Zeile 0 oben, Zeilen 7 und 8 liegen unter der Grundlinie.
// Benachbarte Pixel werden zu Rechtecken zusammengefasst (weniger Kanten und Dreiecke).
// ============================================================

namespace
{
struct SchriftZeichen
{
    int zeichen; // Unicode (Latin-1 genuegt: Umlaute, ß)
    const char *zeilen[9];
};

const SchriftZeichen SCHRIFT[] = {
    {'A', {".###.", "#...#", "#...#", "#####", "#...#", "#...#", "#...#"}},
    {'B', {"####.", "#...#", "#...#", "####.", "#...#", "#...#", "####."}},
    {'C', {".###.", "#...#", "#....", "#....", "#....", "#...#", ".###."}},
    {'D', {"###..", "#..#.", "#...#", "#...#", "#...#", "#..#.", "###.."}},
    {'E', {"#####", "#....", "#....", "####.", "#....", "#....", "#####"}},
    {'F', {"#####", "#....", "#....", "####.", "#....", "#....", "#...."}},
    {'G', {".###.", "#...#", "#....", "#.###", "#...#", "#...#", ".####"}},
    {'H', {"#...#", "#...#", "#...#", "#####", "#...#", "#...#", "#...#"}},
    {'I', {"###", ".#.", ".#.", ".#.", ".#.", ".#.", "###"}},
    {'J', {"..###", "...#.", "...#.", "...#.", "...#.", "#..#.", ".##.."}},
    {'K', {"#...#", "#..#.", "#.#..", "##...", "#.#..", "#..#.", "#...#"}},
    {'L', {"#....", "#....", "#....", "#....", "#....", "#....", "#####"}},
    {'M', {"#...#", "##.##", "#.#.#", "#.#.#", "#...#", "#...#", "#...#"}},
    {'N', {"#...#", "#...#", "##..#", "#.#.#", "#..##", "#...#", "#...#"}},
    {'O', {".###.", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."}},
    {'P', {"####.", "#...#", "#...#", "####.", "#....", "#....", "#...."}},
    {'Q', {".###.", "#...#", "#...#", "#...#", "#.#.#", "#..#.", ".##.#"}},
    {'R', {"####.", "#...#", "#...#", "####.", "#.#..", "#..#.", "#...#"}},
    {'S', {".####", "#....", "#....", ".###.", "....#", "....#", "####."}},
    {'T', {"#####", "..#..", "..#..", "..#..", "..#..", "..#..", "..#.."}},
    {'U', {"#...#", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."}},
    {'V', {"#...#", "#...#", "#...#", "#...#", "#...#", ".#.#.", "..#.."}},
    {'W', {"#...#", "#...#", "#...#", "#.#.#", "#.#.#", "#.#.#", ".#.#."}},
    {'X', {"#...#", "#...#", ".#.#.", "..#..", ".#.#.", "#...#", "#...#"}},
    {'Y', {"#...#", "#...#", ".#.#.", "..#..", "..#..", "..#..", "..#.."}},
    {'Z', {"#####", "....#", "...#.", "..#..", ".#...", "#....", "#####"}},

    {'a', {".....", ".....", ".###.", "....#", ".####", "#...#", ".####"}},
    {'b', {"#....", "#....", "#.##.", "##..#", "#...#", "#...#", "####."}},
    {'c', {".....", ".....", ".###.", "#....", "#....", "#...#", ".###."}},
    {'d', {"....#", "....#", ".##.#", "#..##", "#...#", "#...#", ".####"}},
    {'e', {".....", ".....", ".###.", "#...#", "#####", "#....", ".###."}},
    {'f', {"..##", ".#..", ".#..", "###.", ".#..", ".#..", ".#.."}},
    {'g', {".....", ".....", ".####", "#...#", "#...#", "#...#", ".####", "....#", ".###."}},
    {'h', {"#....", "#....", "#.##.", "##..#", "#...#", "#...#", "#...#"}},
    {'i', {".#.", "...", "##.", ".#.", ".#.", ".#.", "###"}},
    {'j', {"..#", "...", ".##", "..#", "..#", "..#", "..#", "#.#", ".#."}},
    {'k', {"#...", "#...", "#..#", "#.#.", "##..", "#.#.", "#..#"}},
    {'l', {"##.", ".#.", ".#.", ".#.", ".#.", ".#.", "###"}},
    {'m', {".....", ".....", "##.#.", "#.#.#", "#.#.#", "#...#", "#...#"}},
    {'n', {".....", ".....", "#.##.", "##..#", "#...#", "#...#", "#...#"}},
    {'o', {".....", ".....", ".###.", "#...#", "#...#", "#...#", ".###."}},
    {'p', {".....", ".....", "####.", "#...#", "#...#", "#...#", "####.", "#....", "#...."}},
    {'q', {".....", ".....", ".####", "#...#", "#...#", "#...#", ".####", "....#", "....#"}},
    {'r', {".....", ".....", "#.##.", "##..#", "#....", "#....", "#...."}},
    {'s', {".....", ".....", ".###.", "#....", ".###.", "....#", "####."}},
    {'t', {".#..", ".#..", "###.", ".#..", ".#..", ".#.#", "..#."}},
    {'u', {".....", ".....", "#...#", "#...#", "#...#", "#..##", ".##.#"}},
    {'v', {".....", ".....", "#...#", "#...#", "#...#", ".#.#.", "..#.."}},
    {'w', {".....", ".....", "#...#", "#...#", "#.#.#", "#.#.#", ".#.#."}},
    {'x', {".....", ".....", "#...#", ".#.#.", "..#..", ".#.#.", "#...#"}},
    {'y', {".....", ".....", "#...#", "#...#", "#...#", "#...#", ".####", "....#", ".###."}},
    {'z', {".....", ".....", "#####", "...#.", "..#..", ".#...", "#####"}},

    {'0', {".###.", "#...#", "#..##", "#.#.#", "##..#", "#...#", ".###."}},
    {'1', {"..#..", ".##..", "..#..", "..#..", "..#..", "..#..", ".###."}},
    {'2', {".###.", "#...#", "....#", "...#.", "..#..", ".#...", "#####"}},
    {'3', {"#####", "...#.", "..#..", "...#.", "....#", "#...#", ".###."}},
    {'4', {"...#.", "..##.", ".#.#.", "#..#.", "#####", "...#.", "...#."}},
    {'5', {"#####", "#....", "####.", "....#", "....#", "#...#", ".###."}},
    {'6', {"..##.", ".#...", "#....", "####.", "#...#", "#...#", ".###."}},
    {'7', {"#####", "....#", "...#.", "..#..", ".#...", ".#...", ".#..."}},
    {'8', {".###.", "#...#", "#...#", ".###.", "#...#", "#...#", ".###."}},
    {'9', {".###.", "#...#", "#...#", ".####", "....#", "...#.", ".##.."}},

    {'.', {"..", "..", "..", "..", "..", "##", "##"}},
    {',', {"..", "..", "..", "..", "..", "##", "##", ".#", "#."}},
    {'!', {"#", "#", "#", "#", "#", ".", "#"}},
    {'?', {".###.", "#...#", "....#", "...#.", "..#..", ".....", "..#.."}},
    {'-', {"....", "....", "....", "####", "....", "....", "...."}},
    {'+', {".....", "..#..", "..#..", "#####", "..#..", "..#..", "....."}},
    {':', {"..", "##", "##", "..", "##", "##", ".."}},
    {';', {"..", "##", "##", "..", "##", "##", ".#", "#."}},
    {'(', {"..#", ".#.", "#..", "#..", "#..", ".#.", "..#"}},
    {')', {"#..", ".#.", "..#", "..#", "..#", ".#.", "#.."}},
    {'/', {".....", "....#", "...#.", "..#..", ".#...", "#....", "....."}},
    {'\'', {"#", "#", ".", ".", ".", ".", "."}},
    {'"', {"#.#", "#.#", "...", "...", "...", "...", "..."}},
    {'=', {".....", ".....", "#####", ".....", "#####", ".....", "....."}},
    {'_', {".....", ".....", ".....", ".....", ".....", ".....", "#####"}},
    {'*', {".....", "..#..", "#.#.#", ".###.", "#.#.#", "..#..", "....."}},
    {'#', {".#.#.", ".#.#.", "#####", ".#.#.", "#####", ".#.#.", ".#.#."}},
    {'%', {"##...", "##..#", "...#.", "..#..", ".#...", "#..##", "...##"}},
    {'&', {".##..", "#..#.", "#.#..", ".#...", "#.#.#", "#..#.", ".##.#"}},
    {'@', {".###.", "#...#", "....#", ".##.#", "#.#.#", "#.#.#", ".###."}},
    {'<', {"...#", "..#.", ".#..", "#...", ".#..", "..#.", "...#"}},
    {'>', {"#...", ".#..", "..#.", "...#", "..#.", ".#..", "#..."}},

    {0xC4, {"#...#", ".###.", "#...#", "#...#", "#####", "#...#", "#...#"}}, // Ä
    {0xD6, {"#...#", ".###.", "#...#", "#...#", "#...#", "#...#", ".###."}}, // Ö
    {0xDC, {"#...#", ".....", "#...#", "#...#", "#...#", "#...#", ".###."}}, // Ü
    {0xE4, {".#.#.", ".....", ".###.", "....#", ".####", "#...#", ".####"}}, // ä
    {0xF6, {".#.#.", ".....", ".###.", "#...#", "#...#", "#...#", ".###."}}, // ö
    {0xFC, {".#.#.", ".....", "#...#", "#...#", "#...#", "#..##", ".##.#"}}, // ü
    {0xDF, {".##..", "#..#.", "#..#.", "#.##.", "#...#", "#...#", "#.##."}}, // ß
};

const double PIXEL = 1.0 / 7.0;  // Hoehe eines Pixels in "size" (Grossbuchstaben = 7 Pixel = size)
const double ABSTAND = 1.0 / 7.0; // ein Pixel Abstand zwischen Zeichen
const double LEERZEICHEN = 3.0 / 7.0;

// UTF-8 in Zeichen zerlegen (1- und 2-Byte-Folgen; alles andere wird uebersprungen)
std::vector<int> zeichenAus(const std::string &text)
{
    std::vector<int> v;
    for (size_t i = 0; i < text.size(); ++i)
    {
        unsigned char c = (unsigned char)text[i];
        if (c < 0x80)
            v.push_back(c);
        else if ((c & 0xE0) == 0xC0 && i + 1 < text.size())
        {
            v.push_back(((c & 0x1F) << 6) | ((unsigned char)text[i + 1] & 0x3F));
            ++i;
        }
        else if ((c & 0xF0) == 0xE0)
            i += 2;
        else if ((c & 0xF8) == 0xF0)
            i += 3;
    }
    return v;
}
} // namespace

// Pixelbild -> Rechtecke: waagrechte Laeufe je Zeile, gleiche Laeufe in Folgezeilen zusammengelegt
Glyph Text3D::makeGlyph(int c)
{
    Glyph g;
    const SchriftZeichen *z = nullptr;
    for (const auto &s : SCHRIFT)
        if (s.zeichen == c)
            z = &s;
    if (!z)
        return g;

    // benutzte Spalten (Zeichen ohne Rand links/rechts: schmale Zeichen wie i, l, . sind schmal)
    int links = 99, rechts = -1;
    for (const char *zeile : z->zeilen)
        if (zeile)
            for (int x = 0; zeile[x]; ++x)
                if (zeile[x] == '#')
                {
                    links = std::min(links, x);
                    rechts = std::max(rechts, x);
                }
    if (rechts < 0)
        return g;
    g.breite = (rechts - links + 1) * PIXEL;

    struct Lauf
    {
        int x0, x1, zeileOben, zeileUnten;
    };
    std::vector<Lauf> offen, fertig;

    for (int zeile = 0; zeile < 9; ++zeile)
    {
        std::vector<Lauf> neu;
        const char *text = z->zeilen[zeile];
        if (text)
            for (int x = 0; text[x];)
            {
                if (text[x] != '#')
                {
                    ++x;
                    continue;
                }
                int start = x;
                while (text[x] == '#')
                    ++x;
                neu.push_back({start, x, zeile, zeile});
            }

        // Lauf mit gleichen Spalten wie in der Zeile darueber: verlaengern
        for (auto &n : neu)
            for (auto it = offen.begin(); it != offen.end(); ++it)
                if (it->x0 == n.x0 && it->x1 == n.x1)
                {
                    n.zeileOben = it->zeileOben;
                    offen.erase(it);
                    break;
                }
        fertig.insert(fertig.end(), offen.begin(), offen.end());
        offen = neu;
    }
    fertig.insert(fertig.end(), offen.begin(), offen.end());

    for (const auto &l : fertig)
    {
        // Zeile 0 oben an der Versalhoehe, Zeile 6 auf der Grundlinie, 7 und 8 darunter
        Rect2D r;
        r.x0 = (l.x0 - links) * PIXEL;
        r.x1 = (l.x1 - links) * PIXEL;
        r.y0 = (6 - l.zeileUnten) * PIXEL;
        r.y1 = (7 - l.zeileOben) * PIXEL;
        g.rects.push_back(r);
    }
    return g;
}

// ============================================================
// FONT
// ============================================================

void Text3D::buildFont()
{
    font.clear();
    for (const auto &s : SCHRIFT)
        font[s.zeichen] = makeGlyph(s.zeichen);
}

// ============================================================
// TEXT BUILD
// ============================================================

void Text3D::buildText()
{
    tris.clear();

    double x = 0.0;
    for (int c : zeichenAus(text))
    {
        if (c == ' ')
        {
            x += LEERZEICHEN * size;
            continue;
        }
        double breite = addGlyphMesh(c, Vector3D(x, 0, 0));
        x += (breite + ABSTAND) * size;
    }
}

// ============================================================
// GLYPH → MESH (unbekannte Zeichen als Fragezeichen); liefert die Breite in "size"
// ============================================================

double Text3D::addGlyphMesh(int c, const Vector3D &offset)
{
    auto it = font.find(c);
    if (it == font.end())
        it = font.find('?');
    if (it == font.end())
        return 0.0;

    extrudeGlyph(it->second, offset);
    return it->second.breite;
}

// ============================================================
// GLYPH EXTRUSION: Quader je Rechteck (gleiche Form wie createWall)
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

        Vector3D versatz(0, 0, depth);
        Vector3D a2 = a + versatz, b2 = b + versatz, c2 = c + versatz, d2 = d + versatz;

        // Textkoordinaten -> lokale Koordinaten ueber die (gedrehten/skalierten) Achsen
        auto lokal = [&](const Vector3D& p)
        {
            return achseX * p.x + achseY * p.y + achseZ * p.z;
        };

        auto viereck = [&](const Vector3D& p, const Vector3D& q, const Vector3D& r, const Vector3D& s)
        {
            tris.emplace_back(lokal(p), lokal(q), lokal(r), mat);
            tris.emplace_back(lokal(p), lokal(r), lokal(s), mat);
        };

        viereck(a, b, c, d);     // vorne
        viereck(a2, d2, c2, b2); // hinten
        viereck(a, a2, b2, b);   // unten
        viereck(b, b2, c2, c);   // rechts
        viereck(c, c2, d2, d);   // oben
        viereck(d, d2, a2, a);   // links
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
        matHere = makeMaterialSimple(invertColorHighlight(mat.diffuse));
    else if (mode == DrawMode::HOVER)
        matHere = makeMaterialSimple(invertColor(mat.diffuse));
    else
        matHere = makeMaterialSimple(mat.diffuse);

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

        Farbe base = mat.diffuse;

        if (mode == DrawMode::HIGHLIGHT)
            base = invertColorHighlight(mat.diffuse);
        else if (mode == DrawMode::HOVER)
            base = invertColor(mat.diffuse);

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
        matHere = makeMaterialSimple(invertColorHighlight(mat.diffuse));
    else if (mode == DrawMode::HOVER)
        matHere = makeMaterialSimple(invertColor(mat.diffuse));
    else
        matHere = makeMaterialSimple(mat.diffuse);

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