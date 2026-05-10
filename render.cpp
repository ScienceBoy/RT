#include "render.h"
#include "vector3d.h"
#include "ray.h"
#include "hit.h"
#include "scene.h"
#include "threading.h"
#include "light.h"
#include "camera.h"
#include <iostream>
#include <atomic>
#include "texture.h"
#include <chrono>
#include <algorithm>
#include <cmath>
#include <numeric>
#include <vector>

Farbe background = Texture::backgroundCalc("sky", 1);

void render(Fenster& f, int xStart, int xEnd, int yStart, int yEnd,
            std::atomic<int>& linesDone, std::atomic<bool>& stopRendering)
{
    VolumeResult vol;

    for (int x = xStart; x < xEnd; x++)
        f.pixel(x, yStart, Farbe(1,0,0)), f.pixel(x, yEnd-1, Farbe(1,0,0));

    for (int y = yStart; y < yEnd; y++)
        f.pixel(xStart, y, Farbe(1,0,0)), f.pixel(xEnd-1, y, Farbe(1,0,0));

    for (int y = yStart; y < yEnd; y++)
    {
        
        if (stopRendering.load())
            return;


        for (int x = xStart; x < xEnd; x++)
        {
            
            if (stopRendering.load())
                return;

            //Ray r(Vector3D(x,y,0), Vector3D(0,0,1));    // sende Ray Parallel-projektion
            Ray r = cam[0].makeRay(x, y, f);
            Hit h = ClosestPointOfIntersection(r);      // Prüfen auf Treffen von Objekten

            if(h.hit)                             // object hit ?
            {
                //Farbe farbe = flatShading(h.farbe, h.normale, h.punkt); // Nur Flat Shading
                //Farbe farbe = flatShadingAndshadow(h.farbe, h.normale, h.punkt);  // Flat Shading mit Schatten
                //farbe = farbe * (1.0 - h.t/(2*sceneMax.z));    // Depth Cueing
                //Farbe farbe = blinnPhong(*h.material, h.normale, h.position, cam[0].position); // Blinn-Phong
                Farbe farbe = trace(r, 10, 1.0); 
                //farbe = farbe + Farbe(0.05, 0.05, 0.05);  // Globale ambiente Helligkeit
                if (nebelVorhanden) farbe = nebel(h, farbe);
                if (rauchVorhanden) farbe = rauch(r, farbe);
                if (wolkeVolumeVorhanden) farbe = wolke(r, farbe);
                f.pixel(x,y, farbe);
            }
            else
            {
                //background = Texture::backgroundCalc("sky", r.direction.y);
                background =  Texture::backgroundCalc("sky", r.direction);
                if (nebelVorhanden) background = nebel(h, background);
                if (rauchVorhanden) background = rauch(r, background);
                if (wolkeVolumeVorhanden) background = wolke(r, background);
                f.pixel(x,y, background);
            }
        }

        linesDone++;
    }
}

Farbe nebel(Hit h, Farbe farbe)
{
    double d = h.t; // Abstand Kamera → Schnittpunkt
    double T = exp(-fogDensity * d);

    // kleine fogDensity ergibt leichter Dunst
    // grosse fogDensity ergibt starker, weisser Nebel
    Farbe FarbeAbgeschwaecht = farbe * T;
    if (!nebelBeleuchtet)
        return FarbeAbgeschwaecht + fogColor * (1.0 - T);

    // simples In‑Scattering als Simulation der Mie-Streuung
    Farbe Nebelselbst = fogColor      * fogPartVisible   * (1.0 - T);
    Farbe MieStreuung = fogLightColor * fogPartLightning * (1.0 - T);

    return FarbeAbgeschwaecht + Nebelselbst + MieStreuung;
}

