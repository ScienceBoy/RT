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
#include "lightSample.h"
#include "random.h"
#include "animation.h"
#include <memory>
#include "render_einstellungen.h"
#include "regen.h"
#include "pointlight.h"
#include <limits>
#include <thread>

static Farbe lichtkegelImNebel(const Ray &ray, double tMax); // Lichtkegel im Nebel (unten)

// ---------- Einstellungen gegen Rauschen (render_einstellungen.h) ----------
bool adaptivAn = true;
bool indirektesLicht = true;
int adaptivMin = 4;
int adaptivMax = 64;
double rauschSchwelle = 0.03;
double indirektGrenze = 3.0;
int schnellBlock = 1; // Schnell-Rendering: ein Wert je Block aus n x n Pixeln (1 = jeder Pixel)
bool volumenSchatten = true;    // Rauch und Wolken werfen Schatten auf Objekte
int volumenSchattenSchritte = 12; // Abtastpunkte je Schattenstrahl und Volumen
bool nebelDaempftLicht = true;  // Licht von Punkt-/Kugellichtern verliert im Nebel an Kraft
int kachelGroesse = 1;
int renderBreite = 0, renderHoehe = 0; // Groesse des gerenderten Bildes (0 = wie das Render-Fenster)

// ---------- Stil (Zeichentrick) ----------
bool zeichentrick = false;   // Farbstufen, harte Glanzpunkte, Randlicht
int tonStufen = 3;           // Helligkeitsstufen des direkten Lichts
double saettigung = 1.0;     // 1 = unveraendert, > 1 kraeftiger (wirkt immer, auch ohne Zeichentrick)
bool konturen = true;        // schwarze Linien an Silhouetten und Kanten (nur mit Zeichentrick)
double konturDicke = 1.5;    // Abstand der Nachbarpunkte in Pixeln
double randlicht = 0.3;      // heller Saum an den Raendern (0 = aus)
double blende = 0.0;         // Tiefenschaerfe: Radius der Linse in Einheiten (0 = alles scharf)
double fokusAbstand = 0.0;   // scharfe Ebene entlang der Blickrichtung (0 = Abstand zum Kamera-Ziel)
int bewegungTeilbilder = 1;  // Bewegungsunschaerfe in Animationen: Teilbilder je Bild (1 = aus)
double verschluss = 0.5;     // Verschlusszeit als Anteil der Bildzeit (0,5 = 180-Grad-Verschluss)

// Tiefenschaerfe (duenne Linse): der Strahl startet an einem zufaelligen Punkt der Linsenscheibe (Radius
// 'blende', aufgespannt von den Kamera-Achsen u, v) und zielt auf den Punkt, den der Lochkamera-Strahl in der
// Fokusebene trifft. Dort ist alles scharf, davor und dahinter verschwimmt es umso mehr, je groesser die Blende.
static Ray linsenStrahl(const Ray &r)
{
    if (!(blende > 0.0) || cam.empty())
        return r;
    const Camera &c = cam[0];
    Vector3D vorne = c.lookAt - c.position;
    const double zumZiel = vorne.length();
    if (!(zumZiel > 1e-12))
        return r;
    vorne = vorne * (1.0 / zumZiel);
    const double fokus = fokusAbstand > 0.0 ? fokusAbstand : zumZiel;
    const double cosW = r.direction * vorne;
    if (!(cosW > 1e-6))
        return r;
    const Vector3D ziel = r.origin + r.direction * (fokus / cosW); // Punkt in der Fokusebene
    const double rad = blende * std::sqrt(randomDouble(0.0, 1.0)), phi = 2.0 * M_PI * randomDouble(0.0, 1.0);
    Vector3D u = c.u, v = c.v;
    u = u.length() > 1e-12 ? u.normalized() : Vector3D(1, 0, 0);
    v = v.length() > 1e-12 ? v.normalized() : Vector3D(0, 1, 0);
    const Vector3D start = r.origin + u * (rad * std::cos(phi)) + v * (rad * std::sin(phi));
    return Ray(start, (ziel - start).normalized());
}

// Helligkeit in harte Stufen (0, 1/n, 2/n ... 1): flache Zeichentrick-Flaechen mit klarer Schattenkante
static double tonStufe(double d)
{
    const int n = std::max(1, std::min(tonStufen, 64));
    return std::min(1.0, std::floor(d * n + 0.5) / n);
} // Kacheln der Render-Threads (threading.cpp); > 1: mit rotem Rahmen

// Schnell-Rendering: welche Bloecke schon berechnet sind. Die Render-Threads rufen render() fuer
// beliebige Bereiche auf (auch einzelne Pixel in zufaelliger Reihenfolge); wer einen Block zuerst
// erreicht, berechnet ihn und fuellt ihn ganz, alle anderen ueberspringen ihn.
// Tabelle als shared_ptr: noch laufende Threads eines angehaltenen Renderns behalten ihre alte Tabelle,
// waehrend das naechste Rendern schon eine neue anlegt.
struct BlockTabelle
{
    int groesse = 0, spalten = 0, zeilen = 0;
    std::unique_ptr<std::atomic<unsigned char>[]> erledigt;
};
static std::shared_ptr<BlockTabelle> blockTabelle;

void schnellRenderVorbereiten(int breite, int hoehe)
{
    const int B = std::max(1, std::min(schnellBlock, 64));
    std::shared_ptr<BlockTabelle> neu;
    if (B > 1 && breite > 0 && hoehe > 0)
    {
        neu = std::make_shared<BlockTabelle>();
        neu->groesse = B;
        neu->spalten = (breite + B - 1) / B;
        neu->zeilen = (hoehe + B - 1) / B;
        const size_t n = size_t(neu->spalten) * size_t(neu->zeilen);
        neu->erledigt.reset(new std::atomic<unsigned char>[n]);
        for (size_t i = 0; i < n; ++i)
            neu->erledigt[i].store(0, std::memory_order_relaxed);
    }
    std::atomic_store(&blockTabelle, neu);
}

// Abstand, um den Folge- und Schattenstrahlen von der Flaeche abheben: relativ zur Szenengroesse
// (vorher fest 1e-4: in grossen Szenen trafen Strahlen ihre eigene Flaeche -> dunkle Sprenkel)
static std::atomic<double> strahlAbstand{1e-4}; // alle Render-Threads setzen denselben Wert
static void strahlAbstandSetzen()
{
    double ausdehnung = (sceneMax - sceneMin).length();
    strahlAbstand.store((std::isfinite(ausdehnung) && ausdehnung > 0) ? std::max(1e-4, ausdehnung * 1e-6) : 1e-4,
                        std::memory_order_relaxed);
}

// true fuer den Strahl direkt nach einer diffusen Streuung: trifft er ein Licht, zaehlt dessen Emission
// nicht (das direkte Licht wird schon gezielt abgetastet; vorher doppelt gezaehlt -> helle Ausreisser)
static thread_local bool nachDiffuserStreuung = false;

// Texturen: Oeffnungswinkel eines Bildpunkts (rad) fuer die Mipmap-Stufe; Pixelgroesse am Treffer = t * Winkel
static std::atomic<double> texturPixelWinkel{0.0};
// Alpha-Ausschnitte: wie oft ein Strahl hintereinander durch ausgeschnittene Stellen weiterlaeuft
static thread_local int alphaSchichten = 0;

// Volumen bis zum ersten Objekt (weiter unten)
static Farbe rauchBis(const Ray &ray, double tMax, Farbe farbe);
static Farbe wolkeBis(const Ray &ray, double tMax, Farbe farbe);

Farbe background = Texture::backgroundCalc("sky", 1);

