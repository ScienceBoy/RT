#pragma once
// Animation aus Snapshots: zu mehreren Zeitpunkten wird der Zustand der Szene festgehalten
// (alle Objekte mit Geometrie, Material, Groesse, Drehung; Lichter; Kamera). Zeitpunkte
// dazwischen werden zwischen den beiden benachbarten Snapshots berechnet: Verschieben und
// Skalieren geradlinig, Drehungen auf dem Kreisbogen (kein Schrumpfen unterwegs).
// Dazu kann ein Objekt je Abschnitt eine Wiederholung bekommen (Drehen, Pendeln, Wippen, Abrollen).
// Implementiert in bearbeitung.cpp (dort ist der Zugriff auf alle Objektzustaende).

#include "vector3d.h"
#include "affin.h"
#include <iosfwd>
#include <map>
#include <string>
#include <vector>

bool animationAktiv(); // Haken "Animation" in den Render-Einstellungen (Zeitleiste sichtbar)
void animationAktivSetzen(bool an);

int snapshotAnzahl();
double snapshotZeit(int i);              // Sekunden; Snapshots sind nach Zeit sortiert
int snapshotAufnehmen(double zeit);      // aktuelle Szene festhalten; liefert den Index
void snapshotErsetzen(int i);            // Snapshot mit der aktuellen Szene ueberschreiben
void snapshotLoeschen(int i);
int snapshotZeitSetzen(int i, double z); // liefert den neuen Index (neu sortiert)
std::string snapshotLabel(int i);        // Beschriftung (UTF-8), leer = keine
void snapshotLabelSetzen(int i, const std::string &label);

// ---------- Wiederholungen ----------
// Gilt fuer den Abschnitt vom Snapshot bis zum naechsten und wird ueber die Snapshot-Bewegung gelegt.
// Die Periode wird so angepasst, dass im Abschnitt eine ganze Anzahl Umdrehungen bzw. Schwingungen
// liegt: am Ende steht das Objekt wieder genau wie im naechsten Snapshot (kein Sprung).
class object;
enum WiederholungsArt
{
    WIEDERHOLUNG_KEINE = 0,
    WIEDERHOLUNG_DREHEN = 1,   // volle Umdrehungen um die Achse durch die Objektmitte
    WIEDERHOLUNG_PENDELN = 2,  // hin und her um die Achse, Ausschlag 'weite' Grad
    WIEDERHOLUNG_WIPPEN = 3,   // auf und ab entlang der Achse, Weg 'weite' Einheiten
    WIEDERHOLUNG_ABROLLEN = 4, // Drehung passend zur Strecke im Abschnitt (Rad auf dem Boden)
};
// Auch fuer Teile in Gruppen (Gelenkkette einer Figur): erst wird das Elternteil bewegt, dann das Kind
// um sein mitbewegtes Gelenk.
struct Wiederholung
{
    object *o = nullptr;
    int art = WIEDERHOLUNG_KEINE;
    int achse = 2;      // 0 X, 1 Y, 2 Z, 3 frei (achsVektor)
    bool lokal = false; // Achse des Objekts (mit seiner Drehung) statt der Weltachse
    double periode = 1; // Sekunden je Umdrehung bzw. Schwingung (gewuenscht)
    double weite = 30;  // Pendeln: Grad; Wippen: Einheiten (Ausschlag um den Mittelwert)
    int richtung = 1;   // +1 oder -1 (Drehsinn bzw. Startrichtung)
    double phase = 0;   // Grad: Versatz im Takt (z.B. linkes und rechtes Bein 180 Grad)
    double mittel = 0;  // Pendeln: Grundwinkel in Grad (Knie 50 = immer gebeugt); Wippen: Grundversatz
    bool mitGelenk = false; // um 'gelenk' statt um die Objektmitte
    Vector3D gelenk;        // Gelenkpunkt in den Koordinaten des Snapshots am Abschnittsbeginn
    Vector3D achsVektor = Vector3D(0, 0, 1); // fuer achse 3 (z.B. Gelenkachse einer gedrehten Figur)
};
std::vector<Wiederholung> wiederholungen(int snapshot); // des Abschnitts snapshot -> snapshot + 1
bool wiederholungVon(int snapshot, const object *o, Wiederholung &w);
void wiederholungSetzen(int snapshot, const Wiederholung &w); // ersetzt die des Objekts; art 0 = entfernen
void wiederholungenEntfernen(int snapshot);                   // alle im Abschnitt
// Anzahl Umdrehungen/Schwingungen im Abschnitt (0: keine Wirkung, z.B. Abrollen ohne Strecke)
// und die tatsaechliche Periode; bei Abrollen auch der erkannte Radius
int wiederholungZyklen(int snapshot, const Wiederholung &w, double *periode = nullptr, double *radius = nullptr);
// Punkt in der Szene (wie gerade angezeigt) -> Gelenkpunkt fuer o im Abschnitt (Snapshot-Koordinaten).
// Die aktuelle Zeit muss im Abschnitt liegen; false sonst.
bool gelenkAusWelt(int snapshot, const object *o, const Vector3D &welt, Vector3D &gelenk);
// Zustand von o (aktuell, um 'versatz' verschoben) in den Snapshot eintragen bzw. dort ersetzen
void snapshotObjektEintragen(int snapshot, object *o, const Vector3D &versatz);