// =======================================================
// Fügt Rauchfarbe entlang eines Rays zur bestehenden Farbe hinzu
// =======================================================
Farbe rauch(Ray ray, Farbe farbe)
{
    double t_entry, t_exit;   // Eintritts- und Austrittspunkt des Rays in die Rauchbox

    // Prüfen, ob der Ray die Rauchbox schneidet
    if (intersectSmokeBox(ray, t_entry, t_exit))
    {
        // Volumetrische Integration innerhalb der Box
        Farbe smoke = rauchInBox(ray, t_entry, t_exit, Farbe(1,1,1));

        // Rauchfarbe zur bisherigen Farbe addieren
        farbe += smoke;
    }

    // Endfarbe zurückgeben
    return farbe;
}

// =======================================================
// Fügt Wolkenfarbe entlang eines Rays zur bestehenden Farbe hinzu
// =======================================================
Farbe wolke(Ray ray, Farbe farbe)
{
    double t_entry, t_exit;   // Eintritts- und Austrittspunkt des Rays in die Wolkenbox

    // Prüfen, ob der Ray die Rauchbox schneidet
    if (intersectCloudBox(ray, t_entry, t_exit))
    {
        // Volumetrische Integration innerhalb der Box
        Farbe wolke = wolkeInBox(ray, t_entry, t_exit, Farbe(1,1,1));

        // Rauchfarbe zur bisherigen Farbe addieren
        farbe += wolke;
    }

    // Endfarbe zurückgeben
    return farbe;
}

// =======================================================
// Raymarching durch das Rauchvolumen
// =======================================================
Farbe rauchInBox(Ray ray, double t_entry, double t_exit, Farbe smokeColor)
{

    // Schrittweite: feste Anzahl von Samples im Volumen
    double stepSize = (t_exit - t_entry) / 64.0;
    double t = t_entry;

    Farbe color = Farbe(0,0,0);     // aufsummierte Streufarbe
    double transmittance = 1.0;     // verbleibendes Licht (Beer-Lambert)

    // Raymarching-Schleife
    while (t < t_exit)
    {
        // Aktuelle Sample-Position im Raum
        Vector3D pos = ray.origin + t * ray.direction;

        // ---------------------------
        // Verzerrung der Abtastposition
        // ---------------------------
        Vector3D p = pos;
        p.x += noise(Vector3D(0, p.y * 0.5, 0)) * 0.2;
        p.z += noise(Vector3D(0, p.y * 0.5, 1)) * 0.2;

        // Rauchdichte an dieser Position sampeln
        double density = sampleDensityOfSmoke(p);

        // Nur relevante Dichten berücksichtigen
        if (density > 0.0001)
        {
            // ---------------------------
            // Absorption (Extinktion)
            // ---------------------------
            double absorption = density * stepSize;
            transmittance *= exp(-absorption);

            // Licht- und Blickrichtung
            Vector3D L = (lights[0].position - pos).normalized();   // Richtung zum Licht
            Vector3D V = (-ray.direction).normalized();             // Blickrichtung

            // ---------------------------
            // Schatten innerhalb des Rauchs
            // ---------------------------
            double light = computeShadowOfSmoke(pos, L);
            light = 0.2 + 0.8 * light;   // Minimum an Umgebungslicht

            // ---------------------------
            // Phase Function (anisotrope Streuung)
            // ---------------------------
            double cosTheta = L * V;
            double phase = phaseHG(cosTheta, 0.6) * 10.0;

            // ---------------------------
            // Streulicht-Beitrag
            // ---------------------------
            color += smokeColor
                   * transmittance
                   * density
                   * light
                   * phase
                   * stepSize
                   * 10;
        }

        // Frühzeitiger Abbruch bei fast vollständiger Absorption
        if (transmittance < 0.01)
            break;

        // Nächster Samplepunkt entlang des Rays
        t += stepSize;
    }

    return color;
}

