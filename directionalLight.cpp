#include "directionallight.h"
#include "lichtSymbol.h"
#include "material.h"
#include <random>
#include <limits>
#include <cmath>

DirectionalLight::DirectionalLight(
    const Vector3D& dir,
    const Farbe& col,
    double irradiance)

{
    direction = dir.normalized();
    farbe = col;
    this->irradiance = irradiance; // vorher "irradiance = 1.0": setzte nur den Parameter, der Wert aus der XML ging verloren
    editorPos = direction * 20.0;  // Symbol auf der Seite, von der das Licht kommt (vorher (0,0,0))
}

LightSample DirectionalLight::samplePoint() const
{
    return {
        Vector3D(0,0,0),
        Vector3D(0,0,0),
        Vector3D(0,0,0),
        0.0,
        farbe,
        1.0
    };
}

Vector3D DirectionalLight::directionFrom(
    const Vector3D&) const
{
    return direction;
}

double DirectionalLight::distanceFrom(
    const Vector3D&) const
{
    return std::numeric_limits<double>::infinity();
}

Farbe DirectionalLight::intensityAt(
    const Vector3D&) const
{
    return farbe * irradiance;
}

void DirectionalLight::drawFlat(
    Wireframe& wf,
    DrawMode mode) const
{
    drawWireframePixels(wf, mode);
}

void DirectionalLight::drawWireframePixels(
    Wireframe& wf,
    DrawMode mode) const
{
    Farbe color;

    if (mode == DrawMode::HIGHLIGHT)
        color = Farbe(1,1,0);
    else if (mode == DrawMode::HOVER)
        color = Farbe(0,1,1);
    else
        color = farbe;

    // ======================================================
    // Editor-Position
    // ======================================================
    // 'direction' zeigt zum Licht (directionFrom, sample: directionToLight). Symbol dort, woher
    // das Licht kommt, Pfeil in Laufrichtung des Lichts (vorher umgekehrt: Pfeil auf ein Objekt
    // hiess "Licht kommt von hinten")
    Vector3D center = editorPosition();
    Vector3D dir = -direction.normalized(); // Laufrichtung des Lichts

    // Laenge in Pixeln statt Welteinheiten (vorher fest 15: in grossen Szenen unsichtbar)
    const double jePixel = wf.weltJePixel(center);
    if (!(jePixel > 0))
        return; // hinter der Kamera
    double len = 70.0 * jePixel;

    Vector3D end = center + dir * len;

    int x0,y0,x1,y1;
    double z0,z1;

    // Hauptlinie (Lichtrichtung)
    wf.project(center,x0,y0,z0);
    wf.project(end,x1,y1,z1);

    wf.drawLine(x0,y0,z0,x1,y1,z1,makeMaterialSimple(color));

    // ======================================================
    // Pfeilspitze (besseres Debug-Visual)
    // ======================================================

    Vector3D right = dir.cross(Vector3D(0,1,0));
    if (right.length() < 0.001)
        right = dir.cross(Vector3D(1,0,0));

    right = right.normalized();

    double headSize = len * 0.2;

    Vector3D up = right.cross(dir).normalized();

    Vector3D p1 = end - dir * headSize + right * headSize * 0.5;
    Vector3D p2 = end - dir * headSize - right * headSize * 0.5;
    Vector3D p3 = end - dir * headSize + up * headSize * 0.5;
    Vector3D p4 = end - dir * headSize - up * headSize * 0.5;

    wf.project(end,x0,y0,z0);

    int ax,ay; double az;

    wf.project(p1,ax,ay,az);
    wf.drawLine(x0,y0,z0,ax,ay,az,makeMaterialSimple(color));

    wf.project(p2,ax,ay,az);
    wf.drawLine(x0,y0,z0,ax,ay,az,makeMaterialSimple(color));

    wf.project(p3,ax,ay,az);
    wf.drawLine(x0,y0,z0,ax,ay,az,makeMaterialSimple(color));

    wf.project(p4,ax,ay,az);
    wf.drawLine(x0,y0,z0,ax,ay,az,makeMaterialSimple(color));
}

void DirectionalLight::intersect(
    const Ray& ray,
    Hit& hit) const
{
    // absichtlich leer
}

void DirectionalLight::save(
    std::ostream& out) const
{
    out << "        <light type=\"directional\" irradiance=\"" << irradiance << "\">\n"; // Staerke (im Editor einstellbar)

    out << "            <direction x=\""
        << direction.x
        << "\" y=\""
        << direction.y
        << "\" z=\""
        << direction.z
        << "\"/>\n";

    out << "            <color r=\""
        << farbe.r
        << "\" g=\""
        << farbe.g
        << "\" b=\""
        << farbe.b
        << "\"/>\n";

    // Position des Symbols in den Ansichten (fuers Rendering ohne Bedeutung)
    Vector3D p = editorPosition();
    out << "            <position x=\"" << p.x << "\" y=\"" << p.y << "\" z=\"" << p.z << "\"/>\n";

    out << "        </light>\n\n";
}


// Symbol in den Ansichten = editorPos (dasselbe, was getPosition() liefert)
Vector3D richtungslichtSymbol(const DirectionalLight* l)
{
    return l->editorPos;
}

void richtungslichtSymbolSetzen(DirectionalLight* l, const Vector3D& position)
{
    l->editorPos = position;
}

Vector3D DirectionalLight::editorPosition() const
{
    return editorPos;
}

// Verschieben bewegt nur das Symbol (wie bei den anderen Lichtern um -delta); die Richtung
// aendert man im Objektfenster (Zeile "Richtung") bzw. mit Rotate und den Buttons -X..+Z
void DirectionalLight::translate(const Vector3D& delta)
{
    editorPos -= delta;
}

LightSample DirectionalLight::sample(const Vector3D& p) const
{
    LightSample s;

    s.directionToLight = direction.normalized();
    s.distance = std::numeric_limits<double>::infinity();
    s.radiance = farbe * irradiance;

    return s;
}

Farbe DirectionalLight::getEmission() const
{
    return Farbe(0,0,0);
}