// ---------- Ablaeufe (wiederverwendbare Bewegungen, z.B. "ab und zu rennen") ----------
// Eine eigene Ebene ueber den Snapshots: ein Ablauf-Einsatz bewegt ein Objekt (meist die Gruppe einer Figur oder
// eines Tiers) in einer Zeitspanne - Glieder pendeln um ihre Gelenke, das Ganze laeuft vorwaerts - mit weichem
// Ein- und Auslaufen. Die Fortbewegung bleibt nach dem Ende erhalten. Einsaetze koennen sich als Muster
// wiederholen (alle N s bis zur Zeit 'bis'). Snapshots werden dafuer nicht geteilt.
// Lagen und Achsen stehen im Rahmen des Objekts: vorne, oben, quer; Gelenke relativ zur Mitte des Objekts.
struct AblaufGlied
{
    std::string pfad;               // Namen der Teile ab dem Objekt, "/" getrennt (leer = das Objekt selbst)
    int art = WIEDERHOLUNG_PENDELN; // Pendeln, Drehen, Wippen oder Abrollen (Rad: weite = Radius, rollt mit der Bahn)
    bool mitGelenk = true;          // um 'gelenk' statt um die Mitte des Teils
    Vector3D gelenk;                // im Rahmen, relativ zur Mitte des Objekts
    Vector3D achse = Vector3D(0, 0, 1); // im Rahmen
    double periode = 1, weite = 0, mittel = 0, phase = 0; // wie bei Wiederholung (Periode bei Tempo 1)
    int richtung = 1;
};
// Schluesselpose: Lage der Teile zur Zeit 'zeit' (Sekunden ab Beginn, bezogen auf 'dauer' der Vorlage).
// Je Teil (Namenspfad, "" = das ganze Objekt) eine Abbildung im Rahmen, bezogen auf die Ruhelage und auf das
// Elternteil: das ganze Objekt bewegt sich auf seiner Bahn, ein Bein dreht um die Huefte usw. Zwischen zwei Posen
// wird auf einer Schraubbahn gemischt (Drehung um eine feste Achse + Schub): ein Auto faehrt einen Kreisbogen.
struct AblaufPose
{
    double zeit = 0;
    std::vector<std::pair<std::string, Affin>> teile;
};
struct AblaufVorlage
{
    std::string name;
    std::vector<AblaufGlied> glieder;
    Vector3D geschwindigkeit; // Fortbewegung im Rahmen (Einheiten/s bei Tempo 1)
    double dauer = 3;         // Vorgabe fuer neue Einsaetze
    std::vector<AblaufPose> posen; // Schluesselposen (leer: nur Glieder und Fortbewegung)
    double einlauf = -1;      // weiches Anfahren/Anhalten in s; < 0 = Vorgabe (0,35 s; mit Posen 25 % der Dauer, hoechstens 0,6 s)
};
struct AblaufEinsatz
{
    object *o = nullptr;
    AblaufVorlage v;
    Vector3D vorne = Vector3D(1, 0, 0), oben = Vector3D(0, 1, 0), quer = Vector3D(0, 0, 1); // Rahmen (Welt)
    double start = 0, dauer = 3, tempo = 1;
    double alle = 0, bis = 0; // Muster: alle 'alle' Sekunden neu beginnen, bis 'bis' (0 = nur einmal)
};
enum AblaufBauart
{
    ABLAUF_RENNEN = 1,
    ABLAUF_GEHEN,
    ABLAUF_WINKEN,
    ABLAUF_TIER_GEHEN,
    ABLAUF_SCHWANZ,
    ABLAUF_KATZE_SPRINGEN, // Schluesselposen: ducken, abspringen, fliegen, landen
    ABLAUF_FIGUR_SETZEN,   // hinsetzen (rueckwaerts = aufstehen)
    ABLAUF_TUER,           // Tuer oeffnen (Scharnier an der Kante; rueckwaerts = schliessen)
    ABLAUF_SCHRANKE,       // Schranke oeffnen (Schlagbaum hebt sich; rueckwaerts = schliessen)
};
const int ABLAUF_BAUARTEN_ANZAHL = 9;
// Vorlage rueckwaerts: Posen in umgekehrter Reihenfolge, bezogen auf die Endlage (z.B. oeffnen -> schliessen,
// hinsetzen -> aufstehen); Fortbewegung umgekehrt
AblaufVorlage ablaufRueckwaerts(const AblaufVorlage &v);
// Schluesselposen aus den Snapshots von bis (einschliesslich): Lage von 'oben' und allen Teilen, bezogen auf den
// ersten; Raeder (Teile, deren Name mit "Rad" beginnt) rollen mit der Bahn
bool ablaufAusPosen(int von, int bis, object *oben, AblaufVorlage &v, std::string &fehler);
std::vector<AblaufEinsatz> &ablaufEinsaetze(); // alle Einsaetze der Szene (gespeichert mit der Animation)
std::vector<AblaufVorlage> &ablaufVorlagen();  // gespeicherte Ablaeufe (mit der Szene gespeichert)
// fertige Bewegung fuer eine Figur bzw. ein Tier aus "+ Neu" (Teile werden am Namen erkannt); false + fehler sonst
bool ablaufBauen(object *o, int bauart, AblaufEinsatz &e, std::string &fehler);
// Einsatz einer gespeicherten Vorlage fuer o (Rahmen aus dem Objekt)
bool ablaufAusVorlage(object *o, const AblaufVorlage &v, AblaufEinsatz &e);
// Wiederholungen eines Abschnitts (Teile von 'oben') als Vorlage; Fortbewegung aus den beiden Snapshots
bool ablaufAusAbschnitt(int abschnitt, object *oben, AblaufVorlage &v, std::string &fehler);
// Einsatz eintragen; legt fehlende Snapshots an (mindestens zwei, bis zum Ende des Musters) und nimmt o in alle auf
void ablaufHinzufuegen(const AblaufEinsatz &e);
std::vector<std::pair<double, double>> ablaufZeiten(const AblaufEinsatz &e); // alle Vorkommen (Beginn, Ende)
object *obersteGruppe(object *o);
bool ablaufGueltig(const AblaufEinsatz &e); // gibt es das Objekt noch? // Objekt der Szene, das o enthaelt (o selbst, wenn es oben liegt)