// =======================================================
// Raymarching durch das Woleknvolumen
// =======================================================
Farbe wolkeInBox(Ray ray, double t_entry, double t_exit, Farbe cloudColor)
{
    // Schrittweite: feste Anzahl von Samples im Volumen
    double stepSize = (t_exit - t_entry) / 64.0;
    double t = t_entry;

    Farbe color = Farbe(0,0,0);     // aufsummierte Streufarbe
    double transmittance = 1.0;     // verbleibendes Licht (Beer-Lambert)

    // Raymarching-Schleife
    while (t < t_exit)
    {
        // Aktuelle Sample-Position im Raum
        Vector3D pos = ray.origin + t * ray.direction;

        // ---------------------------
        // Verzerrung der Abtastposition
        // ---------------------------
        Vector3D p = pos;
        p.x += noise(Vector3D(0, p.y * 0.5, 0)) * 0.2;
        p.z += noise(Vector3D(0, p.y * 0.5, 1)) * 0.2;

        // Rauchdichte an dieser Position sampeln
        double density = sampleDensityCloud(p);

        // Nur relevante Dichten berücksichtigen
        if (density > 0.0001)
        {
            // ---------------------------
            // Absorption (Extinktion)
            // ---------------------------
            double absorption = density * stepSize;
            transmittance *= exp(-absorption);

            // Licht- und Blickrichtung
            Vector3D L = (lights[0].position - pos).normalized();   // Richtung zum Licht
            Vector3D V = (-ray.direction).normalized();             // Blickrichtung

            // ---------------------------
            // Schatten innerhalb des Rauchs
            // ---------------------------
            double light = computeShadowOfCloud(pos, L);
            light = 0.2 + 0.8 * light;   // Minimum an Umgebungslicht

            // ---------------------------
            // Phase Function (anisotrope Streuung)
            // ---------------------------
            double cosTheta = L * V;
            double phase = phaseHG(cosTheta, 0.6) * 10.0;

            // ---------------------------
            // Streulicht-Beitrag
            // ---------------------------
            color += cloudColor
                   * transmittance
                   * density
                   * light
                   * phase
                   * stepSize
                   * 10;
        }

        // Frühzeitiger Abbruch bei fast vollständiger Absorption
        if (transmittance < 0.01)
            break;

        // Nächster Samplepunkt entlang des Rays
        t += stepSize;
    }

    return color;
}

// =======================================================
// Henyey–Greenstein-Phase-Funktion
// beschreibt gerichtete Lichtstreuung
// =======================================================
double phaseHG(double cosTheta, double g)
{
    double denom = 1.0 + g*g - 2.0*g*cosTheta;
    return (1.0 - g*g) / (4.0 * 3.14159265 * pow(denom, 1.5));
}

// =======================================================
// Ray–AABB-Schnitt mit der Rauchbox
// =======================================================
bool intersectSmokeBox(const Ray& ray, double& tmin, double& tmax)
{
    const double eps = 1e-8;

    // Sichere Invertierung der Richtungsvektoren
    auto safeInv = [&](double d)
    {
        return (fabs(d) > eps) ? 1.0 / d : 1e8;
    };

    Vector3D invDir(safeInv(ray.direction.x),
                    safeInv(ray.direction.y),
                    safeInv(ray.direction.z));

    // Schnittintervalle für jede Achse
    double tx1 = (RauchboxMin.x - ray.origin.x) * invDir.x;
    double tx2 = (RauchboxMax.x - ray.origin.x) * invDir.x;

    double ty1 = (RauchboxMin.y - ray.origin.y) * invDir.y;
    double ty2 = (RauchboxMax.y - ray.origin.y) * invDir.y;

    double tz1 = (RauchboxMin.z - ray.origin.z) * invDir.z;
    double tz2 = (RauchboxMax.z - ray.origin.z) * invDir.z;

    // Gemeinsames Eintritts- und Austrittsintervall
    tmin = std::max(std::max(std::min(tx1, tx2), std::min(ty1, ty2)), std::min(tz1, tz2));
    tmax = std::min(std::min(std::max(tx1, tx2), std::max(ty1, ty2)), std::max(tz1, tz2));

    // Box liegt vollständig hinter dem Ray
    if (tmax < 0.0)
        return false;

    // Fall: Startpunkt liegt bereits in der Box
    tmin = std::max(tmin, 0.0);

    return tmax > tmin;
}

