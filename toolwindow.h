#pragma once

#include <windows.h>
#include <commctrl.h>
#include <atomic>
#include <functional>
#include <string>
#include <vector>

#include "object.h"
#include "scene.h"

#pragma comment(lib, "comctl32")

extern HWND hHauptfenster; // Rahmen um Leiste, Ansichten, Objekt-, Zeit- und Statusleiste
extern HWND hToolWindow;   // Werkzeugleiste ueber den Ansichten
extern HWND hObjektFenster; // einklappbares Objektfenster oben rechts
extern HWND hTreeObjects;  // Objektliste im Objektfenster

extern object* selectedObject;
extern light* selectedLight;

extern int selectionIndex;

extern std::function<void(WPARAM)> gHandleKeys;

// Fuehrt eine Aenderung an scene/lights sicher aus: haelt das Raytracing an, sperrt die Szene
// und aktualisiert die Ansichten (in main.cpp gesetzt, kennt die Ansichten)
extern std::function<void(const std::function<void()> &)> gSzeneAendern;
// Autom. Rendern (main.cpp setzt sie): Bereich x0..x1, y0..y1 (Anteile 0..1 der Bildbreite/-hoehe) rendern (ganz:
// ganzes Bild); false, wenn es gerade nicht geht (Rendern laeuft, Animation). Abbrechen: unfertiges autom. Rendern
// anhalten und das vorige Bild wieder zeigen; true, wenn eines unfertig war.
extern std::function<bool(double, double, double, double, bool)> gAutoRendernStarten;
extern std::function<bool()> gAutoRendernAbbrechen;
// Szene aus einer Datei laden (Menue "Zuletzt geoeffnet"; fragt bei ungespeicherten Aenderungen)
extern std::function<void(const std::string &)> gSzeneOeffnen;

void CreateToolWindow(HINSTANCE hInst);

// Rendering-Fenster, das der Button "Fenster" in der Leiste ein- und ausblendet
void RenderfensterSetzen(HWND fenster);

// Bettet die vier Ansichten ins Hauptfenster ein (Anordnung wie bisher auf dem Bildschirm)
// und zeigt das Hauptfenster maximiert
void WerkzeugleisteAndocken(const std::vector<HWND> &ansichten);
void UpdateObjectList();

// Fragt nach und loescht das ausgewaehlte Objekt bzw. Licht (Button, Entf-Taste)
void AusgewaehltesObjektLoeschen();

// Ein Schritt fuer die Auswahl entlang der Weltachse (0=X, 1=Y, 2=Z), Richtung +1/-1:
// je nach Modus verschieben, drehen oder skalieren, mit der Schrittweite aus dem Objektfenster
// (Buttons -X..+Z und Ziffernblock). Lichter werden nur verschoben.
void SchrittAusfuehren(int achse, int vorzeichen);

// Bearbeiten (Tasten in main.cpp, Buttons im Werkzeugfenster); alle mit Rueckgaengig
void AuswahlDuplizieren();      // Strg+D, Button "Kopie"
void AuswahlInstanzErstellen(); // Strg+Umschalt+D: verknuepfte Kopie (Instanz)
void AuswahlInstanzLoesen();    // Instanz -> eigenstaendige Kopie
void AuswahlKopieren();         // Strg+C
void ZwischenablageEinfuegen(); // Strg+V
void AllesAuswaehlen();         // Strg+A
void AuswahlGruppieren();       // Strg+G: ausgewaehlte Objekte zu einer Gruppe
void GruppeAufheben();          // Strg+Umschalt+G
void BaumAufteilen();           // Baum -> Gruppe aus Stamm und Blaettern (Kontextmenue der Liste)
void Rueckgaengig();            // Strg+Z
void Wiederholen();             // Strg+Y / Strg+Umschalt+Z