void render(Fenster& f, int xStart, int xEnd, int yStart, int yEnd,
            std::atomic<int>& linesDone, std::atomic<bool>& stopRendering)
{
    VolumeResult vol;
    strahlAbstandSetzen();
    int HaltonSamples = antialiasingSamples; // 16 Berechnungen pro Pixel gegen Antialiasing. Dafür musste ich clamp() ausschalten
    if (!antialiasing) 
        HaltonSamples = 1;

    const int B = std::max(1, std::min(schnellBlock, 64));
    const int bildB = f.breite(), bildH = f.hoehe();
    if (!cam.empty())
        texturPixelWinkel.store(2.0 * cam[0].halfHeight / std::max(1, bildH), std::memory_order_relaxed); // wie makeRay
    // Tabelle passt zu Blockgroesse und Bild? Sonst Bloecke nur innerhalb des Bereichs
    const std::shared_ptr<BlockTabelle> tab = std::atomic_load(&blockTabelle);
    const bool tabelle = B > 1 && tab && tab->groesse == B && tab->spalten == (bildB + B - 1) / B &&
                         tab->zeilen == (bildH + B - 1) / B;

    // Aktueller Berechnungsbereich rot umranden. Beim Schnell-Rendering nur, wenn der Bereich auf dem
    // Blockraster liegt (Kacheln aus threading.cpp): sonst bliebe der Rahmen in Bloecken stehen,
    // die ein anderer Aufruf schon gefuellt hat
    const bool rahmen = B <= 1 || (xStart % B == 0 && yStart % B == 0 &&
                                   ((xEnd - xStart) % B == 0 || xEnd == bildB) &&
                                   ((yEnd - yStart) % B == 0 || yEnd == bildH));
    if (rahmen)
    for (int x = xStart; x < xEnd; x++)
        f.pixel(x, yStart, Farbe(1,0,0)), f.pixel(x, yEnd-1, Farbe(1,0,0));

    if (rahmen)
    for (int y = yStart; y < yEnd; y++)
        f.pixel(xStart, y, Farbe(1,0,0)), f.pixel(xEnd-1, y, Farbe(1,0,0));

    // Rendering; beim Schnell-Rendering ein Wert je Block (Mitte des Blocks), der ganze Block in dieser Farbe.
    // Bloecke liegen auf einem festen Raster ueber das ganze Bild (Vielfache von B).
    const int y0 = tabelle ? (yStart / B) * B : yStart;
    const int x0 = tabelle ? (xStart / B) * B : xStart;
    for (int y = y0; y < yEnd; y += B)
    {
        for (int x = x0; x < xEnd; x += B)
        {
            if (stopRendering.load())
                return;

            // Bereich, den dieser Block fuellt
            const int fx1 = tabelle ? std::min(x + B, bildB) : std::min(x + B, xEnd);
            const int fy1 = tabelle ? std::min(y + B, bildH) : std::min(y + B, yEnd);
            if (tabelle && tab->erledigt[size_t(y / B) * size_t(tab->spalten) + size_t(x / B)].exchange(1))
                continue; // schon von einem anderen Aufruf berechnet

            const int mx = std::min(x + B / 2, fx1 - 1); // abgetasteter Pixel: Mitte des Blocks
            const int my = std::min(y + B / 2, fy1 - 1);

            // eine Abtastung: Unterpixel-Position s (Halton), pathSamples Pfade, dann Nebel/Rauch/Wolken
            auto abtastung = [&](int s) -> Farbe
            {
                double sx = mx;
                double sy = my;

                if (antialiasing || s > 0)
                {
                    sx = mx + Halton(s, 2) - 0.5;                // inkl. Subpixel offset;
                    sy = my + Halton(s, 3) - 0.5;                // inkl. Subpixel offset
                }

                Ray r = linsenStrahl(cam[0].makeRay(sx, sy, f)); // mit Tiefenschaerfe: durch die Linse
                Hit h = ClosestPointOfIntersection(r);      // Prüfen auf Treffen von Objekten

                Farbe sampleColor(0,0,0);
                int gueltig = 0;

                for(int p = 0; p < pathSamples; p++)
                {
                    double px = sx + randomDouble(-0.5, 0.5);
                    double py = sy + randomDouble(-0.5, 0.5);
                    Ray rp = linsenStrahl(cam[0].makeRay(px, py, f));
                    Farbe pfad = trace(rp, renderingDepth, 1.0);

                    // ungueltige Werte (NaN/unendlich) verwerfen: ein einziges NaN machte
                    // sonst den ganzen Pixel schwarz
                    if (std::isfinite(pfad.r) && std::isfinite(pfad.g) && std::isfinite(pfad.b))
                    {
                        sampleColor += pfad;
                        ++gueltig;
                    }
                }

                if (gueltig > 0)
                    sampleColor = sampleColor * (1.0 / gueltig);

                // Nebel, Rauch und Wolken je Abtastung
                if (nebelVorhanden)
                {
                    sampleColor = nebel(h, sampleColor);
                    if (nebelBeleuchtet) // Lichtkegel von Punkt-/Spotlichtern im Nebel
                        sampleColor += lichtkegelImNebel(r, h.hit ? h.t : std::numeric_limits<double>::infinity());
                }
                const double tObjekt = h.hit ? h.t : 1e30; // Volumen nur bis zum ersten Objekt
                if (rauchVorhanden) sampleColor = rauchBis(r, tObjekt, sampleColor);
                if (wolkeVolumeVorhanden) sampleColor = wolkeBis(r, tObjekt, sampleColor);
                if (regen::gRegen.an) // Regen: Striche zwischen Kamera und erstem Objekt
                    sampleColor = regen::entlang(r.origin, r.direction, (h.hit || h.hitLight) ? h.t : std::numeric_limits<double>::infinity(),
                                                 sampleColor, aktuelleZeit(),
                                                 texturPixelWinkel.load(std::memory_order_relaxed),
                                                 [](const Vector3D &richtung) { return Texture::backgroundCalc("sky", richtung); });
                return sampleColor;
            };

            // Adaptiv: erst die Grund-Abtastungen (mindestens adaptivMin), dabei die Streuung der
            // Helligkeit messen; solange der Mittelwert noch unsicher ist, weitere Abtastungen bis adaptivMax.
            // So wird nur dort laenger gerechnet, wo es rauscht (vorher feste Anzahl fuer alle Pixel).
            Farbe farbe(0,0,0);
            double summe = 0.0, summeQuadrat = 0.0;
            int n = 0;
            auto dazu = [&](const Farbe &c)
            {
                farbe += c;
                double l = 0.2126 * c.r + 0.7152 * c.g + 0.0722 * c.b;
                summe += l;
                summeQuadrat += l * l;
                ++n;
            };
            const int grund = adaptivAn ? std::max(HaltonSamples, std::max(1, adaptivMin)) : HaltonSamples;
            for (int s = 0; s < grund; s++)
                dazu(abtastung(s));
            if (adaptivAn && n >= 2)
            {
                const int hoechstens = std::max(grund, adaptivMax);
                while (n < hoechstens)
                {
                    double mittel = summe / n;
                    double varianz = std::max(0.0, summeQuadrat / n - mittel * mittel) * n / (n - 1);
                    double fehler = std::sqrt(varianz / n) / (mittel + 0.1); // dunkle Pixel nicht ueberbewerten
                    if (fehler <= rauschSchwelle)
                        break;
                    if (stopRendering.load())
                        return;
                    dazu(abtastung(n));
                }
            }
            farbe = farbe * (1.0 / n);

            // Konturen (Zeichentrick): Nachbarpunkte im Abstand konturDicke abtasten (nur Treffer, ohne
            // Beleuchtung). Kante, wenn ein Nachbar ins Leere geht (Silhouette), die Tiefe springt oder
            // die Flaechen deutlich anders ausgerichtet sind.
            if (zeichentrick && konturen && konturDicke > 0.0)
            {
                auto primaer = [&](double px, double py, double &t, Vector3D &nor) -> bool
                {
                    Ray rr = cam[0].makeRay(px, py, f);
                    Hit hh = ClosestPointOfIntersection(rr);
                    if (!hh.hit && !hh.hitLight)
                        return false;
                    t = hh.t;
                    nor = hh.normale.normalized();
                    if (nor * rr.direction > 0) // Rueckseite: wie Vorderseite behandeln
                        nor = -nor;
                    return true;
                };
                const double d = konturDicke;
                double tc = 0;
                Vector3D nc;
                const bool c = primaer(mx, my, tc, nc);
                bool kante = false;
                const double versatz[2][2][2] = {{{-d, 0}, {d, 0}}, {{0, -d}, {0, d}}};
                for (int achse = 0; achse < 2 && !kante; ++achse)
                {
                    double ta = 0, tb = 0;
                    Vector3D na, nb;
                    bool a = primaer(mx + versatz[achse][0][0], my + versatz[achse][0][1], ta, na);
                    bool b = primaer(mx + versatz[achse][1][0], my + versatz[achse][1][1], tb, nb);
                    if (a != c || b != c)
                        kante = true; // Silhouette
                    else if (c)
                    {
                        // zweite Ableitung der Tiefe: auf ebenen Flaechen ~0, an Kanten/Stufen gross
                        if (std::fabs(ta + tb - 2.0 * tc) > 0.02 * d * tc)
                            kante = true;
                        else if (nc * na < 0.8 || nc * nb < 0.8) // Knick > ~35 Grad
                            kante = true;
                    }
                }
                if (kante)
                    farbe = farbe * 0.05;
            }

            Farbe finalColor = toneMapping(farbe, exposure);

            // Saettigung: Abstand jeder Farbe zur Helligkeit vergroessern (1 = unveraendert)
            if (std::fabs(saettigung - 1.0) > 1e-9)
            {
                double l = 0.2126 * finalColor.r + 0.7152 * finalColor.g + 0.0722 * finalColor.b;
                finalColor = Farbe(std::max(0.0, l + (finalColor.r - l) * saettigung),
                                   std::max(0.0, l + (finalColor.g - l) * saettigung),
                                   std::max(0.0, l + (finalColor.b - l) * saettigung));
            }
            finalColor = linearToSRGB(finalColor);
            //finalColor.clamp();

            for (int by = y; by < fy1; by++)
                for (int bx = x; bx < fx1; bx++)
                    f.pixel(bx, by, finalColor);

        }
    }
    linesDone += yEnd - yStart; // Fortschritt zaehlt Bildzeilen des Bereichs (wie bisher eine je Zeile)
}

double phaseHG(double cosTheta, double g);

// =======================================================
// Lichtkegel im Nebel: Streulicht der Punkt- und Spotlichter entlang des Kamerastrahls (Raymarching).
// Je Schritt: Licht am Ort (mit Kegelfaktor des Spots und 1/d^2), Schattenstrahl zur Lampe, Phasenfunktion
// (Henyey-Greenstein, leicht vorwaerts), geschwaecht um den Nebel bis zur Kamera:
//     L += T(t) * sigma * p(theta) * L_licht * dt,   T(t) = exp(-sigma * t)
// Der Start jedes Strahls ist zufaellig versetzt (kein Streifenmuster); die Abtastungen je Pixel mitteln das.
// Staerke: "Nebel leuchtet"-Anteil (fogPartLightning). Kugel-, Flaechen-, Mesh- und Richtungslicht: wie bisher.
// =======================================================
static Farbe lichtkegelImNebel(const Ray &ray, double tMax)
{
    Farbe summe(0, 0, 0);
    const double sigma = fogDensity;
    if (!(sigma > 0.0) || fogPartLightning <= 0.0)
        return summe;
    const Vector3D d = ray.direction.normalized();
    const double laengeRichtung = ray.direction.length();
    if (!(laengeRichtung > 1e-12))
        return summe;
    const double tMaxN = std::isfinite(tMax) ? tMax * laengeRichtung : tMax; // t in Einheiten der Laenge
    const double abheben = strahlAbstand.load(std::memory_order_relaxed);
    const int SCHRITTE = 32;
    for (const auto &l : lights)
    {
        const auto *pl = dynamic_cast<const PointLight *>(l.get()); // auch SpotLight
        if (!pl)
            continue;
        // Abschnitt des Strahls in der Naehe der Lampe: bis zum Objekt, hoechstens bis weit hinter der Lampe
        const Vector3D zuLampe = pl->position - ray.origin;
        const double tc = zuLampe * d; // naechster Punkt des Strahls zur Lampe
        const double abstand = zuLampe.length();
        double t1 = std::min(tMaxN, std::max(tc, 0.0) + 3.0 * abstand + 1.0);
        if (!(t1 > 0.0))
            continue;
        const double dt = t1 / SCHRITTE;
        const double versatz = randomDouble(0.0, 1.0);
        Farbe teil(0, 0, 0);
        for (int i = 0; i < SCHRITTE; ++i)
        {
            const double t = (i + versatz) * dt;
            const Vector3D pos = ray.origin + d * t;
            LightSample s = l->sample(pos); // enthaelt Leistung/(4 pi d^2) und den Kegelfaktor
            if (s.radiance.r + s.radiance.g + s.radiance.b <= 0.0 || !(s.distance > 1e-9))
                continue;
            // Schatten: liegt ein Objekt zwischen Ort und Lampe?
            Hit sh = ClosestPointOfIntersection(Ray(pos, s.directionToLight));
            if (sh.hit && !sh.hitLight && sh.t > abheben && sh.t < s.distance - abheben)
                continue;
            const double T = std::exp(-sigma * t);
            const double cosTheta = (-d) * s.directionToLight; // Winkel zwischen Licht und Blick zur Kamera
            const double phase = phaseHG(-cosTheta, 0.3);
            teil += s.radiance * (T * sigma * phase * dt);
        }
        summe += teil;
    }
    return summe * fogPartLightning;
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
    return rauchBis(ray, 1e30, farbe); // ohne Objekt-Abstand
}