// =======================================================
// Ray–AABB-Schnitt mit der Wolkenbox
// =======================================================
bool intersectCloudBox(const Ray& ray, double& tmin, double& tmax)
{
    const double eps = 1e-8;

    // Sichere Invertierung der Richtungsvektoren
    auto safeInv = [&](double d)
    {
        return (fabs(d) > eps) ? 1.0 / d : 1e8;
    };

    Vector3D invDir(safeInv(ray.direction.x),
                    safeInv(ray.direction.y),
                    safeInv(ray.direction.z));

    // Schnittintervalle für jede Achse
    double tx1 = (SkyMin.x - ray.origin.x) * invDir.x;
    double tx2 = (SkyMax.x - ray.origin.x) * invDir.x;

    double ty1 = (SkyMin.y - ray.origin.y) * invDir.y;
    double ty2 = (SkyMax.y - ray.origin.y) * invDir.y;

    double tz1 = (SkyMin.z - ray.origin.z) * invDir.z;
    double tz2 = (SkyMax.z - ray.origin.z) * invDir.z;

    // Gemeinsames Eintritts- und Austrittsintervall
    tmin = std::max(std::max(std::min(tx1, tx2), std::min(ty1, ty2)), std::min(tz1, tz2));
    tmax = std::min(std::min(std::max(tx1, tx2), std::max(ty1, ty2)), std::max(tz1, tz2));

    // Box liegt vollständig hinter dem Ray
    if (tmax < 0.0)
        return false;

    // Fall: Startpunkt liegt bereits in der Box
    tmin = std::max(tmin, 0.0);

    return tmax > tmin;
}

// =======================================================
// Berechnet Rauch-Schatten entlang eines Lichtstrahls
// =======================================================
double computeShadowOfSmoke(Vector3D pos, Vector3D lightDir)
{
    double t = 0.0;
    double maxDist = 10.0;
    double transmittance = 1.0;
    double stepSize = 0.2;

    while (t < maxDist)
    {
        Vector3D samplePos = pos + t * lightDir;

        // Rauchdichte entlang des Lichtstrahls
        double density = sampleDensityOfSmoke(samplePos);

        // weichere (volumetrische) Abschattung
        transmittance *= exp(-density * stepSize * 1.5);

        if (transmittance < 0.01)
            break;

        t += stepSize;
    }

    // konstantes Ambient-Light mischen
    return 0.2 + 0.8 * transmittance;
}

// =======================================================
// Berechnet Wolken-Schatten entlang eines Lichtstrahls
// =======================================================
double computeShadowOfCloud(Vector3D pos, Vector3D lightDir)
{
    double t = 0.0;
    double maxDist = 8.0;
    double stepSize = 0.15;

    double transmittance = 1.0;

    while (t < maxDist)
    {
        Vector3D samplePos = pos + t * lightDir;

        double density = sampleDensityCloud(samplePos);

        // KONTRAST BOOST
        density = density * density;

        // physikalische Extinktion
        transmittance *= exp(-density * stepSize * 1.8);

        if (transmittance < 0.01)
            break;

        t += stepSize;
    }

    // nichtlineare Lichtkurve 
    double light = transmittance;
    light = light * light * (3.0 - 2.0 * light); // smooth contrast curve

    // optional: leichtes Ambient
    double ambient = 0.25;

    return ambient + (1.0 - ambient) * light;
}

// =======================================================
// Rauchdichte-Funktion (Form + Struktur + Animation)
// =======================================================
double sampleDensityOfSmoke(Vector3D p)
{
    // Mittelpunkt der Rauchbox
    double cx = (RauchboxMin.x + RauchboxMax.x) * 0.5;
    double cz = (RauchboxMin.z + RauchboxMax.z) * 0.5;

    // Normalisierte Höhe in der Box
    double h = clamp((p.y - RauchboxMin.y) / (RauchboxMax.y - RauchboxMin.y), 0.0, 1.0);

    // ---------------------------
    // Zeit für Animation
    // ---------------------------
    double t = getTime() * 0.5;

    // ---------------------------
    // Schlängelnde Mittelachse
    // ---------------------------
    Vector3D center;
    center.y = p.y;
    center.x = cx + fbm(Vector3D(p.y * 0.6, 0, t)) * 0.3;
    center.z = cz + fbm(Vector3D(0, p.y * 0.6, t)) * 0.3;

    // ---------------------------
    // Radialer Abstand zur Achse
    // ---------------------------
    double dx = p.x - center.x;
    double dz = p.z - center.z;
    double dist = sqrt(dx*dx + dz*dz);

    // ---------------------------
    // Höhenabhängiger Radius
    // ---------------------------
    double radius = 0.1 + 0.3 * exp(-pow((h - 0.5) * 3.0, 2.0));

    double radial = exp(-dist * dist / (radius * radius));

    // ---------------------------
    // Feinstruktur per FBM
    // ---------------------------
    double n = fbm(p * scale);
    n = pow(n, 2.0);

    // ---------------------------
    // Vertikaler Zerfall
    // ---------------------------
    double falloff = exp(-h * 3.0);

    return radial * n * falloff * 5.0;
}