// laufende Render-Starts (main.cpp: Y erhoeht, nach startRendering wieder weniger); fuer "Stop"
extern std::atomic<int> gRenderLaeufe;
// Fortschritt des laufenden Bildes in Prozent (threading.cpp; -1 = kein Rendering)
extern std::atomic<int> gRenderProzent;

// Fenster "Render-Einstellungen" ein-/ausblenden (Button in der Leiste)
void EinstellungenZeigen();

// Umgebungsbild: zuletzt geladene Datei (leer: beim Start aus initScene); fuer Speichern/Laden
const std::string &UmgebungsbildDatei();
void UmgebungsbildGeladen(const std::string &datei);

// zuletzt geoeffnete bzw. gespeicherte Szene (raytracer_editor.ini): beim Start anbieten
void LetzteSzeneMerken(const std::string &datei);
const std::string &LetzteSzene();

// ---------- Animation ----------
// Rendern aller Bilder (main.cpp setzt es; Argument: Pfad ohne ".bmp", es entstehen basis_0001.bmp ...)
extern std::function<void(const std::wstring &)> gAnimationRendern;
// Fortschritt fuer die Statusleiste (Bilder = 0: keine Animation in Arbeit)
extern std::atomic<int> gAnimationBild, gAnimationBilder; // Bild -1: Video wird erstellt
// Beginn des Renderns (GetTickCount64) und erstes dabei gerechnete Bild: fuer die Restzeit in der Statusleiste
extern std::atomic<unsigned long long> gAnimationStartMs;
extern std::atomic<int> gAnimationAbBild;
extern bool gAnimationVideo; // nach dem Rendern mit ffmpeg ein MP4 erstellen
// f auf dem Hauptthread ausfuehren und warten (aus dem Render-Thread der Animation)
void AufHauptthread(const std::function<void()> &f);

// kleiner Knopf unten rechts in einer Ansicht ("alles ins Bild"); aktion laeuft auf dem Hauptthread
void EinpassKnopfAnlegen(HWND ansicht, std::function<void()> aktion, const wchar_t *tipp);
// Massstab (Balken mit runder Laenge) links neben dem Einpass-Knopf; einheitenProPixel: 0 = unbekannt
void MassstabAnlegen(HWND ansicht, std::function<double()> einheitenProPixel, const wchar_t *tipp);

// Hauptfenster: Reiter "Rendering" (Render-Bild an Stelle der Ansichten) bzw. "Ansichten"
void RenderReiterZeigen(bool rendering);
// Kamerasicht (Perspektive): Render-Bild darin zeigen (Knopf/Taste B), auf einen anderen Bildschirm (Menue "Datei")
class Fenster;
void KamerasichtAnmelden(HWND kamerasicht, Fenster *render);
void KamerasichtBildUmschalten();
// beim Start des Renderns: true = Bild erscheint in der Kamerasicht (Ansichten bleiben), false = Reiter "Rendering"
bool RenderStartInKamerasicht();
// Kanten-Modus: Tasten (Entf, K, U, Esc) - true = erledigt; Rechtsklick in einer Ansicht zeigt das Kanten-Menue
bool KantenTaste(WPARAM taste);
bool PinselTaste(WPARAM taste); // Esc beendet den Terrain-Pinsel
bool AuswahlEbeneHoch();        // Esc: von Teilen einer Gruppe zur Gruppe
void KantenMenue(HWND fenster, int x, int y);
// Rechtsklick in einer Ansicht: Kanten-Modus -> Kanten-Menue, sonst Objekt-Menue (wie in der Liste).
// kette: getroffenes Objekt der Szene bis zum innersten Teil (Wireframe::objektKetteUnter)
void AnsichtMenue(HWND fenster, int x, int y, const std::vector<class object *> &kette);
bool RenderReiterAktiv();
// kleine Beschriftung oben links in einer Ansicht ("Top", "Front", ...)
void AnsichtBeschriftungAnlegen(HWND ansicht, const wchar_t *text);