// ---------- Wiederholungen kopieren / einfuegen (Zeitleiste) ----------
// nur: wenn nicht leer, nur Wiederholungen dieser Objekte und ihrer Teile; liefert die Anzahl
int wiederholungenKopieren(int abschnitt, const std::vector<object *> &nur);
// ziel: Objekt der Szene (z.B. eine andere Figur gleichen Aufbaus; Teile ueber ihre Namen), nullptr = dieselben Objekte
int wiederholungenEinfuegen(int abschnitt, object *ziel);
int wiederholungenInAblage();

double animationsBeginn(); // Zeit des ersten Snapshots (0 ohne Snapshots)
double animationsEnde();   // Zeit des letzten Snapshots
double aktuelleZeit();

// Szene auf den Zustand zum Zeitpunkt 'zeit' setzen (nicht im Rueckgaengig-Verlauf).
// interaktiv: ueber gSzeneAendern (haelt ein laufendes Raytracing an); sonst nur unter sceneMutex
// (fuer das Rendern der Animation, das selbst Bild fuer Bild rendert).
void zeitpunktAnwenden(double zeit, bool interaktiv = true);

int bilderProSekunde(); // Vorgabe 24
void bilderProSekundeSetzen(int fps);
int anzahlBilder(); // Bilder fuer die ganze Animation (erstes bis letztes Snapshot, beide eingeschlossen)

void animationLeeren(); // nach Laden/Leeren der Szene (die Objekte gibt es nicht mehr)

// ---------- Speichern/Laden in der XML ----------
// <animation> nach den Objekten schreiben (nichts ohne Snapshots und ohne "Animation")
void animationSpeichern(std::ostream &out);

// Element der XML unabhaengig vom Parser (SceneLoader wandelt seine Knoten um)
struct AnimationsKnoten
{
    std::string name;
    std::map<std::string, std::string> attr;
    std::vector<AnimationsKnoten> kinder;
};
// nach dem Laden von Objekten, Lichtern und Kamera; ersetzt alle Snapshots
void animationLaden(const AnimationsKnoten &k);