// =======================================================
// Zeitfunktion für Animation (Sekunden seit Start)
// =======================================================
double getTime()
{
    using namespace std::chrono;
    static auto start = high_resolution_clock::now();

    auto now = high_resolution_clock::now();
    duration<double> elapsed = now - start;

    return elapsed.count();
}

// =======================================================
// Fractal Brownian Motion (FBM)
// =======================================================
double fbm(const Vector3D& p)
{
    double value = 0.0;
    double amplitude = 0.5;
    double frequency = 1.0;

    const int OCTAVES = 4;

    for (int i = 0; i < OCTAVES; i++)
    {
        value += amplitude * noise(p * frequency);
        frequency *= 2.0;
        amplitude *= 0.5;
    }

    return value; // ungefähr im Bereich [0,1]
}

// =======================================================
// 3D-Value-Noise mit trilinearer Interpolation
// =======================================================
double noise(const Vector3D& p)
{
    // ---------------------------
    // Gitterkoordinaten (untere Ecke der Zelle)
    // ---------------------------
    int x0 = floor(p.x);
    int y0 = floor(p.y);
    int z0 = floor(p.z);

    // ---------------------------
    // Gitterkoordinaten (obere Ecke der Zelle)
    // ---------------------------
    int x1 = x0 + 1;
    int y1 = y0 + 1;
    int z1 = z0 + 1;

    // ---------------------------
    // Lokale Koordinaten innerhalb der Zelle [0,1]
    // ---------------------------
    double fx = p.x - x0;
    double fy = p.y - y0;
    double fz = p.z - z0;

    // ---------------------------
    // Glättungsfunktion (Fade / Smoothstep)
    // verhindert harte Übergänge im Rauschen
    // ---------------------------
    fx = smooth(fx);
    fy = smooth(fy);
    fz = smooth(fz);

    // ---------------------------
    // Zufallswerte an den 8 Ecken der Zelle
    // ---------------------------
    double n000 = rand3(x0,y0,z0);
    double n100 = rand3(x1,y0,z0);
    double n010 = rand3(x0,y1,z0);
    double n110 = rand3(x1,y1,z0);
    double n001 = rand3(x0,y0,z1);
    double n101 = rand3(x1,y0,z1);
    double n011 = rand3(x0,y1,z1);
    double n111 = rand3(x1,y1,z1);

    // ---------------------------
    // Interpolation entlang der X-Achse
    // ---------------------------
    double nx00 = lerp(n000, n100, fx);
    double nx10 = lerp(n010, n110, fx);
    double nx01 = lerp(n001, n101, fx);
    double nx11 = lerp(n011, n111, fx);

    // ---------------------------
    // Interpolation entlang der Y-Achse
    // ---------------------------
    double nxy0 = lerp(nx00, nx10, fy);
    double nxy1 = lerp(nx01, nx11, fy);

    // ---------------------------
    // Interpolation entlang der Z-Achse
    // ---------------------------
    double nxyz = lerp(nxy0, nxy1, fz);

    // ---------------------------
    // Normalisierung von [-1,1] auf [0,1]
    // ---------------------------
    return 0.5 * (nxyz + 1.0); // → [0,1]
}