// =======================================================
// Fügt Wolkenfarbe entlang eines Rays zur bestehenden Farbe hinzu
// =======================================================
Farbe wolke(Ray ray, Farbe farbe)
{
    return wolkeBis(ray, 1e30, farbe);
}

// =======================================================
// Volumen (Rauch, Wolken) entlang eines Strahls bis zum ersten Objekt
//
// Korrekturen gegenueber vorher:
//  - Das Volumen endet am ersten getroffenen Objekt (vorher schien Rauch/Wolke durch Objekte hindurch).
//  - Was hinter dem Volumen liegt, wird abgeschwaecht: farbe * Durchlaessigkeit + Streulicht
//    (vorher wurde das Streulicht nur dazugezaehlt).
//  - Groessen relativ zur Box bzw. zur Wolkenschicht (vorher feste Weltmasse: Rauchsaeule 0,4 Einheiten
//    breit, Wolken 60-200 Einheiten, Wolkenmitte immer bei y = 0).
//  - Zeit aus der Animation statt der Uhr (vorher sah jeder Bildteil eine andere Zeit).
//  - Rauschen unabhaengig vom Zoom der Ansichten (vorher fbm(p * scale), scale = Zoom der Drahtansichten).
// =======================================================

static double glatt(double kante0, double kante1, double x) // smoothstep, auch mit kante0 > kante1
{
    double t = std::clamp((x - kante0) / (kante1 - kante0), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

static double zellZufall(double x, double z, double k) // 0..1, fest je Zelle
{
    double s = std::sin(x * 127.1 + z * 311.7 + k * 74.7) * 43758.5453;
    return s - std::floor(s);
}

// gemittelte Richtung zu den Lichtern (fuer die Streuung im Volumen)
static Vector3D lichtRichtungImVolumen(const Vector3D &pos)
{
    Vector3D L(0, 0, 0);
    for (const auto &l : lights)
    {
        Vector3D d = l->directionFrom(pos);
        double n = d.length();
        if (n > 1e-12 && std::isfinite(n))
            L += d * (1.0 / n);
    }
    double n = L.length();
    return n > 1e-9 ? L * (1.0 / n) : Vector3D(0, 1, 0); // ohne Licht: von oben
}

// Hoehenbereich der Wolken: Wolkenschicht ("Wolken ab/bis Hoehe") innerhalb des Himmelsbereichs.
// Ueberschneiden sich beide nicht (Vorgabe in scene.cpp: Schicht 1..20, Himmel 30..100), gilt der Himmelsbereich.
static bool wolkenSchicht(double &unten, double &oben)
{
    double hu = std::min(SkyMin.y, SkyMax.y), ho = std::max(SkyMin.y, SkyMax.y);
    double su = std::min(WolkeboxMin.y, WolkeboxMax.y), so = std::max(WolkeboxMin.y, WolkeboxMax.y);
    unten = std::max(hu, su);
    oben = std::min(ho, so);
    if (oben - unten <= 1e-9)
    {
        unten = hu;
        oben = ho;
    }
    return oben - unten > 1e-9;
}

// Rauch: Streulicht der Boxen, Strahl von 0 bis tMax (erstes Objekt)
static Farbe rauchBis(const Ray &ray, double tMax, Farbe farbe)
{
    const Vector3D V = (-ray.direction).normalized();
    for (const auto &box : smokeBoxes)
    {
        double t0, t1;
        if (!intersectSmokeBox(ray, box, t0, t1))
            continue;
        t1 = std::min(t1, tMax);
        if (t1 <= t0)
            continue;

        const int SCHRITTE = 96;
        const double schritt = (t1 - t0) / SCHRITTE;
        double T = 1.0;
        Farbe streu(0, 0, 0);
        for (int i = 0; i < SCHRITTE && T > 0.01; ++i)
        {
            Vector3D pos = ray.origin + (t0 + (i + 0.5) * schritt) * ray.direction;
            double dichte = sampleDensityOfSmoke(box, pos) * box.densityMultiplier;
            if (dichte < 1e-5)
                continue;
            double anteil = 1.0 - std::exp(-dichte * schritt);

            // Schatten im Rauch: Richtung Licht bis zum Rand der Box
            Vector3D L = lichtRichtungImVolumen(pos);
            double licht = 1.0;
            double s0, s1;
            if (intersectSmokeBox(Ray(pos, L), box, s0, s1))
            {
                const int SCHATTEN = 8;
                double ds = (s1 - s0) / SCHATTEN, tiefe = 0;
                for (int k = 0; k < SCHATTEN; ++k)
                    tiefe += sampleDensityOfSmoke(box, pos + (s0 + (k + 0.5) * ds) * L) * box.densityMultiplier * ds;
                licht = std::exp(-tiefe);
            }
            licht = 0.2 + 0.8 * licht; // etwas Umgebungslicht
            double phase = 0.35 + 2.5 * phaseHG(L * V, 0.6); // gleichmaessig + vorwaerts

            streu += box.color * (T * anteil * licht * phase);
            T *= 1.0 - anteil;
        }
        farbe = farbe * T + streu;
    }
    return farbe;
}

// Wolken: Streulicht in der Wolkenschicht, Strahl von 0 bis tMax (erstes Objekt)
static Farbe wolkeBis(const Ray &ray, double tMax, Farbe farbe)
{
    double t0, t1;
    if (!intersectCloudBox(ray, t0, t1))
        return farbe;
    double unten, oben;
    if (!wolkenSchicht(unten, oben))
        return farbe;
    // nur in der Hoehenschicht marschieren (vorher 64 Schritte durch den ganzen Himmel)
    if (std::fabs(ray.direction.y) > 1e-12)
    {
        double ta = (unten - ray.origin.y) / ray.direction.y, tb = (oben - ray.origin.y) / ray.direction.y;
        t0 = std::max(t0, std::min(ta, tb));
        t1 = std::min(t1, std::max(ta, tb));
    }
    else if (ray.origin.y < unten || ray.origin.y > oben)
        return farbe;
    t1 = std::min(t1, tMax);
    if (t1 <= t0)
        return farbe;

    const double H = oben - unten;
    const double ausloeschung = 8.0 / H; // Dichte 1 -> optische Tiefe 8 ueber die Schichtdicke
    const Vector3D V = (-ray.direction).normalized();
    const int SCHRITTE = 64;
    const double schritt = (t1 - t0) / SCHRITTE;
    double T = 1.0;
    Farbe streu(0, 0, 0);
    for (int i = 0; i < SCHRITTE && T > 0.01; ++i)
    {
        Vector3D pos = ray.origin + (t0 + (i + 0.5) * schritt) * ray.direction;
        double dichte = sampleDensityCloud(pos) * ausloeschung;
        if (dichte < 1e-6)
            continue;
        double anteil = 1.0 - std::exp(-dichte * schritt);

        // Schatten: wenige Schritte Richtung Licht, hoechstens bis zur Schichtdicke
        Vector3D L = lichtRichtungImVolumen(pos);
        const int SCHATTEN = 5;
        double ds = H * 0.6 / SCHATTEN, tiefe = 0;
        for (int k = 1; k <= SCHATTEN; ++k)
            tiefe += sampleDensityCloud(pos + (k * ds) * L) * ausloeschung * ds;
        double licht = std::exp(-tiefe);
        licht = 0.3 + 0.7 * licht;
        double phase = 0.6 + 2.0 * phaseHG(L * V, 0.6);

        streu += Farbe(1, 1, 1) * (T * anteil * licht * phase);
        T *= 1.0 - anteil;
    }
    return farbe * T + streu;
}

// =======================================================
// Raymarching durch das Rauchvolumen
// =======================================================
//Farbe rauchInBox(Ray ray, double t_entry, double t_exit, Farbe smokeColor)
Farbe rauchInBox(Ray ray, double t_entry, double t_exit, const SmokeBox& box, Farbe smokeColor, double densityMultiplier)
{
    // Schrittweite: feste Anzahl von Samples im Volumen
    double stepSize = (t_exit - t_entry) / 256.0;
    double t = t_entry;

    Farbe color = Farbe(0,0,0);     // aufsummierte Streufarbe
    double transmittance = 1.0;     // verbleibendes Licht (Beer-Lambert)

    // Raymarching-Schleife
    while (t < t_exit)
    {
        // Aktuelle Sample-Position im Raum
        Vector3D pos = ray.origin + t * ray.direction;

        /*Vector3D size = box.max - box.min;
        Vector3D localPos(
            (pos.x - box.min.x) / size.x,
            (pos.y - box.min.y) / size.y,
            (pos.z - box.min.z) / size.z
        );*/
        
        // ---------------------------
        // Verzerrung der Abtastposition
        // ---------------------------
        double seed = box.seed;
        Vector3D p = pos;
        //Vector3D p = localPos;
        p.x += noise(Vector3D(seed, p.y * 0.5, 0)) * 0.2;
        p.z += noise(Vector3D(seed, p.y * 0.5, 1)) * 0.2;

        // Rauchdichte an dieser Position sampeln
        //double density = sampleDensityOfSmoke(p);
        double density = sampleDensityOfSmoke(box, p) * densityMultiplier;

        // Nur relevante Dichten berücksichtigen
        if (density > 0.0001)
        {
            // ---------------------------
            // Absorption (Extinktion)
            // ---------------------------
            double absorption = density * stepSize;
            transmittance *= exp(-absorption);

            // Licht- und Blickrichtung
            //Vector3D L = (lights[0].position - pos).normalized();   // Richtung zum Licht
            Vector3D L(0,0,0);

            for (const auto& light : lights)
            {
                L += light->directionFrom(pos);
            }

            L = L.normalized();
            Vector3D V = (-ray.direction).normalized();             // Blickrichtung

            // ---------------------------
            // Schatten innerhalb des Rauchs
            // ---------------------------
            double light = computeShadowOfSmoke(box, pos, L);
            light = 0.2 + 0.8 * light;   // Minimum an Umgebungslicht

            // ---------------------------
            // Phase Function (anisotrope Streuung)
            // ---------------------------
            double cosTheta = L * V;
            double phase = phaseHG(cosTheta, 0.6) * 4.0;

            // ---------------------------
            // Streulicht-Beitrag
            // ---------------------------
            color += smokeColor
                   * transmittance
                   * density //
                   * light
                   * phase
                   * stepSize
                   * 1.0
                   ;
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
            //Vector3D L = (lights[0].position - pos).normalized();   // Richtung zum Licht
            Vector3D L(0,0,0);

            for (const auto& light : lights)
            {
                L += light->directionFrom(pos);
            }

            L = L.normalized();
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
    return (1.0 - g*g) / (4.0 * M_PI * pow(denom, 1.5));
}

// =======================================================
// Ray–AABB-Schnitt mit der Rauchbox
// =======================================================
/*bool intersectSmokeBox(const Ray& ray, double& tmin, double& tmax)
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
    double tx1 = (box.min.x - ray.origin.x) * invDir.x;
    double tx2 = (box.max.x - ray.origin.x) * invDir.x;

    double ty1 = (box.min.y - ray.origin.y) * invDir.y;
    double ty2 = (box.max.y - ray.origin.y) * invDir.y;

    double tz1 = (box.min.z - ray.origin.z) * invDir.z;
    double tz2 = (box.max.z - ray.origin.z) * invDir.z;

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
*/

bool intersectSmokeBox(const Ray& ray, const SmokeBox& box, double& tmin, double& tmax)
{
    const double eps = 1e-8;

    // Sichere Invertierung der Richtungsvektoren
    auto safeInv = [&](double d)
    {
        return (fabs(d) > eps) ? 1.0 / d : 1e8;
    };

    Vector3D invDir(
        safeInv(ray.direction.x),
        safeInv(ray.direction.y),
        safeInv(ray.direction.z));

    // Schnittintervalle für jede Achse
    double tx1 = (box.min.x - ray.origin.x) * invDir.x;
    double tx2 = (box.max.x - ray.origin.x) * invDir.x;

    double ty1 = (box.min.y - ray.origin.y) * invDir.y;
    double ty2 = (box.max.y - ray.origin.y) * invDir.y;

    double tz1 = (box.min.z - ray.origin.z) * invDir.z;
    double tz2 = (box.max.z - ray.origin.z) * invDir.z;

    // Gemeinsames Eintritts- und Austrittsintervall
    tmin = std::max(
        std::max(std::min(tx1, tx2), std::min(ty1, ty2)),
        std::min(tz1, tz2));

    tmax = std::min(
        std::min(std::max(tx1, tx2), std::max(ty1, ty2)),
        std::max(tz1, tz2));

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
double computeShadowOfSmoke(const SmokeBox& box, Vector3D pos, Vector3D lightDir)
{
    Ray lightRay(pos, lightDir);

    double tmin, tmax;

    if (!intersectSmokeBox(lightRay, box, tmin, tmax))
    {
        return 1.0;
    }

    double t = tmin;

    double transmittance = 1.0;

    double stepSize = 0.2;

    while (t < tmax)
    {
        Vector3D samplePos =
            pos + t * lightDir;

        double density =
            sampleDensityOfSmoke(
                box,
                samplePos);

        transmittance *=
            exp(-density * stepSize * 1.5);

        if (transmittance < 0.01)
            break;

        t += stepSize;
    }

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
//double sampleDensityOfSmoke(Vector3D p)
double sampleDensityOfSmoke(const SmokeBox& box, Vector3D p)
{
    // alles relativ zur Box: halbe Breiten rx/rz, Hoehe ry (vorher feste Weltmasse)
    const double rx = std::max(1e-9, std::fabs(box.max.x - box.min.x) * 0.5);
    const double rz = std::max(1e-9, std::fabs(box.max.z - box.min.z) * 0.5);
    const double ry = std::max(1e-9, std::fabs(box.max.y - box.min.y));
    const double rMittel = 0.5 * (rx + rz);
    const double cx = (box.min.x + box.max.x) * 0.5;
    const double cz = (box.min.z + box.max.z) * 0.5;

    // normalisierte Hoehe in der Box
    double h = (p.y - std::min(box.min.y, box.max.y)) / ry;
    if (h < 0.0 || h > 1.0)
        return 0.0;

    // Zeit der Animation: der Rauch steigt und schlaengelt sich von Bild zu Bild
    const double zeit = aktuelleZeit() * 0.5;
    const double s = box.seed;

    // schlaengelnde Mittelachse (in halben Boxbreiten)
    double ax = (fbm(Vector3D(h * 1.8, s * 1.7, zeit)) - 0.5) * 0.6;
    double az = (fbm(Vector3D(s * 1.3 + 7.0, h * 1.8, zeit)) - 0.5) * 0.6;
    double dx = (p.x - cx) / rx - ax;
    double dz = (p.z - cz) / rz - az;

    // Radius je Hoehe (in der Mitte am breitesten)
    double radius = 0.2 + 0.6 * std::exp(-std::pow((h - 0.5) * 3.0, 2.0));
    double radial = std::exp(-(dx * dx + dz * dz) / (radius * radius));
    if (radial < 1e-4)
        return 0.0;

    // Feinstruktur, steigt mit der Zeit nach oben
    Vector3D q((p.x - cx) / rMittel, h * ry / rMittel - zeit * 1.5, (p.z - cz) / rMittel);
    double n = fbm(q + Vector3D(s * 3.1, 0.0, s * 5.7));
    n = n * n;

    double falloff = std::exp(-h * 3.0); // oben verduennt

    // Dichte je Welteinheit: bei einer 1 breiten Box wie bisher, groessere Boxen gleich deckend
    return radial * n * falloff * 5.0 * (0.5 / rMittel);
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
    double unten, oben;
    if (!wolkenSchicht(unten, oben) || p.y < unten || p.y > oben)
        return 0.0;
    const double H = oben - unten;

    // Zellgroesse nach dem Himmelsbereich: etwa fuenf Wolkenzellen ueber die Breite,
    // mindestens so gross wie die Schicht dick ist (vorher fest 500 Einheiten)
    const double breite = std::max(std::fabs(SkyMax.x - SkyMin.x), std::fabs(SkyMax.z - SkyMin.z));
    const double C = std::max(H * 1.0, breite / 5.0);

    // Wind: Wolken ziehen mit der Zeit der Animation
    const double zeit = aktuelleZeit();
    p.x -= zeit * C * 0.02;

    // leichte Verzerrung (eine Oktave reicht, fbm war hier teuer)
    Vector3D w(noise(p * (1.5 / C)), noise(p * (1.5 / C) + Vector3D(11, 3, 0)), noise(p * (1.5 / C) + Vector3D(23, 17, 0)));
    p += Vector3D(w.x - 0.5, (w.y - 0.5) * 0.8, w.z - 0.5) * (C * 0.1);

    const double zx = std::floor(p.x / C), zz = std::floor(p.z / C);
    const double hRel = (p.y - unten) / H;
    double dichte = 0.0;

    for (int dx = -1; dx <= 1; dx++)
        for (int dz = -1; dz <= 1; dz++)
        {
            const double cx = zx + dx, cz = zz + dz;
            if (zellZufall(cx, cz, 0) < 0.15) // keine Wolke in dieser Zelle
                continue;

            // Mitte und Groesse der Wolke (Mitte jetzt in der Schicht, vorher immer y = 0)
            Vector3D mitte((cx + 0.2 + 0.6 * zellZufall(cx, cz, 1)) * C,
                           unten + H * (0.4 + 0.2 * zellZufall(cx, cz, 2)),
                           (cz + 0.2 + 0.6 * zellZufall(cx, cz, 3)) * C);
            double g = zellZufall(cx, cz, 4);
            double groesse = C * (0.3 + 0.3 * g * g);

            Vector3D q((p.x - mitte.x) / groesse, (p.y - mitte.y) / (H * 0.6), (p.z - mitte.z) / groesse);
            double form = glatt(1.2, 0.5, q.length()); // Grundform (Cumulus)
            if (form <= 0.0)
                continue;

            // Hoehe: flacher Boden, runde Oberseite
            double b0 = 0.1 * zellZufall(cx, cz, 5), b1 = 1.0 - 0.1 * zellZufall(cx, cz, 6);
            double hh = (hRel - b0) / (b1 - b0);
            double hoehe = glatt(0.0, 0.15, hh) * glatt(1.0, 0.4, hh);
            if (hoehe <= 0.01)
                continue;

            // Struktur relativ zur Zellgroesse
            double basis = std::pow(fbm(p * (6.0 / C)), 1.2);
            double detail = fbm(p * (40.0 / C) + Vector3D(17.0, 43.0, 11.0));
            detail *= detail;
            double erosion = std::pow(fbm(p * (8.0 / C) + Vector3D(5.0, 9.0, 13.0)), 1.5);

            form = std::clamp(form - erosion * 1.2, 0.0, 1.0);
            double d = (basis + detail * 0.3) * form * hoehe * std::clamp(basis * 1.2, 0.0, 1.0);
            dichte += d;
        }
    return dichte;
}

/*Farbe flatShadingAndshadow(const Farbe& objectFarbe, const Vector3D& normale, const Vector3D& punkt)
{
    Vector3D N = normale.normalized();
    Farbe result(0,0,0);

    for (const auto& light : lights)
    {
        //Vector3D toLight = light.position - punkt;
        Vector3D toLight = light->directionFrom(punkt);
        double distToLight = toLight.length();
        Vector3D s = toLight.normalized();

        // Shadow Ray (kleiner Offset gegen Self Intersection)
        Ray shadowRay(punkt + N * strahlAbstand.load(std::memory_order_relaxed), s);

        Hit shadowHit = ClosestPointOfIntersection(shadowRay);

        // Prüfen: liegt etwas zwischen Punkt und light?
        if (shadowHit.hit && shadowHit.t > 0 && shadowHit.t < distToLight)
        {
            continue; // im Schatten, also keine Farbe zurückgeben
        }

        // Beleuchtung 
        //double intensity = std::max(0.0, s * N);
        Vector3D L = light->directionFrom(shadowHit.position);
        double diff = std::max(0.0, shadowHit.normal * L);

        if(diff > 0)
            //result = result + objectFarbe * light.farbe * intensity;
            Farbe Li = light->intensityAt(shadowHit.position);
            result += objectFarbe * Li * diff;
    }

    return result;// * (1.0/lights.size());
}*/

// =======================================================
// Wie viel Licht kommt durch Rauch, Wolken und Nebel bei 'punkt' an? (1 = alles, 0 = nichts)
// Fuer den Schattenstrahl des direkten Lichts: vorher prueften Schattenstrahlen nur Objekte,
// Volumen warfen keinen Schatten.
// =======================================================
static double volumenDurchlaessigkeit(const Vector3D &punkt, const Vector3D &richtung, double abstand)
{
    const bool endlich = std::isfinite(abstand);
    const double laenge = richtung.length();
    if (laenge < 1e-12)
        return 1.0;
    const Vector3D L = richtung * (1.0 / laenge);
    double tiefe = 0.0; // optische Tiefe

    // Nebel: gleichmaessig dicht, nur bei Lichtern mit Abstand (Richtungslicht: Nebel ohne Rand)
    if (nebelVorhanden && nebelDaempftLicht && endlich)
        tiefe += fogDensity * abstand;

    if (volumenSchatten)
    {
        const int N = std::max(1, std::min(volumenSchattenSchritte, 256));
        const Ray strahl(punkt, L);

        if (rauchVorhanden)
            for (const auto &box : smokeBoxes)
            {
                double t0, t1;
                if (!intersectSmokeBox(strahl, box, t0, t1))
                    continue;
                if (endlich)
                    t1 = std::min(t1, abstand);
                if (t1 <= t0)
                    continue;
                const double ds = (t1 - t0) / N;
                for (int i = 0; i < N; ++i)
                    tiefe += sampleDensityOfSmoke(box, punkt + (t0 + (i + 0.5) * ds) * L) * box.densityMultiplier * ds;
            }

        if (wolkeVolumeVorhanden)
        {
            double t0, t1, unten, oben;
            if (intersectCloudBox(strahl, t0, t1) && wolkenSchicht(unten, oben))
            {
                bool drin = true;
                if (std::fabs(L.y) > 1e-12)
                {
                    double ta = (unten - punkt.y) / L.y, tb = (oben - punkt.y) / L.y;
                    t0 = std::max(t0, std::min(ta, tb));
                    t1 = std::min(t1, std::max(ta, tb));
                }
                else
                    drin = punkt.y >= unten && punkt.y <= oben;
                if (endlich)
                    t1 = std::min(t1, abstand);
                if (drin && t1 > t0)
                {
                    const double ausloeschung = 8.0 / (oben - unten); // wie wolkeBis
                    const double ds = (t1 - t0) / N;
                    for (int i = 0; i < N; ++i)
                        tiefe += sampleDensityCloud(punkt + (t0 + (i + 0.5) * ds) * L) * ausloeschung * ds;
                }
            }
        }
    }
    return tiefe > 0.0 ? std::exp(-tiefe) : 1.0;
}

Farbe blinnPhong(
    const Material& mat,
    const Vector3D& normale,
    const Vector3D& punkt,
    const Hit h,
    const Vector3D& viewPos)
{
    Vector3D N = normale.normalized();
    Vector3D V = (viewPos - punkt).normalized();

    Farbe result(0,0,0);

    // Ambient: nur ohne indirektes Licht. Mit Path Tracing kommt die Grundhelligkeit aus dem
    // indirekten Licht (Himmel, andere Flaechen); Umgebung * kA dazu waere doppelt gezaehlt.
    const bool mitUmgebung = !indirektesLicht && mat.kA > 0.0 &&
                             (mat.ambient.r > 0.0 || mat.ambient.g > 0.0 || mat.ambient.b > 0.0);

    // =========================
    // Texture
    // =========================
    Farbe mat_diffuse =
        mat.diffuse;

    if (h.obj && h.obj->texture)
    {
        mat_diffuse =
            Texture::texture(
                mat_diffuse,
                h
            );
    }

    if (mitUmgebung)
    {
        Farbe umgebung = mat.ambient;
        // mit Textur: Umgebungslicht in der Farbe der Textur (Helligkeit wie die Umgebungsfarbe)
        if (h.obj && h.obj->texture)
        {
            auto hell = [](const Farbe &c) { return 0.2126 * c.r + 0.7152 * c.g + 0.0722 * c.b; };
            const double grund = std::max(hell(mat.diffuse), 1e-3);
            umgebung = mat_diffuse * (hell(mat.ambient) / grund);
        }
        result += umgebung * mat.kA;
    }
    
    for (const auto& light : lights)
    {
        int lightSamples = light->numberOfSamples(punkt);
        //int lightSamples = light->isArea() ? 64 : 1;
        Farbe lightSum(0,0,0);

        for (int i = 0; i < lightSamples; i++)
        {
            LightSample s = light->sample(punkt);

            Vector3D L = s.directionToLight;
            double dist = s.distance;
            Farbe Li = s.radiance;

            // Shadow ray
            // Schattenstrahl; durchsichtige Objekte (Glas) lassen Licht durch, geschwaecht um die
            // Transparenz (ohne Lichtbuendelung). Vorher warf Glas einen ganz schwarzen Schatten.
            const double abheben = strahlAbstand.load(std::memory_order_relaxed);
            bool directional = std::isinf(dist);
            Vector3D schattenStart = punkt + N * abheben;
            double zurueck = 0.0; // schon zurueckgelegter Weg
            double durchlass = 1.0;
            bool verdeckt = false;
            // bis 128 Schichten: Glas, Alpha-Ausschnitte und Fell-Huellen (je Huelle bis zu zwei Treffer; vorher 16 -
            // bei Beinen mit 10 Huellen endete der Strahl, bevor er das Bein traf: hohle, gestreifte Schatten)
            bool frei = false;
            for (int schicht = 0; schicht < 128; ++schicht)
            {
                Ray shadowRay(schattenStart, L);
                Hit shadowHit = ClosestPointOfIntersection(shadowRay);
                const double rest = dist - zurueck;
                const double schattenBis = rest - std::max(10.0 * abheben, dist * 1e-6);
                if (!(shadowHit.hit && shadowHit.t > 0 && (directional || shadowHit.t < schattenBis)))
                {
                    frei = true;
                    break; // frei bis zum Licht
                }
                // Das Symbol des Lichts selbst (Punkt-/Spotlicht: Kugel mit Radius 0,1 um die Lampe) oder ein
                // anderes Licht wirft keinen Schatten. Vorher traf jeder Schattenstrahl die Kugel der eigenen
                // Lampe kurz vor dem Ziel -> Punkt- und Spotlichter beleuchteten gar nichts.
                if (shadowHit.hitLight)
                {
                    frei = true;
                    break;
                }
                double t = !shadowHit.material ? 0.0 : shadowHit.material->transparency;
                if (shadowHit.obj && Texture::alphaBei(shadowHit) < 0.5)
                    t = 1.0; // ausgeschnittene Stelle (Alpha): Licht geht ungehindert durch
                else if (shadowHit.obj && shadowHit.obj->texturPar().fellHuelle)
                    // Haar der Fell-Imitation: auf andere Huellen nur ein Hauch Schatten je Huelle (Tiefe im Fell,
                    // 10 Huellen ~ 43 % Licht); auf Koerper, Boden und alles andere keiner - sonst entsteht dort ein
                    // gesprenkelter Kranz aus Haarspitzen-Schatten (der Schatten des Koerpers bleibt)
                    t = std::max(t, (h.obj && h.obj->texturPar().fellHuelle) ? 0.92 : 1.0);
                if (t <= 0.0)
                {
                    verdeckt = true;
                    break;
                }
                durchlass *= std::min(1.0, t);
                if (durchlass < 1e-3)
                {
                    verdeckt = true;
                    break;
                }
                zurueck += shadowHit.t + abheben;
                schattenStart = shadowHit.position + L * abheben;
            }
            if (verdeckt || !frei) // zu viele Schichten: lieber Schatten als ein falsch heller Fleck
                continue;
            Li = Li * durchlass;

            // (Flaechen-, Kugel- und Mesh-Licht: der Abtastpunkt liegt auf der Lichtoberflaeche selbst;
            // der Treffer dort, t ~ dist, ist kein Schatten -> schattenBis oben)

            // Diffuse
            double diff = std::max(0.0, N * L);
            if (zeichentrick)
                diff = tonStufe(diff);

            if (diff <= 0.0) continue;

            // Rauch, Wolken und Nebel zwischen Punkt und Licht schwaechen das Licht (Volumenschatten)
            if (volumenSchatten || (nebelVorhanden && nebelDaempftLicht))
            {
                double durch = volumenDurchlaessigkeit(punkt + N * strahlAbstand.load(std::memory_order_relaxed), L, dist);
                if (durch < 1e-4)
                    continue;
                Li = Li * durch;
            }

            // Specular
            Vector3D H = (L + V).normalized();
            double spec = pow(std::max(0.0, N * H), mat.shininess);
            if (zeichentrick)
                spec = spec > 0.5 ? 1.0 : 0.0; // kleiner, scharf begrenzter Glanzpunkt

            // =========================
            // Lighting Contribution
            // =========================
            //std::cout << mat_diffuse << " " << Li << " " << mat.kD << " " << diff << std::endl;
            Farbe diffuse = 
                mat_diffuse *
                Li *
                mat.kD *
                diff;

            
            Farbe specular = 
                mat.specular *
                mat.specularStrength *
                Li *
                spec;

            // Lambert: BRDF = Farbe / pi (wie beim indirekten Licht, das mit der Farbe allein
            // multipliziert, weil sich dort das pi gegen die Wahrscheinlichkeitsdichte kuerzt).
            // Vorher fehlte 1/pi: das direkte Licht war im Verhaeltnis zum indirekten ~3x zu stark.
            // Der Glanz wird gleich skaliert, damit Glanzpunkte zum diffusen Licht passen wie bisher.
            constexpr double EIN_DURCH_PI = 1.0 / M_PI;
            if (mat.reflection == 0.0)
                lightSum += (diffuse + specular) * EIN_DURCH_PI;
            else
                lightSum += diffuse * EIN_DURCH_PI;
        }

        result += lightSum * (1.0 / lightSamples);
    }
    // Randlicht (Zeichentrick): heller, scharf begrenzter Saum, wo die Flaeche vom Betrachter wegkippt
    if (zeichentrick && randlicht > 0.0)
    {
        double r = 1.0 - std::max(0.0, N * V);
        double saum = std::clamp((r * r * r - 0.35) / 0.1, 0.0, 1.0);
        if (saum > 0.0)
            result += (mat_diffuse * 0.5 + Farbe(0.5, 0.5, 0.5)) * (saum * randlicht);
    }

    // Emission (Leuchten) wird in trace() dazugezaehlt, nicht hier: hier haette sie mit kd
    // (Anteil diffus) multipliziert an Spiegeln und Glas fast keine Wirkung gehabt

    return result;
}

/*Farbe flatShading(const Farbe& objectFarbe, const Vector3D& normale,  const Vector3D& punkt)
{
    Vector3D N = normale.normalized();
    Farbe result(0,0,0);

    for (const auto& light : lights)
    {
        //Vector3D s = (light.position - punkt).normalized();
        Vector3D s = (light->directionFrom(punkt)).normalized();

        double intensity = s * N; 

        if(intensity > 0) 
            result = result + objectFarbe * light.farbe * intensity;
    }

    return result;// * (1.0/lights.size());
}*/

// ======================================================================
// Szenen-BVH: Beschleunigungsstruktur ueber alle Objekte der Szene
// ----------------------------------------------------------------------
// Vorher prueft jeder Strahl jedes Objekt der Szene (bei 600 Kieseln bis zu 600 Huellen-Tests je Strahl, auch
// fuer jeden Schatten-, Spiegel- und Streustrahl). Jetzt: ein Binaerbaum aus Huellquadern, geteilt am Median
// der Objektmitten entlang der laengsten Achse; ein Strahl steigt nur in Boxen ab, die er vor dem bisher
// naechsten Treffer erreicht, die naehere zuerst. Etwa log2(n) Tests statt n.
// Gebaut in startRendering (vor den Render-Threads, danach nur gelesen); Aenderungen an der Szene laufen ueber
// gSzeneAendern, das vorher das Rendern anhaelt - beim naechsten Rendern wird neu gebaut (Animation: je Bild).
// Objekte ohne gueltige Grenzen werden wie bisher einzeln geprueft.
// ======================================================================
// Grenzen eines Objekts wie in den Ansichten (gpu_ansichten.cpp): Netze aus ihren Dreiecken, Stab mit Radius ...
bool szenenObjektGrenzen(const object *o, Vector3D &mn, Vector3D &mx);

namespace
{
struct SzKnoten
{
    Vector3D mn, mx;
    int links = -1, rechts = -1; // innere Knoten
    int start = 0, anzahl = 0;   // Blatt: Objekte [start, start + anzahl)
};
struct SzenenBVH
{
    std::vector<SzKnoten> knoten;
    std::vector<object *> objekte;
    std::vector<object *> einzeln; // ohne gueltige Grenzen
    bool gueltig = false;
};
SzenenBVH gSzBVH;

struct SzEintrag
{
    object *o;
    Vector3D mn, mx, mitte;
};

int szBauen(std::vector<SzEintrag> &e, int von, int bis, SzenenBVH &b)
{
    SzKnoten k;
    k.mn = e[size_t(von)].mn;
    k.mx = e[size_t(von)].mx;
    Vector3D cmn = e[size_t(von)].mitte, cmx = cmn;
    for (int i = von; i < bis; ++i)
    {
        const SzEintrag &x = e[size_t(i)];
        k.mn = Vector3D(std::min(k.mn.x, x.mn.x), std::min(k.mn.y, x.mn.y), std::min(k.mn.z, x.mn.z));
        k.mx = Vector3D(std::max(k.mx.x, x.mx.x), std::max(k.mx.y, x.mx.y), std::max(k.mx.z, x.mx.z));
        cmn = Vector3D(std::min(cmn.x, x.mitte.x), std::min(cmn.y, x.mitte.y), std::min(cmn.z, x.mitte.z));
        cmx = Vector3D(std::max(cmx.x, x.mitte.x), std::max(cmx.y, x.mitte.y), std::max(cmx.z, x.mitte.z));
    }
    const int index = int(b.knoten.size());
    b.knoten.push_back(k);
    const Vector3D ausd = cmx - cmn;
    if (bis - von <= 2 || std::max({ausd.x, ausd.y, ausd.z}) <= 0.0)
    {
        b.knoten[size_t(index)].start = int(b.objekte.size());
        b.knoten[size_t(index)].anzahl = bis - von;
        for (int i = von; i < bis; ++i)
            b.objekte.push_back(e[size_t(i)].o);
        return index;
    }
    const int achse = ausd.x >= ausd.y && ausd.x >= ausd.z ? 0 : ausd.y >= ausd.z ? 1 : 2;
    auto wert = [achse](const SzEintrag &x) { return achse == 0 ? x.mitte.x : achse == 1 ? x.mitte.y : x.mitte.z; };
    const int mitte = (von + bis) / 2;
    std::nth_element(e.begin() + von, e.begin() + mitte, e.begin() + bis,
                     [&](const SzEintrag &a, const SzEintrag &c) { return wert(a) < wert(c); });
    const int l = szBauen(e, von, mitte, b);
    const int r = szBauen(e, mitte, bis, b);
    b.knoten[size_t(index)].links = l;
    b.knoten[size_t(index)].rechts = r;
    return index;
}

// Abstand, ab dem der Strahl die Box trifft (false: verfehlt oder erst hinter tMax)
inline bool szBox(const Ray &ray, const Vector3D &mn, const Vector3D &mx, double tMax, double &tNah)
{
    double t0 = 0.0, t1 = tMax;
    const double o[3] = {ray.origin.x, ray.origin.y, ray.origin.z}, d[3] = {ray.direction.x, ray.direction.y, ray.direction.z};
    const double a[3] = {mn.x, mn.y, mn.z}, b[3] = {mx.x, mx.y, mx.z};
    for (int i = 0; i < 3; ++i)
    {
        if (d[i] == 0.0)
        {
            if (o[i] < a[i] || o[i] > b[i])
                return false;
            continue;
        }
        const double inv = ray.invDirection[i];
        double ta = (a[i] - o[i]) * inv, tb = (b[i] - o[i]) * inv;
        if (ta > tb)
            std::swap(ta, tb);
        t0 = ta > t0 ? ta : t0;
        t1 = tb < t1 ? tb : t1;
        if (t0 > t1)
            return false;
    }
    tNah = t0;
    return true;
}
} // namespace

void szenenBVHBauen()
{
    SzenenBVH b;
    std::vector<SzEintrag> e;
    for (const auto &o : scene)
    {
        Vector3D mn, mx;
        const bool ok = o && szenenObjektGrenzen(o.get(), mn, mx) && std::isfinite(mn.x) && std::isfinite(mn.y) &&
                        std::isfinite(mn.z) && std::isfinite(mx.x) && std::isfinite(mx.y) && std::isfinite(mx.z) &&
                        mn.x <= mx.x && mn.y <= mx.y && mn.z <= mx.z;
        if (!ok)
        {
            if (o)
                b.einzeln.push_back(o.get());
            continue;
        }
        // etwas Rand gegen Rundung (Treffer genau auf der Grenze, flache Ebenen mit Dicke 0)
        const double rand = 1e-7 * (mx - mn).length() + 1e-9;
        mn = mn - Vector3D(rand, rand, rand);
        mx = mx + Vector3D(rand, rand, rand);
        e.push_back({o.get(), mn, mx, (mn + mx) * 0.5});
    }
    if (!e.empty())
    {
        b.knoten.reserve(e.size() * 2);
        b.objekte.reserve(e.size());
        szBauen(e, 0, int(e.size()), b);
    }
    b.gueltig = true;
    gSzBVH = std::move(b);
}

Hit ClosestPointOfIntersection(const Ray& ray)
{
    Hit best;
    best.t = std::numeric_limits<double>::infinity();
    best.hit = false;

    if (gSzBVH.gueltig)
    {
        for (object *o : gSzBVH.einzeln)
            o->intersect(ray, best);
        if (!gSzBVH.knoten.empty())
        {
            int stapel[128];
            int n = 0;
            double tNah;
            if (szBox(ray, gSzBVH.knoten[0].mn, gSzBVH.knoten[0].mx, best.t, tNah))
                stapel[n++] = 0;
            while (n > 0)
            {
                const SzKnoten &k = gSzBVH.knoten[size_t(stapel[--n])];
                if (k.links < 0)
                {
                    for (int i = 0; i < k.anzahl; ++i)
                        gSzBVH.objekte[size_t(k.start + i)]->intersect(ray, best);
                    continue;
                }
                double tl = 0, tr = 0;
                const bool l = szBox(ray, gSzBVH.knoten[size_t(k.links)].mn, gSzBVH.knoten[size_t(k.links)].mx, best.t, tl);
                const bool r = szBox(ray, gSzBVH.knoten[size_t(k.rechts)].mn, gSzBVH.knoten[size_t(k.rechts)].mx, best.t, tr);
                if (n + 2 > 128) // sehr tiefer Baum (kommt bei Median-Teilung nicht vor): Rest einzeln
                {
                    for (object *o : gSzBVH.objekte)
                        o->intersect(ray, best);
                    break;
                }
                // die naehere Box zuletzt auf den Stapel = zuerst pruefen
                if (l && r)
                {
                    stapel[n++] = tl <= tr ? k.rechts : k.links;
                    stapel[n++] = tl <= tr ? k.links : k.rechts;
                }
                else if (l)
                    stapel[n++] = k.links;
                else if (r)
                    stapel[n++] = k.rechts;
            }
        }
    }
    else
        for(const auto& obj : scene)
            obj->intersect(ray, best);

    // Lights prüfen
    //if (!best.hitObject)
        for (auto& light : lights)
            if (!dynamic_cast<const PointLight *>(light.get())) // Punkt-/Spotlicht: fuer Kamera und Schatten unsichtbar
                light->intersect(ray, best);

    return best;
}

Farbe trace(const Ray& ray, int depth, double current_ior = 1.0)
{
    if(depth <= 0)
        return Farbe(0,0,0);

    const bool nachDiffus = nachDiffuserStreuung;
    nachDiffuserStreuung = false; // gilt nur fuer diesen einen Strahl

    Hit h = ClosestPointOfIntersection(ray);

    // Licht direkt getroffen? Nach diffuser Streuung nicht: schon im direkten Licht enthalten
    if (h.hitLight)
        return nachDiffus ? Farbe(0, 0, 0) : h.light->getEmission();

    if(!h.hit)
        return Texture::backgroundCalc("sky", ray.direction);

    // Texturen: Groesse eines Bildpunkts am Treffer (Mipmap-Stufe, Relief-Schrittweite)
    const double fussHier = h.t * ray.direction.length() * texturPixelWinkel.load(std::memory_order_relaxed);
    texturFussSetzen(fussHier); // (nach Spiegel-/Brechungsstrahlen unten wieder gesetzt)

    // Alpha (Bild mit Transparenz oder Alpha-Bild): ausgeschnittene Stellen sind Luft, der Strahl laeuft weiter
    if (h.obj && alphaSchichten < 64 && Texture::alphaBei(h) < 0.5)
    {
        const Vector3D d = ray.direction.normalized();
        const double weiter = std::max(10.0 * strahlAbstand.load(std::memory_order_relaxed), h.t * 1e-7);
        ++alphaSchichten;
        nachDiffuserStreuung = nachDiffus; // gilt fuer den weiterlaufenden Strahl unveraendert
        Farbe c = trace(Ray(h.position + d * weiter, d), depth, current_ior);
        --alphaSchichten;
        return c;
    }

    Vector3D N = h.normale.normalized();
    Vector3D V = -ray.direction.normalized();

    // ------------------------------------------------------------
    // Spiegelung und Brechung
    // Vorher: (1) beim Austritt aus dem Glas bekam refract() die nach aussen zeigende Normale ->
    // der Strahl wurde ins Glas zurueckgespiegelt, lief dort bis zur Rekursionstiefe hin und her
    // -> schwarz; (2) Gewicht der Spiegelung Rf * (1 - kt) = 0 bei Transparenz 1, auch bei
    // Totalreflexion -> schwarz; (3) "Spiegelung" wirkte nicht, sobald "Brechung" > 0 war.
    // ------------------------------------------------------------
    // Glanzbild: macht Spiegelung und Glanzpunkte stellenweise schwaecher (Kopie des Materials nur dann)
    Material matGlanz;
    const Material *matZeiger = h.material;
    if (h.obj && h.obj->texPar)
    {
        const double glanz = Texture::glanzBei(h);
        if (glanz < 0.999)
        {
            matGlanz = *h.material;
            matGlanz.reflection *= glanz;
            matGlanz.specularStrength *= glanz;
            matZeiger = &matGlanz;
        }
    }
    // Regen: nasse Flaechen (dunkler, Wasserfilm glaenzt und spiegelt etwas) und Wellenringe auf Wasser
    double nassWert = 0.0, ringWert = 0.0;
    if (h.obj)
    {
        nassWert = h.obj->texturPar().nass;
        ringWert = regen::gRegen.an ? h.obj->texturPar().regenRinge : 0.0;
    }
    if (regen::gRegen.an && regen::gRegen.allesNass)
        nassWert = std::max(nassWert, regen::gRegen.nassStaerke);
    if (matZeiger->transparency > 0.5 || matZeiger->reflection > 0.9)
        nassWert = 0.0; // Glas, Wasser, Spiegel: schon nass bzw. glatt
    nassWert = std::clamp(nassWert, 0.0, 1.0);
    if (nassWert > 0.0)
    {
        if (matZeiger != &matGlanz)
        {
            matGlanz = *matZeiger;
            matZeiger = &matGlanz;
        }
        matGlanz.reflection = std::min(1.0, matGlanz.reflection + 0.1 * nassWert);
        matGlanz.roughness = matGlanz.roughness * (1.0 - 0.85 * nassWert);
        matGlanz.specularStrength = std::max(matGlanz.specularStrength, 0.8 * nassWert);
        matGlanz.shininess = std::max(matGlanz.shininess, 150.0);
        const double sp = 0.5 * nassWert;
        matGlanz.specular = Farbe(std::max(matGlanz.specular.r, sp), std::max(matGlanz.specular.g, sp), std::max(matGlanz.specular.b, sp));
    }
    const double nassDunkel = 1.0 - 0.4 * nassWert; // nasse Flaechen sind dunkler

    const Material &mat = *matZeiger;
    const double eps = strahlAbstand.load(std::memory_order_relaxed);

    // Normale gegen den Strahl (geometrisch; die Schattierungsnormale kann bei glatten Netzen abweichen)
    Vector3D Ng = h.geomNormale.length() > 1e-12 ? h.geomNormale.normalized() : N;
    const bool inside = ray.direction * Ng > 0; // Strahl kommt von innen
    const Vector3D Nr = inside ? -Ng : Ng;
    if (N * Nr < 0)
        N = -N;
    // Reliefbild: Normale nach der Steigung des Hoehenbilds kippen (nicht unter die Flaeche)
    if (h.obj && h.obj->texPar)
    {
        const Vector3D Nb = Texture::reliefNormale(h, N);
        if (Nb * Nr > 0.05)
            N = Nb;
    }
    // Wellenringe der Regentropfen: gekippte Normale auch fuer Spiegelung und Brechung (Ns), Spritzer hell
    Vector3D Ns = Nr;
    double spritzer = 0.0;
    if (ringWert > 0.0)
    {
        const Vector3D Nw = regen::ringNormale(h.position, Nr, aktuelleZeit(), ringWert, spritzer);
        if (Nw * Nr > 0.05)
        {
            Ns = Nw;
            N = (N * Nr > 0) ? Nw : -Nw;
        }
    }

    // Brechzahlen: aussen Luft (1), innen das Material
    const double ior = mat.IndexOfRefraction;
    const double n1 = inside ? ior : 1.0;
    const double n2 = inside ? 1.0 : ior;
    (void)current_ior;

    // Fresnel fuer Dielektrika (exakt, mit Totalreflexion)
    double fr = 0.0;
    const double cosI = std::clamp(-(ray.direction.normalized() * Nr), 0.0, 1.0);
    if (ior > 0.0 && std::fabs(n1 - n2) > 1e-9)
    {
        const double eta = n1 / n2;
        const double sin2T = eta * eta * (1.0 - cosI * cosI);
        if (sin2T >= 1.0)
            fr = 1.0; // Totalreflexion
        else
        {
            const double cosT = std::sqrt(1.0 - sin2T);
            const double rs = (n1 * cosI - n2 * cosT) / (n1 * cosI + n2 * cosT);
            const double rp = (n2 * cosI - n1 * cosT) / (n2 * cosI + n1 * cosT);
            fr = std::clamp(0.5 * (rs * rs + rp * rp), 0.0, 1.0);
        }
    }

    const double kt = std::clamp(mat.transparency, 0.0, 1.0);
    // undurchsichtiger Anteil: "Spiegelung" oder Fresnel, was groesser ist
    const double Ro = std::clamp(std::max(mat.reflection, fr), 0.0, 1.0);

    const double kr = kt * fr + (1.0 - kt) * Ro;   // Spiegelung
    const double ktw = kt * (1.0 - fr);            // Brechung (durch das Glas)
    const double kd = (1.0 - kt) * (1.0 - Ro);     // matte Flaeche (direktes + indirektes Licht)

    Farbe reflectColor(0,0,0);
    Farbe refractColor(0,0,0);

    if (kr > 1e-4)
    {
        Vector3D Rdir = reflect(ray.direction, Ns);
        if (Rdir * Nr <= 1e-6) // gekippte Normale: nicht unter die Flaeche spiegeln
            Rdir = reflect(ray.direction, Nr);

        // Roughness = Streuung
        double rough = mat.roughness;
        if (rough > 0.0)
        {
            Vector3D jitter = randomUnitVector() * rough;
            Vector3D r2 = (Rdir + jitter).normalized();
            if (r2 * Nr > 0) // nicht unter die Flaeche
                Rdir = r2;
        }
        Ray reflectRay(h.position + Nr * eps, Rdir);
        reflectColor = trace(reflectRay, depth-1, inside ? ior : 1.0);
    }

    if (ktw > 1e-4)
    {
        Vector3D Tdir = refract(ray.direction, Ns, n1, n2);
        if (Tdir.length() > 0 && Tdir * Nr >= -1e-6) // gekippte Normale: muss in die Flaeche hinein
            Tdir = refract(ray.direction, Nr, n1, n2);

        if(Tdir.length() > 0)
        {
            Ray refractRay(h.position - Nr * eps, Tdir.normalized());
            refractColor = trace(refractRay, depth-1, n2);

            // Absorption auf dem Weg durch das Material (nur beim Eintreten)
            Farbe sigma = mat.absorption;
            if (!inside && (sigma.r > 0 || sigma.g > 0 || sigma.b > 0))
            {
                Hit exitHit = ClosestPointOfIntersection(refractRay);
                if (exitHit.hit && exitHit.obj == h.obj)
                {
                    double d = (exitHit.position - h.position).length();
                    refractColor = refractColor * Farbe(exp(-sigma.r * d), exp(-sigma.g * d), exp(-sigma.b * d));
                }
            }
        }
    }

    // Licht der matten Flaeche nur, wenn sie zaehlt (klares Glas: kd = 0, spart alle Schattenstrahlen)
    texturFussSetzen(fussHier); // die Strahlen oben haben ihre eigene Pixelgroesse gesetzt
    Farbe localColor = kd > 1e-4 ? blinnPhong(mat, N, h.position, h, cam[0].position) : Farbe(0, 0, 0);
    if (nassWert > 0.0)
        localColor = localColor * nassDunkel;

    // Metalle: die Spiegelung hat die Farbe des Metalls. Als Metallfarbe dient "Glanz" (specular),
    // sobald "Spiegelung" > 0 ist (dort zeichnet blinnPhong keine Glanzpunkte, die Farbe war bisher
    // ungenutzt). Flach betrachtet wird auch Metall weisslich (Schlick: F0 -> 1). Glanz = 0 oder
    // Spiegelung = 0: ungefaerbt wie bisher. Vorher wirkte z.B. Gold grau: es spiegelte nur die Umgebung.
    Farbe spiegelFarbe(1, 1, 1);
    const double spiegelAnteilMetall = (1.0 - kt) * Ro;
    if (mat.reflection > 0.0 && std::max({mat.specular.r, mat.specular.g, mat.specular.b}) > 0.0)
    {
        const double f5 = std::pow(1.0 - cosI, 5.0);
        auto schlick = [&](double f0) { return std::clamp(f0, 0.0, 1.0) + (1.0 - std::clamp(f0, 0.0, 1.0)) * f5; };
        spiegelFarbe = Farbe(schlick(mat.specular.r), schlick(mat.specular.g), schlick(mat.specular.b));
    }

    Farbe result = localColor * kd
                + reflectColor * spiegelFarbe * spiegelAnteilMetall
                + reflectColor * (kt * fr)
                + refractColor * ktw;

    // Eigenes Leuchten der Flaeche, unabhaengig von Licht, Spiegelung und Transparenz (dazu ein Leuchtbild)
    result += h.material->emission;
    if (h.obj && h.obj->texPar)
        result += Texture::leuchtenBei(h);
    if (spritzer > 0.0) // Spritzer eines Regentropfens: heller Fleck in der Farbe des Himmels
        result += Texture::backgroundCalc("sky", Vector3D(0, 1, 0)) * (0.8 * spritzer);

    // Path Tracing eingebauet (vorerst nur für diffuse Materialien)
    Farbe indirect(0,0,0);
    if(kd > 0.001 && indirektesLicht) // aus: Umgebung * kA in blinnPhong statt dessen
    {
        // -------------------------------------------------
        // Russian Roulette
        // -------------------------------------------------

        double p = std::max({
            h.material->diffuse.r,
            h.material->diffuse.g,
            h.material->diffuse.b
        });

        p = clamp(p, 0.1, 0.95);

        // erst ab tieferen Bounces
        if(depth < 7)
        {
            if(randomDouble(0,1) > p)
                return result;
        }

        // -------------------------------------------------
        // Diffuse Bounce
        // -------------------------------------------------

        Vector3D bounceDir =
            cosineSampleHemisphere(N);

        Ray bounceRay(
            h.position + N * strahlAbstand.load(std::memory_order_relaxed),
            bounceDir);

        nachDiffuserStreuung = true;
        Farbe incoming =
            trace(bounceRay, depth-1, current_ior);
        nachDiffuserStreuung = false;
        
        // brdf * cosTheta / pdf = (diffuse / pi) * cosTheta * (pi / cosTheta) = diffuse.
        // Vorher ausgerechnet: bei cosTheta = 0 (Richtung genau in der Flaeche, wenn
        // cosineSampleHemisphere r2 = 1 zieht) ergab 0 * (1/0) ein NaN -> schwarzer Pixel.
        // Farbe der Flaeche fuer das indirekte Licht: mit Textur (vorher nur die Materialfarbe; in Szenen,
        // die vor allem vom Himmel beleuchtet werden, war die Textur dadurch fast unsichtbar)
        Farbe albedo = h.material->diffuse;
        texturFussSetzen(fussHier);
        if (h.obj && h.obj->texture && h.obj->textureMode != TextureMode::None)
            albedo = Texture::texture(albedo, h);
        if (nassWert > 0.0)
            albedo = albedo * nassDunkel;
        indirect =
            incoming *
            albedo;

        // Energieausgleich
        indirect =
            indirect * (1.0 / p);

        // Ausreisser begrenzen: ein einzelner Strahl darf nicht beliebig hell werden
        // (Russian Roulette teilt durch p >= 0,1: ein Treffer zaehlte bis zu zehnfach)
        if (indirektGrenze > 0.0)
        {
            double hell = std::max({indirect.r, indirect.g, indirect.b});
            if (hell > indirektGrenze)
                indirect = indirect * (indirektGrenze / hell);
        }
    }

    result += indirect * kd;

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

double Halton(int index, int base)
{
    double result = 0.0;
    double f = 1.0;
    int i = index;

    while (i > 0)
    {
        f /= base;
        result += f * (i % base);
        i /= base;
    }

    return result;
}

Vector3D randomHemisphereDirection(const Vector3D& N)
{
    Vector3D dir;

    do
    {
        dir = randomUnitVector();
    }
    while(dir * N <= 0);

    return dir.normalized();
}

Vector3D cosineSampleHemisphere(const Vector3D& N)
{
    double r1 = randomDouble(0,1);
    double r2 = randomDouble(0,1);

    double phi = 2.0 * M_PI * r1;

    double x = cos(phi) * sqrt(r2);
    double y = sin(phi) * sqrt(r2);
    double z = sqrt(1.0 - r2);

    // Tangent Space erzeugen
    Vector3D up =
        fabs(N.z) < 0.999
        ? Vector3D(0,0,1)
        : Vector3D(1,0,0);

    Vector3D tangent =
        (up.cross(N)).normalized();

    Vector3D bitangent =
        N.cross(tangent);

    // Local -> World
    return (
        tangent * x +
        bitangent * y +
        N * z
    ).normalized();
}
// =======================================================
// Vorschau zu Beginn des Renderns: statt eines schwarzen Bildes zuerst die Szene wie in der perspektivischen
// Ansicht - je Bildpunkt (bzw. 2x2-Block) nur der Kamerastrahl, Grundfarbe mit Textur, Licht von der Kamera aus
// (wie die schattierte Ansicht), Himmel wie im fertigen Bild. Dauert Bruchteile einer Sekunde; die Kacheln des
// eigentlichen Renderns ueberschreiben sie nach und nach. Parallel in den freien Threads.
// =======================================================
void vorschauRendern(Fenster &f, std::atomic<bool> &stop)
{
    if (cam.empty())
        return;
    const int W = f.breite(), H = f.hoehe();
    if (W <= 0 || H <= 0)
        return;
    const int S = (size_t(W) * size_t(H) > 1500000) ? 2 : 1; // grosse Bilder: ein Strahl je 2x2 Bildpunkte
    texturPixelWinkel.store(2.0 * cam[0].halfHeight / std::max(1, H), std::memory_order_relaxed);
    std::atomic<int> naechsteZeile{0};
    auto arbeit = [&]()
    {
        for (;;)
        {
            const int y = naechsteZeile.fetch_add(S);
            if (y >= H || stop.load())
                return;
            for (int x = 0; x < W; x += S)
            {
                Ray r = cam[0].makeRay(x + 0.5 * (S - 1), y + 0.5 * (S - 1), f);
                Hit h = ClosestPointOfIntersection(r);
                Farbe c(0, 0, 0);
                if (h.hitLight && h.light)
                    c = h.light->getEmission();
                else if (!h.hit || !h.material)
                    c = Texture::backgroundCalc("sky", r.direction);
                else
                {
                    Farbe grund = h.material->diffuse;
                    texturFussSetzen(h.t * r.direction.length() * texturPixelWinkel.load(std::memory_order_relaxed));
                    if (h.obj && h.obj->texture && h.obj->textureMode != TextureMode::None)
                        grund = Texture::texture(grund, h);
                    Vector3D n = h.normale.normalized();
                    const double cosK = std::fabs(n * r.direction.normalized()); // Licht von der Kamera
                    c = grund * (0.3 + 0.7 * cosK) + h.material->emission;
                }
                Farbe fertig = linearToSRGB(toneMapping(c, exposure));
                for (int by = y; by < std::min(y + S, H); ++by)
                    for (int bx = x; bx < std::min(x + S, W); ++bx)
                        f.pixel(bx, by, fertig);
            }
        }
    };
    const unsigned n = std::max(1u, std::thread::hardware_concurrency() > 2 ? std::thread::hardware_concurrency() - 2 : 1u);
    std::vector<std::thread> threads;
    for (unsigned i = 1; i < n; ++i)
        threads.emplace_back(arbeit);
    arbeit();
    for (auto &t : threads)
        t.join();
}
