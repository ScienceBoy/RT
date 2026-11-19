#pragma once
#include "farbe.h"
// Einstellungen gegen Rauschen (definiert in render.cpp; Fenster "Render-Einstellungen", <settings> in der XML)

extern bool adaptivAn;          // unruhige Pixel gezielt nachrechnen
extern bool indirektesLicht;    // Path Tracing (Licht von Flaechen und Himmel); aus: Umgebung * kA statt dessen
extern int adaptivMin;          // Abtastungen je Pixel mindestens (zum Schaetzen der Unruhe)
extern int adaptivMax;          // Abtastungen je Pixel hoechstens
extern double rauschSchwelle;   // so ruhig soll ein Pixel werden (relativer Fehler des Mittelwerts)
extern double indirektGrenze;   // indirektes Licht je Strahl hoechstens so hell (0 = keine Grenze)
extern bool volumenSchatten;      // Rauch und Wolken werfen Schatten auf Objekte
extern int volumenSchattenSchritte; // Abtastpunkte je Schattenstrahl und Volumen
extern bool nebelDaempftLicht;    // Licht von Punkt-/Kugellichtern verliert im Nebel an Kraft
extern bool zeichentrick;         // Stil: Farbstufen, harte Glanzpunkte, Randlicht, Konturen
extern int tonStufen;             // Helligkeitsstufen
extern double saettigung;         // 1 = unveraendert
extern bool konturen;             // Konturlinien (mit Zeichentrick)
extern double konturDicke;        // in Pixeln
extern double randlicht;          // Staerke des hellen Saums (0 = aus)
extern int kachelGroesse;
extern int renderBreite, renderHoehe; // Bildgroesse in Pixeln (0 = wie das Render-Fenster)         // Kachelgroesse der Render-Threads (1 = Pixel in zufaelliger Reihenfolge)
extern int schnellBlock;          // Schnell-Rendering: Blockgroesse in Pixeln (1 = aus)

// vor jedem Rendern (startRendering in threading.cpp): Tabelle der Bloecke fuer das Schnell-Rendering leeren
void schnellRenderVorbereiten(int breite, int hoehe);
// vor jedem Rendern (startRendering): Beschleunigungsstruktur (BVH) ueber alle Objekte der Szene bauen
void szenenBVHBauen();

// Kamera: Tiefenschaerfe (Linse) - Blende = Radius der Linse in Einheiten (0 = alles scharf),
// Fokusabstand entlang der Blickrichtung (0 = Abstand zum Kamera-Ziel)
extern double blende;
extern double fokusAbstand;
// Bewegungsunschaerfe in Animationen: Teilbilder je Bild (1 = aus) ueber 'verschluss' (Anteil der Bildzeit)
extern int bewegungTeilbilder;
extern double verschluss;

// Himmel (definiert in texture.cpp): Verlauf ohne Umgebungsbild und Helligkeit von Verlauf bzw. Bild.
// Der Himmel ist auch Lichtquelle: Strahlen ohne Treffer bringen seine Farbe als indirektes Licht mit.
extern Farbe himmelHorizont;    // unten / am Horizont
extern Farbe himmelZenit;       // oben
extern double himmelHelligkeit; // Faktor fuer Verlauf und Umgebungsbild (0 = schwarz, z.B. Weltall)