// =======================================================
// Wolkendichte-Funktion (volumetrische Wolke)
// =======================================================
double sampleDensityCloud(Vector3D p)
{
    double density = 0.0;

    // ---------------------------------------------------
    // 1. Domain Warping (globaler Wind / Verzerrung)
    // ---------------------------------------------------
    double time = getTime() * 0.05;

    Vector3D warp;
    warp.x = fbm(p * 0.15 + Vector3D( 0.0,  0.0, time));
    warp.y = fbm(p * 0.15 + Vector3D(11.0,  3.0, time));
    warp.z = fbm(p * 0.15 + Vector3D(23.0, 17.0, time));

    p += Vector3D(warp.x, warp.y * 0.8, warp.z) * 0.25;


    // ---------------------------------------------------
    // 2. Cloud Grid (Himmel wird in Zellen aufgeteilt)
    // ---------------------------------------------------
    double scale = 0.002;

    Vector3D baseCell = Vector3D(
        floor(p.x * scale),
        0.0,
        floor(p.z * scale)
    );


    // ---------------------------------------------------
    // 3. Nachbarzellen prüfen (wichtig für weiche Übergänge)
    // ---------------------------------------------------
    for (int dx = -1; dx <= 1; dx++)
        for (int dz = -1; dz <= 1; dz++)
        {
            Vector3D cell = baseCell + Vector3D(dx, 0, dz);

            // ---------------------------------------------------
            // 4. stabile Zufallszahl pro Zelle 
            // ---------------------------------------------------
            double rnd = cell.x * 127.1 + cell.z * 311.7;
            rnd = rnd - floor(rnd);

            // keine Wolke in dieser Zelle
            if (rnd < 0.05)
                continue;


            // ---------------------------------------------------
            // 5. Wolkenzentrum innerhalb der Zelle
            // ---------------------------------------------------
            Vector3D offset = Vector3D(
                fbm(cell + Vector3D(1,0,0)),
                0.0,
                fbm(cell + Vector3D(0,0,1))
            );

            Vector3D center = 1.0 / scale * (cell + offset);


            // ---------------------------------------------------
            // 6. Zufällige Wolkengröße
            // ---------------------------------------------------
            double sizeNoise = fbm(cell + Vector3D(5,0,7));
            double size = 60.0 + 140.0 * sizeNoise * sizeNoise;


            // ---------------------------------------------------
            // 7. lokale Position
            // ---------------------------------------------------
            Vector3D q = Vector3D(
                (p.x - center.x) / size,
                (p.y - center.y) / size,
                (p.z - center.z) / size
            );

            double dist = sqrt(q.x*q.x + q.y*q.y + q.z*q.z);


            // ---------------------------------------------------
            // 8. Grundform (Cumulus Blob)
            // ---------------------------------------------------
            double shape = smoothstep(1.2, 0.5, dist);

            if (shape <= 0.0)
                continue;


            // ---------------------------------------------------
            // 9. Höhenvariation pro Wolke
            // ---------------------------------------------------
            double baseHeight = WolkeboxMin.y + 20.0 * fbm(cell + Vector3D(9,0,3));
            double topHeight  = WolkeboxMax.y - 20.0 * fbm(cell + Vector3D(4,0,8));

            double h = (p.y - baseHeight) / (topHeight - baseHeight);

            double heightFalloff =
                smoothstep(0.0, 0.15, h) *
                smoothstep(1.0, 0.4, h);

            if (heightFalloff <= 0.01)
                continue;


            // ---------------------------------------------------
            // 10. Basisstruktur (grobe Form)
            // ---------------------------------------------------
            Vector3D pBase = Vector3D(p.x * 0.06, p.y * 0.08, p.z * 0.06);

            double base = fbm(pBase);
            base = pow(base, 1.2);


            // ---------------------------------------------------
            // 11. Detailstruktur
            // ---------------------------------------------------
            double detail = fbm(p * 1.2 + Vector3D(17.0, 43.0, 11.0));
            detail = pow(detail, 2.0);


            // ---------------------------------------------------
            // 12. Erosion (macht „Cumulus Ausfransung“)
            // ---------------------------------------------------
            double erosion = fbm(p * 0.08 + Vector3D(5.0, 9.0, 13.0));
            erosion = pow(erosion, 1.5);

            shape -= erosion * 1.2;
            shape = clamp(shape, 0.0, 1.0);


            // ---------------------------------------------------
            // 13. Dichte dieser einzelnen Wolke
            // ---------------------------------------------------
            double cloudDensity = base + detail * 0.3;

            cloudDensity *= shape;
            cloudDensity *= heightFalloff;
            cloudDensity *= clamp(base * 1.2, 0.0, 1.0);


            // ---------------------------------------------------
            // 14. Additiv zur Szene hinzufügen
            // ---------------------------------------------------
            density += cloudDensity;
        }

    return density;
}

Farbe flatShadingAndshadow(const Farbe& objectFarbe, const Vector3D& normale, const Vector3D& punkt)
{
    Vector3D N = normale.normalized();
    Farbe result(0,0,0);

    for (const light& light : lights)
    {
        Vector3D toLight = light.position - punkt;
        double distToLight = toLight.length();
        Vector3D s = toLight.normalized();

        // Shadow Ray (kleiner Offset gegen Self Intersection)
        Ray shadowRay(punkt + N * 1e-4, s);

        Hit shadowHit = ClosestPointOfIntersection(shadowRay);

        // Prüfen: liegt etwas zwischen Punkt und light?
        if (shadowHit.hit && shadowHit.t > 0 && shadowHit.t < distToLight)
        {
            continue; // im Schatten, also keine Farbe zurückgeben
        }

        // Beleuchtung 
        double intensity = std::max(0.0, s * N);

        if(intensity > 0)
            result = result + objectFarbe * light.farbe * intensity;
    }

    return result;// * (1.0/lights.size());
}

Farbe blinnPhong(const Material& mat, const Vector3D& normale, const Vector3D& punkt, const Hit h, const Vector3D& viewPos)
{
    Vector3D N = normale.normalized();
    Vector3D V = (viewPos - punkt).normalized();

    Farbe result(0,0,0);

    // Ambient (immer da)
    result += mat.ambient * mat.kA;

    for (const light& light : lights)
    {
        Vector3D toLight = light.position - punkt;
        double distToLight = toLight.length();
        Vector3D L = toLight.normalized();

        // Shadow Ray
        Ray shadowRay(punkt + N * 1e-4, L);
        Hit shadowHit = ClosestPointOfIntersection(shadowRay);

        if (shadowHit.hit && shadowHit.t > 0 && shadowHit.t < distToLight)
            continue;

        // Diffuse
        double diff = std::max(0.0, N * L);

        // Blinn-Phong (HALF VECTOR)
        Vector3D H = (L + V).normalized();
        double spec = pow(std::max(0.0, N * H), mat.kS);

        // Kombination
        auto mat_diffuse = mat.diffuse;

        //std::cout << "vorhanden: " << h.obj->texture << "mode SPHERE ?: " << (h.obj->textureMode == TextureMode::SPHERE) << std::endl;
        if (h.obj->texture)
            mat_diffuse = Texture::texture(mat_diffuse, h);
        Farbe diffuse  = mat_diffuse  * light.farbe * mat.kD * diff;
        Farbe specular = mat.specular * light.farbe * spec * (1.0 - mat.kD);

        result += diffuse + specular;
    }

    return result;
}

Farbe flatShading(const Farbe& objectFarbe, const Vector3D& normale,  const Vector3D& punkt)
{
    Vector3D N = normale.normalized();
    Farbe result(0,0,0);

    for (const light& light : lights)
    {
        Vector3D s = (light.position - punkt).normalized();

        double intensity = s * N; 

        if(intensity > 0) 
            result = result + objectFarbe * light.farbe * intensity;
    }

    return result;// * (1.0/lights.size());
}

Hit ClosestPointOfIntersection(const Ray& ray)
{
    Hit best;

    for(const auto& obj : scene)
    {
        obj->intersect(ray, best);
    }

    return best;
}

Farbe trace(const Ray& ray, int depth, double current_ior = 1.0)
{
    if(depth <= 0)
        return Farbe(0,0,0);

    Hit h = ClosestPointOfIntersection(ray);
    if(!h.hit)
    {
        return Texture::backgroundCalc("sky", ray.direction.y);
    }

    Vector3D N = h.normale.normalized();
    Vector3D V = -ray.direction.normalized();

    Farbe localColor = blinnPhong(*h.material, N, h.position, h, cam[0].position);

    double n1 = current_ior;
    double n2 = h.material->IndexOfRefraction;

    bool inside = (ray.direction * N > 0);
    if (inside)
    {
        N = -N;
        std::swap(n1, n2);
    }

    // ------------------------------------------------------------
    // Fresnel-Berechnung
    // ------------------------------------------------------------
    double R = 0.0;

    bool hasIOR = (h.material->IndexOfRefraction > 0.0);

    if(hasIOR)
        R = fresnel(ray.direction, N, n1, n2);
    else
        R = h.material->reflection;

    // ------------------------------------------------------------
    // Reflection skalieren
    // ------------------------------------------------------------
    double reflectionWeight = R * h.material->reflection;

    Farbe reflectColor(0,0,0);
    Farbe refractColor(0,0,0);

    // ------------------------------------------------------------
    // Reflection
    // ------------------------------------------------------------
    if(reflectionWeight > 0.0)
    {
        Vector3D Rdir = reflect(ray.direction, N);
        Ray reflectRay(h.position + N*1e-4, Rdir);
        reflectColor = trace(reflectRay, depth-1, current_ior);
    }

    // ------------------------------------------------------------
    // Refraction (nur wenn echtes Dielektrikum)
    // ------------------------------------------------------------
    double kt = h.material->transparency;

    if(hasIOR && kt > 0.0)
    {
        Vector3D Tdir = refract(ray.direction, N, n1, n2);

        if(Tdir.length() > 0)
        {
            Vector3D offset = (Tdir * N < 0) ? -N : N;
            Ray refractRay(h.position + offset * 1e-4, Tdir);

            Hit exitHit = ClosestPointOfIntersection(refractRay);

            double d = 0.0;
            if(exitHit.hit && exitHit.obj == h.obj)
                d = (exitHit.position - h.position).length();

            refractColor = trace(refractRay, depth-1, n2);

            Farbe sigma = h.material->absorption;

            Farbe attenuation(
                exp(-sigma.r * d),
                exp(-sigma.g * d),
                exp(-sigma.b * d)
            );

            refractColor = refractColor * attenuation;
        }
    }

    // ------------------------------------------------------------
    // Energy-Mixing (kein doppelt gezähltes Licht)
    // ------------------------------------------------------------

    double diffuseFactor = (1.0 - reflectionWeight) * (1.0 - kt);

    Farbe result(0,0,0);

    // Diffuse / local
    result += localColor * diffuseFactor;

    // Reflection
    result += reflectColor * reflectionWeight;

    // Refraction
    result += refractColor * kt;

    return result;
}

Vector3D reflect(const Vector3D& D, const Vector3D& N)
{
    return D - N * 2.0 * (D * N);
}

Vector3D refract(const Vector3D& I, const Vector3D& N, double n1, double n2)
{
    Vector3D Nn = N.normalized();
    Vector3D In = I.normalized();

    double cosI = -std::max(-1.0, std::min(1.0, In * Nn));
    double eta = n1 / n2;
    double k = 1 - eta*eta * (1 - cosI*cosI);

    if(k < 0)
        return Vector3D(0,0,0); // Totalreflexion
    else
        return In*eta + Nn*(eta*cosI - sqrt(k));
}

double fresnel(const Vector3D& I, const Vector3D& N, double n1, double n2)
{
    // Mit Schlick-Approximation
    double cosI = std::max(-1.0, std::min(1.0, I * N));
    // Wechsel Medium nach innen
    bool entering = cosI > 0;
    if(!entering) std::swap(n1,n2);

    double R0 = pow((n1 - n2)/(n1 + n2), 2);
    return R0 + (1 - R0) * pow(1 - fabs(cosI), 5);
}

