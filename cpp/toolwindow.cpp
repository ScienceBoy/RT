#include "toolwindow.h"

#include "wireframe.h"
#include "camera.h"
#include "mesh.h"
#include "tree.h"
#include "kugel.h"
#include "pfeil.h"
#include "stab.h"
#include "text3d.h"
#include "pointlight.h"
#include "spotlight.h"
#include "sphereLight.h"
#include "directionalLight.h"
#include "areaLight.h"
#include "meshLight.h"
#include "lichtSymbol.h"
#include "SceneLoader.h"
#include "material_library.h"
#include "TextureManager.h"
#include "mesh_factory.h"
#include "gpu_ansichten.h"
#include "drehung.h"
#include <deque>
#include <array>
#include <random>
#include "bearbeitung.h"
#include "gruppe.h"
#include "instanz.h"
#include "textur_abbildung.h"
#include "fell.h"
#include "stabil.h"
#include "terrain_bearbeiten.h"
#include "material_pinsel.h"
#include "regen.h"
#include "fortschritt.h"
#include "animation.h"
#include "kamera_bahn.h"
#include "render_einstellungen.h"
#include "formen.h"
#include "netz_operationen.h"
#include "rotationskoerper.h"
#include "stein.h"
#include "csg.h"
#include "koerper.h"
#include "kanten.h"
#include "volumenbox.h"
#include "drehpunkt.h"
#include <map>
#include <cstring>
#include <mutex>

extern std::mutex sceneMutex; // wireframe.cpp

#include <commctrl.h>
#include <commdlg.h>
#include <windowsx.h>
#include <cmath>
#include <uxtheme.h>
#include <algorithm>
#include <cwchar>
#include <cstdio>
#include <iostream>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#pragma comment(lib, "Comctl32.lib")

// Zwei Fenster:
//  - Werkzeugleiste (hToolWindow): eine Zeile ueber den vier Ansichten
//  - Objektfenster (hObjektFenster): einklappbar oben rechts, Objektliste und Eigenschaften
// Alle Windows-Aufrufe mit Text verwenden ausdruecklich die ...W-Varianten,
// weil UNICODE in dieser Datei nicht definiert sein muss.

HWND hToolWindow = nullptr;
HWND hObjektFenster = nullptr;
HWND hTreeObjects = nullptr;
void SchrittAusfuehren(int achse, int vorzeichen);

// Objektliste: Pfeiltasten und Bild auf/ab verschieben, drehen bzw. skalieren die Auswahl (je nach Modus
// Move/Rotate/Scale, Schrittweite wie die Knoepfe -X..+Z) statt die Liste auf- und zuzuklappen:
//   links/rechts = X, auf/ab = Z (auf = -Z, nach hinten), Bild auf/ab = Y (hoch/runter)
static LRESULT CALLBACK ListeTastenProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR)
{
    if (msg == WM_KEYDOWN && !(GetKeyState(VK_CONTROL) & 0x8000) && !(GetKeyState(VK_MENU) & 0x8000))
    {
        int achse = -1, vz = 0;
        switch (wParam)
        {
        case VK_LEFT: achse = 0, vz = -1; break;
        case VK_RIGHT: achse = 0, vz = +1; break;
        case VK_UP: achse = 2, vz = -1; break;
        case VK_DOWN: achse = 2, vz = +1; break;
        case VK_PRIOR: achse = 1, vz = +1; break;
        case VK_NEXT: achse = 1, vz = -1; break;
        default: break;
        }
        if (achse >= 0)
        {
            SchrittAusfuehren(achse, vz);
            return 0; // Liste nicht auf-/zuklappen und nicht weiterblaettern
        }
    }
    if (msg == WM_GETDLGCODE)
        return DefSubclassProc(hwnd, msg, wParam, lParam) | DLGC_WANTARROWS;
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

std::function<void(WPARAM)> gHandleKeys;
std::function<void(const std::function<void()> &)> gSzeneAendern;
std::function<bool(double, double, double, double, bool)> gAutoRendernStarten;
std::function<bool()> gAutoRendernAbbrechen;
std::function<void(const std::string &)> gSzeneOeffnen;

HFONT gFont = nullptr;

//--------------------------------------------------
// IDs
//--------------------------------------------------

#define ID_LIST_OBJECTS 2000

#define ID_BTN_IMPORT 1001
#define ID_BTN_RENDER 1002
#define ID_BTN_STOP 1003
#define ID_BTN_SAVE 1004

#define ID_BTN_MOVE 1005
#define ID_BTN_ROTATE 1006
#define ID_BTN_SCALE 1007
#define ID_BTN_KANTEN 1096 // Kanten-Modus

#define ID_BTN_CLEAR 1008
#define ID_BTN_QUIT 1009

#define ID_BTN_NEXT 1010
#define ID_BTN_LOESCHEN 1011
#define ID_BTN_KOPF 1012      // Button in der Leiste: Objektfenster ein-/ausblenden
#define ID_BTN_ZUKLAPPEN 1013 // Kopfzeile des Objektfensters: ausblenden
#define ID_BTN_FARBE 1014     // Farbfeld: zeigt die Farbe, Klick oeffnet den Farbdialog
#define ID_BTN_NEU 1015       // Neues Grundobjekt / Licht einfuegen (Menue)
#define ID_BTN_LADEN 1016     // Szene aus XML laden
#define ID_BTN_EXPORT 1017    // ganze Szene als OBJ exportieren
#define ID_BTN_RENDERFENSTER 1018 // Rendering-Fenster ein-/ausblenden
#define ID_BTN_FLAT 1019          // Flaechen / Drahtgitter umschalten (F)
#define ID_BTN_ZENTRIEREN 1020    // Kamera und Ansichten auf die Szenenmitte (Z)
#define ID_BTN_ZOOM_PLUS 1021     // Ortho-Ansichten vergroessern (+)
#define ID_BTN_ZOOM_MINUS 1022    // Ortho-Ansichten verkleinern (-)
#define ID_BTN_SCHRITT_ERSTES 1030 // 1030..1035: -X +X -Y +Y -Z +Z
#define ID_EDIT_SCHRITT 3020      // Schrittweite fuer die Buttons -X..+Z
#define ID_BTN_UNDO 1023          // Rueckgaengig (Strg+Z)
#define ID_BTN_REDO 1024          // Wiederholen (Strg+Y)
#define ID_BTN_FOKUS 1025         // Ansichten auf die Auswahl (.)
#define ID_BTN_KOPIE 1026         // Auswahl duplizieren (Strg+D)
#define ID_BTN_REITER_EIGENSCHAFTEN 1040 // Reiter im Objektfenster
#define ID_BTN_REITER_MATERIAL 1041
#define ID_BTN_REITER_MANIPULATION 1062 // dritter Reiter: Netz-Operationen
#define ID_CMB_MANIP_ART 1063
#define ID_CMB_MANIP_OBJEKT 1064
#define ID_EDIT_MANIP_TIEFE 1065
#define ID_BTN_MANIP_AUSFUEHREN 1066
#define ID_EDIT_MANIP_KNICK 1067
#define ID_EDIT_MANIP_WERT 1068 // Dicke bzw. Hoehe (Einheiten)
#define ID_EDIT_MANIP_AUS_X 1069 // Zufaellige Verteilung: Ausdehnung X, Y, Z
#define ID_EDIT_MANIP_AUS_Y 1070
#define ID_EDIT_MANIP_AUS_Z 1071
#define ID_CMB_MANIP_MODUS 1075 // Subtrahieren: Oeffnung / Form
#define ID_CMB_MANIP_KOPIEN 1076 // Vervielfaeltigen: echte Kopien / Instanzen
#define ID_CMB_MANIP_LAGE 1077   // Absetzen: Ausrichtung behalten / stabil hinlegen
#define ID_CMB_MANIP_PINSEL 1078 // Terrain-Pinsel: Anheben / Absenken / Glaetten / Ebnen
#define ID_BTN_MANIP_ICON 1100  // Symbole der Aktionen: 1100 + Nummer
#define ID_BTN_MATFARBE_ERSTES 1042  // 1042..1046 Farbfelder: Umgebung, Diffus, Glanz, Leuchten, Absorption
#define ID_CMB_VORLAGE 3040          // Materialvorlage
#define ID_EDIT_MATFARBE_ERSTES 3041 // 3041..3055: 5 Farben x 3
#define ID_EDIT_MATWERT_ERSTES 3056  // 3056..3063: 8 Werte
#define ID_EDIT_TEXTUR 3064
#define ID_CMB_TEXTURMODUS 3065
// weitere Textur-Einstellungen (Material-Reiter)
#define ID_EDIT_TEXTUR_KACHEL 3066
#define ID_EDIT_TEXTUR_WINKEL 3067
#define ID_EDIT_TEXTUR_VERSATZ_U 3068
#define ID_EDIT_TEXTUR_VERSATZ_V 3069
#define ID_EDIT_TEXTUR_KUGEL_X 3070 // 3070..3072 Drehung der Kugel-Abbildung
#define ID_EDIT_TEXTUR_BILD 3073
#define ID_EDIT_TEXTUR_STAERKE 3074
#define ID_EDIT_TEXTUR_LETZTES 3074
#define ID_CMB_TEXTUR_BILD 3075
#define ID_EDIT_TEXTUR_RINGE 3080 // Regenringe (Wasser)
#define ID_EDIT_TEXTUR_NASS 3081  // nasse Oberflaeche
#define ID_BTN_TEXTUR_FEST 1058  // Textur haftet am Objekt
#define ID_BTN_TEXTUR_BILD 1059  // weiteres Bild: Datei waehlen
#define ID_BTN_TEXTUR_DATEI 1051 // Material-Reiter: Bilddatei als Textur
#define ID_BTN_TEXTUR_SPIEGEL_U 1052
#define ID_BTN_TEXTUR_SPIEGEL_V 1053
#define ID_BTN_TEXTUR_DREHEN 1054
#define ID_BTN_EINSTELLUNGEN 1050 // Leiste: Fenster "Render-Einstellungen"
#define ID_BTN_AUTO_RENDER 1190 // Leiste: Schalter "Autom. Rendern"
#define ID_BTN_DATEI 1056         // Leiste: Menue "Datei" (Szene/Objekt importieren, exportieren)
#define ID_CMB_BLOCK 1057         // Leiste: Blockgroesse fuer das Schnell-Rendering
#define ID_EIN_ERSTES 4000         // Steuerelemente im Einstellungsfenster (4000..4199)
constexpr int MAT_FARBEN = 5;
constexpr int MAT_WERTE = 8;

#define ID_EDIT_ERSTES 3000 // 3000..3002 Position, 3003..3005 Farbe/LookAt, 3006..3008 Drehung
#define ANZAHL_EDITS 18 // 3009..3011 Groesse, 3012..3014 unterster, 3015..3017 oberster Punkt
#define ID_EDIT_NAME 3030 // (vorher 3010: lag jetzt im Bereich der Eingabefelder)

#define WM_LISTE_AKTUALISIEREN (WM_APP + 1)
#define WM_EINGABE_FERTIG (WM_APP + 2)
#define WM_HAUPTTHREAD (WM_APP + 3) // lParam: const std::function<void()>* (AufHauptthread)
#define WM_SICHTBAR_PRUEFEN (WM_APP + 4) // lParam: HTREEITEM, dessen Haken (sichtbar) sich geaendert hat
#define ID_BTN_ZIEL 1055 // Kamera/Richtungslicht auf ein Objekt ausrichten (kleiner Knopf rechts der Zeile)

#define TIMER_ABGLEICH 1

// lParam der Listeneintraege
constexpr LPARAM EINTRAG_GRUPPE = -1;
constexpr LPARAM EINTRAG_LICHT = 1000000;
constexpr LPARAM EINTRAG_KAMERA = 2000000;
constexpr LPARAM EINTRAG_KIND = 3000000; // Objekte in Gruppen

//--------------------------------------------------
// Layout und Farben
//--------------------------------------------------

constexpr int MARGIN = 8;
constexpr int BTN_H = 30;
constexpr int LABEL_H = 20;
constexpr int ZEILE_H = 26;
constexpr int GAP = 6;
constexpr int ABSCHNITT = 12;
constexpr int GRUPPE = 22; // Abstand zwischen Gruppen der Leiste (mit Trennlinie)
constexpr int ZEILEN_LABEL_W = 66; // "Drehung" ganz sichtbar

constexpr int LEISTE_H = 50; // eine Zeile; rechts der quadratische Objekte-Button (50 x 50)

constexpr int PANEL_W = 340;  // breiter: Beschriftungen im Material-Reiter und "Kopie"/"Delete" passen
constexpr int MAT_LABEL_W = 84; // Beschriftungsspalte im Material-Reiter ("Absorption", "Umgebung")
constexpr int KOPF_H = 30;       // Kopfzeile des Objektfensters
constexpr int PANEL_MIN_H = 854; // + Zeilen "Unterster"/"Oberster"

const COLORREF FARBE_HINTERGRUND = RGB(37, 37, 38);
const COLORREF FARBE_FLAECHE = RGB(28, 28, 28);
const COLORREF FARBE_KOPF = RGB(45, 45, 48);
const COLORREF FARBE_BUTTON = RGB(51, 51, 55);
const COLORREF FARBE_BUTTON_HOVER = RGB(66, 66, 72);
const COLORREF FARBE_BUTTON_GEDRUECKT = RGB(80, 80, 88);
const COLORREF FARBE_AKTIV = RGB(0, 110, 190);
const COLORREF FARBE_AKTIV_HOVER = RGB(20, 128, 210);
const COLORREF FARBE_GEFAHR = RGB(232, 96, 96);
const COLORREF FARBE_TEXT = RGB(230, 230, 230);
const COLORREF FARBE_TEXT_GEDIMMT = RGB(120, 120, 120);
const COLORREF FARBE_UEBERSCHRIFT = RGB(150, 150, 150);
const COLORREF FARBE_LINIEN = RGB(70, 70, 74);
const COLORREF FARBE_BAUMLINIEN = RGB(90, 90, 90);

static HBRUSH pinselHintergrund = nullptr;
static HBRUSH pinselFlaeche = nullptr;

static HFONT symbolFont = nullptr; // Segoe Fluent Icons / Segoe MDL2 Assets
static HWND hoverButton = nullptr; // Button unter der Maus

// Symbol (Zeichen aus der Windows-Symbolschrift) je Button
static const std::unordered_map<int, wchar_t> buttonSymbole =
    {
        {ID_BTN_IMPORT, 0xE8E5},   // OpenFile
        {ID_BTN_SAVE, 0xE74E},     // Save
        {ID_BTN_LADEN, 0xE838},    // FolderOpen
        {ID_BTN_EXPORT, 0xEDE1},   // Export
        {ID_BTN_RENDERFENSTER, 0xE8A7}, // OpenInNewWindow
        {ID_BTN_FLAT, 0xE890},          // View
        {ID_BTN_ZENTRIEREN, 0xE8E3},    // AlignCenter
        {ID_BTN_ZOOM_PLUS, 0xE8A3},     // ZoomIn
        {ID_BTN_ZOOM_MINUS, 0xE71F},    // ZoomOut
        {ID_BTN_UNDO, 0xE7A7},          // Undo
        {ID_BTN_REDO, 0xE7A6},          // Redo
        {ID_BTN_FOKUS, 0xE71E},         // Zoom
        {ID_BTN_KOPIE, 0xE8C8},         // Copy
        {ID_BTN_EINSTELLUNGEN, 0xE713}, // Setting
        {ID_BTN_DATEI, 0xE8B7},         // Folder
        {ID_BTN_RENDER, 0xE768},   // Play
        {ID_BTN_STOP, 0xE71A},     // Stop
        {ID_BTN_AUTO_RENDER, 0xE895}, // Sync
        {ID_BTN_NEXT, 0xE76C},     // ChevronRight
        {ID_BTN_LOESCHEN, 0xE74D}, // Delete
        {ID_BTN_NEU, 0xE710},      // Add
        {ID_BTN_MOVE, 0xE7C2},     // Move
        {ID_BTN_ROTATE, 0xE7AD},   // Rotate
        {ID_BTN_SCALE, 0xE740},    // FullScreen
        {ID_BTN_CLEAR, 0xE74D},    // Delete
        {ID_BTN_QUIT, 0xE7E8},     // PowerButton
};

//--------------------------------------------------
// Steuerelemente und Zustand
//--------------------------------------------------

struct Leiste
{
    HWND btnImport, btnExport, btnLaden, btnSave, btnRender, btnStop, btnRenderfenster;
    HWND btnAutoRender = nullptr; // Schalter "Autom. Rendern"
    HWND btnFlat, btnZentrieren, btnFokus, btnZoomPlus, btnZoomMinus;
    HWND btnUndo, btnRedo;
    HWND btnEinstellungen;
    HWND btnDatei, cmbBlock; // Menue "Datei", Blockgroesse Schnell-Rendering
    HWND btnClear, btnQuit;
    HWND btnObjekte; // rechts aussen: Objektfenster ein-/ausblenden
};

struct Objektfenster
{
    HWND btnKopf, btnNeu, btnKopie, btnLoeschen, btnNext, btnFarbe;
    HWND btnMove, btnRotate, btnScale, btnKanten;
    HWND btnSchritt[6]; // -X +X -Y +Y -Z +Z
    HWND lblSchritt, editSchritt, lblEinheit;

    // Reiter "Eigenschaften" | "Material"
    HWND btnReiter[3];
    // Reiter "Manipulation"
    HWND lblManipArt, cmbManipArt, lblManipObjekt, cmbManipObjekt, lblManipTiefe, editManipTiefe, btnManipAusfuehren, lblManipInfo;
    HWND lblManipKnick, editManipKnick; // Glatt schattieren: Knickwinkel
    HWND lblManipWert, editManipWert;   // Dicke / Faechern: Laenge in Einheiten
    HWND lblManipZiel;                  // zeigt die ausgewaehlten Objekte (statt einer eigenen Auswahlliste)
    HWND lblManipAus, editManipAus[3];  // Zufaellige Verteilung: Ausdehnung in X, Y, Z
    HWND lblManipModus, cmbManipModus;  // Subtrahieren: Art des Ausschneidens
    HWND lblManipKopien, cmbManipKopien; // Vervielfaeltigen: echte Kopien oder Instanzen
    HWND lblManipLage, cmbManipLage;     // Absetzen: Ausrichtung behalten oder stabil hinlegen
    HWND lblManipPinsel, cmbManipPinsel; // Terrain-Pinsel: Art
    HWND btnManipIcon[24];              // Symbole statt Auswahlliste (die Liste bleibt unsichtbar als Speicher)
    HWND lblVorlage, cmbVorlage;
    HWND btnZiel; // "▾": Kamera bzw. Richtungslicht auf ein Objekt richten
    HWND lblMatFarbe[MAT_FARBEN], editMatFarbe[MAT_FARBEN * 3], btnMatFarbe[MAT_FARBEN];
    HWND lblMatWert[MAT_WERTE], editMatWert[MAT_WERTE];
    HWND lblTextur, editTextur, cmbTexturModus, btnTexturDatei, btnTexturU, btnTexturV, btnTexturDrehen;
    // Kachel (m), Winkel, Versatz, haftet am Objekt, Kugel-Drehung, weitere Bilder (Relief, Alpha, Glanz, Leuchten)
    HWND lblTexturKachel, editTexturKachel, lblTexturWinkel, editTexturWinkel;
    HWND lblTexturVersatz, editTexturVersatz[2], btnTexturFest;
    HWND lblTexturKugel, editTexturKugel[3];
    HWND lblTexturBild, cmbTexturBild, editTexturBild, btnTexturBild, editTexturStaerke;
    HWND lblTexturRinge, editTexturRinge, lblTexturNass, editTexturNass; // Regen
    HWND lblEigenschaften, lblName, info, lblDetails, lblZeile1, lblZeile2, lblZeile3, lblZeile4, lblZeile5, lblZeile6; // info = Namensfeld
    HWND edit[ANZAHL_EDITS];
};

static Leiste l = {};
static Objektfenster g = {};

static std::vector<int> trennlinien; // x-Positionen der senkrechten Trennlinien in der Leiste

static bool eingeklappt = false; // true: Objektfenster ausgeblendet
static int objektBreite = 0;      // Breite der rechten Spalte (mit Bildlaufleiste); 0 = Vorgabe; mit dem Teiler ziehbar
static HWND hObjektTeiler = nullptr;
constexpr int TEILER_B = 6;       // Breite des ziehbaren Teilers links vom Objektfenster
static HWND hReiter = nullptr;    // Reiter "Ansichten | Rendering" im Hauptfenster
static int aktiverReiter = 0;     // 0 Ansichten, 1 Rendering
static std::wstring kamerasichtGeraet;             // Bildschirm der abgedockten Kamerasicht (leer: im Hauptfenster)
static std::wstring gespeichertesKamerasichtGeraet;
static int renderFlaecheB = 0, renderFlaecheH = 0; // Flaeche der Ansichten (Groesse des Render-Bildes)
static void bildKnopfSetzen();
static void kamerasichtMenueEintraege(HMENU m);
static bool kamerasichtMenueWahl(int wahl);
constexpr int REITER_H = 30;
static void einbetten(HWND h);
static HWND hObjektRahmen = nullptr;
static LRESULT CALLBACK ObjektTeilerProc(HWND, UINT, WPARAM, LPARAM);
static LRESULT CALLBACK ObjektRahmenProc(HWND, UINT, WPARAM, LPARAM);
static void objektRahmenAnordnen();
static void objektFeldZeigen(HWND feld);
static int objektfensterMindesthoehe();
static LRESULT CALLBACK ReiterProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
static HWND hRenderfenster = nullptr; // Rendering-Fenster (Button "Fenster")
static std::wstring renderTitelAnfang;    // Titel beim Start; danach schreibt der Render-Code seine Infos hinein
static bool renderfensterSichtbar = true; // zuletzt gezeichneter Zustand des Buttons
static bool angezeigtFlat = true;          // Beschriftung des Flat/Draht-Buttons
static int reiter = 0;                     // Objektfenster: 0 Eigenschaften, 1 Material, 2 Manipulation
static bool materialAktiv = false;         // gibt es fuer die Auswahl ein aenderbares Material?
static Farbe angezeigteMatFarben[MAT_FARBEN];
static HWND hStatusleiste = nullptr;       // unter den Ansichten
static std::wstring statusText;            // zuletzt angezeigter Text
static std::vector<object *> angezeigteMehrfachauswahl; // fuer das Neuzeichnen der Liste
constexpr int STATUS_H = 24;

// Schrittweite der Buttons -X..+Z je Modus: Move (Einheiten, 0 = noch aus der Szenengroesse
// bestimmen), Rotate (Grad), Scale (Prozent)
static double schrittWeite[3] = {0.0, 15.0, 10.0};
static int panelHoehe = PANEL_MIN_H;

static bool farbeAktiv = false; // gibt es fuer die Auswahl eine aenderbare Farbe?
static Farbe angezeigteFarbe;
static COLORREF eigeneFarben[16] = {}; // "Benutzerdefinierte Farben" im Farbdialog

static std::vector<HTREEITEM> eintraegeObjekte; // Index wie in 'scene'
// Objekte in Gruppen: lParam = EINTRAG_KIND + Index in diese Liste
static std::vector<std::pair<object *, HTREEITEM>> eintraegeKinder;
static std::vector<HTREEITEM> eintraegeLichter; // Index wie in 'lights'
static HTREEITEM eintragKamera = nullptr;

static size_t bekannteObjektAnzahl = size_t(-1);
static size_t bekannteLichtAnzahl = size_t(-1);

static bool auswahlWirdGesetzt = false; // TVN_SELCHANGED waehrend eigener Auswahl ignorieren

struct Auswahl
{
    SelectedType typ = SelectedType::NONE;
    const void *zeiger = nullptr;

    bool operator!=(const Auswahl &o) const { return typ != o.typ || zeiger != o.zeiger; }
};

static Auswahl angezeigteAuswahl;
static SelectedAction angezeigterModus = SelectedAction::MOVE;

//--------------------------------------------------
// Helper
//--------------------------------------------------

static void schriftSetzen(HWND h)
{
    if (gFont)
        SendMessageW(h, WM_SETFONT, (WPARAM)gFont, TRUE);
}

static WNDPROC alteButtonProc = nullptr;

// Merkt sich den Button unter der Maus fuer den Hover-Effekt
static void abgleichen();
static void blockAuswahlAnzeigen(); // Leiste: Blockgroesse Schnell-Rendering
static void statusAktualisieren();
static void einstellungenAnzeigen();
void EinstellungenZeigen();

// Tasten an den Editor weitergeben (Pfeiltasten, Esc, Entf, F, Ziffernblock ...).
// Nach einem Klick hat der Button bzw. das Fenster den Fokus; die Tasten kamen sonst nie an.
static bool tasteWeitergeben(WPARAM taste)
{
    if (!gHandleKeys)
        return false;
    gHandleKeys(taste);
    abgleichen();
    return true;
}

static LRESULT CALLBACK ButtonProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    // Leertaste loest wie gewohnt den Button aus, alles andere geht an den Editor
    if (msg == WM_KEYDOWN && wParam != VK_SPACE && tasteWeitergeben(wParam))
        return 0;

    if (msg == WM_MOUSEMOVE && hoverButton != hwnd)
    {
        HWND alt = hoverButton;
        hoverButton = hwnd;
        if (alt)
            InvalidateRect(alt, nullptr, FALSE);
        InvalidateRect(hwnd, nullptr, FALSE);

        TRACKMOUSEEVENT tme = {sizeof(tme), TME_LEAVE, hwnd, 0};
        TrackMouseEvent(&tme);
    }
    else if (msg == WM_MOUSELEAVE && hoverButton == hwnd)
    {
        hoverButton = nullptr;
        InvalidateRect(hwnd, nullptr, FALSE);
    }

    return CallWindowProcW(alteButtonProc, hwnd, msg, wParam, lParam);
}

// Buttons zeichnen wir selbst (BS_OWNERDRAW), damit sie zum dunklen Fenster passen
HWND CreateStyledButton(
    HWND parent,
    HINSTANCE hInst,
    int id,
    const wchar_t *text,
    int x,
    int y,
    int w,
    int h)
{
    HWND hBtn =
        CreateWindowW(
            L"BUTTON",
            text,
            WS_VISIBLE | WS_CHILD | BS_OWNERDRAW,
            x,
            y,
            w,
            h,
            parent,
            (HMENU)(INT_PTR)id,
            hInst,
            nullptr);

    schriftSetzen(hBtn);

    WNDPROC alt = (WNDPROC)SetWindowLongPtrW(hBtn, GWLP_WNDPROC, (LONG_PTR)ButtonProc);
    if (!alteButtonProc)
        alteButtonProc = alt;
    return hBtn;
}

HWND CreateLabel(
    HWND parent,
    HINSTANCE hInst,
    const wchar_t *text,
    int x,
    int y,
    int w,
    int h)
{
    HWND hLabel =
        CreateWindowW(
            L"STATIC",
            text,
            WS_VISIBLE | WS_CHILD | SS_NOPREFIX | SS_ENDELLIPSIS,
            x,
            y,
            w,
            h,
            parent,
            nullptr,
            hInst,
            nullptr);

    schriftSetzen(hLabel);
    return hLabel;
}

static WNDPROC alteEditProc = nullptr;

static HWND naechstesFeld(HWND aktuell, bool rueckwaerts);
static void neuMenue();
static int kanal(double wert);
static void reiterZeigen(int neu);
// Autom. Rendern (Schalter neben "Render", weiter unten)
static bool autoRendernAn = false;
static double autoWarten = 3.0; // Sekunden
static void menueLeisteAktualisieren(); // Menueleiste des Hauptfensters (weiter unten)
static void ablaufDialog(HWND fenster, object *o, double start); // Bewegung / Ablauf einsetzen (weiter unten)
static void ablaufMenue(HWND fenster, size_t i, POINT bildschirm); // Rechtsklick auf einen Ablauf-Balken
static bool ablaufVorlageBearbeiten(const std::wstring &titel, AblaufVorlage &v); // Ablauf-Editor
static bool ablaufNameAbfragen(HWND fenster, std::string &name);
static void posenAlsAblauf(HWND fenster, object *o, int von);
static void ablaufMenueLeisteFuellen(HMENU m); // Menue "Animation"
static bool ablaufMenueLeisteBefehl(HWND fenster, int id);
// Material-Pinsel (weiter unten)
struct MaterialAblage
{
    bool da = false;
    Material mat;
    std::shared_ptr<Texture> tex;
    std::string texName, matName;
    TextureMode modus = TextureMode::None;
    std::shared_ptr<const TexturParameter> par;
    bool spiegelU = false, spiegelV = false;
    int drehung = 0;
    std::wstring quelle; // Name fuer Menue und Statuszeile
};
static MaterialAblage materialAblage;
static void materialPinselSetzen(bool an);
static void texturKugelSichtbar(); // Material-Reiter: Zeile "Kugel" (weiter unten)
static void manipObjekteFuellen();
static double passendeGroesse(); // weiter unten (Groesse fuer neue Objekte)
static void manipAnzeigen();
static void manipAusfuehren();
static void manipFell();
static void eigenschaftenAnzeigen();
static void zielMenueZeigen();
static int zeilenY[6] = {}; // y der Eigenschaftszeilen (fuer den Ziel-Knopf)
static int zielKnopfX = 0;
static int zielZeile = -1;  // Zeile, neben der der Ziel-Knopf steht (-1: ausgeblendet)
static void materialFarbeUebernehmen(int zeile);
static void materialWertUebernehmen(int i);
static void texturUebernehmen();
static void schrittAnzeigen();
static void schrittUebernehmen();

// Enter uebernimmt den Wert, Tab/Shift+Tab springt zum naechsten/vorherigen Feld
static LRESULT CALLBACK EditProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_KEYDOWN && wParam == VK_RETURN)
    {
        SendMessageW(GetParent(hwnd), WM_EINGABE_FERTIG, GetDlgCtrlID(hwnd), 0);
        SendMessageW(hwnd, EM_SETSEL, 0, -1);
        return 0;
    }
    if (msg == WM_KEYDOWN && wParam == VK_TAB)
    {
        HWND ziel = naechstesFeld(hwnd, (GetKeyState(VK_SHIFT) & 0x8000) != 0);
        if (ziel)
        {
            objektFeldZeigen(ziel);
            SetFocus(ziel); // Verlassen des Felds uebernimmt den Wert (EN_KILLFOCUS)
            SendMessageW(ziel, EM_SETSEL, 0, -1);
        }
        return 0;
    }
    if (msg == WM_CHAR && (wParam == L'\r' || wParam == L'\n' || wParam == L'\t'))
        return 0; // kein Warnton

    return CallWindowProcW(alteEditProc, hwnd, msg, wParam, lParam);
}

static HWND CreateEingabefeld(HWND parent, HINSTANCE hInst, int id, DWORD ausrichtung)
{
    HWND h = CreateWindowExW(
        0,
        L"EDIT",
        L"",
        WS_VISIBLE | WS_CHILD | WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL | ausrichtung,
        0, 0, 10, 10,
        parent,
        (HMENU)(INT_PTR)id,
        hInst,
        nullptr);

    schriftSetzen(h);
    WNDPROC alt = (WNDPROC)SetWindowLongPtrW(h, GWLP_WNDPROC, (LONG_PTR)EditProc);
    if (!alteEditProc)
        alteEditProc = alt;
    return h;
}

// Reihenfolge fuer Tab: Name, Zeile 1 X/Y/Z, Zeile 2 X/Y/Z, Zeile 3 X/Y/Z; gesperrte Felder werden uebersprungen
static std::vector<HWND> einstellungsFelder(); // Eingabefelder im Einstellungsfenster

static HWND naechstesFeld(HWND aktuell, bool rueckwaerts)
{
    std::vector<HWND> felder;
    if (GetParent(aktuell) != hObjektFenster)
        felder = einstellungsFelder();
    else if (reiter == 0)
    {
        felder.push_back(g.info);
        for (int i = 0; i < ANZAHL_EDITS; ++i)
            felder.push_back(g.edit[i]);
    }
    else if (reiter == 2)
        felder = {g.editManipTiefe, g.editManipKnick, g.editManipWert, g.editManipAus[0], g.editManipAus[1], g.editManipAus[2]};
    else
    {
        for (HWND h : g.editMatFarbe)
            felder.push_back(h);
        for (HWND h : g.editMatWert)
            felder.push_back(h);
        felder.push_back(g.editTextur);
        for (HWND h : {g.editTexturKachel, g.editTexturWinkel, g.editTexturVersatz[0], g.editTexturVersatz[1],
                       g.editTexturKugel[0], g.editTexturKugel[1], g.editTexturKugel[2], g.editTexturBild, g.editTexturStaerke,
                       g.editTexturRinge, g.editTexturNass})
            felder.push_back(h);
    }
    const int N = int(felder.size());

    int index = -1;
    for (int i = 0; i < N; ++i)
        if (felder[i] == aktuell)
            index = i;
    if (index < 0)
        return nullptr;

    for (int schritt = 1; schritt < N; ++schritt)
    {
        int i = (index + (rueckwaerts ? N - schritt : schritt)) % N;
        if (IsWindowEnabled(felder[i]) && IsWindowVisible(felder[i])) // Manipulation: nur die Felder der Aktion
            return felder[i];
    }
    return nullptr;
}

static int CALLBACK schriftGefunden(const LOGFONTW *, const TEXTMETRICW *, DWORD, LPARAM gefunden)
{
    *reinterpret_cast<bool *>(gefunden) = true;
    return 0;
}

static bool schriftVorhanden(const wchar_t *name)
{
    LOGFONTW lf = {};
    lf.lfCharSet = DEFAULT_CHARSET;
    wcsncpy(lf.lfFaceName, name, LF_FACESIZE - 1);

    bool gefunden = false;
    HDC dc = GetDC(nullptr);
    EnumFontFamiliesExW(dc, &lf, schriftGefunden, reinterpret_cast<LPARAM>(&gefunden), 0);
    ReleaseDC(nullptr, dc);
    return gefunden;
}

static std::wstring zahlText(double v, int stellen)
{
    wchar_t puffer[64];
    swprintf(puffer, 64, L"%.*f", stellen, v);
    return puffer;
}

// Akzeptiert Komma und Punkt als Dezimaltrennzeichen
static bool zahlLesen(HWND edit, double &wert)
{
    wchar_t puffer[64] = {};
    GetWindowTextW(edit, puffer, 64);

    std::wstring t(puffer);
    std::replace(t.begin(), t.end(), L',', L'.');

    wchar_t *ende = nullptr;
    wert = wcstod(t.c_str(), &ende);
    if (ende == t.c_str())
        return false;

    while (*ende == L' ')
        ++ende;
    return *ende == 0;
}

static void textSetzen(HWND h, const std::wstring &text)
{
    // nur bei Aenderung (kein Flackern) und nicht, waehrend dort getippt wird
    if (GetFocus() == h)
        return;

    wchar_t puffer[256] = {};
    GetWindowTextW(h, puffer, 256);
    if (text != puffer)
        SetWindowTextW(h, text.c_str());
}

static std::wstring zahlMitPunkten(long long n)
{
    std::wstring s = std::to_wstring(n);
    for (int i = int(s.size()) - 3; i > 0; i -= 3)
        s.insert(size_t(i), L".");
    return s;
}

static bool istRichtungslicht(const light *li)
{
    Vector3D d1 = li->directionFrom(Vector3D(1e4, 0, 0));
    Vector3D d2 = li->directionFrom(Vector3D(-1e4, 3e3, 7e3));
    return (d1 - d2).length() < 1e-9;
}

static std::wstring lichtName(const light *li, size_t i)
{
    std::wstring nummer = L" " + std::to_wstring(i);

    if (auto *sp = dynamic_cast<const SpotLight *>(li)) // vor PointLight (Spotlicht ist eines)
        return L"Spotlicht" + nummer + L"  (" + zahlText(sp->winkel * 2.0, 0) + L"°)";
    if (dynamic_cast<const PointLight *>(li))
        return L"Punktlicht" + nummer;
    if (auto *s = dynamic_cast<const SphereLight *>(li))
        return L"Kugellicht" + nummer + L"  (r = " + zahlText(s->radius, 2) + L")";
    if (auto *a = dynamic_cast<const AreaLight *>(li))
        return L"Flächenlicht" + nummer + L"  (" + zahlText(a->edgeU.length(), 2) + L" \u00D7 " +
               zahlText(a->edgeV.length(), 2) + L")";
    if (auto *m = dynamic_cast<const MeshLight *>(li))
        return L"Mesh-Licht" + nummer + L"  (" + std::to_wstring(m->mesh->triangles.size()) + L" Dreiecke)";
    if (istRichtungslicht(li))
        return L"Richtungslicht" + nummer;
    return L"Licht" + nummer;
}

static std::wstring ausUtf8(const std::string &text)
{
    if (text.empty())
        return L"";

    int laenge = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), int(text.size()), nullptr, 0);
    if (laenge <= 0) // kein gueltiges UTF-8: als Windows-Zeichensatz lesen
    {
        laenge = MultiByteToWideChar(CP_ACP, 0, text.data(), int(text.size()), nullptr, 0);
        std::wstring w(size_t(std::max(laenge, 0)), L'\0');
        MultiByteToWideChar(CP_ACP, 0, text.data(), int(text.size()), &w[0], laenge);
        return w;
    }

    std::wstring w(size_t(laenge), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), &w[0], laenge);
    return w;
}

static std::string zuUtf8(const std::wstring &text)
{
    if (text.empty())
        return "";

    int laenge = WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0, nullptr, nullptr);
    std::string s(size_t(std::max(laenge, 0)), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), &s[0], laenge, nullptr, nullptr);
    return s;
}

// Name des Objekts bzw. "Typ Nummer", falls es keinen Namen hat
static std::wstring objektGrundname(const object *o, size_t i)
{
    return o->name.empty() ? std::wstring(o->getTypeName()) + L" " + std::to_wstring(i)
                           : ausUtf8(o->name);
}

// Zusatzinfo, z.B. "Mesh, 8.738 Dreiecke"
static std::wstring objektDetails(const object *o)
{
    std::wstring typ = o->getTypeName();

    if (auto *g = dynamic_cast<const Gruppe *>(o))
        return L"Gruppe aus " + std::to_wstring(g->kinder.size()) + L" Objekten, " +
               zahlMitPunkten(g->getTriangleCount()) + L" Dreiecke";
    if (auto *in = dynamic_cast<const Instanz *>(o))
        return L"Instanz (verknüpfte Kopie, " + std::to_wstring(instanzenVon(in->vorlage.get())) + L" mit dieser Vorlage), " +
               zahlMitPunkten(o->getTriangleCount()) + L" Dreiecke der Vorlage";
    if (gruppeVon(o))
        typ += L" in einer Gruppe";

    if (auto *k = dynamic_cast<const Kugel *>(o))
        return typ + L", r = " + zahlText(k->radius, 2);

    int dreiecke = o->getTriangleCount();
    if (dreiecke > 0)
        return typ + L", " + zahlMitPunkten(dreiecke) + L" Dreiecke";
    return typ;
}

// Text in der Liste, z.B. "Siedlung  (8.738 Dreiecke)"
static std::wstring objektName(const object *o, size_t i)
{
    std::wstring name = objektGrundname(o, i);

    // Instanz: "⧉ Baum 12 → Baum" (Vorlage)
    if (auto *in = dynamic_cast<const Instanz *>(o))
    {
        std::wstring v = in->vorlage ? (in->vorlage->name.empty() ? std::wstring(in->vorlage->getTypeName()) : ausUtf8(in->vorlage->name))
                                     : std::wstring(L"?");
        return L"\u29C9 " + name + L" \u2192 " + v;
    }

    if (auto *g = dynamic_cast<const Gruppe *>(o))
        return name + L"  (" + std::to_wstring(g->kinder.size()) + L" Objekte)";

    if (auto *k = dynamic_cast<const Kugel *>(o))
        return name + L"  (r = " + zahlText(k->radius, 2) + L")";

    int dreiecke = o->getTriangleCount();
    if (dreiecke > 0)
        return name + L"  (" + zahlMitPunkten(dreiecke) + (dreiecke == 1 ? L" Dreieck)" : L" Dreiecke)");
    return name;
}

static Auswahl aktuelleAuswahl()
{
    Auswahl a;
    a.typ = selectedType;

    if (selectedType == SelectedType::OBJECT)
        a.zeiger = selectedObject;
    else if (selectedType == SelectedType::LIGHT)
        a.zeiger = selectedLight;
    else if (selectedType == SelectedType::CAMERA || selectedType == SelectedType::LOOKAT)
    {
        a.typ = SelectedType::CAMERA; // Kamera und LookAt sind derselbe Listeneintrag
        a.zeiger = cam.empty() ? nullptr : &cam[0];
    }
    return a;
}

static bool enthaelt(const object *gruppe, const object *o)
{
    auto *g = dynamic_cast<const Gruppe *>(gruppe);
    if (!g)
        return false;
    for (auto &k : g->kinder)
        if (k.get() == o || enthaelt(k.get(), o))
            return true;
    return false;
}

// Platz in 'scene'; bei Objekten in einer Gruppe der Platz der (obersten) Gruppe
static int objektIndex(const object *o)
{
    if (!o)
        return -1;
    for (size_t i = 0; i < scene.size(); ++i)
        if (scene[i].get() == o || enthaelt(scene[i].get(), o))
            return int(i);
    return -1;
}

// Eintrag in der Objektliste (auch fuer Objekte in Gruppen)
static HTREEITEM eintragVon(const object *o)
{
    for (auto &k : eintraegeKinder)
        if (k.first == o)
            return k.second;
    for (size_t i = 0; i < scene.size() && i < eintraegeObjekte.size(); ++i)
        if (scene[i].get() == o)
            return eintraegeObjekte[i];
    return nullptr;
}

static int lichtIndex(const light *li)
{
    for (size_t i = 0; i < lights.size(); ++i)
        if (lights[i].get() == li)
            return int(i);
    return -1;
}

static Vector3D objektMitte(const object *o)
{
    Vector3D minimum, maximum;
    if (!GpuAnsichten::objektGrenzen(o, minimum, maximum))
        return o->position;
    return (minimum + maximum) * 0.5;
}

// Bounding-Box ueber mehrere Objekte (wie in den Ansichten berechnet)
static bool grenzenVon(const std::vector<object *> &objekte, Vector3D &minimum, Vector3D &maximum)
{
    bool erstes = true;
    for (const object *o : objekte)
    {
        Vector3D mn, mx;
        if (!GpuAnsichten::objektGrenzen(o, mn, mx))
            mn = mx = o->position;
        if (erstes)
        {
            minimum = mn;
            maximum = mx;
            erstes = false;
            continue;
        }
        minimum = Vector3D(std::min(minimum.x, mn.x), std::min(minimum.y, mn.y), std::min(minimum.z, mn.z));
        maximum = Vector3D(std::max(maximum.x, mx.x), std::max(maximum.y, mx.y), std::max(maximum.z, mx.z));
    }
    return !erstes;
}

//--------------------------------------------------
// Objektliste (TreeView)
//--------------------------------------------------

static HTREEITEM eintragEinfuegen(HTREEITEM eltern, const std::wstring &text, LPARAM wert)
{
    TVINSERTSTRUCTW item{};
    item.hParent = eltern;
    item.hInsertAfter = TVI_LAST;
    item.item.mask = TVIF_TEXT | TVIF_PARAM;
    item.item.pszText = const_cast<LPWSTR>(text.c_str());
    item.item.lParam = wert;

    return reinterpret_cast<HTREEITEM>(
        SendMessageW(hTreeObjects, TVM_INSERTITEMW, 0, reinterpret_cast<LPARAM>(&item)));
}

static void eintragTextSetzen(HTREEITEM eintrag, const std::wstring &text)
{
    TVITEMW item = {};
    item.mask = TVIF_TEXT;
    item.hItem = eintrag;
    item.pszText = const_cast<LPWSTR>(text.c_str());
    SendMessageW(hTreeObjects, TVM_SETITEMW, 0, reinterpret_cast<LPARAM>(&item));
}

static void eintragAuswaehlen(HTREEITEM eintrag)
{
    auswahlWirdGesetzt = true;
    TreeView_SelectItem(hTreeObjects, eintrag);
    if (eintrag)
        TreeView_EnsureVisible(hTreeObjects, eintrag);
    auswahlWirdGesetzt = false;
}

//--------------------------------------------------
// Eigenschaften anzeigen und uebernehmen
//--------------------------------------------------

// anzahl: nur die ersten Felder benutzen (Kamera-FOV: 1), die uebrigen gesperrt und leer
//--------------------------------------------------
// Material: Felder und Vorlagen
//--------------------------------------------------

static const wchar_t *const MAT_FARB_NAMEN[MAT_FARBEN] = {L"Umgebung", L"Diffus", L"Glanz", L"Leuchten", L"Absorption"};
static const wchar_t *const MAT_WERT_NAMEN[MAT_WERTE] = {L"Spiegelung", L"Transparenz", L"Brechung", L"Rauheit",
                                                         L"Glanzschärfe", L"Glanzstärke", L"kA", L"kD"};
static const double MAT_WERT_MAX[MAT_WERTE] = {1, 1, 10, 1, 100000, 100, 100, 100};
static const int MAT_WERT_STELLEN[MAT_WERTE] = {3, 3, 3, 3, 1, 3, 3, 3};

static Farbe &matFarbe(Material &m, int i)
{
    switch (i)
    {
    case 0:
        return m.ambient;
    case 1:
        return m.diffuse;
    case 2:
        return m.specular;
    case 3:
        return m.emission;
    default:
        return m.absorption;
    }
}

static double &matWert(Material &m, int i)
{
    switch (i)
    {
    case 0:
        return m.reflection;
    case 1:
        return m.transparency;
    case 2:
        return m.IndexOfRefraction;
    case 3:
        return m.roughness;
    case 4:
        return m.shininess;
    case 5:
        return m.specularStrength;
    case 6:
        return m.kA;
    default:
        return m.kD;
    }
}

struct MaterialVorlage
{
    const wchar_t *name;
    MaterialType typ;
    const char *id; // wie in der Szenen-XML (SceneLoader::stringToMaterialType)
};
static const MaterialVorlage MAT_VORLAGEN[] = {
    {L"Spiegel", MaterialType::Mirror, "mirror"},       {L"Gold", MaterialType::Gold, "gold"},
    {L"Erde", MaterialType::Earth, "earth"},            {L"Blaues Plastik", MaterialType::BluePlastic, "bluePlastic"},
    {L"Glas", MaterialType::Glass, "glass"},            {L"Grünes Glas", MaterialType::GreenGlass, "greenGlass"},
    {L"Wand", MaterialType::Wall, "wall"},              {L"Einfach", MaterialType::Simple, "simple"},
    {L"Wasser", MaterialType::Wasser, "wasser"}};
constexpr int ANZAHL_VORLAGEN = int(sizeof(MAT_VORLAGEN) / sizeof(MAT_VORLAGEN[0]));

// ---------- weitere Vorlagen (nur im Editor; gespeichert als eigenes Material in der XML) ----------
// So wirken die Werte im Renderer (render.cpp):
//  - Diffus: Farbe der matten Flaeche (direktes und indirektes Licht); kD skaliert das direkte Licht.
//  - Glanz/Glanzstaerke/Glanzschaerfe: Glanzpunkte der Lichter, nur bei Spiegelung = 0.
//  - Spiegelung: Anteil gespiegelt; ist dazu "Glanz" gesetzt, hat die Spiegelung diese Farbe (Metalle).
//    Mit Brechung > 0 spiegelt die Flaeche zusaetzlich nach Fresnel (flach betrachtet mehr: Lack, Kunststoff).
//  - Rauheit: Streuung der Spiegelung (0 = scharf).
//  - Transparenz + Brechung: Glas, Wasser, Edelsteine; Absorption faerbt je Einheit Weg im Material
//    (Werte fuer Objekte von etwa 5-10 cm, Szene in Metern).
//  - Leuchten: eigene Helligkeit (beleuchtet die Umgebung nur ueber das indirekte Licht).
struct MaterialVorlageEigen
{
    const wchar_t *name;
    Material mat;
};

static Material vorlageMaterial(Farbe diffus, Farbe glanz, double glanzStaerke, double glanzSchaerfe, double spiegelung,
                                double rauheit, double brechung, double transparenz, Farbe absorption = Farbe(0, 0, 0),
                                Farbe leuchten = Farbe(0, 0, 0))
{
    Material m;
    m.ambient = Farbe(diffus.r * 0.1, diffus.g * 0.1, diffus.b * 0.1);
    m.diffuse = diffus;
    m.specular = glanz;
    m.emission = leuchten;
    m.absorption = absorption;
    m.kA = 0.2;
    m.kD = transparenz >= 1.0 ? 0.0 : 0.9;
    m.shininess = glanzSchaerfe;
    m.specularStrength = glanzStaerke;
    m.reflection = spiegelung;
    m.roughness = rauheit;
    m.IndexOfRefraction = brechung;
    m.transparency = transparenz;
    return m;
}

static const std::vector<MaterialVorlageEigen> &weitereVorlagen()
{
    const Farbe W(1, 1, 1), K(0, 0, 0);
    auto g = [](double v) { return Farbe(v, v, v); };
    static const std::vector<MaterialVorlageEigen> liste = {
        // --- Metalle: Spiegelung stark, "Glanz" = Metallfarbe der Spiegelung, Diffus = dunkler Grundton ---
        {L"Metall: Chrom", vorlageMaterial(Farbe(0.30, 0.30, 0.32), Farbe(0.62, 0.63, 0.65), 1, 1, 0.95, 0.0, 0, 0)},
        {L"Metall: Silber poliert", vorlageMaterial(Farbe(0.40, 0.40, 0.40), Farbe(0.95, 0.93, 0.88), 1, 1, 0.90, 0.01, 0, 0)},
        {L"Metall: Gold poliert", vorlageMaterial(Farbe(0.45, 0.32, 0.08), Farbe(1.00, 0.78, 0.34), 1, 1, 0.90, 0.02, 0, 0)},
        {L"Metall: Messing", vorlageMaterial(Farbe(0.40, 0.30, 0.12), Farbe(0.91, 0.78, 0.42), 1, 1, 0.85, 0.05, 0, 0)},
        {L"Metall: Kupfer", vorlageMaterial(Farbe(0.40, 0.20, 0.12), Farbe(0.95, 0.64, 0.54), 1, 1, 0.85, 0.05, 0, 0)},
        {L"Metall: Bronze", vorlageMaterial(Farbe(0.35, 0.22, 0.10), Farbe(0.80, 0.55, 0.35), 1, 1, 0.70, 0.10, 0, 0)},
        {L"Metall: Edelstahl gebürstet", vorlageMaterial(Farbe(0.35, 0.35, 0.36), Farbe(0.67, 0.66, 0.65), 1, 1, 0.80, 0.25, 0, 0)},
        {L"Metall: Aluminium matt", vorlageMaterial(Farbe(0.50, 0.50, 0.52), Farbe(0.91, 0.92, 0.92), 1, 1, 0.70, 0.35, 0, 0)},
        {L"Metall: Eisen rostig", vorlageMaterial(Farbe(0.45, 0.22, 0.10), g(0.3), 0.10, 10, 0, 1, 0, 0)},
        // --- Kunststoff, Lack, Keramik (Glanzpunkte, Fresnel ueber Brechung 1.5) ---
        {L"Kunststoff: Weiß glänzend", vorlageMaterial(g(0.85), W, 0.6, 150, 0, 0, 1.5, 0)},
        {L"Kunststoff: Rot glänzend", vorlageMaterial(Farbe(0.75, 0.08, 0.06), W, 0.6, 150, 0, 0, 1.5, 0)},
        {L"Kunststoff: Schwarz glänzend", vorlageMaterial(g(0.03), W, 0.7, 200, 0, 0, 1.5, 0)},
        {L"Kunststoff: Gelb matt", vorlageMaterial(Farbe(0.85, 0.70, 0.10), W, 0.15, 20, 0, 1, 0, 0)},
        {L"Gummi: Schwarz", vorlageMaterial(g(0.04), W, 0.10, 8, 0, 1, 0, 0)},
        {L"Autolack: Blau", vorlageMaterial(Farbe(0.05, 0.15, 0.50), W, 0.8, 300, 0, 0, 1.5, 0)},
        {L"Autolack: Rot", vorlageMaterial(Farbe(0.55, 0.03, 0.03), W, 0.8, 300, 0, 0, 1.5, 0)},
        {L"Keramik: Weiß glasiert", vorlageMaterial(Farbe(0.90, 0.90, 0.88), W, 0.7, 400, 0, 0, 1.5, 0)},
        {L"Porzellan", vorlageMaterial(Farbe(0.95, 0.94, 0.92), W, 0.5, 200, 0, 0, 1.5, 0)},
        {L"Marmor: Weiß poliert", vorlageMaterial(Farbe(0.88, 0.87, 0.84), W, 0.5, 250, 0, 0, 1.5, 0)},
        // --- Glas, Fluessigkeiten, Edelsteine ---
        {L"Glas: Klar (Fensterglas)", vorlageMaterial(K, W, 0, 1, 0, 0, 1.52, 1, Farbe(0.3, 0.1, 0.3))},
        {L"Glas: Blau", vorlageMaterial(K, W, 0, 1, 0, 0, 1.52, 1, Farbe(12, 6, 0.5))},
        {L"Glas: Bernstein", vorlageMaterial(K, W, 0, 1, 0, 0, 1.55, 1, Farbe(1, 4, 15))},
        {L"Glas: Milchglas", vorlageMaterial(g(0.85), W, 0.3, 60, 0, 0, 1.5, 0.5)},
        {L"Diamant", vorlageMaterial(K, W, 0, 1, 0, 0, 2.42, 1)},
        {L"Rubin", vorlageMaterial(K, W, 0, 1, 0, 0, 1.76, 1, Farbe(0.5, 25, 20))},
        {L"Smaragd", vorlageMaterial(K, W, 0, 1, 0, 0, 1.57, 1, Farbe(20, 0.8, 12))},
        {L"Eis", vorlageMaterial(Farbe(0.80, 0.90, 1.00), W, 0.3, 80, 0, 0, 1.31, 0.85, Farbe(0.3, 0.1, 0.05))},
        {L"Rotwein", vorlageMaterial(K, W, 0, 1, 0, 0, 1.34, 1, Farbe(5, 40, 35))},
        // --- Natuerliche und Bau-Materialien (matt) ---
        {L"Holz: Hell", vorlageMaterial(Farbe(0.75, 0.55, 0.35), W, 0.20, 30, 0, 1, 0, 0)},
        {L"Holz: Dunkel lackiert", vorlageMaterial(Farbe(0.30, 0.16, 0.08), W, 0.5, 120, 0, 0, 1.5, 0)},
        {L"Beton", vorlageMaterial(Farbe(0.55, 0.54, 0.52), W, 0.05, 5, 0, 1, 0, 0)},
        {L"Ziegel", vorlageMaterial(Farbe(0.60, 0.25, 0.15), W, 0.05, 5, 0, 1, 0, 0)},
        {L"Papier", vorlageMaterial(Farbe(0.90, 0.90, 0.86), K, 0, 1, 0, 1, 0, 0)},
        {L"Stoff: Rot", vorlageMaterial(Farbe(0.60, 0.08, 0.10), W, 0.05, 3, 0, 1, 0, 0)},
        {L"Leder: Braun", vorlageMaterial(Farbe(0.35, 0.20, 0.10), W, 0.25, 25, 0, 1, 0, 0)},
        {L"Haut", vorlageMaterial(Farbe(0.85, 0.62, 0.50), W, 0.15, 20, 0, 1, 0, 0)},
        {L"Schnee", vorlageMaterial(Farbe(0.95, 0.96, 1.00), W, 0.10, 10, 0, 1, 0, 0)},
        {L"Gras", vorlageMaterial(Farbe(0.20, 0.45, 0.10), W, 0.05, 5, 0, 1, 0, 0)},
        {L"Sand", vorlageMaterial(Farbe(0.80, 0.70, 0.50), W, 0.05, 5, 0, 1, 0, 0)},
        // --- Leuchtend (Leuchten > 1: auch nach Tonemapping kraeftig, hellt die Umgebung ueber indirektes Licht auf) ---
        {L"Leuchten: Warmweiß", vorlageMaterial(Farbe(0.9, 0.85, 0.75), K, 0, 1, 0, 1, 0, 0, K, Farbe(4.0, 3.2, 2.2))},
        {L"Leuchten: Neon Blau", vorlageMaterial(Farbe(0.2, 0.4, 0.9), K, 0, 1, 0, 1, 0, 0, K, Farbe(0.3, 1.2, 6.0))},
        {L"Leuchten: Neon Pink", vorlageMaterial(Farbe(0.9, 0.2, 0.6), K, 0, 1, 0, 1, 0, 0, K, Farbe(6.0, 0.4, 2.6))},
        {L"Leuchten: Neon Grün", vorlageMaterial(Farbe(0.3, 0.9, 0.3), K, 0, 1, 0, 1, 0, 0, K, Farbe(0.5, 6.0, 0.8))},
        {L"Leuchten: Neon Rot", vorlageMaterial(Farbe(0.9, 0.2, 0.2), K, 0, 1, 0, 1, 0, 0, K, Farbe(6.0, 0.5, 0.3))},
        {L"Leuchten: Glut / Lava", vorlageMaterial(Farbe(0.2, 0.05, 0.0), K, 0, 1, 0, 1, 0, 0, K, Farbe(6.0, 1.5, 0.15))},
    };
    return liste;
}

static const wchar_t *const TEXTUR_MODI[] = {L"keine", L"Kugel (SPHERE)", L"Ebene (PLANAR)", L"UV", L"Zylinder (CYLINDER)",
                                             L"Würfel (BOX)", L"Würfel weich (TRIPLANAR)"};
static const TextureMode TEXTUR_MODUS_WERTE[] = {TextureMode::None, TextureMode::SPHERE, TextureMode::PLANAR, TextureMode::UV,
                                                  TextureMode::CYLINDER, TextureMode::BOX, TextureMode::TRIPLANAR};
static constexpr int TEXTUR_MODUS_ANZAHL = 7;

static bool materialGleich(const Material &a, const Material &b)
{
    auto f = [](const Farbe &x, const Farbe &y)
    { return std::fabs(x.r - y.r) < 1e-9 && std::fabs(x.g - y.g) < 1e-9 && std::fabs(x.b - y.b) < 1e-9; };
    auto w = [](double x, double y)
    { return std::fabs(x - y) < 1e-9; };
    return f(a.ambient, b.ambient) && f(a.diffuse, b.diffuse) && f(a.specular, b.specular) && f(a.emission, b.emission) &&
           f(a.absorption, b.absorption) && w(a.kA, b.kA) && w(a.kD, b.kD) && w(a.shininess, b.shininess) &&
           w(a.specularStrength, b.specularStrength) && w(a.roughness, b.roughness) && w(a.reflection, b.reflection) &&
           w(a.IndexOfRefraction, b.IndexOfRefraction) && w(a.transparency, b.transparency);
}

// Material, das fuer ein Objekt angezeigt wird (Mesh: das des ersten Dreiecks, wie bei der Farbe)
static Material objektMaterial(const object *o)
{
    if (auto *g = dynamic_cast<const Gruppe *>(o)) // Gruppe: das erste Objekt darin
        for (auto &k : g->kinder)
            if (!dynamic_cast<const Tree *>(k.get()))
                return objektMaterial(k.get());
    if (auto *m = dynamic_cast<const Mesh *>(o))
        if (!m->triangles.empty())
            return m->triangles.front().mat;
    return o->mat;
}

static void aktivieren(HWND h, bool aktiv)
{
    if (h && bool(IsWindowEnabled(h)) != aktiv)
        EnableWindow(h, aktiv);
}

static void zeileAnzeigen(int zeile, bool aktiv, const Vector3D &v, int stellen, int anzahl = 3)
{
    for (int k = 0; k < 3; ++k)
    {
        HWND h = g.edit[zeile * 3 + k];
        bool feldAktiv = aktiv && k < anzahl;
        if (bool(IsWindowEnabled(h)) != feldAktiv)
            EnableWindow(h, feldAktiv);

        if (!feldAktiv)
        {
            textSetzen(h, L"");
            continue;
        }

        double wert = k == 0 ? v.x : k == 1 ? v.y
                                            : v.z;
        textSetzen(h, zahlText(wert, stellen));
    }
}

static void farbfeldSetzen(bool aktiv, const Farbe &f)
{
    bool geaendert = aktiv != farbeAktiv || f.r != angezeigteFarbe.r || f.g != angezeigteFarbe.g || f.b != angezeigteFarbe.b;
    farbeAktiv = aktiv;
    angezeigteFarbe = f;

    EnableWindow(g.btnFarbe, aktiv);
    if (geaendert)
        InvalidateRect(g.btnFarbe, nullptr, FALSE);
}

static void zielKnopfSetzen(); // weiter unten
static void zeilenTippsSetzen(); // weiter unten

static void eigenschaftenAnzeigen()
{
    Vector3D leer(0, 0, 0);
    zielKnopfSetzen();
    zeilenTippsSetzen();
    if (reiter == 2)
        manipObjekteFuellen(); // Anzeige der Auswahl im Reiter "Manipulation"

    switch (selectedType)
    {
    case SelectedType::OBJECT:
    {
        const object *o = selectedObject;
        int index = objektIndex(o);
        if (!o || index < 0)
            break;

        std::vector<object *> alle = ausgewaehlteObjekte();
        Vector3D mn, mx;
        if (!grenzenVon(alle.empty() ? std::vector<object *>{const_cast<object *>(o)} : alle, mn, mx))
            mn = mx = o->position;

        if (alle.size() > 1)
        {
            // Mehrfachauswahl: Mitte und Groesse aller Objekte zusammen, Drehung nur einzeln
            EnableWindow(g.info, FALSE);
            textSetzen(g.info, std::to_wstring(alle.size()) + L" Objekte ausgewählt");
            textSetzen(g.lblDetails, L"Hauptobjekt: " + objektGrundname(o, size_t(index)));
            zeileAnzeigen(2, false, leer, 1);
        }
        else
        {
            EnableWindow(g.info, TRUE);
            textSetzen(g.info, objektGrundname(o, size_t(index)));
            textSetzen(g.lblDetails, objektDetails(o));
            zeileAnzeigen(2, true, objektDrehwinkel(o), 1); // Grad um X, Y, Z
        }
        textSetzen(g.lblZeile1, L"Mitte");
        textSetzen(g.lblZeile2, L"Farbe");
        textSetzen(g.lblZeile3, L"Drehung");
        textSetzen(g.lblZeile4, L"Größe");
        textSetzen(g.lblZeile5, L"Unterster");
        zeileAnzeigen(0, true, (mn + mx) * 0.5, 2);
        zeileAnzeigen(3, true, mx - mn, 3); // Ausdehnung X, Y, Z
        {
            const Vector3D mitte = (mn + mx) * 0.5;
            zeileAnzeigen(4, true, Vector3D(mitte.x, mn.y, mitte.z), 3); // unterster Punkt (Mitte unten)
            zeileAnzeigen(5, true, Vector3D(mitte.x, mx.y, mitte.z), 3); // oberster Punkt (Mitte oben)
        }

        // Tree hat Holz und Blaetter in verschiedenen Farben: nicht als eine Farbe aenderbar
        if (dynamic_cast<const Tree *>(o) || dynamic_cast<const VolumenBox *>(o))
        {
            zeileAnzeigen(1, false, leer, 3);
            farbfeldSetzen(false, Farbe());
            return;
        }

        Farbe f = o->mat.diffuse;
        if (auto *m = dynamic_cast<const Mesh *>(o))
            if (!m->triangles.empty())
                f = m->triangles.front().mat.diffuse;

        zeileAnzeigen(1, true, Vector3D(f.r, f.g, f.b), 3);
        farbfeldSetzen(true, f);
        return;
    }

    case SelectedType::LIGHT:
    {
        const light *li = selectedLight;
        int index = lichtIndex(li);
        if (!li || index < 0)
            break;

        EnableWindow(g.info, FALSE); // Lichter haben keinen Namen
        textSetzen(g.info, lichtName(li, size_t(index)));
        textSetzen(g.lblDetails, L"Licht");
        textSetzen(g.lblZeile1, L"Position");
        textSetzen(g.lblZeile2, L"Farbe");
        // Richtungslicht: Zeile "Richtung" = Laufrichtung des Lichts (wohin der Pfeil zeigt)
        const auto *spot = dynamic_cast<const SpotLight *>(li);
        if (spot)
        {
            textSetzen(g.lblDetails, L"Spotlicht: leuchtet in einen Kegel (Strich = Richtung)");
            textSetzen(g.lblZeile3, L"Richtung");
            zeileAnzeigen(2, true, spot->richtung, 3);
        }
        else if (auto *d = dynamic_cast<const DirectionalLight *>(li))
        {
            textSetzen(g.lblDetails, L"Richtungslicht (Position = nur das Symbol)");
            textSetzen(g.lblZeile3, L"Richtung");
            zeileAnzeigen(2, true, -d->direction.normalized(), 3);
        }
        else if (auto *a = dynamic_cast<const AreaLight *>(li))
        {
            textSetzen(g.lblDetails, L"Flächenlicht: leuchtet in Richtung Kante U \u00D7 Kante V (Strich in den Ansichten)");
            textSetzen(g.lblZeile3, L"Kante U");
            zeileAnzeigen(2, true, a->edgeU, 3);
        }
        else if (auto *k = dynamic_cast<const SphereLight *>(li))
        {
            textSetzen(g.lblDetails, L"Kugellicht");
            textSetzen(g.lblZeile3, L"Radius");
            zeileAnzeigen(2, true, Vector3D(k->radius, 0, 0), 3, 1);
        }
        else
        {
            if (dynamic_cast<const MeshLight *>(li))
                textSetzen(g.lblDetails, L"Mesh-Licht: die ganze Oberfläche leuchtet");
            textSetzen(g.lblZeile3, L"Drehung");
            zeileAnzeigen(2, false, leer, 1);
        }
        if (auto *a = dynamic_cast<const AreaLight *>(li))
        {
            textSetzen(g.lblZeile4, L"Kante V");
            zeileAnzeigen(3, true, a->edgeV, 3);
        }
        else if (spot)
        {
            textSetzen(g.lblZeile4, L"Kegel ° / weich");
            zeileAnzeigen(3, true, Vector3D(spot->winkel, spot->weich, 0), 2, 2);
        }
        else
        {
            textSetzen(g.lblZeile4, L"Größe");
            zeileAnzeigen(3, false, leer, 3);
        }
        {
            double leistung = -1;
            if (auto *a = dynamic_cast<const AreaLight *>(li))
                leistung = a->power;
            else if (auto *k = dynamic_cast<const SphereLight *>(li))
                leistung = k->power;
            else if (auto *m = dynamic_cast<const MeshLight *>(li))
                leistung = m->power;
            else if (auto *pl = dynamic_cast<const PointLight *>(li))
                leistung = pl->power;
            const auto *rl = dynamic_cast<const DirectionalLight *>(li);
            if (rl)
                leistung = rl->irradiance;
            textSetzen(g.lblZeile5, rl ? L"Stärke" : leistung >= 0 ? L"Leistung (W)" : L"Unterster");
            zeileAnzeigen(4, leistung >= 0, Vector3D(leistung, 0, 0), 1, 1);
        }
        zeileAnzeigen(5, false, leer, 3);
        zeileAnzeigen(0, true, li->getPosition(), 2);
        zeileAnzeigen(1, true, Vector3D(li->farbe.r, li->farbe.g, li->farbe.b), 3);
        farbfeldSetzen(true, li->farbe);
        return;
    }

    case SelectedType::CAMERA:
    case SelectedType::LOOKAT:
    {
        if (cam.empty())
            break;

        EnableWindow(g.info, FALSE);
        textSetzen(g.info, L"Kamera");
        textSetzen(g.lblDetails, L"fov = " + zahlText(cam[0].fov, 1) + L"°");
        textSetzen(g.lblZeile1, L"Position");
        textSetzen(g.lblZeile2, L"LookAt");
        textSetzen(g.lblZeile3, L"FOV"); // Oeffnungswinkel in Grad statt Drehung
        zeileAnzeigen(2, true, Vector3D(cam[0].fov, 0, 0), 1, 1);
        zeileAnzeigen(3, false, leer, 3);
        zeileAnzeigen(4, false, leer, 3);
        zeileAnzeigen(5, false, leer, 3);
        zeileAnzeigen(0, true, cam[0].position, 2);
        zeileAnzeigen(1, true, cam[0].lookAt, 2);
        farbfeldSetzen(false, Farbe());
        return;
    }

    default:
        break;
    }

    EnableWindow(g.info, FALSE);
    textSetzen(g.info, L"Nichts ausgewählt");
    textSetzen(g.lblDetails, L"");
    textSetzen(g.lblZeile1, L"Position");
    textSetzen(g.lblZeile2, L"Farbe");
    zeileAnzeigen(0, false, leer, 2);
    zeileAnzeigen(1, false, leer, 3);
    textSetzen(g.lblZeile3, L"Drehung");
    textSetzen(g.lblZeile4, L"Größe");
    textSetzen(g.lblZeile5, L"Unterster");
    zeileAnzeigen(2, false, leer, 1);
    zeileAnzeigen(3, false, leer, 3);
    zeileAnzeigen(4, false, leer, 3);
    zeileAnzeigen(5, false, leer, 3);
    farbfeldSetzen(false, Farbe());
}

// Instanzen durch ihre Vorlage ersetzen (Material, Glaetten, Netz-Operationen wirken auf alle Instanzen)
static std::vector<object *> mitVorlagen(const std::vector<object *> &liste)
{
    std::vector<object *> r;
    for (object *o : liste)
    {
        object *z = o;
        for (int t = 0; t < 8; ++t) // Vorlage kann selbst eine Instanz sein
            if (auto *in = dynamic_cast<Instanz *>(z))
                z = in->vorlage.get();
        if (z && std::find(r.begin(), r.end(), z) == r.end())
            r.push_back(z);
    }
    return r;
}

static void farbeSetzen(object *o, const Farbe &f)
{
    o->mat.diffuse = f;

    if (auto *g = dynamic_cast<Gruppe *>(o))
    {
        for (auto &k : g->kinder)
            if (!dynamic_cast<Tree *>(k.get()))
                farbeSetzen(k.get(), f);
        return;
    }

    // Mesh und Pfeil zeichnen/rendern mit dem Material jedes einzelnen Dreiecks
    if (auto *m = dynamic_cast<Mesh *>(o))
        for (auto &t : m->triangles)
            t.mat.diffuse = f;
    else if (auto *p = dynamic_cast<Pfeil *>(o))
        for (auto &t : p->tris)
            t.mat.diffuse = f;
    else if (auto *tx = dynamic_cast<Text3D *>(o))
        tx->setFarbe(f);
}

static void objektVerschieben(object *o, const Vector3D &delta)
{
    o->translate(delta);
}

// Farbe des ausgewaehlten Objekts bzw. Lichts setzen
static void farbeUebernehmen(const Farbe &f)
{
    if (selectedType == SelectedType::OBJECT && selectedObject && objektIndex(selectedObject) >= 0)
    {
        std::vector<object *> alle = mitVorlagen(ausgewaehlteObjekte()); // Instanz: Farbe der Vorlage
        aenderungBeginnen("Farbe", alle);
        for (object *o : alle)
            if (!dynamic_cast<Tree *>(o))
                farbeSetzen(o, f);
        aenderungAbschliessen();
    }
    else if (selectedType == SelectedType::LIGHT && selectedLight && lichtIndex(selectedLight) >= 0)
    {
        aenderungBeginnen("Lichtfarbe", {}, {selectedLight});
        selectedLight->farbe = f;
        aenderungAbschliessen();
    }
}

static int kanal(double wert)
{
    return std::clamp(int(wert * 255.0 + 0.5), 0, 255);
}

// Windows-Farbdialog mit der aktuellen Farbe
static void farbdialog()
{
    if (!farbeAktiv)
        return;

    CHOOSECOLORW cc = {};
    cc.lStructSize = sizeof(cc);
    cc.hwndOwner = hObjektFenster;
    cc.rgbResult = RGB(kanal(angezeigteFarbe.r), kanal(angezeigteFarbe.g), kanal(angezeigteFarbe.b));
    cc.lpCustColors = eigeneFarben;
    cc.Flags = CC_FULLOPEN | CC_RGBINIT | CC_ANYCOLOR;

    if (!ChooseColorW(&cc))
        return;

    farbeUebernehmen(Farbe(GetRValue(cc.rgbResult) / 255.0,
                           GetGValue(cc.rgbResult) / 255.0,
                           GetBValue(cc.rgbResult) / 255.0));
    eigenschaftenAnzeigen();
}

static void umbenennen()
{
    if (!SendMessageW(g.info, EM_GETMODIFY, 0, 0))
        return;
    SendMessageW(g.info, EM_SETMODIFY, FALSE, 0);

    int index = objektIndex(selectedObject);
    if (selectedType != SelectedType::OBJECT || index < 0)
        return;

    wchar_t puffer[256] = {};
    GetWindowTextW(g.info, puffer, 256);

    std::wstring name(puffer);
    while (!name.empty() && name.back() == L' ')
        name.pop_back();
    while (!name.empty() && name.front() == L' ')
        name.erase(name.begin());

    if (auto *tx = dynamic_cast<Text3D *>(selectedObject))
    {
        // beim 3D-Text ist der Name der Text: Buchstaben neu aufbauen
        if (!name.empty() && gSzeneAendern)
        {
            std::string text = zuUtf8(name);
            aenderungBeginnen("Text", {tx});
            gSzeneAendern([&]
                          { tx->setText(text); });
            aenderungAbschliessen();
        }
    }
    else
    {
        // leer = Name entfernen, dann wird wieder "Typ Nummer" angezeigt
        aenderungBeginnen("Umbenennen", {selectedObject});
        selectedObject->name = zuUtf8(name);
        aenderungAbschliessen();
    }

    if (HTREEITEM e = eintragVon(selectedObject))
        eintragTextSetzen(e, objektName(selectedObject, size_t(index)));

    SetWindowTextW(g.info, objektGrundname(selectedObject, size_t(index)).c_str());
}

// Liest die Zeile des geaenderten Felds und wendet sie auf die Auswahl an
static void texturBildUebernehmen();            // Material-Reiter, weiter unten
static void texturWertUebernehmen(int id);      // Material-Reiter, weiter unten

static void eingabeUebernehmen(int editId)
{
    if (editId == ID_EDIT_NAME)
    {
        umbenennen();
        return;
    }
    if (editId == ID_EDIT_SCHRITT)
    {
        schrittUebernehmen();
        return;
    }
    if (editId >= ID_EDIT_MATFARBE_ERSTES && editId < ID_EDIT_MATFARBE_ERSTES + MAT_FARBEN * 3)
    {
        materialFarbeUebernehmen((editId - ID_EDIT_MATFARBE_ERSTES) / 3);
        return;
    }
    if (editId >= ID_EDIT_MATWERT_ERSTES && editId < ID_EDIT_MATWERT_ERSTES + MAT_WERTE)
    {
        materialWertUebernehmen(editId - ID_EDIT_MATWERT_ERSTES);
        return;
    }
    if (editId == ID_EDIT_TEXTUR)
    {
        texturUebernehmen();
        return;
    }
    if (editId == ID_EDIT_TEXTUR_BILD)
    {
        texturBildUebernehmen();
        return;
    }
    if ((editId > ID_EDIT_TEXTUR && editId <= ID_EDIT_TEXTUR_LETZTES) || editId == ID_EDIT_TEXTUR_RINGE ||
        editId == ID_EDIT_TEXTUR_NASS)
    {
        texturWertUebernehmen(editId);
        return;
    }

    int zeile = (editId - ID_EDIT_ERSTES) / 3;
    if (zeile < 0 || zeile > 5)
        return;
    const bool kamera = selectedType == SelectedType::CAMERA || selectedType == SelectedType::LOOKAT;
    const bool fov = zeile == 2 && kamera; // bei der Kamera: Zeile "FOV", nur das erste Feld
    const bool richtung = zeile == 2 && selectedType == SelectedType::LIGHT &&
                          (dynamic_cast<DirectionalLight *>(selectedLight) != nullptr ||
                           dynamic_cast<SpotLight *>(selectedLight) != nullptr);
    const bool kegel = zeile == 3 && selectedType == SelectedType::LIGHT && dynamic_cast<SpotLight *>(selectedLight) != nullptr;
    const bool lichtLicht = selectedType == SelectedType::LIGHT && selectedLight;
    const bool kante = lichtLicht && (zeile == 2 || zeile == 3) && dynamic_cast<AreaLight *>(selectedLight) != nullptr;
    const bool radius = lichtLicht && zeile == 2 && dynamic_cast<SphereLight *>(selectedLight) != nullptr;
    const bool leistung = lichtLicht && zeile == 4 &&
                          (dynamic_cast<AreaLight *>(selectedLight) || dynamic_cast<SphereLight *>(selectedLight) ||
                           dynamic_cast<MeshLight *>(selectedLight) || dynamic_cast<PointLight *>(selectedLight) ||
                           dynamic_cast<DirectionalLight *>(selectedLight));
    if (zeile >= 2 && selectedType != SelectedType::OBJECT && !fov && !richtung && !kante && !radius && !leistung && !kegel)
        return; // Drehung und Groesse nur fuer Objekte
    const int felder = fov || radius || leistung ? 1 : kegel ? 2 : 3;

    // Nur wenn wirklich getippt wurde (sonst wuerde z.B. das Verlassen des Felds
    // das Objekt um Rundungsreste der Anzeige verschieben)
    bool geaendert = false;
    for (int k = 0; k < felder; ++k)
        geaendert = geaendert || SendMessageW(g.edit[zeile * 3 + k], EM_GETMODIFY, 0, 0);
    if (!geaendert)
        return;

    for (int k = 0; k < 3; ++k)
        SendMessageW(g.edit[zeile * 3 + k], EM_SETMODIFY, FALSE, 0);

    double w[3] = {0, 0, 0};
    for (int k = 0; k < felder; ++k)
    {
        if (!zahlLesen(g.edit[zeile * 3 + k], w[k]))
        {
            // ungueltige Eingabe: Text stehen lassen, damit man ihn korrigieren kann
            SendMessageW(g.edit[zeile * 3 + k], EM_SETMODIFY, TRUE, 0);
            MessageBeep(MB_ICONWARNING);
            return;
        }
    }

    Vector3D v(w[0], w[1], w[2]);
    Farbe f(std::max(0.0, w[0]), std::max(0.0, w[1]), std::max(0.0, w[2]));

    if (selectedType == SelectedType::OBJECT && selectedObject && objektIndex(selectedObject) >= 0)
    {
        if (zeile == 1)
            farbeUebernehmen(f); // fuer alle ausgewaehlten, mit Rueckgaengig
        else
        {
            std::vector<object *> alle = ausgewaehlteObjekte();
            Vector3D mn, mx;
            grenzenVon(alle, mn, mx);
            const Vector3D mitte = (mn + mx) * 0.5;

            // Groesse: Faktor je Achse aus neuer / bisheriger Ausdehnung
            Vector3D faktor(1, 1, 1);
            if (zeile == 3)
            {
                Vector3D alt = mx - mn;
                // flache Objekte (Ebene, Kreisflaeche, 2D-Ring): Ausdehnung 0 in einer Richtung. Diese Achse
                // laesst sich nicht skalieren und zaehlt nicht mit; vorher wurde jede Eingabe abgelehnt,
                // weil dort 0 steht (Groesse > 0 verlangt)
                const double flach = 1e-12;
                if ((alt.x > flach && v.x <= 0) || (alt.y > flach && v.y <= 0) || (alt.z > flach && v.z <= 0))
                {
                    MessageBeep(MB_ICONWARNING);
                    eigenschaftenAnzeigen();
                    return;
                }
                faktor = Vector3D(alt.x > 1e-12 ? v.x / alt.x : 1.0,
                                  alt.y > 1e-12 ? v.y / alt.y : 1.0,
                                  alt.z > 1e-12 ? v.z / alt.z : 1.0);

                // Kugel kann nur gleichmaessig wachsen: die geaenderte Achse gilt fuer alle
                if (alle.size() == 1 && dynamic_cast<Kugel *>(alle[0]))
                {
                    double f1 = faktor.x;
                    if (std::fabs(faktor.y - 1.0) > std::fabs(f1 - 1.0))
                        f1 = faktor.y;
                    if (std::fabs(faktor.z - 1.0) > std::fabs(f1 - 1.0))
                        f1 = faktor.z;
                    faktor = Vector3D(f1, f1, f1);
                }
            }

            aenderungBeginnen(zeile == 0 || zeile >= 4 ? "Verschieben" : zeile == 2 ? "Drehen" : "Größe", alle);
            {
                std::lock_guard<std::mutex> lock(sceneMutex);
                if (zeile == 0)
                    for (object *o : alle)
                        objektVerschieben(o, v - mitte);
                else if (zeile >= 4) // unterster/oberster Punkt an die eingegebene Stelle
                {
                    Vector3D bisher(mitte.x, zeile == 4 ? mn.y : mx.y, mitte.z);
                    for (object *o : alle)
                        objektVerschieben(o, v - bisher);
                }
                else if (zeile == 2)
                    objektDrehwinkelSetzen(selectedObject, v);
                else
                    for (object *o : alle)
                        objektSkalieren(o, faktor, mitte);
            }
            aenderungAbschliessen();
            computeSceneBounds();
        }
    }
    else if (selectedType == SelectedType::LIGHT && selectedLight && lichtIndex(selectedLight) >= 0)
    {
        if (zeile == 0)
        {
            aenderungBeginnen("Licht verschieben", {}, {selectedLight});
            selectedLight->translate(selectedLight->getPosition() - v); // translate verschiebt um -delta
            aenderungAbschliessen();
        }
        else if (richtung)
        {
            if (v.length() < 1e-9)
            {
                MessageBeep(MB_ICONWARNING);
                eigenschaftenAnzeigen();
                return;
            }
            aenderungBeginnen("Lichtrichtung", {}, {selectedLight});
            if (auto *sp = dynamic_cast<SpotLight *>(selectedLight))
                sp->richtung = v.normalized(); // Spotlicht: wohin der Kegel zeigt
            else
                static_cast<DirectionalLight *>(selectedLight)->direction = (-v).normalized(); // gespeichert: zum Licht hin
            aenderungAbschliessen();
        }
        else if (kegel)
        {
            if (!(w[0] > 0.0 && w[0] < 180.0) || w[1] < 0.0 || w[1] > 1.0)
            {
                MessageBoxW(hObjektFenster, L"Kegel: halber Öffnungswinkel 0 bis 180 Grad, weicher Rand 0 bis 1", L"Spotlicht",
                            MB_OK | MB_ICONWARNING);
                eigenschaftenAnzeigen();
                return;
            }
            aenderungBeginnen("Lichtkegel", {}, {selectedLight});
            auto *sp = static_cast<SpotLight *>(selectedLight);
            sp->winkel = w[0];
            sp->weich = w[1];
            aenderungAbschliessen();
            abgleichen(); // Name in der Liste (Winkel)
        }
        else if (kante || radius || leistung)
        {
            auto *a = dynamic_cast<AreaLight *>(selectedLight);
            // Kanten muessen eine Flaeche aufspannen; Radius und Leistung > 0
            bool gueltig = true;
            if (kante)
            {
                Vector3D u = zeile == 2 ? v : a->edgeU, w2 = zeile == 3 ? v : a->edgeV;
                gueltig = u.cross(w2).length() > 1e-12;
            }
            else
                gueltig = w[0] > 0;
            if (!gueltig)
            {
                MessageBeep(MB_ICONWARNING);
                eigenschaftenAnzeigen();
                return;
            }
            aenderungBeginnen(kante ? "Lichtgröße" : radius ? "Radius" : "Leistung", {}, {selectedLight});
            {
                std::lock_guard<std::mutex> lock(sceneMutex);
                if (kante)
                {
                    Vector3D mitte = a->getPosition(); // die Mitte bleibt stehen
                    (zeile == 2 ? a->edgeU : a->edgeV) = v;
                    a->setPosition(mitte);
                }
                else if (radius)
                    static_cast<SphereLight *>(selectedLight)->radius = w[0];
                else if (a)
                    a->power = w[0];
                else if (auto *k = dynamic_cast<SphereLight *>(selectedLight))
                    k->power = w[0];
                else if (auto *m = dynamic_cast<MeshLight *>(selectedLight))
                    m->power = w[0];
                else if (auto *pl = dynamic_cast<PointLight *>(selectedLight))
                    pl->power = w[0];
                else if (auto *rl = dynamic_cast<DirectionalLight *>(selectedLight))
                    rl->irradiance = w[0];
            }
            aenderungAbschliessen();
            abgleichen(); // Name in der Liste (Groesse)
        }
        else
            farbeUebernehmen(f);
    }
    else if (fov && !cam.empty())
    {
        cam[0].fov = std::clamp(w[0], 1.0, 179.0);
        cam[0].update();
    }
    else if ((selectedType == SelectedType::CAMERA || selectedType == SelectedType::LOOKAT) && !cam.empty())
    {
        if (zeile == 0)
            cam[0].position = v;
        else
            cam[0].lookAt = v;
        cam[0].update();
        kameraBahnAbgleichen(cam[0]);
    }

    eigenschaftenAnzeigen();
}

//--------------------------------------------------
// Reiter "Material"
//--------------------------------------------------

static std::vector<HWND> eigenschaftenSteuerelemente()
{
    std::vector<HWND> v = {g.lblName, g.info, g.lblDetails, g.lblZeile1, g.lblZeile2, g.lblZeile3, g.lblZeile4, g.lblZeile5, g.lblZeile6, g.btnFarbe};
    for (HWND h : g.edit)
        v.push_back(h);
    return v;
}

static std::vector<HWND> materialSteuerelemente()
{
    std::vector<HWND> v = {g.lblVorlage, g.cmbVorlage, g.lblTextur, g.editTextur, g.cmbTexturModus, g.btnTexturDatei, g.btnTexturU,
                           g.btnTexturV, g.btnTexturDrehen,
                           g.lblTexturKachel, g.editTexturKachel, g.lblTexturWinkel, g.editTexturWinkel, g.lblTexturVersatz,
                           g.editTexturVersatz[0], g.editTexturVersatz[1], g.btnTexturFest, g.lblTexturBild, g.cmbTexturBild,
                           g.editTexturBild, g.btnTexturBild, g.editTexturStaerke,
                           g.lblTexturRinge, g.editTexturRinge, g.lblTexturNass, g.editTexturNass};
    // Zeile "Kugel (°)" nur bei der Abbildung Kugel: texturKugelSichtbar()
    for (int i = 0; i < MAT_FARBEN; ++i)
    {
        v.push_back(g.lblMatFarbe[i]);
        v.push_back(g.btnMatFarbe[i]);
    }
    for (HWND h : g.editMatFarbe)
        v.push_back(h);
    for (int i = 0; i < MAT_WERTE; ++i)
    {
        v.push_back(g.lblMatWert[i]);
        v.push_back(g.editMatWert[i]);
    }
    return v;
}

static void reiterZeigen(int neu)
{
    const bool wechsel = reiter != neu;
    reiter = neu;
    if (hObjektFenster && wechsel)
        SendMessageW(hObjektFenster, WM_SETREDRAW, FALSE, 0); // ein Neuzeichnen am Ende statt je Element
    for (HWND h : eigenschaftenSteuerelemente())
        ShowWindow(h, reiter == 0 ? SW_SHOW : SW_HIDE);
    for (HWND h : materialSteuerelemente())
        ShowWindow(h, reiter == 1 ? SW_SHOW : SW_HIDE);
    texturKugelSichtbar();
    ShowWindow(g.cmbManipObjekt, SW_HIDE); // Ziel ist die Auswahl (Liste/Ansichten)
    for (HWND h : g.btnManipIcon)
        if (h)
            ShowWindow(h, reiter == 2 ? SW_SHOW : SW_HIDE);
    ShowWindow(g.cmbManipArt, SW_HIDE);
    for (HWND h : {g.lblManipArt, g.lblManipObjekt, g.lblManipZiel, g.lblManipTiefe, g.editManipTiefe,
                   g.btnManipAusfuehren, g.lblManipInfo, g.lblManipKnick, g.editManipKnick, g.lblManipWert, g.editManipWert,
                   g.lblManipAus, g.editManipAus[0], g.editManipAus[1], g.editManipAus[2], g.lblManipModus, g.cmbManipModus,
                   g.lblManipKopien, g.cmbManipKopien, g.lblManipLage, g.cmbManipLage, g.lblManipPinsel, g.cmbManipPinsel})
        ShowWindow(h, reiter == 2 ? SW_SHOW : SW_HIDE);
    if (reiter == 2)
    {
        manipObjekteFuellen();
        manipAnzeigen(); // Rekursionstiefe nur bei Porath
    }
    if (hObjektFenster && wechsel)
        SendMessageW(hObjektFenster, WM_SETREDRAW, TRUE, 0);
    zielKnopfSetzen(); // nur im Reiter "Eigenschaften"
    if (hObjektFenster && wechsel) // Reste des anderen Reiters entfernen
        RedrawWindow(hObjektFenster, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
}

// ausgewaehlte Objekte, deren Material sich aendern laesst (Tree: Holz und Blaetter verschieden)
static std::vector<object *> materialObjekte()
{
    std::vector<object *> v;
    for (object *o : mitVorlagen(ausgewaehlteObjekte()))
        if (!dynamic_cast<Tree *>(o))
            v.push_back(o);
    return v;
}



static bool tooltipAendern(HWND fenster, const wchar_t *text); // nach tooltipFuer

// Tooltips der sechs Eigenschaftszeilen passend zur Auswahl (Objekt, Lichtart, Kamera)
static void zeilenTippsSetzen()
{
    static const wchar_t *const T_MITTE = L"Mitte der Auswahl (Hüllquader). Ändern verschiebt die Auswahl dorthin.";
    static const wchar_t *const T_FARBE = L"Diffus-Farbe (Grundfarbe) der Auswahl: bestimmt, welche Farbe die Fläche im Licht zeigt und\n"
                                          L"an ihre Umgebung weitergibt. Weitere Werte im Reiter \"Material\".";
    static const wchar_t *const T_DREHUNG = L"Drehwinkel um X, Y, Z in Grad (um die Mitte der Auswahl).";
    static const wchar_t *const T_GROESSE = L"Ausdehnung in X, Y, Z (Hüllquader). Ändern skaliert um die Mitte.\n"
                                            L"Wirkt auf Licht nur über die Geometrie; Texturen und Absorption richten sich nach der neuen Größe.";
    static const wchar_t *const T_UNTEN = L"Unterster Punkt der Auswahl (Mitte der Unterseite): Y = tiefste Stelle.\n"
                                          L"Y ändern verschiebt senkrecht, z.B. Y = 0 setzt das Objekt auf den Boden.";
    static const wchar_t *const T_OBEN = L"Oberster Punkt der Auswahl (Mitte der Oberseite): Y = höchste Stelle.\n"
                                         L"Ändern verschiebt das Objekt so, dass dieser Punkt dort liegt.";
    static const wchar_t *const T_LPOS = L"Position des Lichts (Flächenlicht: Mitte des Rechtecks; Richtungslicht: nur das Symbol,\n"
                                         L"ohne Wirkung aufs Bild).";
    static const wchar_t *const T_LFARBE = L"Lichtfarbe: multipliziert alles, was dieses Licht beleuchtet (Werte bis 1).\n"
                                           L"Die Stärke stellt man mit \"Leistung\" bzw. \"Stärke\" ein.";
    static const wchar_t *const T_LEISTUNG_PUNKT = L"Leistung in Watt. Beleuchtung im Abstand d: Leistung / (4 pi d²) – nimmt mit dem\n"
                                                   L"Quadrat des Abstands ab. Faustregel für Helligkeit ~1 im Abstand d: Leistung = 12 · d².\n"
                                                   L"In großen Szenen (Abstände in Hunderten) entsprechend riesige Werte.";
    static const wchar_t *const T_LEISTUNG_FLAECHE = L"Leistung in Watt. Beleuchtung unter der Lampe im Abstand d: Leistung · cos / (pi d²);\n"
                                                     L"leuchtet nur zur Vorderseite (Kante U × Kante V). Helligkeit ~1 im Abstand d: Leistung = 3 · d².\n"
                                                     L"Sichtbare Helligkeit der Fläche selbst: Leistung / (pi · Fläche).";
    static const wchar_t *const T_LEISTUNG_NETZ = L"Leistung in Watt, verteilt auf die ganze Oberfläche des Netzes; Beleuchtung wie beim\n"
                                                  L"Flächenlicht (Leistung · cos / (pi d²)). Größeres Netz bei gleicher Leistung = dunklere Oberfläche.";
    static const wchar_t *const T_STAERKE = L"Stärke des Richtungslichts (Sonne): gleich hell an jeder Stelle, unabhängig vom Abstand.\n"
                                            L"Tageslicht etwa 2,5 bis 3,5; Mondlicht etwa 0,3. Erzeugt harte Schatten.";
    static const wchar_t *const T_RICHTUNG = L"Laufrichtung des Lichts (wohin der Pfeil zeigt). Knopf rechts: auf ein Objekt richten.\n"
                                             L"Flacher Winkel = lange Schatten (Morgen, Abend).";
    static const wchar_t *const T_RADIUS = L"Radius der leuchtenden Kugel: größer = weichere Schatten, aber dunklere Oberfläche\n"
                                           L"(gleiche Leistung auf mehr Fläche). In Spiegelungen als heller Fleck sichtbar.";
    static const wchar_t *const T_KANTE_U = L"Erste Kante des Rechtecks (Richtung und Länge). Leuchtrichtung = Kante U × Kante V.\n"
                                            L"Größere Fläche = weichere Schatten.";
    static const wchar_t *const T_KANTE_V = L"Zweite Kante des Rechtecks. Vertauschen von U und V dreht die Leuchtrichtung um.";
    static const wchar_t *const T_KPOS = L"Position der Kamera (Auge).";
    static const wchar_t *const T_LOOKAT = L"Punkt, auf den die Kamera schaut (Bildmitte). Knopf rechts: auf ein Objekt richten.";
    static const wchar_t *const T_FOV = L"Öffnungswinkel in Grad (senkrecht): klein = Tele (Ausschnitt, flache Perspektive),\n"
                                        L"groß = Weitwinkel. Ändert nicht die Helligkeit.";

    const wchar_t *t[6] = {T_MITTE, T_FARBE, T_DREHUNG, T_GROESSE, T_UNTEN, T_OBEN};
    if (selectedType == SelectedType::LIGHT && selectedLight)
    {
        t[0] = T_LPOS;
        t[1] = T_LFARBE;
        t[2] = t[3] = t[5] = L"";
        t[4] = L"";
        if (dynamic_cast<SpotLight *>(selectedLight))
            t[2] = L"Richtung, in die der Lichtkegel zeigt (muss nicht normiert sein). Knopf rechts: auf ein Objekt richten.",
            t[3] = L"1. Feld: halber Öffnungswinkel des Kegels in Grad (30 = Kegel von 60°; Taschenlampe 10–15, Bühne 15–25).\n"
                   L"2. Feld: weicher Rand, Anteil des Winkels mit sanftem Übergang (0 = harte Kante, 0,25 = üblich, 1 = weich von der Mitte an).",
            t[4] = T_LEISTUNG_PUNKT;
        else if (dynamic_cast<DirectionalLight *>(selectedLight))
            t[2] = T_RICHTUNG, t[4] = T_STAERKE;
        else if (dynamic_cast<AreaLight *>(selectedLight))
            t[2] = T_KANTE_U, t[3] = T_KANTE_V, t[4] = T_LEISTUNG_FLAECHE;
        else if (dynamic_cast<SphereLight *>(selectedLight))
            t[2] = T_RADIUS, t[4] = T_LEISTUNG_PUNKT;
        else if (dynamic_cast<MeshLight *>(selectedLight))
            t[4] = T_LEISTUNG_NETZ;
        else if (dynamic_cast<PointLight *>(selectedLight))
            t[4] = T_LEISTUNG_PUNKT;
    }
    else if ((selectedType == SelectedType::CAMERA || selectedType == SelectedType::LOOKAT) && !cam.empty())
    {
        t[0] = T_KPOS;
        t[1] = T_LOOKAT;
        t[2] = T_FOV;
        t[3] = t[4] = t[5] = L"";
    }
    static const wchar_t *gesetzt[6] = {};
    HWND labels[6] = {g.lblZeile1, g.lblZeile2, g.lblZeile3, g.lblZeile4, g.lblZeile5, g.lblZeile6};
    for (int z = 0; z < 6; ++z)
    {
        if (gesetzt[z] == t[z])
            continue; // nur bei Aenderung
        if (!tooltipAendern(labels[z], t[z]))
            continue; // Tooltips noch nicht angelegt: spaeter erneut
        gesetzt[z] = t[z];
        for (int k = 0; k < 3; ++k)
            tooltipAendern(g.edit[z * 3 + k], t[z]);
    }
}

// ---------- Ziel-Knopf: Kamera (Zeile "LookAt") bzw. Richtungslicht (Zeile "Richtung") auf ein Objekt richten ----------
static void zielKnopfSetzen()
{
    if (!g.btnZiel)
        return;
    int zeile = -1;
    if ((selectedType == SelectedType::CAMERA || selectedType == SelectedType::LOOKAT) && !cam.empty())
        zeile = 1; // LookAt (dort steht sonst das leere Farbfeld)
    else if (selectedType == SelectedType::LIGHT &&
             (dynamic_cast<DirectionalLight *>(selectedLight) || dynamic_cast<SpotLight *>(selectedLight)))
        zeile = 2; // Richtung
    const bool zeigen = zeile >= 0 && reiter == 0 && !scene.empty();
    if (zeile != zielZeile && zeile >= 0)
        SetWindowPos(g.btnZiel, HWND_TOP, zielKnopfX, zeilenY[zeile], ZEILE_H, ZEILE_H, SWP_NOACTIVATE);
    zielZeile = zeile;
    ShowWindow(g.btnZiel, zeigen ? SW_SHOWNA : SW_HIDE);
    if (g.btnFarbe) // bei der Kamera steht der Knopf an Stelle des (dort leeren) Farbfelds
        ShowWindow(g.btnFarbe, (reiter == 0 && zeile != 1) ? SW_SHOWNA : SW_HIDE);
}

// Mitte eines Objekts (Huellquader)
static Vector3D objektMitte(object *o)
{
    Vector3D mn = o->position, mx = o->position;
    o->getWorldAABB(mn, mx);
    return (mn + mx) * 0.5;
}

static void zielMenueZeigen()
{
    // Objekte der Szene, Gruppen mit ihren Teilen (eingerueckt); ausgeblendete auch
    std::vector<object *> ziele;
    HMENU menue = CreatePopupMenu();
    std::function<void(object *, const std::wstring &, int)> eintrag = [&](object *o, const std::wstring &text, int tiefe)
    {
        if (!o || dynamic_cast<VolumenBox *>(o))
            return;
        ziele.push_back(o);
        std::wstring t = std::wstring(size_t(tiefe) * 4, L' ') + text;
        if (o->versteckt)
            t += L"  (ausgeblendet)";
        AppendMenuW(menue, MF_STRING, UINT_PTR(ziele.size()), t.c_str());
        if (auto *gr = dynamic_cast<Gruppe *>(o))
            for (size_t n = 0; n < gr->kinder.size() && tiefe < 3; ++n)
                eintrag(gr->kinder[n].get(), objektName(gr->kinder[n].get(), n), tiefe + 1);
    };
    for (size_t i = 0; i < scene.size(); ++i)
        eintrag(scene[i].get(), objektName(scene[i].get(), i), 0);
    if (ziele.empty())
    {
        DestroyMenu(menue);
        return;
    }
    RECT r;
    GetWindowRect(g.btnZiel, &r);
    int wahl = int(TrackPopupMenu(menue, TPM_RETURNCMD | TPM_RIGHTALIGN | TPM_TOPALIGN | TPM_NONOTIFY, r.right, r.bottom, 0,
                                  hObjektFenster, nullptr));
    DestroyMenu(menue);
    if (wahl <= 0 || wahl > int(ziele.size()))
        return;
    const Vector3D ziel = objektMitte(ziele[size_t(wahl - 1)]);

    if ((selectedType == SelectedType::CAMERA || selectedType == SelectedType::LOOKAT) && !cam.empty())
    {
        if ((ziel - cam[0].position).length() < 1e-9)
        {
            MessageBeep(MB_ICONWARNING); // Kamera steht in der Objektmitte
            return;
        }
        cam[0].lookAt = ziel;
        cam[0].update();
        kameraBahnAbgleichen(cam[0]);
    }
    else if (auto *sp = dynamic_cast<SpotLight *>(selectedLight))
    {
        Vector3D zumZiel = ziel - sp->position;
        if (zumZiel.length() < 1e-9)
        {
            MessageBeep(MB_ICONWARNING);
            return;
        }
        aenderungBeginnen("Lichtrichtung", {}, {selectedLight});
        sp->richtung = zumZiel.normalized();
        aenderungAbschliessen();
    }
    else if (auto *d = dynamic_cast<DirectionalLight *>(selectedLight))
    {
        // Licht laeuft vom Symbol zum Objekt; gespeichert wird die Richtung ZUM Licht
        Vector3D zumLicht = richtungslichtSymbol(d) - ziel;
        if (zumLicht.length() < 1e-9)
        {
            MessageBeep(MB_ICONWARNING);
            return;
        }
        aenderungBeginnen("Lichtrichtung", {}, {selectedLight});
        d->direction = zumLicht.normalized();
        aenderungAbschliessen();
    }
    eigenschaftenAnzeigen();
}

// ---------- Ein-/Ausblenden ueber die Haken in der Objektliste ----------
static object *eintragsObjekt(LPARAM wert)
{
    if (wert >= 0 && wert < EINTRAG_LICHT && size_t(wert) < scene.size())
        return scene[size_t(wert)].get();
    if (wert >= EINTRAG_KIND && size_t(wert - EINTRAG_KIND) < eintraegeKinder.size())
        return eintraegeKinder[size_t(wert - EINTRAG_KIND)].first;
    return nullptr;
}

// Haken setzen (Objekt) bzw. entfernen (Wurzeln, Lichter, Kamera: kein Kaestchen)
static void eintragHakenSetzen(HTREEITEM e, const object *o)
{
    TVITEMW item = {};
    item.mask = TVIF_STATE;
    item.hItem = e;
    item.stateMask = TVIS_STATEIMAGEMASK;
    item.state = o ? INDEXTOSTATEIMAGEMASK(o->versteckt ? 1 : 2) : 0;
    SendMessageW(hTreeObjects, TVM_SETITEMW, 0, reinterpret_cast<LPARAM>(&item));
}

static void sichtbarkeitAusEintrag(HTREEITEM e)
{
    TVITEMW item = {};
    item.mask = TVIF_PARAM | TVIF_STATE;
    item.stateMask = TVIS_STATEIMAGEMASK;
    item.hItem = e;
    if (!SendMessageW(hTreeObjects, TVM_GETITEMW, 0, reinterpret_cast<LPARAM>(&item)))
        return;
    object *o = eintragsObjekt(item.lParam);
    const UINT bild = (item.state & TVIS_STATEIMAGEMASK) >> 12;
    if (!o)
    {
        if (bild != 0) // kein Objekt: Kaestchen wieder weg
            eintragHakenSetzen(e, nullptr);
        return;
    }
    const bool versteckt = bild == 1;
    if (o->versteckt == versteckt)
        return;
    o->versteckt = versteckt;
    ++gVersteckAenderungen;
    szeneGeaendertMarkieren();
    InvalidateRect(hTreeObjects, nullptr, FALSE); // ausgeblendete grau
}


// ---------- Reiter "Manipulation": Netz-Operationen (netz_operationen.h) ----------
struct ManipArt
{
    const wchar_t *name;
    const wchar_t *info;
};
static const ManipArt MANIP_ARTEN[] = {
    {L"Objekte in 3D-Ebenen umwandeln",
     L"Zerlegt das Objekt in seine ebenen Flächen: jede Fläche (zusammenhängende Dreiecke in einer Ebene, z.B. eine "
     L"Würfelseite) wird ein eigenes Objekt, das Original wird gelöscht.\n"
     L"Wozu: einzelne Seiten verschieben, einfärben, mit eigener Textur versehen oder löschen, z.B. aus einem Würfel "
     L"einen offenen Raum oder eine Kiste ohne Deckel machen."},
    {L"Delaunay-Flipping für Flächen",
     L"Optimiert die Dreiecke jeder ebenen Fläche: die gemeinsame Kante zweier Dreiecke wird umgeklappt, wenn die "
     L"gegenüberliegende Ecke im Umkreis des anderen Dreiecks liegt (bis keine Kante mehr umklappt). Die Form bleibt exakt gleich.\n"
     L"Wozu: lange, spitze Dreiecke (Splitter) werden zu möglichst gleichmäßigen. Das gibt eine schönere Loop-Glättung, "
     L"weniger Schattierungsfehler bei glatten Normalen und Texturen ohne Verzerrung. Gut nach der Porath-Triangulation "
     L"oder bei importierten OBJ-Dateien."},
    {L"Porath-Triangulation für Flächen",
     L"Trianguliert jede ebene Fläche neu: vom Mittelpunkt der Fläche ein Fächer zu den Randecken (nach Winkel sortiert), "
     L"dann wird jedes Dreieck \"Rekursionstiefe\"-mal von seinem Mittelpunkt aus in drei geteilt. Die Form bleibt gleich, "
     L"die Fläche bekommt mehr Ecken (je Stufe dreimal so viele Dreiecke).\n"
     L"Wozu: Vorbereitung für Glättung und Verformung (mehr Punkte = feinere Rundung bei Loop). Tipp: danach "
     L"Delaunay-Flipping, das die spitzen Dreiecke der Mittelpunkt-Teilung ausgleicht. Nicht konvexe Flächen: nur die Teilung."},
    {L"Glatt schattieren (Normalen interpolieren)",
     L"Gibt jeder Dreiecksecke eine eigene Normale: das Mittel der Flächennormalen aller Dreiecke an dieser Stelle "
     L"(gewichtet mit dem Eckwinkel). Der Renderer interpoliert sie über jedes Dreieck (Phong-Schattierung): die "
     L"Helligkeit läuft weich über die Kanten, die Form selbst bleibt unverändert. Nachbarn, die um mehr als den "
     L"Knickwinkel abweichen, zählen nicht mit: dort bleibt die Kante scharf. 0° entfernt die Ecknormalen wieder.\n"
     L"Für alle Objekte aus Dreiecken (Würfel, Körper, OBJ, Terrain, Baum, 3D-Text, Pfeil), einzeln, als Gruppe oder "
     L"alle auf einmal. Kugeln und Stäbe sind ohnehin glatt (exakt gerechnet)."},
    {L"Loop-Triangulation (glätten)",
     L"Glättet das ganze Objekt mit einem Schritt Loop-Subdivision: jedes Dreieck wird zu vier, alle Ecken rücken zum "
     L"gewichteten Mittel ihrer Nachbarn, Kanten und Ecken werden rund; dazu glatte Normalen für weiche Schattierung.\n"
     L"Wozu: organische, weiche Formen aus groben Netzen, z.B. Würfel zu Kugel-ähnlich, kantige OBJ-Modelle weicher. "
     L"Mehrmals ausführen = runder (×4 Dreiecke je Schritt). Das Objekt schrumpft dabei etwas; offene Ränder bleiben auf ihrer Linie."},
    {L"Dicke für Flächen",
     L"Gibt Flächen eine Dicke: eine zweite Lage im Abstand \"Dicke\" in Normalenrichtung, die offenen Ränder werden "
     L"mit Seitenwänden geschlossen. Die Wand ist überall gleich dick (an Kanten und Ecken entsprechend versetzt). "
     L"Positive Dicke nach außen (in Normalenrichtung), negative nach innen.\n"
     L"Wozu: aus einer Ebene eine Platte machen, aus einzelnen Flächen (\"Objekte in 3D-Ebenen umwandeln\") Wände mit "
     L"Stärke, aus einer Vase ohne Boden ein Gefäß mit Wandstärke (für Glas wichtig: Innen- und Außenfläche). "
     L"Geschlossene Körper wie ein Würfel werden hohl."},
    {L"Pyramide/Mulde aus Ebene",
     L"Setzt in die Mitte jeder ebenen Fläche (Schwerpunkt) einen neuen Punkt, um \"Höhe\" in Normalenrichtung versetzt, "
     L"und fächert die Fläche von dort aus zu jeder Randkante in Dreiecke auf.\n"
     L"Höhe > 0: eine Spitze nach außen (Würfel mit Pyramiden auf jeder Seite, Stacheln, Dachformen), Höhe < 0: eine "
     L"Mulde nach innen, Höhe 0: flacher Fächer (mehr Dreiecke, gleiche Form; z.B. vor Loop oder Glatt schattieren)."},
    {L"Zufällige Verteilung",
     L"Verteilt die ausgewählten Objekte zufällig in einem Quader der Größe \"Ausdehnung X / Y / Z\" um die Mitte der "
     L"Auswahl. Eine Ausdehnung 0 lässt diese Koordinate unverändert: Y = 0 verteilt nur in der Ebene, jedes Objekt "
     L"behält seine Höhe (Steine bleiben auf dem Boden). Die Objekte werden möglichst ohne Überlappung gesetzt. Ist "
     L"genau eine Gruppe ausgewählt (z.B. \"Steine\"), werden ihre Teile verteilt. Jedes Ausführen ergibt eine neue "
     L"Verteilung; Rückgängig mit Strg+Z."},
    {L"Zufällige Drehung",
     L"Dreht jedes ausgewählte Objekt um seine eigene Mitte zufällig um X, Y und Z, jeweils zwischen −Winkel und +Winkel "
     L"(0 = um diese Achse nicht drehen, 180 = beliebig). Die Unterkante bleibt auf ihrer Höhe: Steine, Bäume oder Kisten "
     L"bleiben auf dem Boden. Ist genau eine Gruppe ausgewählt (z.B. \"Steine\"), wird jedes ihrer Teile gedreht.\n"
     L"Beispiele: 0 / 180 / 0 = nur um die senkrechte Achse (Bäume, Häuser, Figuren); 180 / 180 / 180 = ganz zufällig "
     L"(Steine, Trümmer); 8 / 180 / 8 = leicht schief (Pfähle, Grabsteine). Jedes Ausführen ergibt neue Winkel."},
    {L"Zufällige Größen",
     L"Skaliert jedes ausgewählte Objekt mit einem zufälligen Faktor zwischen \"min\" und \"max\" (1 = Originalgröße, "
     L"0,5 = halb so groß, 2 = doppelt). \"Ungleichmäßig\" 0 = Form bleibt (alle Richtungen gleich), bis 1 = jede "
     L"Richtung eigener Zufall (z.B. schlanke und breite Bäume). Die Unterkante bleibt auf ihrer Höhe und die Mitte an "
     L"ihrem Platz. Ist genau eine Gruppe ausgewählt, wird jedes ihrer Teile skaliert.\n"
     L"Beispiel: 100 Bäume vervielfältigen, zufällig verteilen, zufällig drehen (0/180/0), zufällige Größen 0,7 bis 1,3."},
    {L"Auf Oberfläche absetzen",
     L"Lässt jedes ausgewählte Objekt senkrecht auf die Fläche darunter fallen (Terrain, Boden, Tisch, Dach ...) und "
     L"stellt es mit seiner Unterseite darauf; auf Wunsch neigt es sich mit dem Hang. Als Untergrund zählen nur Objekte, "
     L"die deutlich größer sind als das abgesetzte (\"Mindestgröße Fläche\", z.B. 2 = doppelt so breit) – so landet "
     L"Gras auf dem Terrain und nicht auf dem Nachbarbüschel. Ohne Fläche darunter: auf den Boden (Höhe 0).\n"
     L"Einsinken: Anteil der eigenen Höhe, um den es in den Boden einsinkt (Gras 0,05, Steine 0,2). Neigung: 0 = bleibt "
     L"senkrecht (Bäume, Häuser), 1 = steht senkrecht auf dem Hang (Gras, Steine). Ist genau eine Gruppe ausgewählt, "
     L"wird jedes ihrer Teile abgesetzt."},
    {L"Vervielfältigen",
     L"Legt die ausgewählten Objekte (und ein gewähltes Licht) \"Anzahl\"-mal als Kopie an, jede Kopie um den "
     L"\"Versatz je Kopie\" weiter verschoben: eine Reihe (Zaunpfähle, Säulen, Stuhlreihe, Treppenstufen mit Versatz in "
     L"X und Y). Alle Kopien zusammen sind ein Schritt für Strg+Z und danach ausgewählt – z.B. gleich weiter mit "
     L"\"Zufällige Verteilung\" oder \"Zufällige Drehung\". Für ein Raster: erst eine Reihe in X, dann alle "
     L"auswählen und in Z vervielfältigen."},
    {L"Objekte addieren (vereinigen)",
     L"Verschmilzt alle ausgewählten Objekte zu einem einzigen Körper: Teile, die ineinander stecken, werden "
     L"verbunden, innere Flächen verschwinden (wichtig für Glas und Wasser). Das Ergebnis heißt wie das Hauptobjekt "
     L"und behält die Materialien der einzelnen Flächen.\n"
     L"Geht mit Würfeln, Wänden, Körpern, Steinen, Rotationsobjekten, OBJ-Netzen; Kugeln und Stäbe werden dafür in "
     L"Dreiecke umgewandelt. Die Körper sollten geschlossen sein. Ein Schritt für Strg+Z."},
    {L"Objekte subtrahieren (ausschneiden)",
     L"Schneidet aus dem größten ausgewählten Objekt (z.B. der Hauswand) alle anderen ausgewählten heraus (z.B. "
     L"Türrahmen, Fensterrahmen, eine Kugel für ein rundes Loch).\n"
     L"Öffnung: es wird der Umriss-Quader des Schneiders ausgeschnitten, durch die ganze Wand hindurch – der Rahmen "
     L"bleibt stehen und passt genau in das Loch (auch bei gedrehten Rahmen).\n"
     L"Form: genau die Form des Schneiders (z.B. Kugel = runde Mulde); der Schneider bleibt oder wird entfernt.\n"
     L"Vorgehen Tür: Wand anklicken, mit Strg den Türrahmen dazu, \"Objekte subtrahieren\", Öffnung, Ausführen."},
    {L"Terrain unter Objekt absenken",
     L"Senkt das Terrain unter den anderen ausgewählten Objekten (Wegbett, Teich, Fundament, Platz) bis knapp unter deren "
     L"Unterseite ab, mit weichem Übergang am Rand. So schaut das Gelände nirgends mehr durch.\n"
     L"Auswahl: das Terrain (das größte gewählte Netz) und mit Strg die Objekte darauf. Abstand: so weit liegt das "
     L"Terrain danach unter der Unterseite. Rand: Breite des sanften Übergangs. Angleichen 1: Mulden unter dem Objekt "
     L"werden auch aufgefüllt (ebener Platz), 0: nur absenken. Ein Schritt für Strg+Z."},
    {L"Terrain-Pinsel",
     L"Formt das Terrain mit der Maus: Terrain auswählen, Art wählen (Anheben, Absenken, Glätten, Ebnen) und "
     L"\"Ausführen\" schaltet den Pinsel ein. Dann in einer Ansicht mit gedrückter linker Maustaste über das Terrain "
     L"ziehen; jeder Strich ist ein Schritt für Strg+Z. Esc oder erneut \"Ausführen\" beendet den Pinsel.\n"
     L"Radius: Größe des Pinsels in Einheiten. Stärke 0 bis 1: wie stark jeder Schritt wirkt (Anheben/Absenken: bis "
     L"10 % des Radius je Schritt). Glätten gleicht Buckel aus, Ebnen zieht auf die Höhe am Startpunkt des Strichs."},
    {L"Fell-Imitation",
     L"Legt mehrere Hüllen um jedes ausgewählte Teil (auch alle Teile einer Gruppe, z.B. eine ganze Katze): jede Hülle "
     L"etwas weiter außen, mit einem Bild aus Haarspitzen, das alles andere ausschneidet, und der Fellfarbe (nach innen "
     L"dunkler). Zusammen wirkt das wie Fell mit flauschigem Umriss.\n"
     L"Länge: wie weit die Haare abstehen (Einheiten). Hüllen: Anzahl (mehr = feiner, langsamer; 6 bis 12). Dichte: "
     L"Haare je cm². Hängen 0 bis 1: die Spitzen fallen nach unten.\n"
     L"Farbe: die Textur des Teils (z.B. ein Fellbild) – vorher zuweisen; ohne Textur die Grundfarbe. Die Hüllen folgen "
     L"ihrem Teil bei jeder Änderung und in der Animation. Die Bilder werden im Ordner \"fell_imitation\" gespeichert. "
     L"Ein Schritt für Strg+Z; zum Entfernen die Gruppe \"Fell: …\" löschen."},
};
constexpr int ANZAHL_MANIP_ARTEN = int(sizeof(MANIP_ARTEN) / sizeof(MANIP_ARTEN[0]));
static std::wstring manipSymbolTipps[24]; // Tooltips der Symbole (Name + erster Satz der Beschreibung)
constexpr int MANIP_GLATT = 3, MANIP_LOOP = 4, MANIP_DICKE = 5, MANIP_FAECHERN = 6, MANIP_VERTEILEN = 7,
              MANIP_DREHEN = 8, MANIP_GROESSEN = 9, MANIP_ABSETZEN = 10, MANIP_VIELFACH = 11, MANIP_ADDIEREN = 12,
              MANIP_SUBTRAHIEREN = 13, MANIP_TERRAIN = 14, MANIP_PINSEL = 15, MANIP_FELL = 16; // Reihenfolge in MANIP_ARTEN
static void objektfensterLayout(int w, int h);

// Material-Reiter: Zeile "Kugel (°)" nur bei der Abbildung Kugel
static bool texturKugelZeile = false;
static void texturKugelSichtbar()
{
    const bool zeigen = reiter == 1 && texturKugelZeile;
    for (HWND h : {g.lblTexturKugel, g.editTexturKugel[0], g.editTexturKugel[1], g.editTexturKugel[2]})
        if (h && ((GetWindowLongW(h, GWL_STYLE) & WS_VISIBLE) != 0) != zeigen)
            ShowWindow(h, zeigen ? SW_SHOW : SW_HIDE);
}

static std::vector<object *> manipObjekte; // Eintraege der Objekt-Auswahl

// Ziel der Aktion = Auswahl; hier nur angezeigt
static void manipObjekteFuellen()
{
    if (!g.lblManipZiel)
        return;
    const int art = int(SendMessageW(g.cmbManipArt, CB_GETCURSEL, 0, 0));
    std::vector<object *> sel = ausgewaehlteObjekte();
    std::wstring t;
    if (sel.empty())
        t = art == MANIP_GLATT ? L"nichts ausgewählt (dann: alle Objekte)" : L"nichts ausgewählt – Objekt anklicken";
    else if (sel.size() == 1)
    {
        int i = objektIndex(sel[0]);
        t = objektName(sel[0], size_t(i >= 0 ? i : 0));
    }
    else
        t = std::to_wstring(sel.size()) + L" Objekte ausgewählt";
    if (art == MANIP_SUBTRAHIEREN && sel.size() >= 2) // Grundkoerper = groesstes Objekt
    {
        object *grund = nullptr;
        double groesste = -1;
        for (object *o : sel)
        {
            Vector3D mn, mx;
            const double d = GpuAnsichten::objektGrenzen(o, mn, mx) ? (mx - mn).length() : 0.0;
            if (d > groesste)
                groesste = d, grund = o;
        }
        t = objektName(grund, size_t(std::max(0, objektIndex(grund)))) + L" − " +
            (sel.size() == 2 ? objektName(sel[sel[0] == grund ? 1 : 0], size_t(std::max(0, objektIndex(sel[sel[0] == grund ? 1 : 0]))))
                             : std::to_wstring(sel.size() - 1) + L" Objekte");
    }
    textSetzen(g.lblManipZiel, t);
}

// Grenzen aller Objekte der Szene (wie in den Ansichten)
static bool szenenGrenzen(Vector3D &mn, Vector3D &mx)
{
    std::vector<object *> alle;
    for (auto &o : scene)
        alle.push_back(o.get());
    return grenzenVon(alle, mn, mx);
}

// 1, 2 oder 5 mal Zehnerpotenz (fuer Vorgabewerte)
static double rundeSchoen(double v)
{
    if (!(v > 1e-9))
        return 1.0;
    const double basis = std::pow(10.0, std::floor(std::log10(v)));
    for (double f : {1.0, 2.0, 5.0})
        if (basis * f >= v * 0.75)
            return basis * f;
    return basis * 10.0;
}

static std::wstring szenenGroesseText()
{
    Vector3D mn, mx;
    if (!szenenGrenzen(mn, mx))
        return L"Szene: leer";
    std::wstring t = L"Größe der Szene: X " + zahlText(mx.x - mn.x, 2) + L" × Y " + zahlText(mx.y - mn.y, 2) + L" × Z " +
                     zahlText(mx.z - mn.z, 2) + L"  (von " + zahlText(mn.x, 2) + L" / " + zahlText(mn.y, 2) + L" / " +
                     zahlText(mn.z, 2) + L" bis " + zahlText(mx.x, 2) + L" / " + zahlText(mx.y, 2) + L" / " +
                     zahlText(mx.z, 2) + L")";
    std::vector<object *> sel = ausgewaehlteObjekte();
    Vector3D a, b;
    if (!sel.empty() && grenzenVon(sel, a, b))
        t += L"\nAuswahl: X " + zahlText(b.x - a.x, 2) + L" × Y " + zahlText(b.y - a.y, 2) + L" × Z " +
             zahlText(b.z - a.z, 2) + L", Mitte " + zahlText((a.x + b.x) * 0.5, 2) + L" / " + zahlText((a.y + b.y) * 0.5, 2) +
             L" / " + zahlText((a.z + b.z) * 0.5, 2);
    return t;
}

// Ziele der Zufalls-Aktionen und des Absetzens: ist genau eine Gruppe gewaehlt, wirken sie auf ihre Teile
// (Steine, Grasbueschel) oder auf die Gruppe als Ganzes. Gruppen mit Untergruppen sind Gelenkketten (Figur, Tier):
// immer als Ganzes, sonst zerfielen sie. Bei anderen Gruppen wird einmal nachgefragt und die Antwort gemerkt.
static std::map<const Gruppe *, bool> gruppeTeileEinzeln;
static std::vector<object *> manipZiele(const std::vector<object *> &oben, Gruppe *&gruppe, bool &abbruch)
{
    gruppe = nullptr;
    abbruch = false;
    if (oben.size() != 1)
        return oben;
    auto *gr = dynamic_cast<Gruppe *>(oben[0]);
    if (!gr || gr->kinder.size() < 2)
        return oben;
    for (auto &k : gr->kinder)
        if (dynamic_cast<Gruppe *>(k.get()))
            return oben; // Gelenkkette: als Ganzes
    bool einzeln;
    auto it = gruppeTeileEinzeln.find(gr);
    if (it != gruppeTeileEinzeln.end())
        einzeln = it->second;
    else
    {
        std::wstring frage = L"Die Gruppe \"" + ausUtf8(gr->name.empty() ? std::string("Gruppe") : gr->name) + L"\" hat " +
                             std::to_wstring(gr->kinder.size()) +
                             L" Teile.\n\nJa: jedes Teil einzeln (z.B. Steine, Grasbüschel, Bäume)\n"
                             L"Nein: die Gruppe als Ganzes (z.B. ein Möbelstück aus mehreren Teilen)\n\n"
                             L"Die Antwort gilt für diese Gruppe, bis das Programm beendet wird.";
        const int a = MessageBoxW(hObjektFenster, frage.c_str(), L"Manipulation", MB_YESNOCANCEL | MB_ICONQUESTION);
        if (a == IDCANCEL)
        {
            abbruch = true;
            return {};
        }
        einzeln = a == IDYES;
        gruppeTeileEinzeln[gr] = einzeln;
    }
    if (!einzeln)
        return oben;
    gruppe = gr;
    std::vector<object *> teile;
    for (auto &k : gr->kinder)
        teile.push_back(k.get());
    return teile;
}

// Zufaellige Verteilung der Auswahl (bzw. der Teile einer einzelnen ausgewaehlten Gruppe)
static void manipVerteilen()
{
    if (!gSzeneAendern)
        return;
    double aus[3];
    for (int i = 0; i < 3; ++i)
        if (!zahlLesen(g.editManipAus[i], aus[i]) || aus[i] < 0)
        {
            MessageBoxW(hObjektFenster, L"Ausdehnung X, Y, Z: Zahlen ab 0 (0 = diese Richtung nicht verteilen)",
                        L"Zufällige Verteilung", MB_OK | MB_ICONWARNING);
            return;
        }
    std::vector<object *> oben = ausgewaehlteObjekte();
    Gruppe *gruppe = nullptr;
    bool abbruch = false;
    std::vector<object *> teile = manipZiele(oben, gruppe, abbruch);
    if (abbruch)
        return;
    if (teile.size() < 2 || (aus[0] <= 0 && aus[1] <= 0 && aus[2] <= 0))
    {
        MessageBoxW(hObjektFenster, aus[0] <= 0 && aus[1] <= 0 && aus[2] <= 0
                                        ? L"Mindestens eine Ausdehnung muss größer als 0 sein."
                                        : L"Bitte mindestens zwei Objekte auswählen (Strg- oder Shift-Klick in der Liste) "
                                          L"oder eine Gruppe, deren Teile verteilt werden sollen.",
                    L"Zufällige Verteilung", MB_OK | MB_ICONINFORMATION);
        return;
    }
    Vector3D gmn, gmx;
    grenzenVon(teile, gmn, gmx);
    const Vector3D mitte = (gmn + gmx) * 0.5;

    // Mitte und Radius (halbe Diagonale, nur in den verteilten Richtungen) je Objekt
    struct Teil
    {
        object *o;
        Vector3D alt, neu;
        double r;
    };
    std::vector<Teil> liste;
    for (object *o : teile)
    {
        Vector3D mn, mx;
        if (!GpuAnsichten::objektGrenzen(o, mn, mx))
            mn = mx = o->position;
        const Vector3D h = (mx - mn) * 0.5;
        const double r = std::sqrt((aus[0] > 0 ? h.x * h.x : 0) + (aus[1] > 0 ? h.y * h.y : 0) + (aus[2] > 0 ? h.z * h.z : 0));
        liste.push_back({o, (mn + mx) * 0.5, Vector3D(), r});
    }
    // groesste zuerst setzen, damit sie noch Platz finden
    std::vector<size_t> reihenfolge(liste.size());
    for (size_t i = 0; i < reihenfolge.size(); ++i)
        reihenfolge[i] = i;
    std::sort(reihenfolge.begin(), reihenfolge.end(), [&](size_t a, size_t b) { return liste[a].r > liste[b].r; });

    std::mt19937 rng(unsigned(GetTickCount()) ^ 0x9E3779B9u);
    std::uniform_real_distribution<double> gleich(-0.5, 0.5);
    auto abstand = [&](const Vector3D &a, const Vector3D &b)
    {
        const double dx = aus[0] > 0 ? a.x - b.x : 0, dy = aus[1] > 0 ? a.y - b.y : 0, dz = aus[2] > 0 ? a.z - b.z : 0;
        return std::sqrt(dx * dx + dy * dy + dz * dz);
    };
    std::vector<size_t> gesetzt;
    size_t ueberlappend = 0;
    for (size_t i : reihenfolge)
    {
        Teil &t = liste[i];
        Vector3D bester = t.alt;
        double besteLuecke = -1e300;
        for (int versuch = 0; versuch < 80; ++versuch)
        {
            const Vector3D p(aus[0] > 0 ? mitte.x + gleich(rng) * aus[0] : t.alt.x,
                             aus[1] > 0 ? mitte.y + gleich(rng) * aus[1] : t.alt.y,
                             aus[2] > 0 ? mitte.z + gleich(rng) * aus[2] : t.alt.z);
            double luecke = 1e300;
            for (size_t j : gesetzt)
                luecke = std::min(luecke, abstand(p, liste[j].neu) - liste[j].r - t.r);
            if (luecke > besteLuecke)
            {
                besteLuecke = luecke;
                bester = p;
            }
            if (luecke >= 0)
                break;
        }
        ueberlappend += besteLuecke < 0;
        t.neu = bester;
        gesetzt.push_back(i);
    }

    aenderungBeginnen("Zufällige Verteilung", oben);
    gSzeneAendern([&]
                  {
        for (Teil &t : liste)
            t.o->translate(t.neu - t.alt);
        if (gruppe)
            gruppe->grenzenAktualisieren(); });
    aenderungAbschliessen();
    abgleichen();
    textSetzen(g.lblManipInfo, std::to_wstring(liste.size()) + L" Objekte zufällig verteilt in " + zahlText(aus[0], 2) +
                                   L" × " + zahlText(aus[1], 2) + L" × " + zahlText(aus[2], 2) + L" um " +
                                   zahlText(mitte.x, 2) + L" / " + zahlText(mitte.y, 2) + L" / " + zahlText(mitte.z, 2) +
                                   (ueberlappend ? L"; " + std::to_wstring(ueberlappend) +
                                                       L" davon ohne freien Platz (überlappen evtl.) – Ausdehnung vergrößern"
                                                 : std::wstring(L"; ohne Überlappung")) +
                                   L".\nNochmals Ausführen = neue Verteilung, Strg+Z = rückgängig.\n\n" + szenenGroesseText());
}

// Zufaellige Drehung jedes Objekts um seine Mitte (bzw. der Teile einer einzelnen ausgewaehlten Gruppe)
static void manipZufallDrehen()
{
    if (!gSzeneAendern)
        return;
    double max[3];
    for (int i = 0; i < 3; ++i)
        if (!zahlLesen(g.editManipAus[i], max[i]) || max[i] < 0 || max[i] > 360)
        {
            MessageBoxW(hObjektFenster, L"Max. Winkel X, Y, Z: Zahlen von 0 bis 360 (0 = um diese Achse nicht drehen)",
                        L"Zufällige Drehung", MB_OK | MB_ICONWARNING);
            return;
        }
    std::vector<object *> oben = ausgewaehlteObjekte();
    Gruppe *gruppe = nullptr;
    bool abbruch = false;
    std::vector<object *> teile = manipZiele(oben, gruppe, abbruch);
    if (abbruch)
        return;
    if (teile.empty() || (max[0] <= 0 && max[1] <= 0 && max[2] <= 0))
    {
        MessageBoxW(hObjektFenster, teile.empty() ? L"Bitte zuerst Objekte auswählen (Strg- oder Shift-Klick in der Liste)."
                                                  : L"Mindestens ein Winkel muss größer als 0 sein.",
                    L"Zufällige Drehung", MB_OK | MB_ICONINFORMATION);
        return;
    }
    std::mt19937 rng(unsigned(GetTickCount()) * 2654435761u + 7u);
    std::uniform_real_distribution<double> gleich(-1.0, 1.0);
    aenderungBeginnen("Zufällige Drehung", oben);
    gSzeneAendern([&]
                  {
        for (object *o : teile)
        {
            Vector3D mn, mx;
            if (!GpuAnsichten::objektGrenzen(o, mn, mx))
                mn = mx = o->position;
            const Vector3D mitte = (mn + mx) * 0.5;
            const double untenVorher = mn.y;
            for (int achse = 0; achse < 3; ++achse)
                if (max[achse] > 0)
                    objektDrehen(o, achse, gleich(rng) * max[achse], mitte);
            // Unterkante bleibt auf ihrer Hoehe (auf dem Boden)
            Vector3D mn2, mx2;
            if (GpuAnsichten::objektGrenzen(o, mn2, mx2) && std::fabs(mn2.y - untenVorher) > 1e-12)
                o->translate(Vector3D(0, untenVorher - mn2.y, 0));
        }
        if (gruppe)
            gruppe->grenzenAktualisieren(); });
    aenderungAbschliessen();
    abgleichen();
    textSetzen(g.lblManipInfo, std::to_wstring(teile.size()) + L" Objekte zufällig gedreht (bis ±" + zahlText(max[0], 0) +
                                   L"° / ±" + zahlText(max[1], 0) + L"° / ±" + zahlText(max[2], 0) +
                                   L"° um X / Y / Z, Unterkante bleibt).\nNochmals Ausführen = neue Winkel, Strg+Z = rückgängig.");
}

// Zufaellige Groesse jedes Objekts (bzw. der Teile einer einzelnen ausgewaehlten Gruppe); Unterkante und Mitte bleiben
static void manipZufallGroessen()
{
    if (!gSzeneAendern)
        return;
    double w[3];
    for (int i = 0; i < 3; ++i)
        if (!zahlLesen(g.editManipAus[i], w[i]))
            w[i] = -1;
    double fMin = w[0], fMax = w[1];
    const double ungleich = w[2];
    if (fMin > fMax)
        std::swap(fMin, fMax);
    if (!(fMin > 0.001 && fMax <= 1000 && ungleich >= 0 && ungleich <= 1))
    {
        MessageBoxW(hObjektFenster, L"Faktor min und max: Zahlen größer als 0 (z.B. 0,7 und 1,3); ungleichmäßig: 0 bis 1",
                    L"Zufällige Größen", MB_OK | MB_ICONWARNING);
        return;
    }
    std::vector<object *> oben = ausgewaehlteObjekte();
    Gruppe *gruppe = nullptr;
    bool abbruch = false;
    std::vector<object *> teile = manipZiele(oben, gruppe, abbruch);
    if (abbruch)
        return;
    if (teile.empty())
    {
        MessageBoxW(hObjektFenster, L"Bitte zuerst Objekte auswählen (Strg- oder Shift-Klick in der Liste).", L"Zufällige Größen",
                    MB_OK | MB_ICONINFORMATION);
        return;
    }
    std::mt19937 rng(unsigned(GetTickCount()) * 2246822519u + 3u);
    // gleichmaessig auf der logarithmischen Skala: halb so gross ist so wahrscheinlich wie doppelt so gross
    std::uniform_real_distribution<double> gleich(std::log(fMin), std::log(fMax));
    double kleinster = 1e300, groesster = 0;
    aenderungBeginnen("Zufällige Größen", oben);
    gSzeneAendern([&]
                  {
        for (object *o : teile)
        {
            Vector3D mn, mx;
            if (!GpuAnsichten::objektGrenzen(o, mn, mx))
                mn = mx = o->position;
            const Vector3D fuss((mn.x + mx.x) * 0.5, mn.y, (mn.z + mx.z) * 0.5); // Unterkante, Mitte
            const double gemeinsam = std::exp(gleich(rng));
            double f[3];
            for (int k = 0; k < 3; ++k)
                f[k] = std::exp((1.0 - ungleich) * std::log(gemeinsam) + ungleich * gleich(rng));
            objektSkalieren(o, Vector3D(f[0], f[1], f[2]), fuss);
            kleinster = std::min({kleinster, f[0], f[1], f[2]});
            groesster = std::max({groesster, f[0], f[1], f[2]});
        }
        if (gruppe)
            gruppe->grenzenAktualisieren(); });
    aenderungAbschliessen();
    abgleichen();
    textSetzen(g.lblManipInfo, std::to_wstring(teile.size()) + L" Objekte zufällig skaliert (Faktoren " + zahlText(kleinster, 2) +
                                   L" bis " + zahlText(groesster, 2) + L", Unterkante bleibt).\nNochmals Ausführen = neue "
                                   L"Größen (wirkt auf die jetzigen Größen), Strg+Z = rückgängig.");
}

// Objekte senkrecht auf die Flaeche darunter setzen (optional mit dem Hang geneigt)
static bool mengenDreiecke(object *o, std::vector<Dreieck> &d); // weiter unten

static void manipAbsetzen()
{
    if (!gSzeneAendern)
        return;
    double w[3];
    for (int i = 0; i < 3; ++i)
        if (!zahlLesen(g.editManipAus[i], w[i]))
            w[i] = -1;
    const double einsinken = w[0], neigung = w[1], mindest = w[2];
    if (!(einsinken >= 0 && einsinken <= 1 && neigung >= 0 && neigung <= 1 && mindest >= 0 && mindest <= 1000))
    {
        MessageBoxW(hObjektFenster, L"Einsinken: 0 bis 1 (Anteil der Höhe); Neigung: 0 bis 1; Mindestgröße Fläche: ab 0 (z.B. 2)",
                    L"Auf Oberfläche absetzen", MB_OK | MB_ICONWARNING);
        return;
    }
    std::vector<object *> oben = ausgewaehlteObjekte();
    Gruppe *gruppe = nullptr;
    bool abbruch = false;
    std::vector<object *> teile = manipZiele(oben, gruppe, abbruch);
    if (abbruch)
        return;
    if (teile.empty())
    {
        MessageBoxW(hObjektFenster, L"Bitte zuerst die Objekte auswählen, die abgesetzt werden sollen (z.B. die Gruppe mit dem Gras).",
                    L"Auf Oberfläche absetzen", MB_OK | MB_ICONINFORMATION);
        return;
    }

    // Stabil hinlegen: jedes Objekt zuerst auf die flache Seite kippen, auf der es von seiner Lage aus zur Ruhe kaeme
    // (gehoert zum selben Rueckgaengig-Schritt; danach werden Grenzen und Fusspunkt neu bestimmt)
    const bool stabilLegen = SendMessageW(g.cmbManipLage, CB_GETCURSEL, 0, 0) == 1;
    aenderungBeginnen("Auf Oberfläche absetzen", oben);
    size_t gekippt = 0;
    if (stabilLegen)
        gSzeneAendern([&]
                      {
            fortschritt::Abschnitt fortschrittStabil(L"Stabil hinlegen");
            size_t nr = 0;
            for (object *o : teile)
            {
                fortschritt::schritt(nr, teile.size(), L"Objekt " + std::to_wstring(nr + 1) + L" von " + std::to_wstring(teile.size()));
                ++nr;
                std::vector<Dreieck> d;
                if (dynamic_cast<Kugel *>(o) || !mengenDreiecke(o, d) || d.size() < 4)
                    continue;
                const Vector3D sp = stabil::schwerpunkt(d);
                const Vector3D n = stabil::ruheRichtung(stabil::ecken(d), sp);
                // "oben" = -n soll nach +Y: erst um X (Komponente z weg), dann um Z (Komponente x weg), um den Schwerpunkt
                const Vector3D u = -n;
                if (u.y > 1.0 - 1e-9)
                    continue;
                double gx = std::atan2(-u.z, u.y) * 180.0 / M_PI;
                Vector3D v = drehenUmAchse(u, drehAchse(0), gx);
                if (std::fabs(v.z) > 1e-6)
                {
                    gx = -gx;
                    v = drehenUmAchse(u, drehAchse(0), gx);
                }
                double gz = std::atan2(v.x, v.y) * 180.0 / M_PI;
                Vector3D w = drehenUmAchse(v, drehAchse(2), gz);
                if (std::fabs(w.x) > 1e-6 || w.y < 0)
                    gz = -gz;
                objektDrehen(o, 0, gx, sp);
                objektDrehen(o, 2, gz, sp);
                ++gekippt;
            }
            if (gruppe)
                gruppe->grenzenAktualisieren(); });

    // Untergrund: alle anderen sichtbaren Objekte; Gruppen, in denen abgesetzte Objekte stecken, werden in ihre Teile
    // zerlegt (sonst traefe der Strahl die Nachbarn in derselben Gruppe)
    std::vector<object *> innen = teile; // abgesetzte und ihre umgebenden Gruppen
    for (object *o : teile)
        for (Gruppe *gr = gruppeVon(o); gr; gr = gruppeVon(gr))
            innen.push_back(gr);
    if (gruppe)
        innen.push_back(gruppe);
    struct Flaeche
    {
        object *o;
        Vector3D mn, mx;
    };
    std::vector<Flaeche> flaechen;
    std::function<void(object *)> sammeln = [&](object *o)
    {
        if (std::find(teile.begin(), teile.end(), o) != teile.end() || dynamic_cast<VolumenBox *>(o))
            return;
        if (std::find(innen.begin(), innen.end(), o) != innen.end())
        {
            if (auto *gr = dynamic_cast<Gruppe *>(o))
                for (auto &k : gr->kinder)
                    sammeln(k.get());
            return;
        }
        Vector3D mn, mx;
        if (GpuAnsichten::objektGrenzen(o, mn, mx))
            flaechen.push_back({o, mn, mx});
    };
    for (auto &o : scene)
        sammeln(o.get());

    struct Ziel
    {
        object *o;
        Vector3D verschiebung, fuss, richtung; // richtung: neue "oben"-Richtung
        bool aufFlaeche;
    };
    std::vector<Ziel> ziele;
    double hoechstes = 0;
    for (auto &f : flaechen)
        hoechstes = std::max(hoechstes, f.mx.y);
    for (object *o : teile)
    {
        Vector3D mn, mx;
        if (!GpuAnsichten::objektGrenzen(o, mn, mx))
            continue;
        const double fussBreite = std::max(mx.x - mn.x, mx.z - mn.z);
        const Vector3D mitte((mn.x + mx.x) * 0.5, 0, (mn.z + mx.z) * 0.5);
        const Ray strahl(Vector3D(mitte.x, std::max(hoechstes, mx.y) + 1.0, mitte.z), Vector3D(0, -1, 0));
        double bestY = -1e300;
        Vector3D normale(0, 1, 0);
        for (const Flaeche &f : flaechen)
        {
            if (mitte.x < f.mn.x || mitte.x > f.mx.x || mitte.z < f.mn.z || mitte.z > f.mx.z)
                continue; // nicht darunter
            if (std::max(f.mx.x - f.mn.x, f.mx.z - f.mn.z) < mindest * fussBreite)
                continue; // zu klein als Untergrund (Nachbarbueschel, Blume ...)
            Hit h;
            f.o->intersect(strahl, h);
            if (h.hit && h.position.y > bestY)
            {
                bestY = h.position.y;
                Vector3D n = h.geomNormale.length() > 1e-12 ? h.geomNormale : h.normale;
                if (n.length() > 1e-12)
                    normale = n.normalized() * (n.y < 0 ? -1.0 : 1.0); // nach oben zeigend
            }
        }
        const bool aufFlaeche = bestY > -1e299;
        const double boden = aufFlaeche ? bestY : 0.0;
        const Vector3D fussVorher(mitte.x, mn.y, mitte.z);
        const Vector3D fuss(mitte.x, boden - einsinken * (mx.y - mn.y), mitte.z);
        Vector3D r = Vector3D(0, 1, 0) * (1.0 - neigung) + normale * neigung;
        r = r.length() > 1e-12 ? r.normalized() : Vector3D(0, 1, 0);
        ziele.push_back({o, fuss - fussVorher, fuss, r, aufFlaeche});
    }

    size_t aufFlaechen = 0;
    gSzeneAendern([&]
                  {
        for (const Ziel &z : ziele)
        {
            z.o->translate(z.verschiebung);
            if (neigung > 0 && z.richtung.y < 0.999999)
            {
                // "oben" -> richtung: erst um X (ax = asin(r.z)), dann um Z (az = atan2(-r.x, r.y)), um den Fusspunkt
                const double ax = std::asin(std::clamp(z.richtung.z, -1.0, 1.0)) * 180.0 / M_PI;
                const double az = std::atan2(-z.richtung.x, z.richtung.y) * 180.0 / M_PI;
                objektDrehen(z.o, 0, ax, z.fuss);
                objektDrehen(z.o, 2, az, z.fuss);
            }
            aufFlaechen += z.aufFlaeche;
        }
        if (gruppe)
            gruppe->grenzenAktualisieren(); });
    aenderungAbschliessen();
    abgleichen();
    textSetzen(g.lblManipInfo, std::to_wstring(ziele.size()) + L" Objekte abgesetzt: " + std::to_wstring(aufFlaechen) +
                                   L" auf einer Fläche" +
                                   (ziele.size() > aufFlaechen
                                        ? L", " + std::to_wstring(ziele.size() - aufFlaechen) +
                                              L" ohne passende Fläche darunter auf den Boden (Höhe 0)"
                                        : std::wstring()) +
                                   (stabilLegen ? L"; " + std::to_wstring(gekippt) + L" davon auf eine flache Seite gelegt" : std::wstring()) +
                                   L".\nStrg+Z = rückgängig.");
}

// Auswahl n-mal kopieren, jede Kopie um 'versatz' weiter
static void kopienEinfuegen(std::vector<std::unique_ptr<object>> objekte, std::vector<std::unique_ptr<light>> lichter,
                            const Vector3D &versatz, const std::string &name); // weiter unten
static void auswahlKopien(std::vector<std::unique_ptr<object>> &objekte, std::vector<std::unique_ptr<light>> &lichter);
static std::vector<object *> auswahlAlsInstanzen();

static void manipVervielfaeltigen()
{
    double anzahl = 0, v[3];
    bool ok = zahlLesen(g.editManipWert, anzahl) && anzahl >= 1 && anzahl <= 1000;
    for (int i = 0; i < 3; ++i)
        ok = ok && zahlLesen(g.editManipAus[i], v[i]);
    if (!ok)
    {
        MessageBoxW(hObjektFenster, L"Anzahl Kopien: 1 bis 1000; Versatz je Kopie X, Y, Z: Zahlen (Einheiten)", L"Vervielfältigen",
                    MB_OK | MB_ICONWARNING);
        return;
    }
    const int n = int(std::lround(anzahl));
    const Vector3D schrittV(v[0], v[1], v[2]);
    // Instanzen: die Auswahl wird (falls noetig) selbst zur Instanz, die Kopien sind weitere Instanzen
    const bool instanzen = SendMessageW(g.cmbManipKopien, CB_GETCURSEL, 0, 0) == 1;
    if (instanzen && !ausgewaehlteObjekte().empty())
        auswahlAlsInstanzen();
    std::vector<std::unique_ptr<object>> objekte;
    std::vector<std::unique_ptr<light>> lichter;
    size_t jeKopie = 0;
    {
    fortschritt::Abschnitt fortschrittKopien(L"Vervielfältigen");
    for (int k = 1; k <= n; ++k)
    {
        fortschritt::schritt(size_t(k - 1), size_t(n), L"Kopie " + std::to_wstring(k) + L" von " + std::to_wstring(n));
        std::vector<std::unique_ptr<object>> o1;
        std::vector<std::unique_ptr<light>> l1;
        auswahlKopien(o1, l1);
        jeKopie = o1.size() + l1.size();
        for (auto &o : o1)
        {
            o->translate(schrittV * double(k));
            objekte.push_back(std::move(o));
        }
        for (auto &l : l1)
        {
            l->translate(-(schrittV * double(k))); // translate() der Lichter verschiebt um -delta
            lichter.push_back(std::move(l));
        }
    }
    }
    if (jeKopie == 0)
    {
        MessageBoxW(hObjektFenster, L"Bitte zuerst ein Objekt (oder Licht) auswählen.", L"Vervielfältigen", MB_OK | MB_ICONINFORMATION);
        return;
    }
    const size_t gesamt = objekte.size() + lichter.size();
    kopienEinfuegen(std::move(objekte), std::move(lichter), Vector3D(0, 0, 0), "Vervielfältigen");
    textSetzen(g.lblManipInfo, std::to_wstring(n) + (instanzen ? (n == 1 ? L" Instanz" : L" Instanzen") : (n == 1 ? L" Kopie" : L" Kopien")) +
                                   L" angelegt (" +
                                   std::to_wstring(gesamt) + L" Objekte), je " + zahlText(v[0], 3) + L" / " +
                                   zahlText(v[1], 3) + L" / " + zahlText(v[2], 3) +
                                   L" weiter. Die Kopien sind ausgewählt; Strg+Z entfernt alle.");
}

// Dreiecke eines Objekts fuer die Mengenoperationen (Welt); Kugeln und Staebe als Netz; false: nicht moeglich
static bool mengenDreiecke(object *o, std::vector<Dreieck> &d)
{
    if (auto *g = dynamic_cast<Gruppe *>(o))
    {
        for (auto &k : g->kinder)
            if (!mengenDreiecke(k.get(), d))
                return false;
        return true;
    }
    if (dynamic_cast<VolumenBox *>(o))
        return false;
    if (auto *in = dynamic_cast<Instanz *>(o)) // Dreiecke der Vorlage in der Lage der Instanz
    {
        std::vector<Dreieck> v;
        if (!in->vorlage || !mengenDreiecke(in->vorlage.get(), v))
            return false;
        affinAnwenden(in->abb, v);
        d.insert(d.end(), v.begin(), v.end());
        return true;
    }
    if (auto *k = dynamic_cast<Kugel *>(o))
    {
        auto n = csg::kugelNetz(k->position, k->radius, k->mat, 32);
        d.insert(d.end(), n.begin(), n.end());
        return true;
    }
    if (auto *st = dynamic_cast<Stab *>(o))
    {
        auto n = csg::zylinderNetz(st->position + st->p1, st->position + st->p2, st->radius, st->mat, 24);
        d.insert(d.end(), n.begin(), n.end());
        return true;
    }
    bool etwas = false;
    for (auto *liste : objektDreiecke(o))
        for (const Dreieck &t : *liste)
        {
            Dreieck k = t;
            k.a = t.a + t.position;
            k.b = t.b + t.position;
            k.c = t.c + t.position;
            k.position = Vector3D(0, 0, 0);
            d.push_back(k);
            etwas = true;
        }
    return etwas;
}

// Umriss-Quader eines Objekts in seiner eigenen Ausrichtung (gedrehter Rahmen -> gedrehter Quader). In der
// duennsten Richtung (quer zur Wand) geht er genau durch die Wandschicht, in der der Rahmen sitzt: entlang einer
// Linie durch die Rahmenmitte werden Ein- und Austritt im Grundkoerper gesucht. Eine zweite, parallele Wand
// desselben Objekts (z.B. ausgehoehlter Hauskoerper) bleibt so unberuehrt. Ringsum minimal groesser.
static std::vector<Dreieck> oeffnungsQuader(object *o, const std::vector<Dreieck> &form, const std::vector<Dreieck> &grund,
                                            const Material &mat)
{
    Vector3D achse[3];
    for (int k = 0; k < 3; ++k)
    {
        achse[k] = Vector3D(o->ausrichtung[k], o->ausrichtung[3 + k], o->ausrichtung[6 + k]);
        if (achse[k].length() < 1e-9)
            achse[k] = Vector3D(k == 0, k == 1, k == 2);
        achse[k] = achse[k].normalized();
    }
    double mn[3] = {1e300, 1e300, 1e300}, mx[3] = {-1e300, -1e300, -1e300};
    for (const Dreieck &t : form)
        for (const Vector3D &p : {t.a, t.b, t.c})
            for (int k = 0; k < 3; ++k)
            {
                const double w = p * achse[k];
                mn[k] = std::min(mn[k], w);
                mx[k] = std::max(mx[k], w);
            }
    int duenn = 0;
    for (int k = 1; k < 3; ++k)
        if (mx[k] - mn[k] < mx[duenn] - mn[duenn])
            duenn = k;
    const double diag = std::sqrt((mx[0] - mn[0]) * (mx[0] - mn[0]) + (mx[1] - mn[1]) * (mx[1] - mn[1]) + (mx[2] - mn[2]) * (mx[2] - mn[2]));
    const int k1 = (duenn + 1) % 3, k2 = (duenn + 2) % 3;
    const Vector3D r = achse[duenn];

    // Wandschicht am Rahmen: Schnittpunkte der Linie (Rahmenmitte, Richtung r) mit dem Grundkoerper
    double von = mn[duenn], bis = mx[duenn];
    bool gefunden = false;
    for (int versuch = 0; versuch < 4 && !gefunden; ++versuch)
    {
        // leicht neben der Mitte (nicht genau auf Diagonalen der Wandflaechen)
        const double f1 = 0.5 + 0.0137 * (versuch + 1) * (versuch % 2 ? -1 : 1), f2 = 0.5 + 0.0071 * (versuch + 1);
        const Vector3D start = achse[k1] * (mn[k1] + (mx[k1] - mn[k1]) * f1) + achse[k2] * (mn[k2] + (mx[k2] - mn[k2]) * f2);
        std::vector<double> treffer; // Lage entlang r
        for (const Dreieck &t : grund)
        {
            const Vector3D e1 = t.b - t.a, e2 = t.c - t.a, pv = r.cross(e2);
            const double det = e1 * pv;
            if (std::fabs(det) < 1e-14 * (e1.length() * e2.length() + 1e-300))
                continue; // parallel zur Linie
            const Vector3D tv = start - t.a;
            const double u = (tv * pv) / det;
            if (u < 0 || u > 1)
                continue;
            const Vector3D qv = tv.cross(e1);
            const double v = (r * qv) / det;
            if (v < 0 || u + v > 1)
                continue;
            treffer.push_back(start * r + (e2 * qv) / det); // Linie: start + r*t, start*r = 0 entlang r
        }
        std::sort(treffer.begin(), treffer.end());
        std::vector<double> einmalig;
        for (double w : treffer)
            if (einmalig.empty() || w - einmalig.back() > 1e-9 * (diag + 1.0))
                einmalig.push_back(w);
        if (einmalig.size() < 2 || einmalig.size() % 2)
            continue; // Kante getroffen: anders versuchen
        // Schichten (Eintritt, Austritt); die mit der groessten Ueberdeckung mit dem Rahmen, sonst die naechste
        const double mitte = 0.5 * (mn[duenn] + mx[duenn]);
        double besteWertung = -1e300;
        for (size_t i = 0; i + 1 < einmalig.size(); i += 2)
        {
            const double a = einmalig[i], b = einmalig[i + 1];
            const double ueberdeckung = std::min(b, mx[duenn]) - std::max(a, mn[duenn]);
            const double abstand = mitte < a ? a - mitte : mitte > b ? mitte - b : 0.0;
            const double wertung = ueberdeckung > 0 ? 1e6 * (diag + 1.0) + ueberdeckung : -abstand;
            if (wertung > besteWertung)
            {
                besteWertung = wertung;
                const double rand = 1e-4 * diag; // hauchduenn ueber beide Wandseiten hinaus (kerbt keinen Boden an)
                von = std::min(mn[duenn], a - rand);
                bis = std::max(mx[duenn], b + rand);
            }
        }
        gefunden = true;
    }

    Vector3D mitte(0, 0, 0), halb[3];
    for (int k = 0; k < 3; ++k)
    {
        double lo = mn[k], hi = mx[k];
        if (k == duenn)
            lo = von, hi = bis;
        const double h = (hi - lo) * 0.5 + 1e-4 * diag; // ringsum minimal groesser: keine hauchduennen Reste
        mitte = mitte + achse[k] * ((lo + hi) * 0.5);
        halb[k] = achse[k] * h;
    }
    return csg::quaderNetz(mitte, halb[0], halb[1], halb[2], mat);
}

// Objekte addieren (Vereinigung) bzw. subtrahieren (groesstes minus die anderen)
static void manipMengen(bool subtrahieren)
{
    const wchar_t *titel = subtrahieren ? L"Objekte subtrahieren" : L"Objekte addieren";
    std::vector<object *> sel = ausgewaehlteObjekte();
    // auch Teile von Gruppen (z.B. Wand und Tuerrahmen im Haus); aber nicht eine Gruppe zusammen mit ihrem eigenen Teil
    std::vector<object *> ziele;
    bool verschachtelt = false;
    for (object *o : sel)
    {
        bool inAnderem = false;
        for (Gruppe *g = gruppeVon(o); g; g = gruppeVon(g))
            inAnderem = inAnderem || std::find(sel.begin(), sel.end(), static_cast<object *>(g)) != sel.end();
        verschachtelt = verschachtelt || inAnderem;
        if (!inAnderem)
            ziele.push_back(o);
    }
    if (ziele.size() < 2 || verschachtelt)
    {
        MessageBoxW(hObjektFenster, verschachtelt
                                        ? L"Eine Gruppe und eines ihrer eigenen Teile sind zusammen gewählt – bitte nur die Teile "
                                          L"(z.B. Wand und Türrahmen) oder nur ganze Gruppen wählen."
                                        : L"Bitte mindestens zwei Objekte wählen (Strg+Klick in der Liste oder Ansicht, auch Teile "
                                          L"von Gruppen).\nSubtrahieren: die Wand und den Türrahmen.",
                    titel, MB_OK | MB_ICONINFORMATION);
        return;
    }
    // Grundkoerper: beim Subtrahieren das groesste Objekt, beim Addieren das Hauptobjekt der Auswahl
    object *grund = ziele.front();
    if (subtrahieren)
    {
        double groesste = -1;
        for (object *o : ziele)
        {
            Vector3D mn, mx;
            const double d = GpuAnsichten::objektGrenzen(o, mn, mx) ? (mx - mn).length() : 0.0;
            if (d > groesste)
                groesste = d, grund = o;
        }
    }
    std::vector<Dreieck> grundD;
    size_t umgedreht = 0;
    if (mengenDreiecke(grund, grundD))
        umgedreht += csg::ausrichten(grundD); // nach aussen zeigend (sonst: Schnittmenge statt Vereinigung)
    else
    {
        MessageBoxW(hObjektFenster, L"Dieses Objekt kann nicht verwendet werden (Rauchbox, Wolkenbereich oder leer).", titel,
                    MB_OK | MB_ICONWARNING);
        return;
    }
    const int modus = int(SendMessageW(g.cmbManipModus, CB_GETCURSEL, 0, 0));

    std::vector<Material> mats;
    std::vector<csg::Vieleck> ergebnis = csg::ausDreiecken({&grundD}, mats);
    size_t dreiecke = grundD.size();
    std::vector<object *> verbraucht;
    {
    fortschritt::Abschnitt fortschrittMengen(subtrahieren ? L"Objekte subtrahieren" : L"Objekte addieren");
    size_t nr = 0;
    for (object *o : ziele)
    {
        fortschritt::schritt(nr, ziele.size(), L"Objekt " + std::to_wstring(nr + 1) + L" von " + std::to_wstring(ziele.size()));
        ++nr;
        if (o == grund)
            continue;
        std::vector<Dreieck> d;
        if (!mengenDreiecke(o, d))
            continue;
        umgedreht += csg::ausrichten(d);
        if (subtrahieren && modus == 0)
            d = oeffnungsQuader(o, d, grundD, d.front().mat);
        dreiecke += d.size();
        if (!subtrahieren || modus == 2)
            verbraucht.push_back(o);
        auto teil = csg::ausDreiecken({&d}, mats);
        ergebnis = subtrahieren ? csg::differenz(ergebnis, teil) : csg::vereinigung(ergebnis, teil);
    }
    }
    if (dreiecke > 60000 &&
        MessageBoxW(hObjektFenster, L"Sehr viele Dreiecke – das kann eine Weile dauern. Trotzdem ausführen?", titel,
                    MB_YESNO | MB_ICONQUESTION) != IDYES)
        return;
    std::vector<Dreieck> tris = csg::zuDreiecken(ergebnis, mats);
    if (tris.empty())
    {
        MessageBoxW(hObjektFenster, L"Vom Objekt bleibt nichts übrig (ganz ausgeschnitten).", titel, MB_OK | MB_ICONWARNING);
        return;
    }
    auto neu = std::make_unique<Mesh>(tris, Vector3D(0, 0, 0));
    neu->name = grund->name;
    neu->mat = grund->mat;
    neu->materialName = grund->materialName;
    neu->texture = grund->texture; // Textur der Wand bleibt (die Texturkoordinaten werden beim Schneiden mitgefuehrt)
    neu->textureName = grund->textureName;
    neu->textureMode = grund->textureMode;
    neu->texPar = grund->texPar;
    neu->texturSpiegelU = grund->texturSpiegelU;
    neu->texturSpiegelV = grund->texturSpiegelV;
    neu->texturDrehung = grund->texturDrehung;
    neu->texturBoxSetzen();
    std::copy(grund->ausrichtung, grund->ausrichtung + 9, neu->ausrichtung);
    const size_t anzahl = tris.size();
    const double vol = csg::volumen(tris);
    ersetzenUndEntfernenMitVerlauf(subtrahieren ? "Subtrahieren" : "Addieren", grund, std::move(neu), verbraucht);
    abgleichen();
    textSetzen(g.lblManipInfo, std::wstring(subtrahieren ? L"Ausgeschnitten: " : L"Vereinigt: ") + std::to_wstring(anzahl) +
                                   L" Dreiecke, Rauminhalt " + zahlText(vol, 3) +
                                   (verbraucht.empty() ? std::wstring() : L"; " + std::to_wstring(verbraucht.size()) + L" Objekte darin aufgegangen") +
                                   (umgedreht ? L"; " + std::to_wstring(umgedreht) + L" nach innen zeigende Dreiecke umgedreht" : std::wstring()) +
                                   L".\nStrg+Z macht alles in einem Schritt rückgängig. Glatte Schattierung danach neu setzen "
                                   L"(Glatt schattieren), die Schnittflächen sind flach.");
}

static void manipAnzeigen()
{
    int art = int(SendMessageW(g.cmbManipArt, CB_GETCURSEL, 0, 0));
    if (art < 0 || art >= ANZAHL_MANIP_ARTEN)
        art = 0;
    const bool tiefe = art == 2 && reiter == 2, knick = art == MANIP_GLATT && reiter == 2;
    ShowWindow(g.lblManipTiefe, tiefe ? SW_SHOW : SW_HIDE);
    ShowWindow(g.editManipTiefe, tiefe ? SW_SHOW : SW_HIDE);
    ShowWindow(g.lblManipKnick, knick ? SW_SHOW : SW_HIDE);
    ShowWindow(g.editManipKnick, knick ? SW_SHOW : SW_HIDE);
    const bool wert = (art == MANIP_DICKE || art == MANIP_FAECHERN || art == MANIP_VIELFACH || art == MANIP_FELL) && reiter == 2;
    ShowWindow(g.lblManipWert, wert ? SW_SHOW : SW_HIDE);
    ShowWindow(g.editManipWert, wert ? SW_SHOW : SW_HIDE);
    const bool modus = art == MANIP_SUBTRAHIEREN && reiter == 2;
    ShowWindow(g.lblManipModus, modus ? SW_SHOW : SW_HIDE);
    ShowWindow(g.cmbManipModus, modus ? SW_SHOW : SW_HIDE);
    const bool kopienArt = art == MANIP_VIELFACH && reiter == 2;
    ShowWindow(g.lblManipKopien, kopienArt ? SW_SHOW : SW_HIDE);
    ShowWindow(g.cmbManipKopien, kopienArt ? SW_SHOW : SW_HIDE);
    const bool lageArt = art == MANIP_ABSETZEN && reiter == 2;
    ShowWindow(g.lblManipLage, lageArt ? SW_SHOW : SW_HIDE);
    ShowWindow(g.cmbManipLage, lageArt ? SW_SHOW : SW_HIDE);
    const bool pinselArt = art == MANIP_PINSEL && reiter == 2;
    ShowWindow(g.lblManipPinsel, pinselArt ? SW_SHOW : SW_HIDE);
    ShowWindow(g.cmbManipPinsel, pinselArt ? SW_SHOW : SW_HIDE);
    const bool dreiFelder = (art == MANIP_VERTEILEN || art == MANIP_DREHEN || art == MANIP_VIELFACH || art == MANIP_GROESSEN ||
                             art == MANIP_ABSETZEN || art == MANIP_TERRAIN || art == MANIP_PINSEL || art == MANIP_FELL) &&
                            reiter == 2;
    for (HWND h : {g.lblManipAus, g.editManipAus[0], g.editManipAus[1], g.editManipAus[2]})
        ShowWindow(h, dreiFelder ? SW_SHOW : SW_HIDE);
    if (wert)
    {
        textSetzen(g.lblManipWert, art == MANIP_DICKE ? L"Dicke (Einheiten)" : art == MANIP_FAECHERN ? L"Höhe (Einheiten)"
                                   : art == MANIP_FELL                                     ? L"Hängen (0–1)"
                                                                                           : L"Anzahl Kopien");
        const wchar_t *tipp = art == MANIP_DICKE      ? L"Dicke der Wand in Einheiten: > 0 nach außen (Normalenrichtung), < 0 nach innen"
                              : art == MANIP_FAECHERN ? L"Abstand der neuen Spitze von der Fläche: > 0 Pyramide nach außen, < 0 Mulde, 0 flach"
                              : art == MANIP_FELL     ? L"Fell-Imitation: 0 = Haare stehen gerade ab, 1 = die Spitzen fallen um eine Haarlänge nach unten\n"
                                                        L"(langes, weiches Fell; 0,2 bis 0,4 wirkt natürlich)"
                                                      : L"Wie viele Kopien angelegt werden (1 bis 1000); das Original bleibt";
        tooltipAendern(g.editManipWert, tipp);
        tooltipAendern(g.lblManipWert, tipp);
    }
    if (dreiFelder)
    {
        textSetzen(g.lblManipAus, art == MANIP_VERTEILEN  ? L"Ausdehnung X / Y / Z"
                                  : art == MANIP_DREHEN   ? L"Max. Winkel X / Y / Z (°)"
                                  : art == MANIP_GROESSEN ? L"Faktor min / max / ungleichmäßig"
                                  : art == MANIP_ABSETZEN ? L"Einsinken / Neigung / Mindestgröße Fläche"
                                  : art == MANIP_TERRAIN  ? L"Abstand / Rand / Angleichen (0 / 1)"
                                  : art == MANIP_PINSEL   ? L"Radius / Stärke (0–1) / –"
                                  : art == MANIP_FELL     ? L"Länge / Hüllen / Dichte (Haare je cm²)"
                                                          : L"Versatz je Kopie X / Y / Z");
        static const wchar_t *tipps[8][3] = {
            {L"Zufällige Verteilung: Breite des Verteilungsgebiets in X, zentriert um die Mitte der Auswahl. 0 = in X nicht verteilen.",
             L"Zufällige Verteilung: Höhe des Gebiets in Y. 0 = nur in der Ebene; jedes Objekt behält seine Höhe.",
             L"Zufällige Verteilung: Tiefe des Gebiets in Z. 0 = in Z nicht verteilen."},
            {L"Zufällige Drehung um X (Kippen nach vorne/hinten) zwischen −Winkel und +Winkel; 0 = nicht",
             L"Zufällige Drehung um Y (senkrechte Achse) zwischen −Winkel und +Winkel; 180 = beliebige Richtung",
             L"Zufällige Drehung um Z (Kippen zur Seite) zwischen −Winkel und +Winkel; 0 = nicht"},
            {L"Vervielfältigen: jede Kopie so viel weiter in X als die vorige",
             L"Vervielfältigen: jede Kopie so viel weiter in Y (z.B. Treppenstufen)",
             L"Vervielfältigen: jede Kopie so viel weiter in Z"},
            {L"Zufällige Größen: kleinster Faktor (1 = Originalgröße, 0,5 = halb so groß)",
             L"Zufällige Größen: größter Faktor (2 = doppelt so groß)",
             L"Zufällige Größen: 0 = Form bleibt (alle Richtungen gleicher Faktor), 1 = jede Richtung eigener Zufall,\n"
             L"dazwischen gemischt (0,3 = leicht unterschiedliche Proportionen)"},
            {L"Auf Oberfläche absetzen: um diesen Anteil der eigenen Höhe einsinken (0 = steht genau auf der Fläche,\n"
             L"0,05 = Gras wurzelt etwas, 0,2 = Stein halb eingegraben)",
             L"Auf Oberfläche absetzen: 0 = bleibt senkrecht (Bäume, Häuser, Pfähle), 1 = steht senkrecht auf dem Hang\n"
             L"(Gras, Steine, Moos), dazwischen gemischt",
             L"Auf Oberfläche absetzen: Untergrund muss mindestens so viel mal so breit sein wie das Objekt (2 = doppelt).\n"
             L"Größer: nur das Terrain zählt; 1: auch gleich große Objekte (z.B. Kiste auf Kiste)"},
            {L"Terrain absenken: so weit liegt das Terrain danach unter der Unterseite der Objekte (Einheiten, z.B. 0,01)",
             L"Terrain absenken: Breite des weichen Übergangs rund um die Objekte (Einheiten; 0 = senkrechte Kante)",
             L"Terrain absenken: 0 = nur absenken (was tiefer liegt, bleibt), 1 = angleichen: auch Mulden auffüllen,\n"
             L"das Gelände liegt dann überall genau unter dem Objekt (ebener Platz)"},
            {L"Terrain-Pinsel: Radius in Einheiten der Szene (Fläche, die ein Strich beeinflusst; zur Mitte stärker)",
             L"Terrain-Pinsel: Stärke je Schritt, 0 bis 1 (0,2 = behutsam, 1 = kräftig)",
             L"(nicht benutzt)"},
            {L"Fell-Imitation: Haarlänge in Einheiten (Katze etwa 0,01 bis 0,03 m)",
             L"Fell-Imitation: Anzahl der Hüllen (1 bis 32; mehr = feinere Haare, langsamer; 8 ist ein guter Start)",
             L"Fell-Imitation: Haare je cm² (Katze 30 bis 100; mehr = dichteres, feineres Fell)"}};
        const int k = art == MANIP_VERTEILEN  ? 0
                      : art == MANIP_DREHEN   ? 1
                      : art == MANIP_GROESSEN ? 3
                      : art == MANIP_ABSETZEN ? 4
                      : art == MANIP_TERRAIN  ? 5
                      : art == MANIP_PINSEL   ? 6
                      : art == MANIP_FELL     ? 7
                                              : 2;
        for (int i = 0; i < 3; ++i)
            tooltipAendern(g.editManipAus[i], tipps[k][i]);
        static std::wstring zusammen;
        zusammen = std::wstring(L"1. ") + tipps[k][0] + L"\n2. " + tipps[k][1] + L"\n3. " + tipps[k][2];
        tooltipAendern(g.lblManipAus, zusammen.c_str());
    }
    static int letzteArt = -1;
    if (art != letzteArt) // Objektliste haengt von der Aktion ab (Glatt schattieren: alle Objektarten)
    {
        // Werte der Felder je Aktion merken (dieselben Felder dienen mehreren Aktionen)
        static std::map<int, std::array<std::wstring, 4>> gemerkt;
        auto feldText = [](HWND h)
        {
            wchar_t t[64] = {};
            GetWindowTextW(h, t, 64);
            return std::wstring(t);
        };
        if (letzteArt >= 0)
            gemerkt[letzteArt] = {feldText(g.editManipAus[0]), feldText(g.editManipAus[1]), feldText(g.editManipAus[2]),
                                  feldText(g.editManipWert)};
        const bool neuerTyp = (letzteArt == MANIP_VIELFACH) != (art == MANIP_VIELFACH);
        letzteArt = art;
        auto it = gemerkt.find(art);
        if (it != gemerkt.end())
        {
            for (int i = 0; i < 3; ++i)
                textSetzen(g.editManipAus[i], it->second[size_t(i)]);
            textSetzen(g.editManipWert, it->second[3]);
        }
        else if (art == MANIP_DICKE || art == MANIP_FAECHERN) // Vorgabe passend zur Szenengroesse
            textSetzen(g.editManipWert, zahlText(passendeGroesse() * (art == MANIP_DICKE ? 0.05 : 0.3), 3));
        else if (art == MANIP_VERTEILEN) // Vorgabe: halbe Szene in X/Z, Y = 0
        {
            Vector3D mn, mx;
            const bool da = szenenGrenzen(mn, mx);
            textSetzen(g.editManipAus[0], zahlText(da ? rundeSchoen((mx.x - mn.x) * 0.5) : 10.0, 3));
            textSetzen(g.editManipAus[1], L"0");
            textSetzen(g.editManipAus[2], zahlText(da ? rundeSchoen((mx.z - mn.z) * 0.5) : 10.0, 3));
        }
        else if (art == MANIP_DREHEN)
        {
            textSetzen(g.editManipAus[0], L"0");
            textSetzen(g.editManipAus[1], L"180");
            textSetzen(g.editManipAus[2], L"0");
        }
        else if (art == MANIP_GROESSEN)
        {
            textSetzen(g.editManipAus[0], L"0.7");
            textSetzen(g.editManipAus[1], L"1.3");
            textSetzen(g.editManipAus[2], L"0");
        }
        else if (art == MANIP_ABSETZEN)
        {
            textSetzen(g.editManipAus[0], L"0");
            textSetzen(g.editManipAus[1], L"1");
            textSetzen(g.editManipAus[2], L"2");
        }
        else if (art == MANIP_TERRAIN) // Vorgabe aus der Szenengroesse
        {
            const double pg = passendeGroesse();
            textSetzen(g.editManipAus[0], zahlText(rundeSchoen(pg * 0.01), 4));
            textSetzen(g.editManipAus[1], zahlText(rundeSchoen(pg * 0.3), 3));
            textSetzen(g.editManipAus[2], L"1");
        }
        else if (art == MANIP_FELL) // Vorgabe: Haarlaenge 4 % der Auswahl
        {
            Vector3D mn, mx;
            const double b = grenzenVon(ausgewaehlteObjekte(), mn, mx) ? (mx - mn).length() : passendeGroesse();
            textSetzen(g.editManipAus[0], zahlText(rundeSchoen(std::max(1e-4, b * 0.04)), 4));
            textSetzen(g.editManipAus[1], L"8");
            textSetzen(g.editManipAus[2], L"60");
            textSetzen(g.editManipWert, L"0.3");
        }
        else if (art == MANIP_PINSEL)
        {
            textSetzen(g.editManipAus[0], zahlText(rundeSchoen(passendeGroesse() * 0.5), 3));
            textSetzen(g.editManipAus[1], L"0.3");
            textSetzen(g.editManipAus[2], L"0");
        }
        else if (art == MANIP_VIELFACH) // Vorgabe: Breite der Auswahl plus etwas Abstand in X
        {
            Vector3D mn, mx;
            const double b = grenzenVon(ausgewaehlteObjekte(), mn, mx) ? (mx.x - mn.x) * 1.25 : passendeGroesse();
            textSetzen(g.editManipWert, L"5");
            textSetzen(g.editManipAus[0], zahlText(b > 1e-9 ? b : passendeGroesse(), 3));
            textSetzen(g.editManipAus[1], L"0");
            textSetzen(g.editManipAus[2], L"0");
        }
        (void)neuerTyp;
        if (hObjektFenster) // die Zeilen haengen von der Aktion ab: neu anordnen
        {
            RECT r;
            GetClientRect(hObjektFenster, &r);
            objektfensterLayout(r.right, r.bottom);
        }
        manipObjekteFuellen();
    }
    if (art == MANIP_VERTEILEN)
        textSetzen(g.lblManipInfo, szenenGroesseText() + L"\n\n" + MANIP_ARTEN[art].info);
    else
        textSetzen(g.lblManipInfo, MANIP_ARTEN[art].info);
    tooltipAendern(g.cmbManipArt, MANIP_ARTEN[art].info);
    textSetzen(g.lblManipArt, MANIP_ARTEN[art].name);
    tooltipAendern(g.lblManipArt, MANIP_ARTEN[art].info);
    InvalidateRect(g.lblManipInfo, nullptr, TRUE);
}

static void manipGlattSchattieren()
{
    if (!gSzeneAendern)
        return;
    double knick = 40;
    if (!zahlLesen(g.editManipKnick, knick) || knick < 0 || knick > 180)
    {
        MessageBoxW(hObjektFenster, L"Knickwinkel: 0 bis 180 Grad", L"Glatt schattieren", MB_OK | MB_ICONWARNING);
        return;
    }
    // Ziel: ein Objekt (oder eine Gruppe) bzw. alle Objekte der Szene
    std::vector<object *> oben = ausgewaehlteObjekte(); // Auswahl; keine: alle Objekte der Szene
    if (oben.empty())
    {
        if (MessageBoxW(hObjektFenster, L"Nichts ausgewählt. Alle Objekte der Szene glatt schattieren?", L"Glatt schattieren",
                        MB_YESNO | MB_ICONQUESTION) != IDYES)
            return;
        for (auto &o : scene)
            oben.push_back(o.get());
    }
    oben = mitVorlagen(oben); // Instanzen: ihre Vorlage
    std::vector<object *> teile;
    for (object *o : oben)
        gruppenAufloesen(o, teile);
    size_t dreiecke = 0, objekte = 0;
    for (object *t : teile)
    {
        size_t n = 0;
        for (auto *liste : objektDreiecke(t))
            n += liste->size();
        dreiecke += n;
        objekte += n > 0;
    }
    if (dreiecke == 0)
    {
        MessageBoxW(hObjektFenster, L"Keine Objekte aus Dreiecken gewählt (Kugeln und Stäbe sind ohnehin glatt).",
                    L"Glatt schattieren", MB_OK | MB_ICONINFORMATION);
        return;
    }
    aenderungBeginnen(knick <= 0 ? std::string("Flach schattieren") : std::string("Glatt schattieren"), oben);
    size_t geglaettet = 0;
    gSzeneAendern([&]
                  {
        fortschritt::Abschnitt fortschrittGlatt(knick <= 0 ? L"Flach schattieren" : L"Glatt schattieren");
        size_t nr = 0;
        for (object *t : teile)
        {
            fortschritt::schritt(nr, teile.size(), L"Objekt " + std::to_wstring(nr + 1) + L" von " + std::to_wstring(teile.size()));
            ++nr;
            for (auto *liste : objektDreiecke(t))
                geglaettet += netz::normalenGlaetten(*liste, knick);
        } });
    aenderungAbschliessen();
    abgleichen();
    textSetzen(g.lblManipInfo, (knick <= 0 ? L"Ecknormalen entfernt (flach): " : L"Glatt schattiert (Knick " +
                                                                                    zahlText(knick, 0) + L"°): ") +
                                   std::to_wstring(objekte) + L" Objekte, " + std::to_wstring(dreiecke) + L" Dreiecke; " +
                                   (knick <= 0 ? std::wstring()
                                               : std::to_wstring(geglaettet) + L" von " + std::to_wstring(3 * dreiecke) +
                                                     L" Ecken geglättet" +
                                                     (geglaettet == 0 ? L" – alle Kanten sind schärfer als der Knickwinkel "
                                                                        L"(z.B. Würfel: 90°), sie bleiben kantig"
                                                                      : L"") +
                                                     L". ") +
                                   L"Neu rendern, um es zu sehen (die Ansichten zeigen immer flach). Rückgängig mit Strg+Z.");
}

static void manipAufMesh(int art, Mesh *m);

// ---------- Terrain bearbeiten ----------

static void statusHinweisSetzen(const std::wstring &t); // weiter unten
static void kantenModusSetzen(bool an);

// Terrain nach einer Aenderung der Hoehen: Normalen (falls glatt), Grenzen, BVH, Textur-Bezug
static void terrainAuffrischen(Mesh *m, bool normalen)
{
    if (normalen)
    {
        bool glatt = false;
        for (const Dreieck &t : m->triangles)
            if (t.hasVertexNormals)
            {
                glatt = true;
                break;
            }
        if (glatt)
            netz::normalenGlaetten(m->triangles, 40.0);
    }
    m->recomputeMeshBoundsFromTriangles();
    m->buildBVH();
    if (m->texture)
        m->setTexture(m->texture);
    m->setTextureMode(m->textureMode);
    ++m->geometrieZaehler; // Ansichten: das Terrain neu hochladen (auch wenn die Stichproben gleich bleiben)
    ++gGeometrieStand;
}

// Terrain = das Netz mit der groessten Grundflaeche unter 'kandidaten' (Instanz: ihre Vorlage zaehlt nicht)
static Mesh *terrainSuchen(const std::vector<object *> &kandidaten)
{
    Mesh *best = nullptr;
    double flaeche = -1;
    for (object *o : kandidaten)
        if (auto *m = dynamic_cast<Mesh *>(o))
        {
            Vector3D mn, mx;
            if (!GpuAnsichten::objektGrenzen(m, mn, mx))
                continue;
            const double f = (mx.x - mn.x) * (mx.z - mn.z);
            if (f > flaeche)
                flaeche = f, best = m;
        }
    return best;
}

static void manipTerrainAbsenken()
{
    if (!gSzeneAendern)
        return;
    double w[3];
    for (int i = 0; i < 3; ++i)
        if (!zahlLesen(g.editManipAus[i], w[i]))
            w[i] = -1;
    if (!(w[0] >= 0 && w[1] >= 0 && (w[2] == 0 || w[2] == 1)))
    {
        MessageBoxW(hObjektFenster, L"Abstand und Rand: ab 0 (Einheiten); Angleichen: 0 oder 1", L"Terrain absenken",
                    MB_OK | MB_ICONWARNING);
        return;
    }
    std::vector<object *> auswahl = ausgewaehlteObjekte();
    std::vector<object *> teile; // Gruppen in ihre Teile (das Terrain kann in einer Gruppe stecken)
    for (object *o : auswahl)
        gruppenAufloesen(o, teile);
    Mesh *terrain = terrainSuchen(teile);
    std::vector<object *> formen;
    for (object *o : auswahl)
        if (o != terrain && !(gruppeVon(terrain) == o))
            formen.push_back(o);
    if (!terrain || formen.empty())
    {
        MessageBoxW(hObjektFenster, L"Bitte das Terrain und (mit Strg) die Objekte darauf auswählen, z.B. die Stücke des "
                                    L"Wegbetts. Das größte gewählte Netz gilt als Terrain.",
                    L"Terrain absenken", MB_OK | MB_ICONINFORMATION);
        return;
    }
    Vector3D tmn, tmx;
    GpuAnsichten::objektGrenzen(terrain, tmn, tmx);
    // Unterseite der Formen: Strahl von unterhalb des Terrains senkrecht nach oben, erster Treffer
    const double start = tmn.y - 1.0 - (tmx - tmn).length();
    fortschritt::Abschnitt fortschrittTerrain(L"Terrain absenken");
    const double erwartet = std::max(1.0, double(terrain->triangles.size()) * 0.5);
    size_t getestet = 0;
    auto unterkante = [&](double x, double z, double &u) -> bool
    {
        if ((++getestet & 0x3FF) == 0)
            fortschritt::melden(std::min(0.95, double(getestet) / erwartet), L"Punkte des Terrains");
        const Ray strahl(Vector3D(x, start, z), Vector3D(0, 1, 0));
        Hit h;
        for (object *o : formen)
            o->intersect(strahl, h);
        if (!h.hit)
            return false;
        u = h.position.y;
        return true;
    };
    size_t punkte = 0;
    aenderungBeginnen("Terrain absenken", {terrain});
    gSzeneAendern([&]
                  {
        punkte = terrainBearbeiten::unterObjektAbsenken(terrain->triangles, unterkante, w[0], w[1], w[2] > 0.5);
        if (punkte)
            terrainAuffrischen(terrain, true); });
    aenderungAbschliessen();
    computeSceneBounds();
    abgleichen();
    textSetzen(g.lblManipInfo, punkte ? std::to_wstring(punkte) + L" Punkte des Terrains angepasst (unter " +
                                            std::to_wstring(formen.size()) + L" Objekten). Strg+Z = rückgängig."
                                      : std::wstring(L"Unter den gewählten Objekten liegt kein Punkt des Terrains – "
                                                     L"ist das Raster zu grob? (Terrain mit mehr Rasterpunkten anlegen)"));
}

// Pinsel: Zustand des laufenden Strichs
static Mesh *pinselTerrain = nullptr;
static bool pinselStrich = false;
static Vector3D pinselLetzter, pinselStart;

static bool pinselTerrainGueltig()
{
    return pinselTerrain && (objektIndex(pinselTerrain) >= 0 || gruppeVon(pinselTerrain));
}

static void pinselBeenden()
{
    if (pinselStrich && terrainPinsel::gEnde)
        terrainPinsel::gEnde();
    terrainPinsel::gAktiv = false;
    pinselTerrain = nullptr;
    statusHinweisSetzen(L"");
    if (hObjektFenster)
        textSetzen(g.lblManipInfo, L"Terrain-Pinsel beendet.");
}

// ein Schritt des Pinsels an der Stelle p (unter der Szenensperre)
static void pinselSchritt(const Vector3D &p)
{
    double w[3];
    for (int i = 0; i < 3; ++i)
        if (!zahlLesen(g.editManipAus[i], w[i]))
            w[i] = -1;
    const double radius = w[0] > 0 ? w[0] : passendeGroesse() * 0.5;
    const double staerke = std::clamp(w[1] >= 0 ? w[1] : 0.3, 0.0, 1.0);
    int art = int(SendMessageW(g.cmbManipPinsel, CB_GETCURSEL, 0, 0));
    if (art < 0 || art > 3)
        art = 0;
    Vector3D mitte = p;
    if (art == terrainBearbeiten::EBNEN)
        mitte.y = pinselStart.y;
    {
        std::lock_guard<std::mutex> lock(sceneMutex);
        terrainBearbeiten::pinsel(pinselTerrain->triangles, mitte, radius, staerke, art);
        terrainAuffrischen(pinselTerrain, false);
    }
    pinselLetzter = p;
}

static void pinselUmschalten()
{
    if (terrainPinsel::gAktiv)
    {
        pinselBeenden();
        return;
    }
    std::vector<object *> teile;
    for (object *o : ausgewaehlteObjekte())
        gruppenAufloesen(o, teile);
    pinselTerrain = terrainSuchen(teile);
    if (!pinselTerrain)
    {
        MessageBoxW(hObjektFenster, L"Bitte zuerst das Terrain auswählen (ein Objekt aus Dreiecken, z.B. \"+ Neu → Terrain …\").",
                    L"Terrain-Pinsel", MB_OK | MB_ICONINFORMATION);
        return;
    }
    if (kanten::gModus)
        kantenModusSetzen(false);
    terrainPinsel::gStart = [](const Ray &strahl) -> bool
    {
        if (!pinselTerrainGueltig())
        {
            pinselBeenden();
            return false;
        }
        Hit h;
        pinselTerrain->intersect(strahl, h);
        if (!h.hit)
            return false; // daneben: normale Auswahl
        aenderungBeginnen("Terrain-Pinsel", {pinselTerrain});
        pinselStrich = true;
        pinselStart = h.position;
        pinselSchritt(h.position);
        return true;
    };
    terrainPinsel::gZiehen = [](const Ray &strahl)
    {
        if (!pinselStrich || !pinselTerrainGueltig())
            return;
        Hit h;
        pinselTerrain->intersect(strahl, h);
        if (!h.hit)
            return;
        double r = 0;
        if (!zahlLesen(g.editManipAus[0], r) || !(r > 0))
            r = passendeGroesse() * 0.5;
        if ((h.position - pinselLetzter).length() < 0.15 * r)
            return; // erst nach einem Stueck Weg wieder (gleichmaessiger Strich)
        pinselSchritt(h.position);
    };
    terrainPinsel::gEnde = []
    {
        if (!pinselStrich)
            return;
        pinselStrich = false;
        if (pinselTerrainGueltig())
        {
            std::lock_guard<std::mutex> lock(sceneMutex);
            terrainAuffrischen(pinselTerrain, true); // glatte Normalen erst am Ende des Strichs
        }
        aenderungAbschliessen();
        computeSceneBounds();
    };
    terrainPinsel::gAktiv = true;
    statusHinweisSetzen(L"Terrain-Pinsel: in einer Ansicht mit gedrückter linker Maustaste über das Terrain ziehen – Esc beendet");
    textSetzen(g.lblManipInfo, L"Pinsel aktiv: mit gedrückter linker Maustaste über das Terrain ziehen (Kamerasicht oder "
                               L"Draufsicht). Klicks neben das Terrain wählen wie gewohnt aus. Esc oder erneut \"Ausführen\" "
                               L"beendet den Pinsel.");
}

// Esc: von gewaehlten Teilen einer Gruppe eine Ebene hoch zur Gruppe (main.cpp)
bool AuswahlEbeneHoch()
{
    if (selectedType != SelectedType::OBJECT)
        return false;
    std::vector<object *> oben;
    for (object *o : ausgewaehlteObjekte())
    {
        Gruppe *g = gruppeVon(o);
        object *z = g ? static_cast<object *>(g) : o;
        if (std::find(oben.begin(), oben.end(), z) == oben.end())
            oben.push_back(z);
        if (!g)
            continue;
    }
    bool teil = false;
    for (object *o : ausgewaehlteObjekte())
        teil = teil || gruppeVon(o) != nullptr;
    if (!teil)
        return false;
    auswahlSetzen(oben);
    abgleichen();
    return true;
}

// ----- Fell-Imitation: Huellen um jedes ausgewaehlte Teil (fell.h) -----
static std::wstring fellPfadBreit(const std::string &s)
{
    int n = MultiByteToWideChar(CP_ACP, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(size_t(std::max(1, n)), L'\0');
    if (n > 0)
        MultiByteToWideChar(CP_ACP, 0, s.c_str(), -1, &w[0], n);
    w.resize(wcslen(w.c_str()));
    return w;
}

static void manipFell()
{
    std::vector<object *> teile, ziele;
    for (object *o : ausgewaehlteObjekte())
        gruppenAufloesen(o, teile);
    std::unordered_set<const object *> da; // was gerade in der Szene ist (geloeschtes Fell zaehlt nicht)
    for (auto &o : scene)
        fell::alleObjekte(o.get(), da);
    // Laenge vorab (fuer zu kleine Teile)
    double laengeVorab = 0;
    zahlLesen(g.editManipAus[0], laengeVorab);
    size_t schonFell = 0, ohneFell = 0;
    for (object *t : teile)
    {
        if (fell::istHuelle(t, da))
            continue;
        // Augen, Pupillen, Nase, Schnurrhaare, Glanzpunkte ... bekommen kein Fell (Namen wie bei "+ Neu -> Tier");
        // ebenso Teile, die kaum groesser als die Haarlaenge sind (Krallen, Zaehne, duenne Staebe)
        if (fell::ohneFell(t->name))
        {
            ++ohneFell;
            continue;
        }
        {
            Vector3D mn, mx;
            t->getWorldAABB(mn, mx);
            const Vector3D gr = mx - mn;
            const double kleinste = std::min({gr.x, gr.y, gr.z});
            if (laengeVorab > 0 && kleinste < 1.5 * laengeVorab)
            {
                ++ohneFell;
                continue;
            }
        }
        if (fell::hatFell(t, da))
        {
            ++schonFell;
            continue;
        }
        if (dynamic_cast<Mesh *>(t) || dynamic_cast<Kugel *>(t) || dynamic_cast<Stab *>(t))
            ziele.push_back(t);
    }
    if (ziele.empty() || !gSzeneAendern)
    {
        MessageBoxW(hObjektFenster,
                    schonFell ? L"Die ausgewählten Teile haben schon eine Fell-Imitation. Zum Ändern die Gruppe \"Fell: …\" löschen und neu erzeugen."
                              : L"Bitte zuerst ein Teil auswählen (Netz, Kugel oder Stab) – oder eine Gruppe wie ein ganzes Tier.",
                    L"Fell-Imitation", MB_OK | MB_ICONINFORMATION);
        return;
    }
    double laenge = 0, anzahl = 0, dichte = 0, haengen = 0;
    if (!zahlLesen(g.editManipAus[0], laenge) || !(laenge > 0) || !zahlLesen(g.editManipAus[1], anzahl) || anzahl < 1 ||
        anzahl > 32 || !zahlLesen(g.editManipAus[2], dichte) || !(dichte > 0) || !zahlLesen(g.editManipWert, haengen) ||
        haengen < 0 || haengen > 1)
    {
        MessageBoxW(hObjektFenster, L"Länge > 0, Hüllen 1 bis 32, Dichte > 0 und Hängen 0 bis 1 eingeben.", L"Fell-Imitation",
                    MB_OK | MB_ICONWARNING);
        return;
    }
    const int n = int(std::lround(anzahl));
    const std::string ordner = "fell_imitation";
    CreateDirectoryW(fellPfadBreit(ordner).c_str(), nullptr);
    const int id = fell::naechsteId();
    const unsigned stempel = unsigned(GetTickCount()) % 100000u;

    fortschritt::Abschnitt fortschrittFell(L"Fell-Imitation");
    std::vector<std::unique_ptr<object>> gruppen;
    std::vector<fell::Huelle> neueHuellen;
    size_t dreiecke = 0;
    for (size_t zi = 0; zi < ziele.size(); ++zi)
    {
        object *basisObj = ziele[zi];
        fortschritt::schritt(zi, ziele.size(), L"Teil " + std::to_wstring(zi + 1) + L" von " + std::to_wstring(ziele.size()));
        std::vector<Dreieck> basis;
        if (!fell::basisDreiecke(basisObj, basis) || basis.empty())
            continue;
        // Kachel der Haare: die der Textur (damit Farbe und Haare zusammenpassen), sonst die Groesse des Teils
        const TexturParameter &bp = basisObj->texturPar();
        Vector3D mn(1e300, 1e300, 1e300), mx(-1e300, -1e300, -1e300);
        for (const Dreieck &t : basis)
            for (const Vector3D &q : {t.a + t.position, t.b + t.position, t.c + t.position})
            {
                mn = Vector3D(std::min(mn.x, q.x), std::min(mn.y, q.y), std::min(mn.z, q.z));
                mx = Vector3D(std::max(mx.x, q.x), std::max(mx.y, q.y), std::max(mx.z, q.z));
            }
        const double kachelM = bp.kachel > 0 ? bp.kachel : std::max({mx.x - mn.x, mx.y - mn.y, mx.z - mn.z, 1e-4});
        const double kachelCm = kachelM * 100.0;
        int s = 128; // Bildgroesse: etwa 6 Bildpunkte Abstand zwischen zwei Haaren
        while (s < 2048 && double(s) * s < dichte * kachelCm * kachelCm * 36.0)
            s *= 2;
        const int haare = int(std::clamp(dichte * kachelCm * kachelCm, 20.0, double(s) * s / 20.0));
        TextureMode modus = basisObj->textureMode;
        if (modus == TextureMode::None || modus == TextureMode::TRIPLANAR)
            modus = TextureMode::BOX; // Haare brauchen eine eindeutige Abbildung (keine gemischte)
        Material mat = basisObj->mat;
        if (auto *bm = dynamic_cast<Mesh *>(basisObj))
            if (!bm->triangles.empty())
                mat = bm->triangles.front().mat;
        mat.transparency = 0;
        mat.reflection = 0;
        mat.IndexOfRefraction = 1;
        mat.specularStrength *= 0.3;
        const std::string teilName = basisObj->name.empty() ? std::string("Teil") : basisObj->name;

        auto gruppe = std::make_unique<Gruppe>();
        gruppe->name = "Fell: " + teilName;
        const std::string vorn = ordner + "/fell" + std::to_string(stempel) + "_" + std::to_string(id) + "_" +
                                 std::to_string(zi + 1) + "_";
        for (int k = 1; k <= n; ++k)
        {
            const double t = double(k) / n;
            const double hell = 0.55 + 0.45 * std::pow(t, 0.8); // innen dunkler (Schatten zwischen den Haaren)
            // Haarbild (Graustufen: weiss = Haar, schwarz = ausgeschnitten)
            const std::vector<uint8_t> h = fell::haarBild(s, haare, k, n, 1000u * unsigned(id) + unsigned(zi));
            std::vector<uint32_t> px(h.size());
            for (size_t i = 0; i < h.size(); ++i)
                px[i] = 0xFF000000u | (uint32_t(h[i]) << 16) | (uint32_t(h[i]) << 8) | h[i];
            const std::string haarDatei = vorn + "haare_" + std::to_string(k) + ".bmp";
            // Farbbild: Textur des Teils, abgedunkelt; ohne Textur die Grundfarbe
            std::vector<uint32_t> farbe;
            int fb = 4, fh = 4;
            if (basisObj->texture && basisObj->texture->stufen() > 0)
            {
                fb = basisObj->texture->stufeBreite(0);
                fh = basisObj->texture->stufeHoehe(0);
                farbe = basisObj->texture->stufePixel(0);
                const double f = std::pow(hell, 1.0 / 2.2); // linearer Faktor in sRGB-Werten
                for (uint32_t &q : farbe)
                {
                    auto kanal = [&](int sh) { return uint32_t(std::lround(((q >> sh) & 0xFF) * f)); };
                    q = 0xFF000000u | (kanal(16) << 16) | (kanal(8) << 8) | kanal(0);
                }
            }
            else
            {
                auto byte = [&](double lin)
                {
                    lin = std::clamp(lin * hell, 0.0, 1.0);
                    const double sr = lin <= 0.0031308 ? 12.92 * lin : 1.055 * std::pow(lin, 1.0 / 2.4) - 0.055;
                    return uint32_t(std::lround(std::clamp(sr, 0.0, 1.0) * 255.0));
                };
                farbe.assign(16, 0xFF000000u | (byte(mat.diffuse.r) << 16) | (byte(mat.diffuse.g) << 8) | byte(mat.diffuse.b));
            }
            const std::string farbDatei = vorn + "farbe_" + std::to_string(k) + ".bmp";
            if (!fell::bmpSchreiben(haarDatei, s, s, px) || !fell::bmpSchreiben(farbDatei, fb, fh, farbe))
            {
                MessageBoxW(hObjektFenster, L"Die Bilder der Fell-Imitation konnten nicht in den Ordner \"fell_imitation\" geschrieben werden.",
                            L"Fell-Imitation", MB_OK | MB_ICONWARNING);
                return;
            }
            auto haarTex = TextureManager::get().load(haarDatei);
            auto farbTex = TextureManager::get().load(farbDatei);
            if (!haarTex || !farbTex)
                continue;
            // Unterseite (weisser Bauch): ebenfalls nach innen abgedunkelt
            std::shared_ptr<Texture> untenTex;
            std::string untenDatei;
            if (const Texture *u = bp.bild[BILD_UNTEN].get())
                if (u->stufen() > 0)
                {
                    std::vector<uint32_t> up = u->stufePixel(0);
                    const double f = std::pow(hell, 1.0 / 2.2);
                    for (uint32_t &q : up)
                    {
                        auto kanal = [&](int sh) { return uint32_t(std::lround(((q >> sh) & 0xFF) * f)); };
                        q = 0xFF000000u | (kanal(16) << 16) | (kanal(8) << 8) | kanal(0);
                    }
                    untenDatei = vorn + "unten_" + std::to_string(k) + ".bmp";
                    if (fell::bmpSchreiben(untenDatei, u->stufeBreite(0), u->stufeHoehe(0), up))
                        untenTex = TextureManager::get().load(untenDatei);
                }

            std::vector<Dreieck> huelle;
            fell::huelleBerechnen(basis, huelle, k, n, laenge, haengen);
            fell::ruheUebernehmen(basis, huelle);
            for (Dreieck &d : huelle)
                d.mat = mat;
            auto m = std::make_unique<Mesh>(huelle, Vector3D(0, 0, 0));
            m->mat = mat;
            m->name = "Fell " + std::to_string(k) + "/" + std::to_string(n);
            auto par = std::make_shared<TexturParameter>();
            par->kachel = bp.kachel;
            par->versatzU = bp.versatzU;
            par->versatzV = bp.versatzV;
            par->winkel = bp.winkel;
            par->kugelDrehung = bp.kugelDrehung;
            par->objektfest = true;
            par->fellHuelle = true;
            par->bild[BILD_ALPHA] = haarTex;
            par->bildName[BILD_ALPHA] = haarDatei;
            if (untenTex)
            {
                par->bild[BILD_UNTEN] = untenTex;
                par->bildName[BILD_UNTEN] = untenDatei;
                par->staerke[BILD_UNTEN] = bp.staerke[BILD_UNTEN];
            }
            m->texPar = par;
            m->texturSpiegelU = basisObj->texturSpiegelU;
            m->texturSpiegelV = basisObj->texturSpiegelV;
            m->texturDrehung = basisObj->texturDrehung;
            m->textureName = farbDatei;
            m->setTexture(farbTex);
            m->setTextureMode(modus);
            fell::ruheUebernehmen(basis, m->triangles); // Ruhelage der Basis (der Mesh-Konstruktor hat sie verworfen)
            m->texturBoxSetzen();
            dreiecke += m->triangles.size();
            fell::Huelle eintrag;
            eintrag.huelle = m.get();
            eintrag.basis = basisObj;
            eintrag.k = k;
            eintrag.n = n;
            eintrag.laenge = laenge;
            eintrag.haengen = haengen;
            eintrag.id = id + int(zi);
            eintrag.pruef = fell::pruefsumme(basis);
            neueHuellen.push_back(eintrag);
            gruppe->kinder.push_back(std::move(m));
        }
        if (gruppe->kinder.empty())
            continue;
        gruppe->grenzenAktualisieren();
        gruppen.push_back(std::move(gruppe));
    }
    if (gruppen.empty())
        return;
    std::vector<object *> zeiger;
    for (auto &gr : gruppen)
        zeiger.push_back(gr.get());
    gSzeneAendern([&]
                  {
        for (auto &gr : gruppen)
            scene.push_back(std::move(gr));
        fell::gHuellen.insert(fell::gHuellen.end(), neueHuellen.begin(), neueHuellen.end()); });
    eingefuegtMerken("Fell-Imitation", zeiger);
    computeSceneBounds();
    abgleichen();
    manipObjekteFuellen();
    textSetzen(g.lblManipInfo, L"Fell-Imitation: " + std::to_wstring(zeiger.size()) + L" Teil(e), je " + std::to_wstring(n) +
                                   L" Hüllen, " + zahlMitPunkten((long long)dreiecke) + L" Dreiecke" +
                                   (ohneFell ? L"; " + std::to_wstring(ohneFell) +
                                                   L" Teile ohne Fell (Augen, Nase, Schnurrhaare, zu kleine Teile)"
                                             : std::wstring()) +
                                   L". Rendern zeigt das Fell; die Hüllen folgen ihrem Teil.\n\n" + MANIP_ARTEN[MANIP_FELL].info);
}

// Esc beendet den Terrain-Pinsel (main.cpp)
bool PinselTaste(WPARAM taste)
{
    if (materialPinsel::gAktiv && taste == VK_ESCAPE) // Material-Pinsel beenden
    {
        materialPinselSetzen(false);
        return true;
    }
    if (!terrainPinsel::gAktiv || taste != VK_ESCAPE)
        return false;
    pinselBeenden();
    return true;
}

static void manipAusfuehren()
{
    const int art = int(SendMessageW(g.cmbManipArt, CB_GETCURSEL, 0, 0));
    if (art == MANIP_GLATT)
    {
        manipGlattSchattieren();
        return;
    }
    if (art == MANIP_VERTEILEN)
    {
        manipVerteilen();
        return;
    }
    if (art == MANIP_DREHEN)
    {
        manipZufallDrehen();
        return;
    }
    if (art == MANIP_VIELFACH)
    {
        manipVervielfaeltigen();
        return;
    }
    if (art == MANIP_ADDIEREN || art == MANIP_SUBTRAHIEREN)
    {
        manipMengen(art == MANIP_SUBTRAHIEREN);
        return;
    }
    if (art == MANIP_GROESSEN)
    {
        manipZufallGroessen();
        return;
    }
    if (art == MANIP_ABSETZEN)
    {
        manipAbsetzen();
        return;
    }
    if (art == MANIP_TERRAIN)
    {
        manipTerrainAbsenken();
        return;
    }
    if (art == MANIP_PINSEL)
    {
        pinselUmschalten();
        return;
    }
    if (art == MANIP_FELL)
    {
        manipFell();
        return;
    }
    // Ziel: die ausgewaehlten Objekte aus Dreiecken ("in Ebenen umwandeln": nur direkt in der Szene, nicht in Gruppen)
    std::vector<Mesh *> ziele;
    for (object *o : mitVorlagen(ausgewaehlteObjekte())) // Instanz: ihre Vorlage (wirkt auf alle Instanzen)
        if (auto *mm = dynamic_cast<Mesh *>(o))
            if (!mm->triangles.empty() && (art != 0 || objektIndex(mm) >= 0))
                ziele.push_back(mm);
    if (art < 0 || art >= ANZAHL_MANIP_ARTEN || ziele.empty() || !gSzeneAendern)
    {
        MessageBoxW(hObjektFenster, L"Bitte zuerst ein Objekt aus Dreiecken auswählen (in der Liste oder einer Ansicht anklicken):\n"
                                    L"Würfel, Wand, Körper, Terrain, OBJ-Import, Rotationsobjekt.",
                    L"Manipulation", MB_OK | MB_ICONINFORMATION);
        return;
    }
    fortschritt::Abschnitt fortschrittNetz(MANIP_ARTEN[art].name);
    for (size_t i = 0; i < ziele.size(); ++i)
    {
        fortschritt::schritt(i, ziele.size(), L"Objekt " + std::to_wstring(i + 1) + L" von " + std::to_wstring(ziele.size()));
        manipAufMesh(art, ziele[i]);
    }
}

static void manipAufMesh(int art, Mesh *m)
{
    const int index = objektIndex(m);
    const std::wstring name = objektGrundname(m, size_t(index >= 0 ? index : 0));
    const size_t vorher = m->triangles.size();

    // ----- Objekte in 3D-Ebenen umwandeln -----
    if (art == 0)
    {
        std::vector<std::vector<Dreieck>> teile = netz::flaechenZerlegen(m->triangles);
        bool einzelneDreiecke = false;
        if (teile.size() < 2)
        {
            // eine einzige ebene Flaeche (z.B. nach Porath/Faechern): stattdessen jedes Dreieck als Objekt
            if (m->triangles.size() < 2)
            {
                MessageBoxW(hObjektFenster, (L"„" + name + L"“ besteht nur aus einem Dreieck.").c_str(), L"Manipulation",
                            MB_OK | MB_ICONINFORMATION);
                return;
            }
            std::wstring frage = L"„" + name + L"“ ist eine einzige ebene Fläche aus " + std::to_wstring(m->triangles.size()) +
                                 L" Dreiecken.\nStattdessen jedes Dreieck als eigenes Objekt anlegen?\n"
                                 L"Das Original wird gelöscht (Rückgängig mit Strg+Z, zweimal).";
            if (MessageBoxW(hObjektFenster, frage.c_str(), L"Objekte in 3D-Ebenen umwandeln", MB_YESNO | MB_ICONQUESTION) != IDYES)
                return;
            teile.clear();
            for (auto &t : m->triangles)
                teile.push_back({t});
            einzelneDreiecke = true;
        }
        else
        {
            std::wstring frage = L"„" + name + L"“ in " + std::to_wstring(teile.size()) +
                                 L" Flächen-Objekte zerlegen?\nDas Original wird gelöscht (Rückgängig mit Strg+Z, zweimal).";
            if (MessageBoxW(hObjektFenster, frage.c_str(), L"Objekte in 3D-Ebenen umwandeln", MB_YESNO | MB_ICONQUESTION) != IDYES)
                return;
        }
        const std::string teilName = einzelneDreiecke ? std::string("Dreieck") : std::string("Fl\xC3\xA4" "che");
        const std::string basis = m->name.empty() ? teilName : m->name + " " + teilName;
        std::vector<std::unique_ptr<object>> neu;
        std::vector<object *> zeiger;
        for (size_t i = 0; i < teile.size(); ++i)
        {
            auto f = std::make_unique<Mesh>(teile[i], Vector3D(0, 0, 0));
            f->mat = m->mat;
            f->name = basis + " " + std::to_string(i + 1);
            f->texturSpiegelU = m->texturSpiegelU;
            f->texturSpiegelV = m->texturSpiegelV;
            f->texturDrehung = m->texturDrehung;
            f->texPar = m->texPar;
            if (m->texture)
            {
                f->setTexture(m->texture);
                f->textureName = m->textureName;
            }
            f->setTextureMode(m->textureMode);
            zeiger.push_back(f.get());
            neu.push_back(std::move(f));
        }
        gSzeneAendern([&]
                      {
            for (auto &o : neu)
                scene.push_back(std::move(o)); });
        eingefuegtMerken("Flächen als Objekte", zeiger);
        loeschenMitVerlauf("Original löschen", {m});
        auswahlSetzen(zeiger);
        computeSceneBounds();
        abgleichen();
        manipObjekteFuellen();
        textSetzen(g.lblManipInfo, std::to_wstring(zeiger.size()) + (einzelneDreiecke ? L" Dreieck-Objekte" : L" Flächen-Objekte") +
                                       L" erzeugt, „" + name + L"“ gelöscht.");
        return;
    }

    // ----- Operationen auf dem Netz selbst -----
    int tiefe = 1;
    if (art == 2)
    {
        double w = 1;
        if (!zahlLesen(g.editManipTiefe, w) || w < 0 || w > 6)
        {
            MessageBeep(MB_ICONWARNING);
            MessageBoxW(hObjektFenster, L"Rekursionstiefe: 0 bis 6", L"Porath-Triangulation", MB_OK | MB_ICONWARNING);
            return;
        }
        tiefe = int(std::round(w));
    }
    double wert = 0;
    if (art == MANIP_DICKE || art == MANIP_FAECHERN)
    {
        if (!zahlLesen(g.editManipWert, wert) || (art == MANIP_DICKE && std::fabs(wert) < 1e-12))
        {
            MessageBoxW(hObjektFenster, art == MANIP_DICKE ? L"Dicke: eine Zahl ungleich 0 (negativ = nach innen)"
                                                           : L"Höhe: eine Zahl (0 = flacher Fächer)",
                        MANIP_ARTEN[art].name, MB_OK | MB_ICONWARNING);
            return;
        }
    }
    // grosse Netze: vorher fragen
    double erwartet = double(vorher);
    if (art == 2)
        erwartet = double(vorher) * 2.0 * std::pow(3.0, tiefe);
    else if (art == MANIP_LOOP)
        erwartet = double(vorher) * 4.0;
    if (erwartet > 1e6)
    {
        std::wstring frage = L"Das Ergebnis hat etwa " + std::to_wstring((long long)erwartet) +
                             L" Dreiecke. Das kann lange dauern und viel Speicher brauchen. Trotzdem ausführen?";
        if (MessageBoxW(hObjektFenster, frage.c_str(), MANIP_ARTEN[art].name, MB_YESNO | MB_ICONWARNING) != IDYES)
            return;
    }

    // Dicke aus Dreiecken mit Vertex-Normalen: die Lage behaelt sie; Material und Textur wie das Original
    std::vector<Dreieck> d = m->triangles;
    std::wstring ergebnis;
    HCURSOR alt = SetCursor(LoadCursor(nullptr, IDC_WAIT));
    if (art == 1)
    {
        int flips = netz::delaunayFlippen(d);
        if (flips == 0)
        {
            SetCursor(alt);
            textSetzen(g.lblManipInfo, L"„" + name + L"“: keine Kante zu flippen, die Flächen sind schon Delaunay.");
            return;
        }
        ergebnis = std::to_wstring(flips) + L" Kanten umgeklappt";
    }
    else if (art == 2)
    {
        netz::porathTriangulieren(d, tiefe);
        ergebnis = L"Porath-Triangulation, Tiefe " + std::to_wstring(tiefe);
    }
    else if (art == MANIP_DICKE)
    {
        netz::dickeGeben(d, wert);
        ergebnis = L"Dicke " + zahlText(wert, 3);
    }
    else if (art == MANIP_FAECHERN)
    {
        netz::faechern(d, wert);
        ergebnis = L"aufgefächert, Höhe " + zahlText(wert, 3);
    }
    else
    {
        netz::loopUnterteilen(d);
        ergebnis = L"ein Loop-Schritt";
    }
    SetCursor(alt);
    if (d.empty())
        return;

    aenderungBeginnen(std::string("Manipulation"), {m});
    gSzeneAendern([&]
                  {
        m->triangles.swap(d);
        m->recomputeTriangleDataAndMeshBounds();
        m->buildBVH();
        if (m->texture)
            m->setTexture(m->texture);
        m->setTextureMode(m->textureMode); /* Textur und Bezugsbox auf die neuen Dreiecke */ });
    aenderungAbschliessen();
    computeSceneBounds();
    abgleichen();
    manipObjekteFuellen();
    textSetzen(g.lblManipInfo, L"„" + name + L"“: " + ergebnis + L"; " + std::to_wstring(vorher) + L" → " +
                                   std::to_wstring(m->triangles.size()) + L" Dreiecke. Rückgängig mit Strg+Z.");
}

// Anzeige im Texturfeld: nur der Dateiname ohne Ordner und ohne ".bmp" (z.B. "Schachbrett")
static std::string texturKurzname(const std::string &name)
{
    size_t schnitt = name.find_last_of("/\\");
    std::string k = schnitt == std::string::npos ? name : name.substr(schnitt + 1);
    if (k.size() > 4)
    {
        std::string ende = k.substr(k.size() - 4);
        for (char &c : ende)
            c = char(std::tolower((unsigned char)c));
        if (ende == ".bmp")
            k.resize(k.size() - 4);
    }
    return k;
}

static int texturBildArt()
{
    const int i = int(SendMessageW(g.cmbTexturBild, CB_GETCURSEL, 0, 0));
    return i >= 0 && i < BILD_ANZAHL ? i : 0;
}

static std::string texturKurzname(const std::string &name); // weiter unten

// weitere Textur-Einstellungen des Hauptobjekts anzeigen (Kachel, Winkel, Versatz, haftet, Kugel, weitere Bilder)
static void texturEinstellungenAnzeigen(const std::vector<object *> &objekte)
{
    const object *o0 = objekte.empty() ? nullptr : objekte.front();
    const bool aktiv = o0 != nullptr;
    const TexturParameter &p = o0 ? o0->texturPar() : TexturParameter::vorgabe();
    for (HWND h : {g.editTexturKachel, g.editTexturWinkel, g.editTexturVersatz[0], g.editTexturVersatz[1], g.btnTexturFest,
                   g.cmbTexturBild, g.editTexturBild, g.btnTexturBild, g.editTexturRinge, g.editTexturNass})
        aktivieren(h, aktiv);
    const bool kugel = aktiv && o0->textureMode == TextureMode::SPHERE;
    for (HWND h : g.editTexturKugel)
        aktivieren(h, kugel);
    if (kugel != texturKugelZeile && hObjektFenster) // Zeile "Kugel" ein-/ausblenden: neu anordnen
    {
        texturKugelZeile = kugel;
        RECT r;
        GetClientRect(hObjektFenster, &r);
        objektfensterLayout(r.right, r.bottom);
    }
    const int art = texturBildArt();
    aktivieren(g.editTexturStaerke, aktiv && art != BILD_ALPHA);
    static int altFest = -1;
    const int fest = aktiv && p.objektfest ? 1 : 0;
    if (fest != altFest)
    {
        altFest = fest;
        InvalidateRect(g.btnTexturFest, nullptr, FALSE);
    }
    if (!aktiv)
    {
        for (HWND h : {g.editTexturKachel, g.editTexturWinkel, g.editTexturVersatz[0], g.editTexturVersatz[1], g.editTexturKugel[0],
                       g.editTexturKugel[1], g.editTexturKugel[2], g.editTexturBild, g.editTexturStaerke, g.editTexturRinge,
                       g.editTexturNass})
            textSetzen(h, L"");
        return;
    }
    textSetzen(g.editTexturKachel, zahlText(p.kachel, 3));
    textSetzen(g.editTexturWinkel, zahlText(p.winkel, 1));
    textSetzen(g.editTexturVersatz[0], zahlText(p.versatzU, 3));
    textSetzen(g.editTexturVersatz[1], zahlText(p.versatzV, 3));
    const double kd[3] = {p.kugelDrehung.x, p.kugelDrehung.y, p.kugelDrehung.z};
    for (int k = 0; k < 3; ++k)
        textSetzen(g.editTexturKugel[k], zahlText(kd[k], 1));
    textSetzen(g.editTexturBild, ausUtf8(texturKurzname(p.bildName[art])));
    textSetzen(g.editTexturStaerke, art == BILD_ALPHA ? L"" : zahlText(p.staerke[art], art == BILD_RELIEF ? 4 : 2));
    textSetzen(g.editTexturRinge, zahlText(p.regenRinge, 2));
    textSetzen(g.editTexturNass, zahlText(p.nass, 2));
}

static void materialAnzeigen()
{
    std::vector<object *> objekte = materialObjekte();
    const bool aktiv = !objekte.empty();
    texturEinstellungenAnzeigen(objekte);

    for (HWND h : g.editMatFarbe)
        aktivieren(h, aktiv);
    for (HWND h : g.editMatWert)
        aktivieren(h, aktiv);
    for (HWND h : {g.cmbVorlage, g.editTextur, g.cmbTexturModus, g.btnTexturDatei})
        aktivieren(h, aktiv);
    {
        // Ausrichtung nur mit Textur; Zustand des Hauptobjekts zeigen
        const object *o0 = objekte.empty() ? nullptr : objekte.front();
        const bool mitTextur = o0 && o0->texture;
        static int altU = -1, altV = -1;
        const int u = mitTextur && o0->texturSpiegelU, v = mitTextur && o0->texturSpiegelV;
        const bool neuMalen = u != altU || v != altV;
        altU = u;
        altV = v;
        for (HWND h : {g.btnTexturU, g.btnTexturV, g.btnTexturDrehen})
        {
            aktivieren(h, mitTextur);
            if (neuMalen) // Zustand (Spiegeln an) hat sich geaendert
                InvalidateRect(h, nullptr, FALSE);
        }
        std::wstring dreh = L"Drehen 90\u00B0";
        if (mitTextur && (o0->texturDrehung & 3))
            dreh += L" (" + std::to_wstring((o0->texturDrehung & 3) * 90) + L"\u00B0)";
        textSetzen(g.btnTexturDrehen, dreh);
    }
    for (HWND h : g.btnMatFarbe)
        aktivieren(h, aktiv);

    if (!aktiv)
    {
        for (HWND h : g.editMatFarbe)
            textSetzen(h, L"");
        for (HWND h : g.editMatWert)
            textSetzen(h, L"");
        textSetzen(g.editTextur, L"");
        if (SendMessageW(g.cmbVorlage, CB_GETCURSEL, 0, 0) != -1)
            SendMessageW(g.cmbVorlage, CB_SETCURSEL, (WPARAM)-1, 0);
        if (materialAktiv)
        {
            materialAktiv = false;
            for (HWND h : g.btnMatFarbe)
                InvalidateRect(h, nullptr, FALSE);
        }
        return;
    }

    object *o = objekte.front();
    Material m = objektMaterial(o);

    for (int i = 0; i < MAT_FARBEN; ++i)
    {
        Farbe f = matFarbe(m, i);
        double werte[3] = {f.r, f.g, f.b};
        for (int k = 0; k < 3; ++k)
            textSetzen(g.editMatFarbe[i * 3 + k], zahlText(werte[k], 3));
        Farbe &alt = angezeigteMatFarben[i];
        if (!materialAktiv || alt.r != f.r || alt.g != f.g || alt.b != f.b)
        {
            alt = f;
            InvalidateRect(g.btnMatFarbe[i], nullptr, FALSE);
        }
    }
    materialAktiv = true;

    for (int i = 0; i < MAT_WERTE; ++i)
        textSetzen(g.editMatWert[i], zahlText(matWert(m, i), MAT_WERT_STELLEN[i]));

    // Vorlage: passt das Material genau zu einer, steht sie da; sonst "(eigenes Material)"
    int vorlage = 0;
    for (int i = 0; i < ANZAHL_VORLAGEN; ++i)
        if (materialGleich(m, MaterialLibrary::get(MAT_VORLAGEN[i].typ)))
            vorlage = i + 1;
    if (vorlage == 0)
        for (size_t i = 0; i < weitereVorlagen().size(); ++i)
            if (materialGleich(m, weitereVorlagen()[i].mat))
            {
                vorlage = 1 + ANZAHL_VORLAGEN + int(i);
                break;
            }
    if (GetFocus() != g.cmbVorlage && SendMessageW(g.cmbVorlage, CB_GETCURSEL, 0, 0) != vorlage)
        SendMessageW(g.cmbVorlage, CB_SETCURSEL, vorlage, 0);

    textSetzen(g.editTextur, ausUtf8(texturKurzname(o->textureName)));
    int modus = 0;
    for (int i = 0; i < TEXTUR_MODUS_ANZAHL; ++i)
        if (o->textureMode == TEXTUR_MODUS_WERTE[i])
            modus = i;
    if (GetFocus() != g.cmbTexturModus && SendMessageW(g.cmbTexturModus, CB_GETCURSEL, 0, 0) != modus)
        SendMessageW(g.cmbTexturModus, CB_SETCURSEL, modus, 0);
}

// Material aller ausgewaehlten Objekte aendern (mit Rueckgaengig). 'aendern' wird auf jedes
// Material einzeln angewandt: bei Meshes auf jedes Dreieck, so bleiben z.B. verschiedene Farben
// erhalten, wenn nur die Spiegelung geaendert wird.
static void materialAendern(const char *name, const std::function<void(Material &)> &aendern)
{
    std::vector<object *> objekte = materialObjekte();
    if (objekte.empty())
        return;

    aenderungBeginnen(name, objekte);
    {
        std::lock_guard<std::mutex> lock(sceneMutex); // GPU-Ansichten lesen die Dreiecke
        std::function<void(object *)> anwenden = [&](object *o)
        {
            if (dynamic_cast<Tree *>(o))
                return; // Holz und Blaetter: erst "Baum aufteilen"
            aendern(o->mat);
            if (auto *g = dynamic_cast<Gruppe *>(o))
                for (auto &k : g->kinder)
                    anwenden(k.get());
            else if (auto *m = dynamic_cast<Mesh *>(o))
                for (auto &t : m->triangles)
                    aendern(t.mat);
            else if (auto *pf = dynamic_cast<Pfeil *>(o))
                for (auto &t : pf->tris)
                    aendern(t.mat);
            else if (auto *tx = dynamic_cast<Text3D *>(o))
                tx->setMaterial(o->mat);
        };
        for (object *o : objekte)
            anwenden(o);
    }
    aenderungAbschliessen();
    materialAnzeigen();
    eigenschaftenAnzeigen();
}

static void materialFarbeUebernehmen(int zeile)
{
    bool geaendert = false;
    for (int k = 0; k < 3; ++k)
        geaendert = geaendert || SendMessageW(g.editMatFarbe[zeile * 3 + k], EM_GETMODIFY, 0, 0);
    if (!geaendert)
        return;
    double w[3];
    for (int k = 0; k < 3; ++k)
    {
        SendMessageW(g.editMatFarbe[zeile * 3 + k], EM_SETMODIFY, FALSE, 0);
        if (!zahlLesen(g.editMatFarbe[zeile * 3 + k], w[k]) || w[k] < 0)
        {
            SendMessageW(g.editMatFarbe[zeile * 3 + k], EM_SETMODIFY, TRUE, 0);
            MessageBeep(MB_ICONWARNING);
            return;
        }
    }
    Farbe f(w[0], w[1], w[2]);
    materialAendern("Material", [&](Material &m)
                    { matFarbe(m, zeile) = f; });
}

static void materialWertUebernehmen(int i)
{
    HWND h = g.editMatWert[i];
    if (!SendMessageW(h, EM_GETMODIFY, 0, 0))
        return;
    SendMessageW(h, EM_SETMODIFY, FALSE, 0);
    double v;
    if (!zahlLesen(h, v) || v < 0 || v > MAT_WERT_MAX[i])
    {
        SendMessageW(h, EM_SETMODIFY, TRUE, 0);
        MessageBeep(MB_ICONWARNING);
        return;
    }
    materialAendern("Material", [&](Material &m)
                    { matWert(m, i) = v; });
}

static void materialFarbdialog(int zeile)
{
    if (!materialAktiv)
        return;
    Farbe a = angezeigteMatFarben[zeile];
    CHOOSECOLORW cc = {};
    cc.lStructSize = sizeof(cc);
    cc.hwndOwner = hObjektFenster;
    cc.rgbResult = RGB(kanal(std::min(1.0, a.r)), kanal(std::min(1.0, a.g)), kanal(std::min(1.0, a.b)));
    cc.lpCustColors = eigeneFarben;
    cc.Flags = CC_FULLOPEN | CC_RGBINIT | CC_ANYCOLOR;
    if (!ChooseColorW(&cc))
        return;
    Farbe f(GetRValue(cc.rgbResult) / 255.0, GetGValue(cc.rgbResult) / 255.0, GetBValue(cc.rgbResult) / 255.0);
    materialAendern("Material", [&](Material &m)
                    { matFarbe(m, zeile) = f; });
}

static void vorlageUebernehmen()
{
    int i = int(SendMessageW(g.cmbVorlage, CB_GETCURSEL, 0, 0)) - 1;
    if (i >= ANZAHL_VORLAGEN && i - ANZAHL_VORLAGEN < int(weitereVorlagen().size()))
    {
        // weitere Vorlage: wird als eigenes Material gespeichert (kein Name aus der MaterialLibrary)
        const Material vorlage = weitereVorlagen()[size_t(i - ANZAHL_VORLAGEN)].mat;
        materialAendern("Materialvorlage", [&](Material &m)
                        { m = vorlage; });
        for (object *o : materialObjekte())
            if (!dynamic_cast<Mesh *>(o))
                o->materialName.clear();
        return;
    }
    if (i < 0 || i >= ANZAHL_VORLAGEN)
        return; // "(eigenes Material)": nichts zu tun
    const MaterialVorlage &v = MAT_VORLAGEN[i];
    Material vorlage = MaterialLibrary::get(v.typ);
    SceneLoader::materialMap[v.id] = vorlage; // "Save XML" schreibt das Material mit

    materialAendern("Materialvorlage", [&](Material &m)
                    { m = vorlage; });
    for (object *o : materialObjekte())
        if (!dynamic_cast<Mesh *>(o)) // bei Meshes steht in materialName die OBJ-Datei
            o->materialName = v.id;
}

// Textur fuer die Material-Objekte setzen: Name aus textures.txt oder Pfad einer Bilddatei.
// Ist noch kein Modus gewaehlt ("keine"), wird einer passend zum Objekt gesetzt (sonst bliebe die Textur unsichtbar).
static void texturSetzen(const std::string &name)
{
    std::shared_ptr<Texture> tex;
    if (!name.empty())
    {
        tex = TextureManager::get().getTexture(name); // Name aus textures.txt
        if (!tex)
            tex = TextureManager::get().load(name); // sonst als Datei
        if (!tex || tex->width() <= 0 || tex->height() <= 0)
        {
            std::wstring text = L"Die Textur \"" + ausUtf8(name) +
                                L"\" konnte nicht geladen werden.\nErwartet: ein Name aus textures.txt oder eine Bilddatei (BMP, PNG, JPG, GIF, TIFF).";
            MessageBoxW(hObjektFenster, text.c_str(), L"Textur", MB_OK | MB_ICONWARNING);
            return;
        }
    }

    std::vector<object *> objekte = materialObjekte();
    aenderungBeginnen("Textur", objekte);
    {
        std::lock_guard<std::mutex> lock(sceneMutex);
        for (object *o : objekte)
        {
            const bool neu = tex && !o->texture; // erste Textur: haftet am Objekt (dreht und bewegt sich mit)
            o->setTexture(tex);
            o->textureName = name;
            if (tex && o->textureMode == TextureMode::None)
                o->setTextureMode(dynamic_cast<Kugel *>(o) ? TextureMode::SPHERE
                                  : dynamic_cast<Stab *>(o) ? TextureMode::CYLINDER
                                                            : TextureMode::BOX); // Wuerfel: jede Seite gerade
            if (neu && !o->texturPar().objektfest)
            {
                texturParAendern(o, [](TexturParameter &p) { p.objektfest = true; });
                if (auto *m = dynamic_cast<Mesh *>(o))
                    m->texturNeuFestmachen();
            }
        }
    }
    aenderungAbschliessen();
    materialAnzeigen();
    // zur Kontrolle in der Konsole
    if (tex)
        std::cout << "Textur \"" << name << "\" (" << tex->width() << " x " << tex->height() << ") auf "
                  << objekte.size() << " Objekt(e)" << std::endl;
}

static void texturUebernehmen()
{
    if (!SendMessageW(g.editTextur, EM_GETMODIFY, 0, 0))
        return;
    SendMessageW(g.editTextur, EM_SETMODIFY, FALSE, 0);

    wchar_t puffer[MAX_PATH] = {};
    GetWindowTextW(g.editTextur, puffer, MAX_PATH);
    std::string name = zuUtf8(puffer);
    while (!name.empty() && name.back() == ' ')
        name.pop_back();
    // unveraendert angezeigter Kurzname: die bisherige Textur (mit vollem Pfad) bleibt
    std::vector<object *> mo = materialObjekte();
    if (!mo.empty() && !name.empty() && name == texturKurzname(mo.front()->textureName))
        return;
    texturSetzen(name);
}

// Knoepfe "Spiegeln ↔/↕" und "Drehen 90°": Ausrichtung der Textur der Material-Objekte
static void texturAusrichten(int id)
{
    std::vector<object *> objekte = materialObjekte();
    if (objekte.empty())
        return;
    const object *o0 = objekte.front();
    const bool u = !o0->texturSpiegelU, v = !o0->texturSpiegelV; // alle wie das Hauptobjekt umschalten
    {
        std::lock_guard<std::mutex> lock(sceneMutex);
        for (object *o : objekte)
        {
            if (id == ID_BTN_TEXTUR_SPIEGEL_U)
                o->texturSpiegelU = u;
            else if (id == ID_BTN_TEXTUR_SPIEGEL_V)
                o->texturSpiegelV = v;
            else
                o->texturDrehung = (o->texturDrehung + 1) & 3;
            if (auto *m = dynamic_cast<Mesh *>(o))
                m->texturBoxSetzen(); // auf die Dreiecke
        }
    }
    szeneGeaendertMarkieren();
    materialAnzeigen();
}

static std::string bildDateiWaehlen(const wchar_t *titel); // weiter unten

// Knopf "…" neben dem Texturnamen: Bilddatei waehlen
static void texturDateiWaehlen()
{
    if (materialObjekte().empty())
    {
        MessageBeep(MB_ICONWARNING);
        return;
    }
    const std::string pfad = bildDateiWaehlen(L"Textur wählen");
    if (!pfad.empty())
        texturSetzen(pfad);
}

// Dateiauswahl fuer Bilder (Textur und weitere Bilder); leer = abgebrochen
static std::string bildDateiWaehlen(const wchar_t *titel)
{
    wchar_t datei[MAX_PATH] = L"";
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hObjektFenster;
    ofn.lpstrFilter = L"Bilder (*.bmp;*.png;*.jpg;*.jpeg;*.gif;*.tif;*.tiff)\0*.bmp;*.png;*.jpg;*.jpeg;*.gif;*.tif;*.tiff\0"
                      L"Alle Dateien (*.*)\0*.*\0";
    ofn.lpstrFile = datei;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = titel;
    ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&ofn))
        return std::string();
    // Pfad im Zeichensatz, den Texture::loadBMP (std::string, fopen/ifstream) erwartet
    int laenge = WideCharToMultiByte(CP_ACP, 0, datei, -1, nullptr, 0, nullptr, nullptr);
    std::string pfad(size_t(std::max(1, laenge) - 1), '\0');
    WideCharToMultiByte(CP_ACP, 0, datei, -1, &pfad[0], laenge, nullptr, nullptr);
    return pfad;
}

static void texturModusUebernehmen()
{
    int i = int(SendMessageW(g.cmbTexturModus, CB_GETCURSEL, 0, 0));
    if (i < 0 || i >= TEXTUR_MODUS_ANZAHL)
        return;
    std::vector<object *> objekte = materialObjekte();
    aenderungBeginnen("Texturmodus", objekte);
    {
        std::lock_guard<std::mutex> lock(sceneMutex);
        for (object *o : objekte)
            o->setTextureMode(TEXTUR_MODUS_WERTE[i]);
    }
    aenderungAbschliessen();
}

// weitere Textur-Einstellungen aller Material-Objekte aendern (mit Rueckgaengig); Netze geben sie an ihre Dreiecke
// weiter. neuFestmachen: Ruhelage der Textur aus der jetzigen Lage (haftet am Objekt eingeschaltet).
static void texturParFuerAlle(const char *name, const std::function<void(TexturParameter &)> &aendern, bool neuFestmachen = false)
{
    std::vector<object *> objekte = materialObjekte();
    if (objekte.empty())
        return;
    aenderungBeginnen(name, objekte);
    {
        std::lock_guard<std::mutex> lock(sceneMutex);
        for (object *o : objekte)
        {
            texturParAendern(o, aendern);
            if (auto *m = dynamic_cast<Mesh *>(o))
            {
                if (neuFestmachen)
                    m->texturNeuFestmachen();
                else
                    m->texturBoxSetzen();
            }
            else if (auto *in = dynamic_cast<Instanz *>(o))
                (void)in; // Instanz: eigene Einstellungen (Abbildung in die Vorlage)
        }
    }
    aenderungAbschliessen();
    materialAnzeigen();
}

// Eingabefelder Kachel, Winkel, Versatz, Kugel-Drehung, Staerke
static void texturWertUebernehmen(int id)
{
    HWND h = GetDlgItem(hObjektFenster, id);
    if (!h || !SendMessageW(h, EM_GETMODIFY, 0, 0))
        return;
    SendMessageW(h, EM_SETMODIFY, FALSE, 0);
    double v = 0;
    if (!zahlLesen(h, v) || !std::isfinite(v))
    {
        SendMessageW(h, EM_SETMODIFY, TRUE, 0);
        MessageBeep(MB_ICONWARNING);
        return;
    }
    if (id == ID_EDIT_TEXTUR_KACHEL && v < 0)
    {
        MessageBeep(MB_ICONWARNING);
        materialAnzeigen();
        return;
    }
    const int art = texturBildArt();
    texturParFuerAlle("Textur", [&](TexturParameter &p)
                      {
        switch (id)
        {
        case ID_EDIT_TEXTUR_KACHEL: p.kachel = v; break;
        case ID_EDIT_TEXTUR_WINKEL: p.winkel = std::fmod(v, 360.0); break;
        case ID_EDIT_TEXTUR_VERSATZ_U: p.versatzU = v; break;
        case ID_EDIT_TEXTUR_VERSATZ_V: p.versatzV = v; break;
        case ID_EDIT_TEXTUR_KUGEL_X: p.kugelDrehung.x = v; break;
        case ID_EDIT_TEXTUR_KUGEL_X + 1: p.kugelDrehung.y = v; break;
        case ID_EDIT_TEXTUR_KUGEL_X + 2: p.kugelDrehung.z = v; break;
        case ID_EDIT_TEXTUR_RINGE: p.regenRinge = std::max(0.0, v); break;
        case ID_EDIT_TEXTUR_NASS: p.nass = std::clamp(v, 0.0, 1.0); break;
        case ID_EDIT_TEXTUR_STAERKE:
            p.staerke[art] = (art == BILD_GLANZ || art == BILD_UNTEN) ? std::clamp(v, 0.0, 1.0)
                             : art == BILD_LEUCHTEN                     ? std::max(0.0, v)
                                                                        : v;
            break;
        default: break;
        } });
}

// Knopf "Haftet am Objekt": umschalten (wie beim Hauptobjekt); einschalten macht die jetzige Lage zur Ruhelage
static void texturFestUmschalten()
{
    std::vector<object *> objekte = materialObjekte();
    if (objekte.empty())
        return;
    const bool an = !objekte.front()->texturPar().objektfest;
    texturParFuerAlle(an ? "Textur am Objekt festmachen" : "Textur in der Welt", [&](TexturParameter &p) { p.objektfest = an; }, an);
    szeneGeaendertMarkieren();
}

// weiteres Bild (Relief, Alpha, Glanz, Leuchten) setzen; leerer Name entfernt es
static void texturBildSetzen(const std::string &name)
{
    const int art = texturBildArt();
    std::shared_ptr<Texture> tex;
    if (!name.empty())
    {
        tex = TextureManager::get().getTexture(name);
        if (!tex)
            tex = TextureManager::get().load(name);
        if (!tex || tex->width() <= 0 || tex->height() <= 0)
        {
            std::wstring text = L"Das Bild \"" + ausUtf8(name) + L"\" konnte nicht geladen werden (BMP, PNG, JPG, GIF oder TIFF).";
            MessageBoxW(hObjektFenster, text.c_str(), L"Textur", MB_OK | MB_ICONWARNING);
            return;
        }
    }
    texturParFuerAlle("Weiteres Bild", [&](TexturParameter &p)
                      {
        p.bild[art] = tex;
        p.bildName[art] = tex ? name : std::string(); });
}

static void texturBildUebernehmen()
{
    if (!SendMessageW(g.editTexturBild, EM_GETMODIFY, 0, 0))
        return;
    SendMessageW(g.editTexturBild, EM_SETMODIFY, FALSE, 0);
    wchar_t puffer[MAX_PATH] = {};
    GetWindowTextW(g.editTexturBild, puffer, MAX_PATH);
    std::string name = zuUtf8(puffer);
    while (!name.empty() && name.back() == ' ')
        name.pop_back();
    std::vector<object *> mo = materialObjekte();
    if (!mo.empty() && !name.empty() && name == texturKurzname(mo.front()->texturPar().bildName[texturBildArt()]))
        return; // unveraendert angezeigter Kurzname
    texturBildSetzen(name);
}

static void texturBildDateiWaehlen()
{
    if (materialObjekte().empty())
    {
        MessageBeep(MB_ICONWARNING);
        return;
    }
    const std::string pfad = bildDateiWaehlen(L"Weiteres Bild wählen");
    if (!pfad.empty())
        texturBildSetzen(pfad);
}

//--------------------------------------------------
// Abgleich mit den Ansichten (Auswahl, Modus, Szene)
//--------------------------------------------------

static void listenAuswahlAbgleichen()
{
    Auswahl a = aktuelleAuswahl();
    HTREEITEM eintrag = nullptr;

    if (a.typ == SelectedType::OBJECT)
    {
        eintrag = eintragVon(selectedObject);
        if (!eintrag) // z.B. Liste noch nicht neu aufgebaut
        {
            int i = objektIndex(selectedObject);
            if (i >= 0 && size_t(i) < eintraegeObjekte.size())
                eintrag = eintraegeObjekte[size_t(i)];
        }
    }
    else if (a.typ == SelectedType::LIGHT)
    {
        int i = lichtIndex(selectedLight);
        if (i >= 0 && size_t(i) < eintraegeLichter.size())
            eintrag = eintraegeLichter[size_t(i)];
    }
    else if (a.typ == SelectedType::CAMERA)
        eintrag = eintragKamera;

    eintragAuswaehlen(eintrag);
    angezeigteAuswahl = a;
}

//--------------------------------------------------
// Fenster "Render-Einstellungen": globale Werte aus scene.cpp (Nebel, Rauch, Wolken, Qualitaet ...)
// Aenderungen ueber gSzeneAendern: das Raytracing laeuft dabei nicht (render.cpp liest die Werte pro Pixel,
// die Rauchboxen pro Strahl).
//--------------------------------------------------

static bool istGedimmt(HWND h);
static void buttonZeichnen(const DRAWITEMSTRUCT *d);
static void tooltip(HWND fenster, const wchar_t *text);
static HWND hEinstellungen = nullptr;
static bool bildGeladen = false;    // Umgebungsbild in backgroundTexture (initScene oder "Bild ...")
static std::string bildDatei;       // zuletzt geladene Datei (wird in der XML gespeichert)
static int rauchAuswahl = 0;        // gewaehlte Rauchbox
static HWND einAnimSchalter = nullptr, einAnimDauer = nullptr, einAnimBilder = nullptr;
static void zeitleisteUmschalten(bool an);
static void zeitleisteAbgleichen();
static void einpassKnoepfeSetzen();
static void hauptfensterAnordnen();
static void hauptfensterAnlegen(HINSTANCE hInst);
static void fortschrittAnzeigen();

struct EinstellZahl
{
    HWND edit = nullptr;
    std::function<double()> lesen;
    std::function<void(double)> schreiben;
    double minimum, maximum;
    int stellen;
    bool rauch = false; // gehoert zur gewaehlten Rauchbox
};
struct EinstellSchalter
{
    HWND knopf = nullptr;
    bool *ziel;
};
struct EinstellFarbe
{
    HWND knopf = nullptr;
    std::function<Farbe *()> ziel; // nullptr: nicht verfuegbar (z.B. keine Rauchbox)
};

static std::vector<EinstellZahl> einZahlen;
static std::vector<EinstellSchalter> einSchalter;
static std::vector<EinstellFarbe> einFarben;
static HWND einRauchListe = nullptr, einBildText = nullptr;
static int naechsteEinId = ID_EIN_ERSTES;

// ---------- Bildgroesse: Vorlagen und zuletzt benutzte Groesse ----------
struct BildFormat
{
    const wchar_t *name;
    int breite, hoehe; // 0 = wie das Render-Fenster, -1 = andere Groesse (Felder)
};
static const BildFormat BILD_FORMATE[] = {
    {L"Wie Render-Fenster", 0, 0},
    {L"Full HD (1920 × 1080)", 1920, 1080},
    {L"2K (2560 × 1440)", 2560, 1440},
    {L"4K (3840 × 2160)", 3840, 2160},
    {L"8K (7680 × 4320)", 7680, 4320},
    {L"Andere Größe", -1, -1}};
constexpr int ANZAHL_BILDFORMATE = int(sizeof(BILD_FORMATE) / sizeof(BILD_FORMATE[0]));
static HWND einBildFormat = nullptr, einBildBreiteFeld = nullptr;

static int bildFormatIndex()
{
    for (int i = 0; i < ANZAHL_BILDFORMATE - 1; ++i)
        if (BILD_FORMATE[i].breite == renderBreite && BILD_FORMATE[i].hoehe == renderHoehe)
            return i;
    return ANZAHL_BILDFORMATE - 1;
}

// Programm-Einstellungen neben der .exe (raytracer_editor.ini): zuletzt benutzte Bildgroesse
static std::string editorIniPfad()
{
    char pfad[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, pfad, MAX_PATH);
    std::string p = pfad;
    size_t schnitt = p.find_last_of("/\\");
    return (schnitt == std::string::npos ? std::string() : p.substr(0, schnitt + 1)) + "raytracer_editor.ini";
}
static int gespeicherteBreite = -2, gespeicherteHoehe = -2, gespeicherteObjektBreite = 0;
static std::string letzteSzeneDatei, gespeicherteLetzteSzene; // zuletzt geoeffnete/gespeicherte Szene
// Menue "Datei -> Zuletzt geoeffnet": neueste zuerst
static const size_t ZULETZT_ANZAHL = 10;
static std::vector<std::string> zuletztGeoeffnet, gespeicherteZuletzt;
static double gespeichertesAutoWarten = 3.0;
static bool gespeichertesAutoAn = false;
static void editorEinstellungenSpeichernFallsGeaendert();

static void editorEinstellungenLaden()
{
    FILE *f = std::fopen(editorIniPfad().c_str(), "r");
    if (!f)
        return;
    char zeile[256];
    while (std::fgets(zeile, sizeof(zeile), f))
    {
        int wert = 0;
        if (std::sscanf(zeile, "bildBreite=%d", &wert) == 1)
            renderBreite = std::max(0, wert);
        else if (std::sscanf(zeile, "bildHoehe=%d", &wert) == 1)
            renderHoehe = std::max(0, wert);
        else if (std::sscanf(zeile, "objektBreite=%d", &wert) == 1)
            objektBreite = std::max(0, wert);
        else if (std::strncmp(zeile, "kamerasicht=", 12) == 0)
        {
            std::string g = zeile + 12;
            while (!g.empty() && (g.back() == '\n' || g.back() == '\r'))
                g.pop_back();
            kamerasichtGeraet = std::wstring(g.begin(), g.end()); // Geraetename wie "\\.\DISPLAY2" (ASCII)
            gespeichertesKamerasichtGeraet = kamerasichtGeraet;
        }
        else if (std::strncmp(zeile, "autoWarten=", 11) == 0)
            autoWarten = std::clamp(std::atof(zeile + 11), 0.5, 120.0);
        else if (std::sscanf(zeile, "autoRendern=%d", &wert) == 1)
            autoRendernAn = wert != 0;
        else if (std::strncmp(zeile, "zuletzt=", 8) == 0)
        {
            std::string pfad = zeile + 8;
            while (!pfad.empty() && (pfad.back() == '\n' || pfad.back() == '\r'))
                pfad.pop_back();
            if (!pfad.empty() && zuletztGeoeffnet.size() < ZULETZT_ANZAHL &&
                std::find(zuletztGeoeffnet.begin(), zuletztGeoeffnet.end(), pfad) == zuletztGeoeffnet.end())
                zuletztGeoeffnet.push_back(pfad);
        }
        else if (std::strncmp(zeile, "letzteSzene=", 12) == 0)
        {
            std::string pfad = zeile + 12;
            while (!pfad.empty() && (pfad.back() == '\n' || pfad.back() == '\r'))
                pfad.pop_back();
            letzteSzeneDatei = pfad;
        }
    }
    std::fclose(f);
    gespeicherteBreite = renderBreite;
    gespeicherteHoehe = renderHoehe;
    gespeicherteObjektBreite = objektBreite;
    gespeicherteLetzteSzene = letzteSzeneDatei;
    // aeltere .ini ohne Liste: die letzte Szene steht vorne
    if (!letzteSzeneDatei.empty() &&
        std::find(zuletztGeoeffnet.begin(), zuletztGeoeffnet.end(), letzteSzeneDatei) == zuletztGeoeffnet.end())
        zuletztGeoeffnet.insert(zuletztGeoeffnet.begin(), letzteSzeneDatei);
    gespeicherteZuletzt = zuletztGeoeffnet;
    gespeichertesAutoWarten = autoWarten;
    gespeichertesAutoAn = autoRendernAn;
}

// bei Aenderung (Abgleich alle 150 ms): Groesse fuer den naechsten Programmstart merken
static void editorEinstellungenSpeichernFallsGeaendert()
{
    if (renderBreite == gespeicherteBreite && renderHoehe == gespeicherteHoehe && objektBreite == gespeicherteObjektBreite &&
        letzteSzeneDatei == gespeicherteLetzteSzene && kamerasichtGeraet == gespeichertesKamerasichtGeraet &&
        zuletztGeoeffnet == gespeicherteZuletzt && autoWarten == gespeichertesAutoWarten && autoRendernAn == gespeichertesAutoAn)
        return;
    gespeicherteZuletzt = zuletztGeoeffnet;
    gespeichertesAutoWarten = autoWarten;
    gespeichertesAutoAn = autoRendernAn;
    gespeichertesKamerasichtGeraet = kamerasichtGeraet;
    gespeicherteLetzteSzene = letzteSzeneDatei;
    gespeicherteBreite = renderBreite;
    gespeicherteHoehe = renderHoehe;
    gespeicherteObjektBreite = objektBreite;
    if (FILE *f = std::fopen(editorIniPfad().c_str(), "w"))
    {
        std::fprintf(f, "bildBreite=%d\nbildHoehe=%d\n", renderBreite, renderHoehe);
        if (objektBreite > 0)
            std::fprintf(f, "objektBreite=%d\n", objektBreite);
        if (!letzteSzeneDatei.empty())
            std::fprintf(f, "letzteSzene=%s\n", letzteSzeneDatei.c_str());
        if (!kamerasichtGeraet.empty())
            std::fprintf(f, "kamerasicht=%s\n", std::string(kamerasichtGeraet.begin(), kamerasichtGeraet.end()).c_str());
        std::fprintf(f, "autoWarten=%.2f\nautoRendern=%d\n", autoWarten, autoRendernAn ? 1 : 0);
        for (const std::string &z : zuletztGeoeffnet)
            std::fprintf(f, "zuletzt=%s\n", z.c_str());
        std::fclose(f);
    }
}

// ---------- Reiter im Fenster "Render-Einstellungen" ----------
// Jede Ueberschrift beginnt einen Abschnitt; dazu gehoeren alle Steuerelemente derselben Spalte bis zur
// naechsten Ueberschrift. Die Abschnitte (Bild, Qualitaet, Kamera, Animation, Stil, Nebel, Rauch, Wolken,
// Volumenschatten) erscheinen oben als Reiter; angezeigt wird immer nur der Inhalt des gewaehlten Reiters
// (vorher: aufklappbare Abschnitte in zwei Spalten, das Fenster wuchs und schrumpfte beim Auf-/Zuklappen).
struct EinAbschnitt
{
    HWND kopf = nullptr; // urspruengliche Ueberschrift (jetzt ausgeblendet; der Reiter ersetzt sie)
    std::wstring titel;
    bool klappbar = false, offen = true;
    const bool *schalter = nullptr; // Punkt im Reiter, wenn eingeschaltet (Nebel, Rauch ...)
    int spalte = 0;
    int yKopf = 0, yInhalt = 0, yEnde = 0; // urspruengliche Lage
    struct Teil
    {
        HWND h;
        int x0, y0; // urspruengliche Lage
    };
    std::vector<Teil> inhalt;
};
static std::vector<EinAbschnitt> einAbschnitte;
static std::vector<std::pair<HWND, int>> einFuss; // Hinweis und "Schliessen" unter dem Inhalt
static int einFussY = 0, einRand = 14, einFensterB = 0, einFussH = 0, einSpalteRechts = 0;
static HWND einReiterLeiste = nullptr;
static int einReiter = 0;                   // gewaehlter Reiter = Nummer des Abschnitts
static std::vector<RECT> einReiterFlaechen; // je Abschnitt, in der Reiterleiste
static std::vector<std::wstring> einReiterTexte;

static std::wstring abschnittTitel(const EinAbschnitt &a)
{
    static const std::pair<const wchar_t *, const wchar_t *> namen[] = {
        {L"BILD", L"Bild"},
        {L"QUALITÄT", L"Qualität"},
        {L"KAMERA (TIEFENSCHÄRFE)", L"Kamera"},
        {L"ANIMATION", L"Animation"},
        {L"STIL", L"Stil"},
        {L"NEBEL", L"Nebel"},
        {L"RAUCH", L"Rauch"},
        {L"WOLKENVOLUMEN", L"Wolken"},
        {L"VOLUMENSCHATTEN", L"Volumenschatten"},
        {L"REGEN", L"Regen"}};
    std::wstring t = a.titel;
    for (auto &[gross, klein] : namen)
        if (a.titel == gross)
            t = klein;
    if (a.schalter && *a.schalter)
        t += L" •"; // eingeschaltet
    return t;
}

// Reiter in Zeilen anordnen (umbrechen, wenn sie nicht in eine Zeile passen); liefert die Hoehe
static int einReiterAnordnen(int breite)
{
    einReiterFlaechen.assign(einAbschnitte.size(), RECT{});
    einReiterTexte.assign(einAbschnitte.size(), std::wstring());
    HDC dc = GetDC(nullptr);
    HGDIOBJ alt = SelectObject(dc, gFont);
    int x = 0, y = 0;
    const int h = REITER_H - 4;
    for (size_t i = 0; i < einAbschnitte.size(); ++i)
    {
        einReiterTexte[i] = abschnittTitel(einAbschnitte[i]);
        RECT t = {0, 0, 0, 0};
        DrawTextW(dc, einReiterTexte[i].c_str(), -1, &t, DT_CALCRECT | DT_SINGLELINE);
        const int b = int(t.right - t.left) + 24;
        if (x > 0 && x + b > breite)
        {
            x = 0;
            y += h + 4;
        }
        einReiterFlaechen[i] = RECT{x, y, x + b, y + h};
        x += b + 4;
    }
    SelectObject(dc, alt);
    ReleaseDC(nullptr, dc);
    return einAbschnitte.empty() ? 0 : y + h;
}

static void einstellungenAnordnen();

static LRESULT CALLBACK EinReiterProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT r;
        GetClientRect(hwnd, &r);
        FillRect(hdc, &r, pinselHintergrund);
        SetBkMode(hdc, TRANSPARENT);
        HGDIOBJ alt = SelectObject(hdc, gFont);
        for (size_t i = 0; i < einReiterFlaechen.size() && i < einReiterTexte.size(); ++i)
        {
            RECT t = einReiterFlaechen[i];
            const bool aktiv = int(i) == einReiter;
            HBRUSH p = CreateSolidBrush(aktiv ? FARBE_FLAECHE : FARBE_BUTTON);
            FillRect(hdc, &t, p);
            DeleteObject(p);
            if (aktiv) // gewaehlter Reiter: blauer Strich oben (wie "Ansichten | Rendering")
            {
                RECT strich = {t.left, t.top, t.right, t.top + 2};
                HBRUSH b = CreateSolidBrush(FARBE_AKTIV);
                FillRect(hdc, &strich, b);
                DeleteObject(b);
            }
            SetTextColor(hdc, aktiv ? FARBE_TEXT : FARBE_TEXT_GEDIMMT);
            DrawTextW(hdc, einReiterTexte[i].c_str(), -1, &t, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
        SelectObject(hdc, alt);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_LBUTTONDOWN:
    {
        POINT p = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        for (size_t i = 0; i < einReiterFlaechen.size(); ++i)
            if (PtInRect(&einReiterFlaechen[i], p) && int(i) != einReiter)
            {
                einReiter = int(i);
                einstellungenAnordnen();
                einstellungenAnzeigen();
                break;
            }
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// nach dem Anlegen: Steuerelemente den Abschnitten zuordnen (urspruengliche Positionen)
static void abschnitteErfassen()
{
    for (auto &a : einAbschnitte)
        a.inhalt.clear();
    einFuss.clear();
    for (HWND h = GetWindow(hEinstellungen, GW_CHILD); h; h = GetWindow(h, GW_HWNDNEXT))
    {
        if (h == einReiterLeiste)
            continue;
        RECT r;
        GetWindowRect(h, &r);
        MapWindowPoints(nullptr, hEinstellungen, reinterpret_cast<POINT *>(&r), 2);
        bool kopf = false;
        for (auto &a : einAbschnitte)
            kopf = kopf || a.kopf == h;
        if (kopf)
            continue;
        if (r.top >= einFussY)
        {
            einFuss.push_back({h, int(r.top)});
            continue;
        }
        const int spalte = r.left >= einSpalteRechts ? 1 : 0;
        EinAbschnitt *ziel = nullptr;
        for (auto &a : einAbschnitte)
            if (a.spalte == spalte && r.top >= a.yKopf && (!ziel || a.yKopf > ziel->yKopf))
                ziel = &a;
        if (ziel)
            ziel->inhalt.push_back({h, int(r.left), int(r.top)});
    }
}

static void einstellungenAnordnen()
{
    // eine Spalte breit; alle Reiter gleich hoch (der hoechste Inhalt), damit das Fenster beim Wechseln nicht springt
    const int spalteB = einSpalteRechts - 2 * einRand;
    const int breite = einRand + spalteB + einRand;
    einFensterB = breite;
    const int reiterH = einReiterAnordnen(spalteB);
    if (einReiterLeiste)
    {
        SetWindowPos(einReiterLeiste, nullptr, einRand, einRand, spalteB, reiterH, SWP_NOZORDER | SWP_NOACTIVATE);
        InvalidateRect(einReiterLeiste, nullptr, FALSE);
    }
    if (einReiter < 0 || einReiter >= int(einAbschnitte.size()))
        einReiter = 0;
    const int oben = einRand + reiterH + 12;
    int inhaltH = 0;
    for (auto &a : einAbschnitte)
        inhaltH = std::max(inhaltH, a.yEnde - a.yInhalt);

    for (size_t i = 0; i < einAbschnitte.size(); ++i)
    {
        EinAbschnitt &a = einAbschnitte[i];
        ShowWindow(a.kopf, SW_HIDE);
        const bool zeigen = int(i) == einReiter;
        const int dx = a.spalte == 1 ? -einSpalteRechts : 0; // rechte Spalte (beginnt bei einSpalteRechts + Rand) nach links
        for (auto &t : a.inhalt)
        {
            if (zeigen)
                SetWindowPos(t.h, nullptr, t.x0 + dx, oben + (t.y0 - a.yInhalt), 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            ShowWindow(t.h, zeigen ? SW_SHOWNA : SW_HIDE);
        }
    }

    // unten: Hinweis ueber die ganze Breite, darunter rechts "Schliessen"
    const int fuss = oben + inhaltH + 12;
    int hinweisH = 0, knopfH = 0;
    auto istKnopf = [](HWND h)
    {
        wchar_t klasse[32] = {};
        GetClassNameW(h, klasse, 32);
        return _wcsicmp(klasse, L"Button") == 0;
    };
    for (auto &[h, y0] : einFuss)
    {
        RECT r;
        GetWindowRect(h, &r);
        if (istKnopf(h))
            knopfH = std::max(knopfH, int(r.bottom - r.top));
        else
            hinweisH = std::max(hinweisH, int(r.bottom - r.top));
    }
    for (auto &[h, y0] : einFuss)
    {
        RECT r;
        GetWindowRect(h, &r);
        const int b = int(r.right - r.left), hh = int(r.bottom - r.top);
        if (istKnopf(h))
            SetWindowPos(h, nullptr, breite - einRand - b, fuss + hinweisH + 8, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        else
            SetWindowPos(h, nullptr, einRand, fuss, breite - 2 * einRand, hh, SWP_NOZORDER | SWP_NOACTIVATE);
    }
    einFussH = hinweisH + 8 + knopfH;
    RECT r = {0, 0, breite, fuss + einFussH + einRand};
    AdjustWindowRectEx(&r, WS_POPUP | WS_CAPTION | WS_SYSMENU, FALSE, WS_EX_TOOLWINDOW);
    SetWindowPos(hEinstellungen, nullptr, 0, 0, r.right - r.left, r.bottom - r.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    InvalidateRect(hEinstellungen, nullptr, TRUE);
}

// Reitertexte nachfuehren (Punkt fuer eingeschaltete Abschnitte)
static void einReiterTexteAktualisieren()
{
    bool anders = einReiterTexte.size() != einAbschnitte.size();
    for (size_t i = 0; !anders && i < einAbschnitte.size(); ++i)
        anders = einReiterTexte[i] != abschnittTitel(einAbschnitte[i]);
    if (!anders || !einReiterLeiste)
        return;
    RECT r;
    GetWindowRect(einReiterLeiste, &r);
    const int alteH = int(r.bottom - r.top);
    if (einReiterAnordnen(int(r.right - r.left)) != alteH)
        einstellungenAnordnen(); // eine Reiterzeile mehr/weniger
    InvalidateRect(einReiterLeiste, nullptr, FALSE);
}

// Klick auf eine (alte) Ueberschrift: gibt es nicht mehr, die Reiter ersetzen sie
static bool abschnittKlick(HWND) { return false; }

static std::vector<HWND> einstellungsFelder()
{
    std::vector<HWND> v;
    for (auto &z : einZahlen)
        v.push_back(z.edit);
    return v;
}

static SmokeBox *gewaehlteRauchbox()
{
    if (rauchAuswahl < 0 || rauchAuswahl >= int(smokeBoxes.size()))
        return nullptr;
    return &smokeBoxes[size_t(rauchAuswahl)];
}

// Aenderung bei angehaltenem Rendering; Ansichten und Statuszeile erfahren davon
static void einstellungAendern(const std::function<void()> &aenderung)
{
    if (gSzeneAendern)
        gSzeneAendern(aenderung);
    else
        aenderung();
    szeneGeaendertMarkieren();
    autoRenderGanz(); // Render-Einstellung: betrifft das ganze Bild
}

static void einstellungenAnzeigen()
{
    if (!hEinstellungen || !IsWindowVisible(hEinstellungen))
        return;

    for (auto &z : einZahlen)
    {
        bool da = !z.rauch || gewaehlteRauchbox();
        aktivieren(z.edit, da);
        textSetzen(z.edit, da ? zahlText(z.lesen(), z.stellen) : L"");
    }
    for (auto &k : einSchalter)
    {
        LRESULT soll = *k.ziel ? BST_CHECKED : BST_UNCHECKED;
        if (SendMessageW(k.knopf, BM_GETCHECK, 0, 0) != soll)
            SendMessageW(k.knopf, BM_SETCHECK, soll, 0);
    }
    for (auto &f : einFarben)
        InvalidateRect(f.knopf, nullptr, FALSE);

    // Rauchboxen in der Liste
    int anzahl = int(SendMessageW(einRauchListe, LB_GETCOUNT, 0, 0));
    if (anzahl != int(smokeBoxes.size()))
    {
        SendMessageW(einRauchListe, LB_RESETCONTENT, 0, 0);
        for (size_t i = 0; i < smokeBoxes.size(); ++i)
            SendMessageW(einRauchListe, LB_ADDSTRING, 0, (LPARAM)(L"Rauchbox " + std::to_wstring(i + 1)).c_str());
    }
    if (rauchAuswahl >= int(smokeBoxes.size()))
        rauchAuswahl = int(smokeBoxes.size()) - 1;
    if (SendMessageW(einRauchListe, LB_GETCURSEL, 0, 0) != rauchAuswahl)
        SendMessageW(einRauchListe, LB_SETCURSEL, rauchAuswahl, 0);

    einReiterTexteAktualisieren();

    // Vorlage der Bildgroesse passend zu Breite/Hoehe (nicht, waehrend die Liste offen ist)
    if (einBildFormat && !SendMessageW(einBildFormat, CB_GETDROPPEDSTATE, 0, 0) &&
        SendMessageW(einBildFormat, CB_GETCURSEL, 0, 0) != bildFormatIndex())
    {
        // "Andere Groesse" gewaehlt, Felder passen noch zu einer Vorlage: Auswahl stehen lassen
        bool andereGewaehlt = SendMessageW(einBildFormat, CB_GETCURSEL, 0, 0) == ANZAHL_BILDFORMATE - 1;
        if (!(andereGewaehlt && GetFocus() && GetParent(GetFocus()) == hEinstellungen))
            SendMessageW(einBildFormat, CB_SETCURSEL, bildFormatIndex(), 0);
    }

    // Animation: Dauer zwischen erstem und letztem Snapshot, Anzahl Bilder
    {
        LRESULT soll = animationAktiv() ? BST_CHECKED : BST_UNCHECKED;
        if (SendMessageW(einAnimSchalter, BM_GETCHECK, 0, 0) != soll)
            SendMessageW(einAnimSchalter, BM_SETCHECK, soll, 0);
        int n = snapshotAnzahl();
        if (n < 2)
        {
            textSetzen(einAnimDauer, n == 0 ? L"\u2013  (keine Snapshots)" : L"0 s  (nur 1 Snapshot)");
            textSetzen(einAnimBilder, L"\u2013  (mindestens 2 Snapshots nötig)");
        }
        else
        {
            textSetzen(einAnimDauer, zahlText(animationsEnde() - animationsBeginn(), 3) + L" s  (" + zahlText(animationsBeginn(), 2) +
                                         L" bis " + zahlText(animationsEnde(), 2) + L" s, " + std::to_wstring(n) + L" Snapshots)");
            textSetzen(einAnimBilder, std::to_wstring(anzahlBilder()) + L" Bilder");
        }
    }

    std::string datei = bildDatei;
    size_t schnitt = datei.find_last_of("/\\");
    if (schnitt != std::string::npos)
        datei = datei.substr(schnitt + 1);
    textSetzen(einBildText, bildGeladen ? (datei.empty() ? std::wstring(L"(beim Start geladen)") : ausUtf8(datei))
                                        : std::wstring(L"(kein Bild geladen)"));
}

// Umgebungsbild (BMP) waehlen und laden; false bei Abbruch
static bool umgebungsbildWaehlen()
{
    wchar_t datei[MAX_PATH] = L"";
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hEinstellungen;
    ofn.lpstrFilter = L"BMP-Bilder (*.bmp)\0*.bmp\0Alle Dateien (*.*)\0*.*\0";
    ofn.lpstrFile = datei;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&ofn))
        return false;

    int laenge = WideCharToMultiByte(CP_ACP, 0, datei, -1, nullptr, 0, nullptr, nullptr);
    std::string pfad(size_t(std::max(1, laenge) - 1), '\0');
    WideCharToMultiByte(CP_ACP, 0, datei, -1, &pfad[0], laenge, nullptr, nullptr);

    einstellungAendern([&]
                       { backgroundTexture.loadBMP(pfad.c_str()); });
    bildGeladen = true;
    bildDatei = pfad;
    return true;
}

static void farbfeldMalen(const DRAWITEMSTRUCT *d, const Farbe *f)
{
    FillRect(d->hDC, &d->rcItem, pinselHintergrund);
    HBRUSH fuellung = f ? CreateSolidBrush(RGB(kanal(std::min(1.0, f->r)), kanal(std::min(1.0, f->g)), kanal(std::min(1.0, f->b))))
                        : CreateSolidBrush(FARBE_FLAECHE);
    HPEN rahmen = CreatePen(PS_SOLID, 1, FARBE_BAUMLINIEN);
    HGDIOBJ altPinsel = SelectObject(d->hDC, fuellung);
    HGDIOBJ altStift = SelectObject(d->hDC, rahmen);
    RoundRect(d->hDC, d->rcItem.left, d->rcItem.top, d->rcItem.right, d->rcItem.bottom, 4, 4);
    SelectObject(d->hDC, altStift);
    SelectObject(d->hDC, altPinsel);
    DeleteObject(rahmen);
    DeleteObject(fuellung);
}

#define ID_EIN_RAUCH_NEU (ID_EIN_ERSTES + 190)
#define ID_EIN_RAUCH_WEG (ID_EIN_ERSTES + 191)
#define ID_EIN_BILD (ID_EIN_ERSTES + 192)
#define ID_EIN_RAUCHLISTE (ID_EIN_ERSTES + 193)
#define ID_EIN_SCHLIESSEN (ID_EIN_ERSTES + 194)
#define ID_EIN_ANIM_RENDERN (ID_EIN_ERSTES + 195)
#define ID_EIN_ANIMATION (ID_EIN_ERSTES + 196)
#define ID_EIN_BILDFORMAT (ID_EIN_ERSTES + 197)
#define ID_EIN_FOKUS_AUSWAHL (ID_EIN_ERSTES + 198)

static void einZahlUebernehmen(EinstellZahl &z)
{
    if (!SendMessageW(z.edit, EM_GETMODIFY, 0, 0))
        return;
    SendMessageW(z.edit, EM_SETMODIFY, FALSE, 0);
    if (z.rauch && !gewaehlteRauchbox())
        return;
    double v;
    if (!zahlLesen(z.edit, v) || v < z.minimum || v > z.maximum)
    {
        MessageBeep(MB_ICONWARNING);
        SendMessageW(z.edit, EM_SETMODIFY, TRUE, 0);
        return;
    }
    einstellungAendern([&]
                       { z.schreiben(v); });
}


static LRESULT CALLBACK EinstellungenProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_CLOSE:
        ShowWindow(hwnd, SW_HIDE);
        return 0;

    case WM_EINGABE_FERTIG: // Enter in einem Feld
        for (auto &z : einZahlen)
            if (GetDlgCtrlID(z.edit) == int(wParam))
                einZahlUebernehmen(z);
        return 0;

    case WM_COMMAND:
    {
        int id = LOWORD(wParam), code = HIWORD(wParam);
        HWND ctrl = (HWND)lParam;

        if (code == EN_KILLFOCUS)
        {
            for (auto &z : einZahlen)
                if (z.edit == ctrl)
                    einZahlUebernehmen(z);
            return 0;
        }
        if (code == STN_CLICKED && abschnittKlick(ctrl))
            return 0;
        if (ctrl == einAnimSchalter && code == BN_CLICKED)
        {
            bool an = SendMessageW(ctrl, BM_GETCHECK, 0, 0) == BST_CHECKED;
            animationAktivSetzen(an);
            zeitleisteUmschalten(an);
            return 0;
        }
        if (id == ID_EIN_FOKUS_AUSWAHL) // Fokusabstand = Entfernung der Auswahl entlang der Blickrichtung
        {
            Vector3D mn, mx;
            const std::vector<object *> auswahl = ausgewaehlteObjekte();
            if (cam.empty() || auswahl.empty() || !grenzenVon(auswahl, mn, mx))
            {
                MessageBoxW(hwnd, L"Bitte zuerst das Objekt auswählen, das scharf sein soll.", L"Fokus", MB_OK | MB_ICONINFORMATION);
                return 0;
            }
            Vector3D vorne = cam[0].lookAt - cam[0].position;
            if (vorne.length() > 1e-12)
            {
                vorne = vorne.normalized();
                fokusAbstand = std::max(1e-6, ((mn + mx) * 0.5 - cam[0].position) * vorne);
                einstellungenAnzeigen();
                szeneGeaendertMarkieren();
            }
            return 0;
        }
        if (id == ID_EIN_ANIM_RENDERN)
        {
            if (anzahlBilder() < 2)
            {
                MessageBoxW(hwnd, L"Für eine Animation braucht es mindestens zwei Snapshots zu verschiedenen Zeiten.\n\n"
                                  L"\"Animation\" einschalten und in der Zeitleiste mit \"+ Snapshot\" festhalten.",
                            L"Animation rendern", MB_OK | MB_ICONINFORMATION);
                return 0;
            }
            if (!gAnimationRendern || gAnimationBilder > 0)
                return 0;
            wchar_t datei[MAX_PATH] = L"animation.bmp";
            OPENFILENAMEW ofn = {};
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner = hwnd;
            ofn.lpstrFilter = L"BMP-Bilder (*.bmp)\0*.bmp\0";
            ofn.lpstrFile = datei;
            ofn.nMaxFile = MAX_PATH;
            ofn.lpstrDefExt = L"bmp";
            ofn.lpstrTitle = L"Animation rendern: Name der Bilder (es entstehen name_0001.bmp, name_0002.bmp, ...)";
            ofn.Flags = OFN_EXPLORER | OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST;
            if (!GetSaveFileNameW(&ofn))
                return 0;
            std::wstring basis = datei;
            if (basis.size() > 4 && _wcsicmp(basis.c_str() + basis.size() - 4, L".bmp") == 0)
                basis.resize(basis.size() - 4);
            gAnimationRendern(basis);
            return 0;
        }
        for (auto &k : einSchalter)
            if (k.knopf == ctrl && code == BN_CLICKED)
            {
                bool an = SendMessageW(ctrl, BM_GETCHECK, 0, 0) == BST_CHECKED;
                // Umgebungsbild ohne geladenes Bild: erst eine Datei waehlen
                if (an && k.ziel == &umgebungsbild && !bildGeladen && !umgebungsbildWaehlen())
                {
                    SendMessageW(ctrl, BM_SETCHECK, BST_UNCHECKED, 0);
                    return 0;
                }
                if (k.ziel == &gAnimationVideo) // betrifft das Bild nicht: Rendern nicht anhalten
                {
                    gAnimationVideo = an;
                    return 0;
                }
                einstellungAendern([&]
                                   { *k.ziel = an; });
                return 0;
            }
        for (auto &f : einFarben)
            if (f.knopf == ctrl && code == BN_CLICKED)
            {
                Farbe *ziel = f.ziel();
                if (!ziel)
                    return 0;
                CHOOSECOLORW cc = {};
                cc.lStructSize = sizeof(cc);
                cc.hwndOwner = hwnd;
                cc.rgbResult = RGB(kanal(std::min(1.0, ziel->r)), kanal(std::min(1.0, ziel->g)), kanal(std::min(1.0, ziel->b)));
                cc.lpCustColors = eigeneFarben;
                cc.Flags = CC_FULLOPEN | CC_RGBINIT | CC_ANYCOLOR;
                if (ChooseColorW(&cc))
                {
                    Farbe neu(GetRValue(cc.rgbResult) / 255.0, GetGValue(cc.rgbResult) / 255.0, GetBValue(cc.rgbResult) / 255.0);
                    einstellungAendern([&]
                                       { *f.ziel() = neu; });
                    InvalidateRect(ctrl, nullptr, FALSE);
                }
                return 0;
            }
        if (id == ID_EIN_RAUCHLISTE && code == LBN_SELCHANGE)
        {
            rauchAuswahl = int(SendMessageW(einRauchListe, LB_GETCURSEL, 0, 0));
            einstellungenAnzeigen();
            return 0;
        }
        if (id == ID_EIN_RAUCH_NEU)
        {
            // neue Box beim Kamera-Zielpunkt, etwa so gross wie neue Objekte (ein Rauchfaden ist schmal und hoch)
            Vector3D p = !cam.empty() ? cam[0].lookAt : (sceneMin + sceneMax) * 0.5;
            double ausdehnung = (sceneMax - sceneMin).length();
            double b = ausdehnung > 1e-9 ? ausdehnung * 0.05 : 1.0;
            einstellungAendern([&]
                               { smokeBoxes.emplace_back(p - Vector3D(b, 0, b), p + Vector3D(b, 4 * b, b), Farbe(1, 1, 1), 50.0,
                                                         double(smokeBoxes.size())); });
            rauchAuswahl = int(smokeBoxes.size()) - 1;
            einstellungenAnzeigen();
            return 0;
        }
        if (id == ID_EIN_RAUCH_WEG)
        {
            if (gewaehlteRauchbox())
            {
                einstellungAendern([&]
                                   { smokeBoxes.erase(smokeBoxes.begin() + rauchAuswahl); });
                einstellungenAnzeigen();
            }
            return 0;
        }
        if (id == ID_EIN_BILD)
        {
            if (umgebungsbildWaehlen())
            {
                einstellungAendern([&]
                                   { umgebungsbild = true; });
                einstellungenAnzeigen();
            }
            return 0;
        }
        if (id == ID_EIN_BILDFORMAT && code == CBN_SELCHANGE)
        {
            int i = int(SendMessageW(einBildFormat, CB_GETCURSEL, 0, 0));
            if (i >= 0 && i < ANZAHL_BILDFORMATE && BILD_FORMATE[i].breite >= 0)
            {
                renderBreite = BILD_FORMATE[i].breite; // gilt ab dem naechsten Rendern
                renderHoehe = BILD_FORMATE[i].hoehe;
                szeneGeaendertMarkieren();
                einstellungenAnzeigen();
            }
            else if (einBildBreiteFeld) // andere Groesse: Breite eintippen
            {
                SetFocus(einBildBreiteFeld);
                SendMessageW(einBildBreiteFeld, EM_SETSEL, 0, -1);
            }
            return 0;
        }
        if (id == ID_EIN_SCHLIESSEN)
        {
            ShowWindow(hwnd, SW_HIDE);
            return 0;
        }
        break;
    }

    case WM_DRAWITEM:
    {
        const DRAWITEMSTRUCT *d = reinterpret_cast<const DRAWITEMSTRUCT *>(lParam);
        for (auto &f : einFarben)
            if (f.knopf == d->hwndItem)
            {
                farbfeldMalen(d, f.ziel());
                return TRUE;
            }
        buttonZeichnen(d);
        return TRUE;
    }

    case WM_CTLCOLORSTATIC:
    {
        HDC hdc = (HDC)wParam;
        bool feld = false;
        for (auto &z : einZahlen)
            feld = feld || z.edit == (HWND)lParam;
        SetTextColor(hdc, feld ? FARBE_TEXT_GEDIMMT : (istGedimmt((HWND)lParam) ? FARBE_UEBERSCHRIFT : FARBE_TEXT));
        SetBkColor(hdc, feld ? FARBE_FLAECHE : FARBE_HINTERGRUND);
        return (LRESULT)(feld ? pinselFlaeche : pinselHintergrund);
    }
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
    {
        HDC hdc = (HDC)wParam;
        SetTextColor(hdc, FARBE_TEXT);
        SetBkColor(hdc, FARBE_FLAECHE);
        return (LRESULT)pinselFlaeche;
    }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void einstellungenAnlegen()
{
    HINSTANCE hInst = GetModuleHandleW(nullptr);
    WNDCLASSW wc{};
    wc.lpfnWndProc = EinstellungenProc;
    wc.hInstance = hInst;
    wc.hbrBackground = pinselHintergrund;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"SZENEN_RENDER_EINSTELLUNGEN";
    RegisterClassW(&wc);

    // Breiten nach der Schrift: laengste Beschriftung bestimmt die Beschriftungsspalte
    HDC dc = GetDC(nullptr);
    HGDIOBJ alteSchrift = SelectObject(dc, gFont);
    auto textBreite = [&](const wchar_t *t)
    {
        RECT r = {0, 0, 0, 0};
        DrawTextW(dc, t, -1, &r, DT_CALCRECT | DT_SINGLELINE);
        return int(r.right - r.left);
    };
    // alle Beschriftungen, die ueber die volle Breite gehen koennen (vorher nur "Farbe des Streulichts":
    // "Abtastungen mindestens" wurde abgeschnitten)
    int labelB = 150;
    for (const wchar_t *t : {L"Farbe des Streulichts", L"Abtastungen mindestens", L"Abtastungen höchstens",
                             L"Schnell-Rendering (Block)", L"Kachelgröße (roter Rahmen)", L"Bildbreite (Pixel)", L"Schritte je Schattenstrahl", L"Konturdicke (Pixel)", L"Zeichentrick (Cel-Shading)", L"Bilder pro Sekunde", L"Zufallswert (seed)",
                             L"Belichtung (exposure)", L"Himmel von (min)", L"Wolken bis Höhe", L"Wartezeit autom. Rendern (s)", L"Fallgeschwindigkeit"})
        labelB = std::max(labelB, textBreite(t) + 16);
    SelectObject(dc, alteSchrift);
    ReleaseDC(nullptr, dc);

    const int rand = 14, feldB = 90, zeile = ZEILE_H + 6;
    const int spalteB = labelB + GAP + 3 * 76 + 2 * GAP; // Platz fuer drei Felder (von/bis)
    const int breite = rand + spalteB + 2 * rand + spalteB + rand;
    const int hinweisH = 2 * LABEL_H + 4;
    int hoehe = 0; // ergibt sich aus dem Aufbau

    DWORD stil = WS_POPUP | WS_CAPTION | WS_SYSMENU;
    RECT bezug;
    GetWindowRect(hToolWindow, &bezug);
    hEinstellungen = CreateWindowExW(WS_EX_TOOLWINDOW, L"SZENEN_RENDER_EINSTELLUNGEN", L"Render-Einstellungen", stil,
                                     bezug.left + 60, bezug.bottom + 30, 100, 100, hToolWindow, nullptr, hInst, nullptr);


    int x = rand, y = rand;
    auto neueId = []
    { return naechsteEinId++; };
    auto ueberschrift = [&](const wchar_t *text, bool klappbar = false, const bool *schalterAn = nullptr,
                            bool anfangsOffen = false)
    {
        if (y > rand)
            y += 8;
        if (!einAbschnitte.empty() && einAbschnitte.back().spalte == (x > rand ? 1 : 0))
            einAbschnitte.back().yEnde = y - 8; // Ende des vorigen Abschnitts dieser Spalte
        HWND h = CreateLabel(hEinstellungen, hInst, text, x, y, spalteB, LABEL_H);
        SetWindowLongPtrW(h, GWLP_USERDATA, 1); // gedimmt wie "EIGENSCHAFTEN"
        EinAbschnitt a;
        a.kopf = h;
        a.titel = text;
        a.klappbar = klappbar;
        a.offen = !klappbar || anfangsOffen; // aufklappbare Abschnitte starten zu, ausser BILD und QUALITAET
        a.schalter = schalterAn;
        a.spalte = x > rand ? 1 : 0;
        a.yKopf = y;
        if (klappbar)
        {
            SetWindowLongPtrW(h, GWL_STYLE, GetWindowLongPtrW(h, GWL_STYLE) | SS_NOTIFY);
            tooltip(h, L"Klicken: Abschnitt auf- bzw. zuklappen");
        }
        y += LABEL_H + 4;
        a.yInhalt = y;
        a.yEnde = y;
        einAbschnitte.push_back(a);
    };
    auto abschnittEnde = [&]
    {
        if (!einAbschnitte.empty())
            einAbschnitte.back().yEnde = y;
    };
    auto beschriftung = [&](const wchar_t *text, const wchar_t *tipp)
    {
        HWND h = CreateLabel(hEinstellungen, hInst, text, x, y + 4, labelB, LABEL_H);
        SetWindowLongPtrW(h, GWL_STYLE, GetWindowLongPtrW(h, GWL_STYLE) | SS_NOTIFY); // Tooltip auch ueber der Beschriftung
        tooltip(h, tipp);
    };
    auto zahl = [&](const wchar_t *text, const wchar_t *tipp, std::function<double()> lesen,
                    std::function<void(double)> schreiben, double mn, double mx, int stellen, bool rauch = false)
    {
        beschriftung(text, tipp);
        EinstellZahl z;
        z.edit = CreateEingabefeld(hEinstellungen, hInst, neueId(), ES_RIGHT);
        MoveWindow(z.edit, x + labelB + GAP, y, feldB, ZEILE_H, FALSE);
        tooltip(z.edit, tipp);
        z.lesen = lesen;
        z.schreiben = schreiben;
        z.minimum = mn;
        z.maximum = mx;
        z.stellen = stellen;
        z.rauch = rauch;
        einZahlen.push_back(z);
        y += zeile;
    };
    // Vektor als drei Felder in einer Zeile
    auto vektor = [&](const wchar_t *text, const wchar_t *tipp, std::function<Vector3D *()> ziel, bool rauch = false)
    {
        beschriftung(text, tipp);
        int b = (spalteB - labelB - 3 * GAP) / 3;
        const wchar_t *achse[3] = {L" (X)", L" (Y)", L" (Z)"};
        for (int k = 0; k < 3; ++k)
        {
            EinstellZahl z;
            z.edit = CreateEingabefeld(hEinstellungen, hInst, neueId(), ES_RIGHT);
            MoveWindow(z.edit, x + labelB + GAP + k * (b + GAP), y, b, ZEILE_H, FALSE);
            static std::vector<std::wstring> tipps; // Tooltip-Texte muessen nicht bleiben, aber so bleibt es einfach
            tipps.push_back(std::wstring(tipp) + achse[k]);
            tooltip(z.edit, tipps.back().c_str());
            z.lesen = [ziel, k]
            { Vector3D *v = ziel(); return v ? (k == 0 ? v->x : k == 1 ? v->y : v->z) : 0.0; };
            z.schreiben = [ziel, k](double w)
            { Vector3D *v = ziel(); if (v) (k == 0 ? v->x : k == 1 ? v->y : v->z) = w; };
            z.minimum = -1e12;
            z.maximum = 1e12;
            z.stellen = 2;
            z.rauch = rauch;
            einZahlen.push_back(z);
        }
        y += zeile;
    };
    auto schalter = [&](const wchar_t *text, const wchar_t *tipp, bool *ziel)
    {
        HWND h = CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                                 x, y, spalteB, ZEILE_H, hEinstellungen, (HMENU)(INT_PTR)neueId(), hInst, nullptr);
        SendMessageW(h, WM_SETFONT, (WPARAM)gFont, TRUE);
        SetWindowTheme(h, L"", L""); // ohne Design: Textfarbe aus WM_CTLCOLORSTATIC (sonst schwarz auf dunkel)
        tooltip(h, tipp);
        einSchalter.push_back({h, ziel});
        y += zeile;
    };
    auto farbe = [&](const wchar_t *text, const wchar_t *tipp, std::function<Farbe *()> ziel)
    {
        beschriftung(text, tipp);
        HWND h = CreateWindowExW(0, L"BUTTON", L"", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                                 x + labelB + GAP, y, feldB, ZEILE_H, hEinstellungen, (HMENU)(INT_PTR)neueId(), hInst, nullptr);
        tooltip(h, tipp);
        einFarben.push_back({h, ziel});
        y += zeile;
    };
    auto knopf = [&](const wchar_t *text, const wchar_t *tipp, int id, int kx, int kb)
    {
        HWND h = CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                                 kx, y, kb, BTN_H, hEinstellungen, (HMENU)(INT_PTR)id, hInst, nullptr);
        SendMessageW(h, WM_SETFONT, (WPARAM)gFont, TRUE);
        tooltip(h, tipp);
        return h;
    };

    // ---------------- linke Spalte ----------------
    ueberschrift(L"BILD", true, nullptr, true);
    schalter(L"Umgebungsbild (Hintergrund)",
             L"An: Strahlen, die nichts treffen, zeigen das geladene BMP-Bild (Kugelpanorama) statt des Himmelsverlaufs.\n"
             L"Wirkung im Renderer: das Bild ist dann Hintergrund UND Lichtquelle: über das indirekte Licht färbt es\n"
             L"Schatten, dunkle Seiten und Spiegelungen (Metall, Glas, Wasser zeigen das Bild). Helligkeit: \"Himmel-Helligkeit\".\n"
             L"Ohne geladenes Bild wird zuerst eine BMP-Datei abgefragt.",
             &umgebungsbild);
    beschriftung(L"Bilddatei", L"BMP-Datei (24 Bit) für den Hintergrund: ein Panorama, das rundherum um die Szene gelegt wird\n"
             L"(links/rechts = einmal herum, oben/unten = Zenit bis Nadir).");
    knopf(L"Bild …", L"BMP-Datei für den Hintergrund wählen und laden", ID_EIN_BILD, x + labelB + GAP, feldB);
    y += BTN_H + 2;
    einBildText = CreateLabel(hEinstellungen, hInst, L"", x + labelB + GAP, y, spalteB - labelB - GAP, LABEL_H);
    SetWindowLongPtrW(einBildText, GWLP_USERDATA, 1);
    y += LABEL_H + 6;
    beschriftung(L"Bildgröße", L"Größe des gerenderten Bildes: Vorlage wählen oder \"Andere Größe\" und Breite/Höhe eintragen");
    einBildFormat = CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST,
                                    x + labelB + GAP, y, spalteB - labelB - GAP, 300, hEinstellungen,
                                    (HMENU)(INT_PTR)ID_EIN_BILDFORMAT, hInst, nullptr);
    SendMessageW(einBildFormat, WM_SETFONT, (WPARAM)gFont, TRUE);
    for (const auto &bf : BILD_FORMATE)
        SendMessageW(einBildFormat, CB_ADDSTRING, 0, (LPARAM)bf.name);
    SendMessageW(einBildFormat, CB_SETCURSEL, bildFormatIndex(), 0);
    tooltip(einBildFormat, L"Vorlagen für die Bildgröße. Die zuletzt benutzte Größe gilt auch nach einem Neustart;\n"
                           L"eine geladene Szene bringt ihre eigene Größe mit (falls darin gespeichert).");
    y += zeile;
    zahl(L"Bildbreite (Pixel)",
         L"Breite des gerenderten Bildes; 0 = so breit wie das Render-Fenster.\n"
             L"Rechenzeit wächst mit der Pixelzahl (doppelte Breite und Höhe = viermal so lang).\n"
             L"Größer als das Fenster geht: im Reiter \"Rendering\" eingepasst oder 1:1 ansehen.\n"
             L"Gilt auch für gespeicherte Bilder und Animationen. Höchstens 16384.",
         []
         { return double(renderBreite); }, [](double v)
         { renderBreite = int(std::round(v)); }, 0, 16384, 0);
    einBildBreiteFeld = einZahlen.back().edit;
    zahl(L"Bildhöhe (Pixel)", L"Höhe des gerenderten Bildes; 0 = so hoch wie das Render-Fenster (z.B. 1920 × 1080, 3840 × 2160).\n"
             L"Das Seitenverhältnis bestimmt mit dem FOV der Kamera den Bildausschnitt.",
         []
         { return double(renderHoehe); }, [](double v)
         { renderHoehe = int(std::round(v)); }, 0, 16384, 0);
    zahl(L"Belichtung (exposure)",
         L"Helligkeit des fertigen Bilds vor dem Tonemapping: +1 = doppelt so hell, +2 = viermal, −1 = halb so hell (−5 bis 5).\n"
             L"Ändert nur die Umrechnung ins Bild, nicht das Licht selbst: Verhältnisse von Licht und Schatten bleiben gleich.\n"
             L"Für dunkle Szenen (Nacht, Innenraum) erhöhen statt die Lichter zu verstärken.",
         []
         { return exposure; }, [](double v)
         { exposure = v; }, -5, 5, 2);
    farbe(L"Himmel unten",
          L"Farbe des Himmelsverlaufs unten/am Horizont (ohne Umgebungsbild).\n"
          L"Der Himmel ist auch eine Lichtquelle: Strahlen, die nichts treffen, bringen diese Farbe als\n"
          L"indirektes Licht mit. Er färbt Schatten und dunkle Seiten (blau = Tageslicht, orange = Mars, schwarz = Weltall).",
          []
          { return &himmelHorizont; });
    farbe(L"Himmel oben",
          L"Farbe des Himmelsverlaufs oben (Zenit). Wirkt wie \"Himmel unten\" als Hintergrund und als\n"
          L"indirektes Licht von oben auf alle Flächen.",
          []
          { return &himmelZenit; });
    zahl(L"Himmel-Helligkeit",
         L"Faktor für Himmelsverlauf und Umgebungsbild (Vorgabe 1). 0 = schwarzer Himmel ohne Himmelslicht\n"
         L"(Nacht, Weltall, geschlossene Räume: dann leuchten nur Lichter und \"Leuchten\"-Materialien).\n"
         L"Größer als 1: helleres Tageslicht in Schatten und auf dunklen Seiten.",
         []
         { return himmelHelligkeit; }, [](double v)
         { himmelHelligkeit = v; }, 0, 100, 2);

    ueberschrift(L"QUALITÄT", true, nullptr, true);
    zahl(L"Wartezeit autom. Rendern (s)",
         L"Schalter \"Autom. Rendern\" (neben Render): so viele Sekunden nach der letzten Änderung beginnt das Rendern\n"
             L"des geänderten Bildbereichs. Eine Änderung in dieser Zeit startet die Wartezeit neu. Wird mit den\n"
             L"Programm-Einstellungen gespeichert (nicht in der Szene).",
         []
         { return autoWarten; }, [](double v)
         { autoWarten = v; }, 0.5, 120, 1);
    zahl(L"Rekursionstiefe",
         L"Wie oft ein Strahl höchstens gespiegelt, gebrochen oder gestreut wird (1 bis 50).\n"
             L"Zu klein: Glas wird dunkel/schwarz (jede Glaswand braucht 2 Stufen), Spiegel in Spiegeln enden schwarz,\n"
             L"indirektes Licht wird schwächer. Glas-Tasse: mindestens 8. Größer kostet nur dort Zeit, wo Strahlen weiterlaufen.",
         []
         { return double(renderingDepth); }, [](double v)
         { renderingDepth = int(std::round(v)); }, 1, 50, 0);
    zahl(L"Kachelgröße (roter Rahmen)",
         L"Die Render-Threads rechnen das Bild in Kacheln dieser Größe, in zufälliger Reihenfolge;\n"
             L"um die gerade berechnete Kachel steht ein roter Rahmen. 1 = Pixel einzeln (ohne Rahmen).\n"
             L"Ändert nur die Reihenfolge, nicht das Ergebnis und kaum die Rechenzeit.",
         []
         { return double(kachelGroesse); }, [](double v)
         { kachelGroesse = int(std::round(v)); }, 1, 1024, 0);
    schalter(L"Antialiasing", L"Kanten glätten: mehrere leicht versetzte Strahlen je Pixel (Halton-Folge), Mittelwert = Pixelfarbe.\n"
             L"Aus: genau ein Strahl je Pixel (treppige Kanten, schnell). An: \"Samples je Pixel\" Strahlen.", &antialiasing);
    zahl(L"Samples je Pixel",
         L"Strahlen je Pixel beim Antialiasing (1 bis 256). Rechenzeit wächst linear.\n"
             L"Jede Abtastung rechnet auch indirektes Licht, Volumen und weiche Schatten: mehr Samples = auch weniger Rauschen.\n"
             L"Mit \"Rauschen adaptiv nachrechnen\" ist das die Grundzahl; unruhige Pixel bekommen mehr.",
         []
         { return double(antialiasingSamples); }, [](double v)
         { antialiasingSamples = int(std::round(v)); }, 1, 256, 0);
    schalter(L"Indirektes Licht (Path Tracing)",
             L"An: Flächen werden auch von anderen Flächen, vom Himmel und von Leuchten-Materialien beleuchtet\n"
             L"(weiche Aufhellung der Schatten, Farbbluten, Licht um Ecken). Realistisch, rauscht aber und kostet Zeit.\n"
             L"Aus: kein indirektes Licht; statt dessen hellt die Materialfarbe \"Umgebung\" × kA jede Fläche\n"
             L"gleichmäßig auf (klassisches Ambient). Viel schneller und rauschfrei, Schatten wirken härter.",
             &indirektesLicht);
    zahl(L"Pfad-Samples",
         L"Pfade je Abtastung für das indirekte Licht (Path Tracing, 1 bis 4096): wie oft vom Treffpunkt aus\n"
             L"in eine zufällige Richtung weitergerechnet wird (Licht von Wänden, Boden, Himmel, Leuchten-Materialien).\n"
             L"Mehr = weniger Rauschen in indirekt beleuchteten Stellen, aber jeder Pixel entsprechend langsamer.\n"
             L"Meist besser: Pfad-Samples 1 und \"Rauschen adaptiv nachrechnen\".",
         []
         { return double(pathSamples); }, [](double v)
         { pathSamples = int(std::round(v)); }, 1, 4096, 0);
    // Schnell-Rendering (Block): jetzt als Auswahlliste in der Leiste links von "Render"
    schalter(L"Rauschen adaptiv nachrechnen",
             L"Je Pixel erst einige Abtastungen, dabei die Streuung der Helligkeit messen. Unruhige Pixel\n"
             L"(Sprenkel durch indirektes Licht, weiche Schatten, raue Spiegelung, Rauch) bekommen weitere Abtastungen,\n"
             L"bis sie ruhig sind; ruhige Flächen kosten nichts extra. Meist der beste Weg zu einem glatten Bild.",
             &adaptivAn);
    zahl(L"Abtastungen mindestens",
         L"So viele Abtastungen bekommt jeder Pixel (mindestens 2, um die Streuung zu schätzen; Vorgabe 4).\n"
             L"Zu klein: seltene helle Treffer werden übersehen und bleiben als einzelne Sprenkel stehen.",
         []
         { return double(adaptivMin); }, [](double v)
         { adaptivMin = int(std::round(v)); }, 1, 4096, 0);
    zahl(L"Abtastungen höchstens",
         L"Obergrenze für unruhige Pixel (Vorgabe 64). Mehr = glattere Schatten, Glas, Rauch und indirektes Licht,\n"
             L"aber an schwierigen Stellen entsprechend langsamer.",
         []
         { return double(adaptivMax); }, [](double v)
         { adaptivMax = int(std::round(v)); }, 1, 65536, 0);
    zahl(L"Rausch-Schwelle",
         L"Wie ruhig ein Pixel werden soll: erlaubte Unsicherheit des Mittelwerts relativ zur Helligkeit\n"
             L"(Vorgabe 0,03 = 3 %). Kleiner = glatter, aber mehr Abtastungen; 0,1 = schnelle Vorschau.",
         []
         { return rauschSchwelle; }, [](double v)
         { rauschSchwelle = v; }, 0.0001, 1, 4);
    zahl(L"Ausreißer-Grenze",
         L"Indirektes Licht eines einzelnen Strahls höchstens so hell (Vorgabe 3; 0 = keine Grenze).\n"
             L"Verhindert einzelne sehr helle Pixel (Fireflies), z.B. wenn ein Streustrahl zufällig eine kleine helle Lampe trifft.\n"
             L"Begrenzt aber auch, wie stark helle Leuchten-Materialien (Neon) ihre Umgebung aufhellen: dafür erhöhen.",
         []
         { return indirektGrenze; }, [](double v)
         { indirektGrenze = v; }, 0, 1e6, 2);

    ueberschrift(L"KAMERA (TIEFENSCHÄRFE)", true);
    zahl(L"Blende (Linsenradius)",
         L"Tiefenschärfe wie bei einem Fotoapparat: Radius der Linse in Einheiten der Szene (0 = alles scharf, Lochkamera).\n"
             L"Was im Fokusabstand liegt, ist scharf; davor und dahinter verschwimmt es umso stärker, je größer die Blende.\n"
             L"Anhaltspunkt: 1 bis 3 % des Fokusabstands = deutlich unscharfer Hintergrund (Porträt), 0,2 % = leicht.\n"
             L"Braucht mehr Abtastungen je Pixel (Antialiasing oder \"Rauschen adaptiv nachrechnen\"), sonst körnig.",
         []
         { return blende; }, [](double v)
         { blende = v; }, 0, 1e9, 4);
    zahl(L"Fokusabstand",
         L"Entfernung der scharfen Ebene von der Kamera, entlang der Blickrichtung (0 = Abstand zum Kamera-Ziel,\n"
             L"also scharf, wohin die Kamera schaut). \"Fokus auf Auswahl\" setzt den Abstand des gewählten Objekts.",
         []
         { return fokusAbstand; }, [](double v)
         { fokusAbstand = v; }, 0, 1e12, 4);
    knopf(L"Fokus auf Auswahl", L"Fokusabstand = Entfernung der Mitte der Auswahl von der Kamera (entlang der Blickrichtung)",
          ID_EIN_FOKUS_AUSWAHL, x + labelB + GAP, spalteB - labelB - GAP);
    y += BTN_H + GAP;

    ueberschrift(L"ANIMATION", true);
    einAnimSchalter = CreateWindowExW(0, L"BUTTON", L"Animation (Zeitleiste mit Snapshots)",
                                      WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, x, y, spalteB, ZEILE_H,
                                      hEinstellungen, (HMENU)(INT_PTR)ID_EIN_ANIMATION, hInst, nullptr);
    SendMessageW(einAnimSchalter, WM_SETFONT, (WPARAM)gFont, TRUE);
    SetWindowTheme(einAnimSchalter, L"", L"");
    tooltip(einAnimSchalter, L"Zeigt über der Statusleiste die Zeitleiste: dort Snapshots der Szene zu verschiedenen Zeiten festhalten.\n"
                             L"Dazwischen werden alle Objekte, Lichter und die Kamera linear berechnet.");
    y += zeile;
    beschriftung(L"Dauer", L"Zeit vom ersten bis zum letzten Snapshot");
    einAnimDauer = CreateLabel(hEinstellungen, hInst, L"", x + labelB + GAP, y + 4, spalteB - labelB - GAP, LABEL_H);
    y += zeile;
    zahl(L"Bilder pro Sekunde", L"Wie viele Bilder je Sekunde Animation gerendert werden (Vorgabe 24)", []
         { return double(bilderProSekunde()); }, [](double v)
         { bilderProSekundeSetzen(int(std::round(v))); }, 1, 1000, 0);
    beschriftung(L"Bilder gesamt", L"Dauer × Bilder pro Sekunde + 1 (erstes und letztes Bild eingeschlossen)");
    einAnimBilder = CreateLabel(hEinstellungen, hInst, L"", x + labelB + GAP, y + 4, spalteB - labelB - GAP, LABEL_H);
    y += zeile;
    zahl(L"Bewegungsunschärfe (Teilbilder)",
         L"Bewegungsunschärfe beim Rendern einer Animation: jedes Bild wird aus so vielen Teilbildern über die\n"
             L"Verschlusszeit gemittelt (1 = aus). Was sich bewegt oder dreht, verwischt entlang seiner Bahn, auch bei\n"
             L"Kamerafahrten. 4 bis 8 sehen weich aus; die Rechenzeit wächst entsprechend (8 Teilbilder = 8-mal so lang).",
         []
         { return double(bewegungTeilbilder); }, [](double v)
         { bewegungTeilbilder = int(std::round(v)); }, 1, 64, 0);
    zahl(L"Verschluss (Anteil)",
         L"Wie lange der Verschluss je Bild offen ist, als Anteil der Bildzeit (0 bis 1). 0,5 entspricht dem\n"
             L"üblichen 180°-Verschluss beim Film; 1 = maximale Unschärfe, kleiner = schärfer.",
         []
         { return verschluss; }, [](double v)
         { verschluss = v; }, 0, 1, 2);
    // "Video (MP4) mit ffmpeg erstellen" wird nicht mehr angezeigt (gAnimationVideo bleibt eingeschaltet)
    knopf(L"Animation rendern …",
          L"Alle Bilder nacheinander raytracen und als name_0001.bmp, name_0002.bmp, ... speichern.\nStop (X) bricht ab.",
          ID_EIN_ANIM_RENDERN, x + labelB + GAP, spalteB - labelB - GAP);
    y += BTN_H + GAP;

    ueberschrift(L"STIL", true, &zeichentrick);
    zahl(L"Sättigung",
         L"Farben des fertigen Bilds kräftiger (> 1) oder blasser (< 1); 1 = unverändert, 0 = schwarzweiß.\n"
             L"Wird nach dem Tonemapping angewendet, ändert also keine Helligkeit. Z.B. 1,3 für leuchtende Farben.",
         []
         { return saettigung; }, [](double v)
         { saettigung = v; }, 0, 4, 2);
    schalter(L"Zeichentrick (Cel-Shading)",
             L"Licht in harten Stufen statt weichem Verlauf (\"Farbstufen\"), kleine scharfe Glanzpunkte,\n"
             L"heller Saum an den Rändern (\"Randlicht\") und auf Wunsch schwarze Konturen: gezeichneter Look.\n"
             L"Wirkt auf das direkte Licht; indirektes Licht, Spiegelung und Glas bleiben normal.",
             &zeichentrick);
    zahl(L"Farbstufen", L"Helligkeitsstufen des direkten Lichts im Zeichentrick-Stil (Vorgabe 3; 2 = sehr flach, 5 = weicher).", []
         { return double(tonStufen); }, [](double v)
         { tonStufen = int(std::round(v)); }, 1, 64, 0);
    schalter(L"Konturen", L"Schwarze Linien an Silhouetten, Tiefensprüngen und Kanten (nur mit Zeichentrick).\n"
             L"Kostet vier zusätzliche Strahlen je Pixel (ohne Beleuchtung).",
             &konturen);
    zahl(L"Konturdicke (Pixel)", L"Wie weit die Nachbarpunkte für die Kanten-Erkennung entfernt sind (Vorgabe 1,5): größer = dickere Linien.", []
         { return konturDicke; }, [](double v)
         { konturDicke = v; }, 0.5, 10, 1);
    zahl(L"Randlicht", L"Stärke des hellen Saums an den Rändern der Objekte im Zeichentrick-Stil (0 = aus, Vorgabe 0,3).\n"
             L"Wo die Fläche vom Betrachter wegkippt, wird sie aufgehellt: hebt Objekte vom Hintergrund ab.", []
         { return randlicht; }, [](double v)
         { randlicht = v; }, 0, 5, 2);
    abschnittEnde();
    int linksUnten = y;

    // ---------------- rechte Spalte ----------------
    x = rand + spalteB + 2 * rand;
    y = rand;

    ueberschrift(L"NEBEL", true, &nebelVorhanden);
    schalter(L"Nebel", L"Nebel über die ganze Szene: jeder Strahl verliert mit der Entfernung Farbe an die Nebelfarbe\n"
             L"(e hoch −Dichte · Strecke). Ferne Objekte verblassen, gibt Tiefe (Dunst, Berge, unter Wasser).", &nebelVorhanden);
    zahl(L"Dichte",
         L"Nebeldichte: Anteil des Lichts, der nach Strecke d übrig bleibt = e hoch (−Dichte · d).\n"
             L"Hängt von der Szenengröße ab: bei Dichte · Szenengröße = 1 bleibt über die ganze Szene etwa ein Drittel.\n"
             L"Klein = leichter Dunst, groß = dichter Nebel.",
         []
         { return fogDensity; }, [](double v)
         { fogDensity = v; }, 0, 100, 4);
    farbe(L"Nebelfarbe", L"Farbe, zu der alles mit zunehmender Entfernung hin verblasst (auch der Himmel hinter der Szene).\n"
             L"Hell = Dunst/Nebel, dunkelblau-grün = unter Wasser, orange = Staub (Mars, Abend).", []
          { return &fogColor; });
    schalter(L"Nebel beleuchtet (Streuung)",
             L"An: der Nebel streut zusätzlich Licht der Lichtquellen (einfache Mie-Streuung): heller Schein um Lampen,\n"
             L"sichtbare Lichtkegel. Mischung über \"Anteil sichtbar\" / \"Anteil leuchtend\". Aus: nur die Nebelfarbe.",
             &nebelBeleuchtet);
    farbe(L"Farbe des Streulichts", L"Farbe des Lichts, das der beleuchtete Nebel streut (z.B. warm um Laternen).", []
          { return &fogLightColor; });
    schalter(L"Nebel dämpft Licht",
             L"Licht von Punkt-, Kugel-, Flächen- und Mesh-Lichtern verliert auf dem Weg durch den Nebel an Kraft\n"
             L"(wie die Sicht): weit entfernte Lampen beleuchten weniger.\n"
             L"Richtungslicht nicht: der Nebel hat keinen Rand, es käme sonst gar kein Licht an.",
             &nebelDaempftLicht);
    zahl(L"Anteil sichtbar", L"Anteil der Nebelfarbe beim beleuchteten Nebel (0 bis 1): wie stark der Nebel selbst die Sicht verdeckt.", []
         { return fogPartVisible; }, [](double v)
         { fogPartVisible = v; }, 0, 1, 2);
    zahl(L"Anteil leuchtend", L"Anteil des Streulichts beim beleuchteten Nebel (0 bis 1): wie hell der Nebel um Lichtquellen leuchtet.", []
         { return fogPartLightning; }, [](double v)
         { fogPartLightning = v; }, 0, 1, 2);

    ueberschrift(L"RAUCH", true, &rauchVorhanden);
    schalter(L"Rauch", L"Rauchsäulen in den Rauchboxen (Raymarching; in den Ansichten als graue Rahmen).\n"
             L"Die Säule füllt die Box: Breite und Höhe der Box bestimmen ihre Form. Rauch verdeckt und färbt,\n"
             L"was dahinter liegt; mit \"Volumenschatten\" wirft er auch Schatten. Kostet Zeit für jeden Strahl durch eine Box.", &rauchVorhanden);
    const int listeH = 4 * LABEL_H + 8, knopfB = 100;
    einRauchListe = CreateWindowExW(0, L"LISTBOX", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY | WS_BORDER,
                                    x, y, spalteB - knopfB - GAP, listeH, hEinstellungen,
                                    (HMENU)(INT_PTR)ID_EIN_RAUCHLISTE, hInst, nullptr);
    SendMessageW(einRauchListe, WM_SETFONT, (WPARAM)gFont, TRUE);
    tooltip(einRauchListe, L"Rauchboxen; die gewählte Box wird unten bearbeitet");
    knopf(L"Neu", L"Neue Rauchbox beim Kamera-Zielpunkt", ID_EIN_RAUCH_NEU, x + spalteB - knopfB, knopfB);
    y += BTN_H + GAP;
    knopf(L"Löschen", L"Gewählte Rauchbox löschen", ID_EIN_RAUCH_WEG, x + spalteB - knopfB, knopfB);
    y += listeH - (BTN_H + GAP) + GAP;
    vektor(L"Box von (min)", L"Untere Ecke der Rauchbox (Welt-Koordinaten). Der Rauch beginnt am Boden der Box.", []
           { SmokeBox *b = gewaehlteRauchbox(); return b ? &b->min : nullptr; }, true);
    vektor(L"Box bis (max)", L"Obere Ecke der Rauchbox (der Rauch steigt von unten nach oben).", []
           { SmokeBox *b = gewaehlteRauchbox(); return b ? &b->max : nullptr; }, true);
    farbe(L"Rauchfarbe", L"Farbe des Rauchs dieser Box: hell = Dampf/Nebelschwaden, dunkel = Brandrauch.", []
          { SmokeBox *b = gewaehlteRauchbox(); return b ? &b->color : nullptr; });
    zahl(L"Dichte", L"Dichte des Rauchs (densityMultiplier), typisch 50 bis 200: wie stark der Rauch Licht schluckt und verdeckt.\n"
             L"Größer = dunklere, undurchsichtigere Säule, deutlichere Schatten.", []
         { SmokeBox *b = gewaehlteRauchbox(); return b ? b->densityMultiplier : 0.0; }, [](double v)
         { if (SmokeBox *b = gewaehlteRauchbox()) b->densityMultiplier = v; }, 0, 1e6, 1, true);
    zahl(L"Zufallswert (seed)", L"Startwert für das Rauschen: andere Zahl = andere Form der Rauchsäule (gleiche Dichte und Größe).\n"
             L"In einer Animation konstant lassen, sonst springt der Rauch.", []
         { SmokeBox *b = gewaehlteRauchbox(); return b ? b->seed : 0.0; }, [](double v)
         { if (SmokeBox *b = gewaehlteRauchbox()) b->seed = v; }, -1e9, 1e9, 1, true);

    ueberschrift(L"WOLKENVOLUMEN", true, &wolkeVolumeVorhanden);
    schalter(L"Wolkenvolumen",
             L"Volumetrische Wolken im Himmelsbereich (Raymarching; in den Ansichten als blaue Rahmen).\n"
             L"Sie verdecken den Himmel, werden vom Licht beleuchtet und werfen mit \"Volumenschatten\" Wolkenschatten.\n"
             L"Kostet Zeit für jeden Strahl, der den Wolkenbereich kreuzt.",
             &wolkeVolumeVorhanden);
    vektor(L"Himmel von (min)", L"Untere Ecke des Himmelsbereichs (SkyMin): nur in diesem Quader werden Wolken gesucht.", []
           { return &SkyMin; });
    vektor(L"Himmel bis (max)", L"Obere Ecke des Himmelsbereichs (SkyMax).", []
           { return &SkyMax; });
    zahl(L"Wolken ab Höhe", L"Untergrenze der Wolkenschicht (WolkeboxMin.y). Liegt die Schicht ganz außerhalb des Himmelsbereichs,\n"
             L"wird dessen ganze Höhe benutzt. Lichter oberhalb leuchten durch die Wolken (mit Volumenschatten geschwächt).", []
         { return WolkeboxMin.y; }, [](double v)
         { WolkeboxMin.y = v; }, -1e12, 1e12, 2);
    zahl(L"Wolken bis Höhe", L"Obergrenze der Wolkenschicht (WolkeboxMax.y). Die Größe der Wolken richtet sich nach Schichtdicke\n"
             L"und Breite des Himmelsbereichs: dicke Schicht = hohe Quellwolken.", []
         { return WolkeboxMax.y; }, [](double v)
         { WolkeboxMax.y = v; }, -1e12, 1e12, 2);

    ueberschrift(L"VOLUMENSCHATTEN", true, &volumenSchatten);
    schalter(L"Rauch und Wolken werfen Schatten",
             L"Das direkte Licht wird auf dem Weg durch Rauchboxen und Wolkenschicht abgeschwächt:\n"
             L"Wolkenschatten auf dem Boden, Halbschatten unter Rauchsäulen. Kostet Zeit nur dort,\n"
             L"wo ein Schattenstrahl ein Volumen kreuzt.",
             &volumenSchatten);
    zahl(L"Schritte je Schattenstrahl",
         L"Abtastpunkte je Schattenstrahl und Volumen (Vorgabe 12). Mehr = genauere, weichere Volumenschatten, aber langsamer.",
         []
         { return double(volumenSchattenSchritte); }, [](double v)
         { volumenSchattenSchritte = int(std::round(v)); }, 1, 256, 0);

    ueberschrift(L"REGEN", true, &regen::gRegen.an);
    schalter(L"Regen", L"Regen im gerenderten Bild: fallende Tropfen als feine Striche (Bewegungsunschärfe), Wellenringe auf\n"
                       L"Flächen mit \"Regenringe\" (Material-Reiter, z.B. Wasser) und nasse Flächen. Bewegt sich mit der Zeitleiste.\n"
                       L"Ohne Objekte: die Tropfen entstehen beim Rendern aus Zufallszahlen (kostet etwas Rechenzeit je Pixel).",
             &regen::gRegen.an);
    zahl(L"Dichte (0..1)",
         L"Anteil der Rasterzellen mit einem Tropfen. 0,1 = Nieselregen, 0,35 = Regen, 0,8 = Wolkenbruch.",
         []
         { return regen::gRegen.dichte; }, [](double v)
         { regen::gRegen.dichte = v; }, 0, 1, 2);
    zahl(L"Tropfenabstand",
         L"Rastergröße in Einheiten (mittlerer Abstand zweier Tropfen). Kleiner = mehr Tropfen, mehr Rechenzeit.",
         []
         { return regen::gRegen.abstand; }, [](double v)
         { regen::gRegen.abstand = v; }, 0.02, 10, 3);
    zahl(L"Fallgeschwindigkeit",
         L"Einheiten pro Sekunde (echter Regen: 6 bis 9 m/s). Wirkt in Animationen.",
         []
         { return regen::gRegen.geschwindigkeit; }, [](double v)
         { regen::gRegen.geschwindigkeit = v; }, 0.1, 100, 1);
    zahl(L"Strichlänge",
         L"Länge eines Tropfen-Strichs in Einheiten (Weg in der Belichtungszeit). Höchstens 0,8 × Tropfenabstand.",
         []
         { return regen::gRegen.laenge; }, [](double v)
         { regen::gRegen.laenge = v; }, 0.001, 10, 3);
    zahl(L"Strichdicke",
         L"Radius eines Strichs in Einheiten (echte Tropfen: 1 bis 3 mm). Dünner als ein Bildpunkt: breiter, dafür blasser.",
         []
         { return regen::gRegen.dicke; }, [](double v)
         { regen::gRegen.dicke = v; }, 0.0001, 1, 4);
    zahl(L"Helligkeit",
         L"Helligkeit der Striche (Anteil der Himmelsfarbe). Vor dunklem Hintergrund deutlicher als vor hellem Himmel.",
         []
         { return regen::gRegen.helligkeit; }, [](double v)
         { regen::gRegen.helligkeit = v; }, 0, 5, 2);
    zahl(L"Deckkraft",
         L"Wie stark ein Strich den Hintergrund verdeckt (0..1).",
         []
         { return regen::gRegen.deckkraft; }, [](double v)
         { regen::gRegen.deckkraft = v; }, 0, 1, 2);
    zahl(L"Wind X",
         L"Wind in Einheiten pro Sekunde entlang X: die Striche fallen schräg.",
         []
         { return regen::gRegen.windX; }, [](double v)
         { regen::gRegen.windX = v; }, -100, 100, 2);
    zahl(L"Wind Z",
         L"Wind in Einheiten pro Sekunde entlang Z.",
         []
         { return regen::gRegen.windZ; }, [](double v)
         { regen::gRegen.windZ = v; }, -100, 100, 2);
    zahl(L"Sichtweite",
         L"Weiter als so viele Einheiten von der Kamera keine Striche (dort wäre es ohnehin nur noch Dunst).",
         []
         { return regen::gRegen.sichtweite; }, [](double v)
         { regen::gRegen.sichtweite = v; }, 0.5, 10000, 1);
    zahl(L"Ringabstand",
         L"Raster der Einschläge auf Flächen mit \"Regenringe\" (Einheiten). Kleiner = mehr Ringe.",
         []
         { return regen::gRegen.ringAbstand; }, [](double v)
         { regen::gRegen.ringAbstand = v; }, 0.01, 10, 3);
    zahl(L"Ringtakt (s)",
         L"Sekunden zwischen zwei Einschlägen je Rasterzelle. Kleiner = unruhigeres Wasser.",
         []
         { return regen::gRegen.ringTakt; }, [](double v)
         { regen::gRegen.ringTakt = v; }, 0.05, 20, 2);
    zahl(L"Ringgröße",
         L"Maßstab der Ringe (1 = Szene in Metern: Ringe wachsen etwa 25 cm/s, Wellenlänge 4,5 cm).",
         []
         { return regen::gRegen.ringGroesse; }, [](double v)
         { regen::gRegen.ringGroesse = v; }, 0.01, 100, 2);
    schalter(L"Spritzer", L"Heller Spritzer an der Einschlagstelle, kurz nach dem Aufprall eines Tropfens.", &regen::gRegen.spritzer);
    schalter(L"Alle Flächen nass", L"Bei Regen alle Flächen nass: dunkler und glänzender (Glas, Wasser und Spiegel ausgenommen).\n"
                                    L"Einzelne Objekte: \"Nass\" im Material-Reiter (wirkt auch ohne Regen).", &regen::gRegen.allesNass);
    zahl(L"Nässe (0..1)",
         L"Wie nass die Flächen bei \"Alle Flächen nass\" sind.",
         []
         { return regen::gRegen.nassStaerke; }, [](double v)
         { regen::gRegen.nassStaerke = v; }, 0, 1, 2);
    abschnittEnde();
    y = std::max(y, linksUnten) + 10;
    einFussY = y;
    HWND hinweis = CreateLabel(hEinstellungen, hInst,
                               L"Änderungen gelten ab dem nächsten Rendern (Y); ein laufendes Rendering wird dafür angehalten.",
                               rand, y, breite - 2 * rand - 140, hinweisH);
    SetWindowLongPtrW(hinweis, GWLP_USERDATA, 1);
    knopf(L"Schließen", L"Fenster ausblenden (Zahnrad in der Leiste öffnet es wieder)", ID_EIN_SCHLIESSEN,
          breite - rand - 120, 120);
    hoehe = y + std::max(hinweisH, BTN_H) + rand;

    RECT r = {0, 0, breite, hoehe};
    AdjustWindowRectEx(&r, stil, FALSE, WS_EX_TOOLWINDOW);
    SetWindowPos(hEinstellungen, nullptr, 0, 0, r.right - r.left, r.bottom - r.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);

    // Abschnitte als Reiter: Lage merken, Reiterleiste oben, dann nur den gewaehlten Abschnitt zeigen
    einRand = rand;
    einFensterB = breite;
    einFussH = std::max(hinweisH, BTN_H);
    einSpalteRechts = rand + spalteB + rand;
    {
        WNDCLASSW wr{};
        wr.lpfnWndProc = EinReiterProc;
        wr.hInstance = hInst;
        wr.hCursor = LoadCursor(nullptr, IDC_HAND);
        wr.lpszClassName = L"SZENEN_EIN_REITER";
        RegisterClassW(&wr);
        einReiterLeiste = CreateWindowExW(0, L"SZENEN_EIN_REITER", L"", WS_CHILD | WS_VISIBLE, rand, rand, spalteB, REITER_H,
                                          hEinstellungen, nullptr, hInst, nullptr);
    }
    abschnitteErfassen();
    einstellungenAnordnen();
}

void EinstellungenZeigen()
{
    if (!hEinstellungen)
    {
        bildGeladen = backgroundTexture.width() > 0; // initScene laedt das Bild nur, wenn umgebungsbild gesetzt ist
        einstellungenAnlegen();
    }
    if (IsWindowVisible(hEinstellungen))
    {
        ShowWindow(hEinstellungen, SW_HIDE);
        return;
    }
    ShowWindow(hEinstellungen, SW_SHOW);
    einstellungenAnzeigen();
}

// zuletzt geoeffnete/gespeicherte Szene: sofort in raytracer_editor.ini (fuer den naechsten Start)
void LetzteSzeneMerken(const std::string &datei)
{
    if (datei.empty())
        return;
    // Liste "Zuletzt geoeffnet": nach vorne, doppelte weg, hoechstens ZULETZT_ANZAHL
    zuletztGeoeffnet.erase(std::remove(zuletztGeoeffnet.begin(), zuletztGeoeffnet.end(), datei), zuletztGeoeffnet.end());
    zuletztGeoeffnet.insert(zuletztGeoeffnet.begin(), datei);
    if (zuletztGeoeffnet.size() > ZULETZT_ANZAHL)
        zuletztGeoeffnet.resize(ZULETZT_ANZAHL);
    letzteSzeneDatei = datei;
    editorEinstellungenSpeichernFallsGeaendert();
    menueLeisteAktualisieren(); // Menue "Datei -> Zuletzt geoeffnet"
}
const std::string &LetzteSzene() { return letzteSzeneDatei; }

// fuer SceneSaver/SceneLoader: Datei des Umgebungsbilds
const std::string &UmgebungsbildDatei() { return bildDatei; }
void UmgebungsbildGeladen(const std::string &datei)
{
    bildGeladen = true;
    bildDatei = datei;
}

//--------------------------------------------------
// Statusleiste unter den Ansichten
//--------------------------------------------------

static std::wstring statusHinweis;    // kurze Meldung (z.B. Ergebnis einer Kanten-Operation)
static ULONGLONG statusHinweisBis = 0;
static std::wstring statusZusammenstellen()
{
    const std::wstring trenner = L"     \u2502     ";
    std::wstring t;

    // Weltpunkt unter der Maus
    const MausPosition &m = gMausPosition;
    if (m.gueltig)
    {
        const wchar_t *namen[3] = {L"X ", L"Y ", L"Z "};
        double werte[3] = {m.punkt.x, m.punkt.y, m.punkt.z};
        t += std::wstring(m.ansicht) + L":   ";
        for (int k = 0; k < 3; ++k)
            t += namen[k] + (m.achse[k] ? zahlText(werte[k], 3) : std::wstring(L"\u2013")) + L"   ";
    }
    else
        t += L"Maus: \u2013";

    // Modus
    if (materialPinsel::gAktiv)
        t += trenner + L"Material-Pinsel (\u201E" + materialAblage.quelle +
             L"\u201C): Klick auf ein Teil überträgt Material und Textur, Umschalt+Klick: ganzes Objekt, Esc: beenden";
    else if (kanten::gModus)
    {
        size_t n;
        {
            std::lock_guard<std::mutex> lock(kanten::gMutex);
            n = kanten::gGewaehlt.size();
        }
        t += trenner + L"Kanten: " + std::to_wstring(n) + L" gewählt (Klick, Strg+Klick) – Rechtsklick/Entf: Loch, K: "
                                                          L"zusammenziehen, U: umklappen, Esc: Auswahl leeren";
    }
    else
        t += trenner + (selectedAction == SelectedAction::MOVE     ? L"Move"
                        : selectedAction == SelectedAction::ROTATE ? L"Rotate"
                                                                   : L"Scale");
    if (!statusHinweis.empty() && GetTickCount64() < statusHinweisBis)
        t += trenner + statusHinweis;

    // Auswahl
    std::vector<object *> alle = ausgewaehlteObjekte();
    if (alle.size() > 1)
        t += trenner + std::to_wstring(alle.size()) + L" Objekte ausgewählt";
    else if (alle.size() == 1)
        t += trenner + objektGrundname(alle[0], size_t(std::max(0, objektIndex(alle[0]))));
    else if (selectedType == SelectedType::LIGHT && selectedLight && lichtIndex(selectedLight) >= 0)
        t += trenner + lichtName(selectedLight, size_t(lichtIndex(selectedLight)));
    else if (selectedType == SelectedType::CAMERA || selectedType == SelectedType::LOOKAT)
        t += trenner + L"Kamera";
    else
        t += trenner + L"keine Auswahl";

    // Datei und Speicherstand
    std::string datei = aktuelleDatei();
    size_t schnitt = datei.find_last_of("/\\");
    if (schnitt != std::string::npos)
        datei = datei.substr(schnitt + 1);
    t += trenner + (datei.empty() ? std::wstring(L"(ohne Datei)") : std::wstring(datei.begin(), datei.end()));
    if (szeneGeaendert())
        t += L"  *";

    // Infos des Renderers (stehen im Titel des Rendering-Fensters, z.B. Fortschritt und Zeit);
    // erst, wenn der Render-Code den Titel geaendert hat
    if (hRenderfenster)
    {
        wchar_t titel[512] = {};
        GetWindowTextW(hRenderfenster, titel, 512);
        if (titel[0] && renderTitelAnfang != titel)
            t += trenner + L"Rendering: " + titel;
    }

    // Animation wird gerendert
    if (gAnimationBilder > 0)
    {
        if (gAnimationBild < 0)
            t += trenner + L"Animation: Video wird mit ffmpeg erstellt …";
        else
        {
            const int bild = gAnimationBild.load(), n = gAnimationBilder.load();
            const int fertig = std::max(0, bild - 1);
            t += trenner + L"Animation: Bild " + std::to_wstring(bild) + L" von " + std::to_wstring(n) + L" (" +
                 std::to_wstring(n > 0 ? fertig * 100 / n : 0) + L" %)";
            // Restzeit aus den in diesem Lauf fertigen Bildern (uebersprungene vorhandene zaehlen nicht)
            const int dieserLauf = fertig - (gAnimationAbBild.load() - 1);
            if (dieserLauf > 0 && gAnimationStartMs > 0)
            {
                double vergangen = double(GetTickCount64() - gAnimationStartMs.load()) / 1000.0;
                double rest = vergangen / dieserLauf * double(n - fertig);
                auto dauer = [](double sek)
                {
                    long long s = (long long)(sek + 0.5);
                    if (s >= 3600)
                        return std::to_wstring(s / 3600) + L" h " + std::to_wstring((s % 3600) / 60) + L" min";
                    if (s >= 60)
                        return std::to_wstring(s / 60) + L" min " + std::to_wstring(s % 60) + L" s";
                    return std::to_wstring(s) + L" s";
                };
                t += L" – je Bild " + dauer(vergangen / dieserLauf) + L", noch ca. " + dauer(rest);
            }
        }
    }

    // naechster Rueckgaengig-Schritt
    std::string schritt = naechstesRueckgaengig();
    if (!schritt.empty())
        t += trenner + L"Strg+Z: " + ausUtf8(schritt);

    return t;
}

static void statusHinweisSetzen(const std::wstring &t)
{
    statusHinweis = t;
    statusHinweisBis = GetTickCount64() + 6000;
}

static void statusAktualisieren()
{
    if (!hStatusleiste)
        return;
    std::wstring t = statusZusammenstellen();
    if (t != statusText)
    {
        statusText = t;
        InvalidateRect(hStatusleiste, nullptr, FALSE);
    }
}

//--------------------------------------------------
// Kanten-Modus (Knopf "Kanten"): Kanten des gewaehlten Netzes in einer Ansicht anklicken (Strg = mehrere),
// dann loeschen (Loch), zusammenziehen oder umklappen - Rechtsklick in der Ansicht oder Entf / K / U
//--------------------------------------------------

static void kantenModusSetzen(bool an)
{
    std::vector<object *> sel = ausgewaehlteObjekte();
    {
        std::lock_guard<std::mutex> lock(kanten::gMutex);
        kanten::gModus = an;
        kanten::gObjekt = an && !sel.empty() ? sel.front() : nullptr;
        kanten::gGewaehlt.clear();
        ++kanten::gStand;
    }
    for (HWND h : {g.btnMove, g.btnRotate, g.btnScale, g.btnKanten})
        InvalidateRect(h, nullptr, FALSE);
    statusAktualisieren();
}

// Auswahl in der Liste gewechselt: das Netz des Kanten-Modus folgt (die gewaehlten Kanten gehoeren zum alten)
static void kantenAbgleichen()
{
    if (!kanten::gModus)
        return;
    std::vector<object *> sel = ausgewaehlteObjekte();
    object *soll = sel.empty() ? nullptr : sel.front();
    if (soll == kanten::gObjekt)
        return;
    std::lock_guard<std::mutex> lock(kanten::gMutex);
    kanten::gObjekt = soll;
    kanten::gGewaehlt.clear();
    ++kanten::gStand;
}

// art: 0 loeschen (Loch), 1 zusammenziehen, 2 umklappen
static void kantenAusfuehren(int art)
{
    static const char *namen[3] = {"Kanten löschen", "Kanten zusammenziehen", "Kanten umklappen"};
    std::vector<kanten::Kante> gewaehlt;
    object *o = nullptr;
    {
        std::lock_guard<std::mutex> lock(kanten::gMutex);
        gewaehlt = kanten::gGewaehlt;
        o = kanten::gObjekt;
    }
    std::vector<object *> sel = ausgewaehlteObjekte();
    if (!o || std::find(sel.begin(), sel.end(), o) == sel.end() || gewaehlt.empty() || !gSzeneAendern)
    {
        MessageBeep(MB_ICONWARNING);
        return;
    }
    auto *mesh = dynamic_cast<Mesh *>(o);
    if (!mesh)
    {
        MessageBoxW(hToolWindow, L"Kanten bearbeiten geht bei Netzen (Würfel, Wand, Körper, Rotationsobjekt, Ellipsoid, Röhre,\n"
                                 L"Stein, OBJ-Import). Kugeln, Stäbe, Bäume oder Texte zuerst in ein Netz umwandeln, z.B. mit\n"
                                 L"\"Objekte addieren\" zusammen mit einem anderen Objekt.",
                    L"Kanten", MB_OK | MB_ICONINFORMATION);
        return;
    }
    const double tol = kanten::toleranz({&mesh->triangles});
    size_t ergebnis = 0;
    const size_t vorher = mesh->triangles.size();
    aenderungBeginnen(namen[art], {o});
    gSzeneAendern([&]
                  {
        if (art == 0)
            ergebnis = kanten::loeschen(mesh->triangles, gewaehlt, tol);
        else if (art == 1)
            ergebnis = kanten::zusammenziehen(mesh->triangles, gewaehlt, tol);
        else
            ergebnis = kanten::umklappen(mesh->triangles, gewaehlt, tol);
        mesh->recomputeMeshBoundsFromTriangles();
        mesh->buildBVH();
        alleGruppenAktualisieren(); });
    aenderungAbschliessen();
    {
        std::lock_guard<std::mutex> lock(kanten::gMutex);
        kanten::gGewaehlt.clear();
        ++kanten::gStand;
    }
    abgleichen();
    std::wstring t;
    if (art == 0)
        t = std::to_wstring(ergebnis) + L" Dreiecke entfernt (Loch).";
    else if (art == 1)
        t = std::to_wstring(gewaehlt.size()) + L" Kanten zusammengezogen, " + std::to_wstring(ergebnis) + L" Dreiecke weggefallen.";
    else
        t = std::to_wstring(ergebnis) + L" von " + std::to_wstring(gewaehlt.size()) + L" Kanten umgeklappt" +
            (ergebnis < gewaehlt.size() ? L" (übrige: nicht genau zwei Dreiecke an der Kante)." : L".");
    (void)vorher;
    statusHinweisSetzen(t + L" Strg+Z = rückgängig.");
}

// Tasten im Kanten-Modus (aus main.cpp, vor allen anderen): true = erledigt
bool KantenTaste(WPARAM taste)
{
    if (!kanten::gModus)
        return false;
    bool leer;
    {
        std::lock_guard<std::mutex> lock(kanten::gMutex);
        leer = kanten::gGewaehlt.empty();
    }
    switch (taste)
    {
    case VK_DELETE:
        if (leer)
            return false; // ohne gewaehlte Kanten: wie bisher (Objekt loeschen)
        kantenAusfuehren(0);
        return true;
    case 'K':
        kantenAusfuehren(1);
        return true;
    case 'U':
        kantenAusfuehren(2);
        return true;
    case VK_ESCAPE:
        if (leer)
            kantenModusSetzen(false);
        else
        {
            std::lock_guard<std::mutex> lock(kanten::gMutex);
            kanten::gGewaehlt.clear();
            ++kanten::gStand;
        }
        return true;
    case 'M':
    case 'R':
    case 'S':
        kantenModusSetzen(false); // anderer Modus: Kanten-Modus aus, Taste wirkt weiter
        return false;
    default:
        return false;
    }
}

// Rechtsklick in einer Ansicht (Kanten-Modus): Menue mit den Operationen
void KantenMenue(HWND fenster, int x, int y)
{
    if (!kanten::gModus || !fenster)
        return;
    size_t n;
    {
        std::lock_guard<std::mutex> lock(kanten::gMutex);
        n = kanten::gGewaehlt.size();
    }
    enum
    {
        K_LOESCHEN = 1,
        K_ZUSAMMEN,
        K_UMKLAPPEN,
        K_LEEREN,
        K_ENDE
    };
    HMENU m = CreatePopupMenu();
    std::wstring kopf = n == 0 ? std::wstring(L"Keine Kante gewählt (Klick auf eine Kante)")
                               : std::to_wstring(n) + (n == 1 ? L" Kante gewählt" : L" Kanten gewählt");
    AppendMenuW(m, MF_STRING | MF_GRAYED, 0, kopf.c_str());
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    const UINT f = n ? 0 : MF_GRAYED;
    AppendMenuW(m, MF_STRING | f, K_LOESCHEN, L"Löschen (Loch: Kante mit ihren Dreiecken)\tEntf");
    AppendMenuW(m, MF_STRING | f, K_ZUSAMMEN, L"Zusammenziehen (Endpunkte auf die Mitte)\tK");
    AppendMenuW(m, MF_STRING | f, K_UMKLAPPEN, L"Umklappen (andere Diagonale)\tU");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING | f, K_LEEREN, L"Kantenauswahl aufheben\tEsc");
    AppendMenuW(m, MF_STRING, K_ENDE, L"Kanten-Modus beenden");
    POINT p = {x, y};
    ClientToScreen(fenster, &p);
    const int wahl = int(TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY, p.x, p.y, 0, fenster, nullptr));
    DestroyMenu(m);
    switch (wahl)
    {
    case K_LOESCHEN: kantenAusfuehren(0); break;
    case K_ZUSAMMEN: kantenAusfuehren(1); break;
    case K_UMKLAPPEN: kantenAusfuehren(2); break;
    case K_LEEREN:
    {
        std::lock_guard<std::mutex> lock(kanten::gMutex);
        kanten::gGewaehlt.clear();
        ++kanten::gStand;
        break;
    }
    case K_ENDE: kantenModusSetzen(false); break;
    default: break;
    }
}

// ---------- Material-Pinsel (material_pinsel.h) ----------
// "Material aufnehmen": Material (Farben, Glanz, Glas ...) und alle Textur-Einstellungen eines Objekts merken.
// Danach uebertraegt jeder Klick in einer Ansicht sie auf das angeklickte Teil (Umschalt+Klick: ganzes Objekt),
// bis Esc; "Material auf Auswahl uebertragen" im Rechtsklick-Menue wirkt auf alle gewaehlten Objekte.

// Gruppe/Instanz: das erste Teil mit eigenem Material
static object *materialQuelle(object *o)
{
    if (!o)
        return nullptr;
    if (auto *in = dynamic_cast<Instanz *>(o))
        return in->vorlage ? materialQuelle(in->vorlage.get()) : nullptr;
    if (auto *g = dynamic_cast<Gruppe *>(o))
    {
        for (auto &k : g->kinder)
            if (object *q = materialQuelle(k.get()))
                return q;
        return nullptr;
    }
    if (dynamic_cast<Tree *>(o) || dynamic_cast<VolumenBox *>(o) || o->texturPar().fellHuelle)
        return nullptr;
    return o;
}

static bool materialAufnehmen(object *o)
{
    object *q = materialQuelle(o);
    if (!q)
    {
        MessageBeep(MB_ICONWARNING);
        return false;
    }
    MaterialAblage a;
    a.da = true;
    a.mat = q->mat;
    if (auto *m = dynamic_cast<Mesh *>(q); m && !m->triangles.empty())
        a.mat = m->triangles.front().mat; // so wird es gerendert
    a.tex = q->texture;
    a.texName = q->textureName;
    a.modus = q->textureMode;
    a.par = q->texPar;
    a.spiegelU = q->texturSpiegelU;
    a.spiegelV = q->texturSpiegelV;
    a.drehung = q->texturDrehung;
    a.matName = dynamic_cast<Mesh *>(q) ? std::string() : q->materialName; // bei Netzen ist es die OBJ-Datei
    a.quelle = ausUtf8(q->name.empty() ? (o->name.empty() ? std::string("Objekt") : o->name) : q->name);
    materialAblage = a;
    return true;
}

static void materialAufObjekt(object *o, const MaterialAblage &a)
{
    if (!o)
        return;
    if (auto *in = dynamic_cast<Instanz *>(o)) // wirkt auf die Vorlage (alle Instanzen)
    {
        if (in->vorlage)
            materialAufObjekt(in->vorlage.get(), a);
        return;
    }
    if (auto *g = dynamic_cast<Gruppe *>(o))
    {
        for (auto &k : g->kinder)
            materialAufObjekt(k.get(), a);
        return;
    }
    if (dynamic_cast<Tree *>(o) || dynamic_cast<VolumenBox *>(o) || o->texturPar().fellHuelle)
        return; // Baum: erst aufteilen; Fell-Huellen behalten ihre Haarbilder
    o->mat = a.mat;
    if (auto *m = dynamic_cast<Mesh *>(o))
        for (auto &t : m->triangles)
            t.mat = a.mat;
    else if (auto *pf = dynamic_cast<Pfeil *>(o))
        for (auto &t : pf->tris)
            t.mat = a.mat;
    else if (auto *tx = dynamic_cast<Text3D *>(o))
        tx->setMaterial(a.mat);
    if (!dynamic_cast<Mesh *>(o))
        o->materialName = a.matName;
    o->texPar = a.par; // vor setTexture: Netze geben die Einstellungen an ihre Dreiecke weiter
    o->texturSpiegelU = a.spiegelU;
    o->texturSpiegelV = a.spiegelV;
    o->texturDrehung = a.drehung;
    o->setTexture(a.tex);
    o->textureName = a.texName;
    o->setTextureMode(a.modus);
    if (auto *m = dynamic_cast<Mesh *>(o))
    {
        if (o->texturPar().objektfest)
            m->texturNeuFestmachen(); // haftet ab der jetzigen Lage
        else
            m->texturBoxSetzen();
    }
}

static void materialUebertragen(const std::vector<object *> &ziele)
{
    if (!materialAblage.da || ziele.empty())
    {
        MessageBeep(MB_ICONWARNING);
        return;
    }
    aenderungBeginnen("Material übertragen", ziele);
    {
        std::lock_guard<std::mutex> lock(sceneMutex); // GPU-Ansichten lesen die Dreiecke
        for (object *o : ziele)
            materialAufObjekt(o, materialAblage);
    }
    aenderungAbschliessen();
    szeneGeaendertMarkieren();
    materialAnzeigen();
    eigenschaftenAnzeigen();
}

static void materialPinselSetzen(bool an)
{
    materialPinsel::gAktiv = an && materialAblage.da;
    if (!materialPinsel::gAnwenden)
        materialPinsel::gAnwenden = [](object *o)
        {
            if (!o)
                return;
            materialUebertragen({o});
            auswahlSetzen({o}); // zeigt, was den neuen Anstrich bekam
            abgleichen();
        };
    statusAktualisieren();
}

// Kontextmenue fuer die Auswahl (Objektliste und Rechtsklick in einer Ansicht)
static void objektKontextMenue(POINT bildschirm)
{
    enum
    {
        M_SICHTBAR = 100,
        M_MAT_AUFNEHMEN = 200,
        M_MAT_UEBERTRAGEN,
        M_MAT_PINSEL_AUS,
        M_ABLAUF,
        M_GRUPPIEREN = 1,
        M_AUFHEBEN,
        M_BAUM,
        M_KOPIE,
        M_LOESCHEN,
        M_INSTANZ,
        M_LOESEN,
        M_AUS_GRUPPE,
        M_IN_GRUPPE = 1000 // + Nummer der Zielgruppe
    };
    std::vector<object *> alle = ausgewaehlteObjekte();
    bool gruppeDabei = false, baum = alle.size() == 1 && dynamic_cast<Tree *>(alle[0]) && !gruppeVon(alle[0]);
    for (object *o : alle)
        gruppeDabei = gruppeDabei || (dynamic_cast<Gruppe *>(o) && !gruppeVon(o));

    // Zeigen / Verstecken: sind alle versteckt, werden sie gezeigt, sonst alle versteckt
    bool alleVersteckt = !alle.empty();
    for (object *o : alle)
        alleVersteckt = alleVersteckt && o->versteckt;
    HMENU menue = CreatePopupMenu();
    AppendMenuW(menue, MF_STRING | (alle.empty() ? MF_GRAYED : 0), M_SICHTBAR,
                alleVersteckt ? L"Zeigen / Verstecken: zeigen" : L"Zeigen / Verstecken: verstecken");
    AppendMenuW(menue, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menue, MF_STRING | (alle.size() >= 2 ? 0 : MF_GRAYED), M_GRUPPIEREN, L"Gruppieren\tStrg+G");
    AppendMenuW(menue, MF_STRING | (gruppeDabei ? 0 : MF_GRAYED), M_AUFHEBEN, L"Gruppe aufheben\tStrg+Umschalt+G");
    AppendMenuW(menue, MF_STRING | (baum ? 0 : MF_GRAYED), M_BAUM, L"Baum aufteilen (Stamm, Blätter)");
    AppendMenuW(menue, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menue, MF_STRING | (alle.empty() ? MF_GRAYED : 0), M_KOPIE, L"Duplizieren\tStrg+D");
    bool instanzDabei = false;
    for (object *o : alle)
        instanzDabei = instanzDabei || hatInstanzen(o); // auch Gruppen mit Instanzen darin
    AppendMenuW(menue, MF_STRING | (alle.empty() ? MF_GRAYED : 0), M_INSTANZ,
                L"Instanz erstellen (verknüpfte Kopie)\tStrg+Umschalt+D");
    AppendMenuW(menue, MF_STRING | (instanzDabei ? 0 : MF_GRAYED), M_LOESEN, L"Instanz lösen (eigenständige Kopie)");
    // Gruppen der Szene (auch verschachtelte) als Ziel; die Auswahl selbst und ihre Teile nicht
    std::vector<Gruppe *> zielGruppen;
    HMENU gruppenMenue = CreatePopupMenu();
    std::function<void(object *, int)> gruppenSammeln = [&](object *o, int tiefe) {
        auto *gr = dynamic_cast<Gruppe *>(o);
        if (!gr || tiefe > 6)
            return;
        bool ausgeschlossen = false;
        for (object *a : alle)
            for (Gruppe *x = gr; x; x = gruppeVon(x))
                ausgeschlossen = ausgeschlossen || x == a;
        if (!ausgeschlossen)
        {
            zielGruppen.push_back(gr);
            std::wstring t = std::wstring(size_t(tiefe) * 4, L' ') + objektGrundname(gr, zielGruppen.size()) + L"  (" +
                             std::to_wstring(gr->kinder.size()) + L" Objekte)";
            AppendMenuW(gruppenMenue, MF_STRING, UINT_PTR(M_IN_GRUPPE + int(zielGruppen.size()) - 1), t.c_str());
        }
        for (auto &k : gr->kinder)
            gruppenSammeln(k.get(), tiefe + 1);
    };
    for (auto &so : scene)
        gruppenSammeln(so.get(), 0);
    bool teilDabei = false;
    for (object *o : alle)
        teilDabei = teilDabei || gruppeVon(o) != nullptr;
    AppendMenuW(menue, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menue, MF_POPUP | (alle.empty() || zielGruppen.empty() ? MF_GRAYED : 0), reinterpret_cast<UINT_PTR>(gruppenMenue),
                L"Zu Gruppe hinzufügen");
    AppendMenuW(menue, MF_STRING | (teilDabei ? 0 : MF_GRAYED), M_AUS_GRUPPE, L"Aus der Gruppe nehmen");
    AppendMenuW(menue, MF_SEPARATOR, 0, nullptr);
    // Material-Pinsel: Material und Textur aufnehmen und auf andere Objekte/Teile uebertragen
    AppendMenuW(menue, MF_STRING | (alle.empty() ? MF_GRAYED : 0), M_MAT_AUFNEHMEN,
                L"Material aufnehmen (Pinsel: danach Klick auf Teile überträgt)");
    {
        const std::wstring t = materialAblage.da
                                   ? L"Material von \u201E" + materialAblage.quelle + L"\u201C auf Auswahl übertragen"
                                   : std::wstring(L"Material auf Auswahl übertragen (zuerst aufnehmen)");
        AppendMenuW(menue, MF_STRING | (materialAblage.da && !alle.empty() ? 0 : MF_GRAYED), M_MAT_UEBERTRAGEN, t.c_str());
    }
    if (materialPinsel::gAktiv)
        AppendMenuW(menue, MF_STRING, M_MAT_PINSEL_AUS, L"Material-Pinsel beenden\tEsc");
    AppendMenuW(menue, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menue, MF_STRING | (alle.empty() ? MF_GRAYED : 0), M_ABLAUF, L"Bewegung / Ablauf einsetzen … (z.B. ab und zu rennen)");
    AppendMenuW(menue, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menue, MF_STRING | (alle.empty() ? MF_GRAYED : 0), M_LOESCHEN, L"Löschen\tEntf");
    int wahl = int(TrackPopupMenu(menue, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY, bildschirm.x, bildschirm.y, 0,
                                  hObjektFenster, nullptr));
    DestroyMenu(menue);

    if (wahl == M_SICHTBAR)
    {
        for (object *o : alle)
            o->versteckt = !alleVersteckt;
        ++gVersteckAenderungen;
        szeneGeaendertMarkieren();
        UpdateObjectList(); // Haken in der Liste
    }
    else if (wahl == M_GRUPPIEREN)
        AuswahlGruppieren();
    else if (wahl == M_AUFHEBEN)
        GruppeAufheben();
    else if (wahl == M_BAUM)
        BaumAufteilen();
    else if (wahl == M_KOPIE)
        AuswahlDuplizieren();
    else if (wahl == M_INSTANZ)
        AuswahlInstanzErstellen();
    else if (wahl == M_LOESEN)
        AuswahlInstanzLoesen();
    else if (wahl == M_AUS_GRUPPE)
    {
        if (!ausGruppeNehmen(alle))
            MessageBeep(MB_ICONWARNING);
        abgleichen();
    }
    else if (wahl >= M_IN_GRUPPE && wahl < M_IN_GRUPPE + int(zielGruppen.size()))
    {
        if (!zurGruppeHinzufuegen(zielGruppen[size_t(wahl - M_IN_GRUPPE)], alle))
            MessageBeep(MB_ICONWARNING);
        abgleichen();
    }
    else if (wahl == M_LOESCHEN)
        AusgewaehltesObjektLoeschen();
    else if (wahl == M_MAT_AUFNEHMEN)
    {
        if (materialAufnehmen(selectedObject && istAusgewaehlt(selectedObject) ? selectedObject : alle.front()))
            materialPinselSetzen(true);
    }
    else if (wahl == M_MAT_UEBERTRAGEN)
        materialUebertragen(alle);
    else if (wahl == M_MAT_PINSEL_AUS)
        materialPinselSetzen(false);
    else if (wahl == M_ABLAUF && !alle.empty())
        ablaufDialog(hObjektFenster, alle.front(), aktuelleZeit());
}

void AnsichtMenue(HWND fenster, int x, int y, const std::vector<object *> &kette)
{
    if (kanten::gModus)
    {
        KantenMenue(fenster, x, y);
        return;
    }
    if (!fenster)
        return;
    // Objekt unter der Maus waehlen, ausser es (oder eines seiner Teile) gehoert schon zur Auswahl
    std::vector<object *> sel = ausgewaehlteObjekte();
    bool dabei = false;
    for (object *o : kette)
        dabei = dabei || std::find(sel.begin(), sel.end(), o) != sel.end();
    if (!kette.empty() && !dabei)
    {
        auswahlSetzen({kette.front()});
        abgleichen();
    }
    if (ausgewaehlteObjekte().empty())
        return;
    POINT p = {x, y};
    ClientToScreen(fenster, &p);
    objektKontextMenue(p);
    abgleichen();
}

static LRESULT CALLBACK StatusProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_ERASEBKGND)
        return 1;
    if (msg == WM_PAINT)
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT r;
        GetClientRect(hwnd, &r);
        FillRect(hdc, &r, pinselFlaeche);

        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, FARBE_TEXT_GEDIMMT);
        HGDIOBJ alt = SelectObject(hdc, gFont);
        RECT text = r;
        text.left += 10;
        text.right -= 10;
        DrawTextW(hdc, statusText.c_str(), -1, &text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        SelectObject(hdc, alt);

        EndPaint(hwnd, &ps);
        return 0;
    }
    if (msg == WM_MOUSEACTIVATE)
        return MA_NOACTIVATE;
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ---------- Autom. Rendern (Schalter neben "Render") ----------
// Nach jeder Aenderung (bearbeitung.cpp sammelt die Bereiche als Weltquader) wird gewartet; kommt in der Wartezeit
// (Render-Einstellungen, Reiter Qualitaet) keine neue Aenderung und ist keine Maustaste gedrueckt, rendert das
// Programm im Hintergrund den Bildbereich, in dem sich etwas geaendert hat (Quader vorher und nachher, auf das Bild
// der Kamera abgebildet, mit etwas Rand). Kamera, Licht und Render-Einstellungen: ganzes Bild. Neue Aenderung,
// waehrend gerendert wird: anhalten, voriges Bild wieder zeigen, Wartezeit beginnt neu.

// Weltquader -> Anteile des Bilds (0..1); false: (teilweise) hinter der Kamera -> ganzes Bild
static bool bildBereichVon(const Vector3D &mn, const Vector3D &mx, double &x0, double &y0, double &x1, double &y1)
{
    if (cam.empty() || !(cam[0].halfWidth > 0) || !(cam[0].halfHeight > 0))
        return false;
    const Camera &c = cam[0];
    x0 = y0 = 1e30;
    x1 = y1 = -1e30;
    for (int i = 0; i < 8; ++i)
    {
        Vector3D p((i & 1) ? mx.x : mn.x, (i & 2) ? mx.y : mn.y, (i & 4) ? mx.z : mn.z);
        Vector3D d = p - c.position;
        const double z = d * (-c.w);
        if (z <= 1e-6)
            return false;
        const double sx = (d * c.u) / (z * c.halfWidth), sy = (d * c.v) / (z * c.halfHeight);
        const double nx = (1.0 - sx) * 0.5, ny = (1.0 - sy) * 0.5; // Umkehrung von Camera::makeRay
        x0 = std::min(x0, nx);
        x1 = std::max(x1, nx);
        y0 = std::min(y0, ny);
        y1 = std::max(y1, ny);
    }
    return true;
}

static void autoRendernPruefen()
{
    static unsigned letzterStand = gAutoRenderStand.load();
    static ULONGLONG letzteAktivitaet = 0;
    static std::vector<std::pair<Vector3D, Vector3D>> offen, laufend;
    static bool offenGanz = false, laufendGanz = false, ausstehend = false;
    static std::vector<double> kameraAlt;

    if (!autoRendernAn)
    {
        std::vector<std::pair<Vector3D, Vector3D>> weg;
        bool g = false;
        autoRenderAbholen(weg, g);
        gAutoAuswahlGeaendert = false;
        letzterStand = gAutoRenderStand.load();
        offen.clear();
        laufend.clear();
        offenGanz = laufendGanz = ausstehend = false;
        kameraAlt.clear();
        return;
    }

    // Kamera oder Bildgroesse geaendert: ganzes Bild
    if (!cam.empty())
    {
        const Camera &c = cam[0];
        std::vector<double> k = {c.position.x, c.position.y, c.position.z, c.lookAt.x, c.lookAt.y, c.lookAt.z,
                                 c.fov, double(renderBreite), double(renderHoehe)};
        if (!kameraAlt.empty() && k != kameraAlt)
            autoRenderGanz();
        kameraAlt = k;
    }

    const ULONGLONG jetzt = GetTickCount64();
    const unsigned stand = gAutoRenderStand.load();
    if (stand != letzterStand)
    {
        letzterStand = stand;
        letzteAktivitaet = jetzt;
        ausstehend = true;
        if (gAutoRendernAbbrechen && gAutoRendernAbbrechen()) // war noch nicht fertig: Bereich bleibt offen
        {
            offen.insert(offen.end(), laufend.begin(), laufend.end());
            offenGanz = offenGanz || laufendGanz;
        }
        laufend.clear();
        laufendGanz = false;
    }
    autoRenderAbholen(offen, offenGanz);
    if (gAutoAuswahlGeaendert.exchange(false)) // ohne Verlaufsschritt geaendert: Bereich der Auswahl
        for (object *o : ausgewaehlteObjekte())
        {
            Vector3D mn = o->position, mx = o->position;
            o->getWorldAABB(mn, mx);
            offen.push_back({mn, mx});
        }

    const bool maus = ((GetAsyncKeyState(VK_LBUTTON) | GetAsyncKeyState(VK_RBUTTON) | GetAsyncKeyState(VK_MBUTTON)) & 0x8000) != 0;
    if (maus && ausstehend)
        letzteAktivitaet = jetzt; // Ziehen dauert noch
    if (!ausstehend || double(jetzt - letzteAktivitaet) < autoWarten * 1000.0)
        return;

    double x0 = 1e30, y0 = 1e30, x1 = -1e30, y1 = -1e30;
    bool ganz = offenGanz;
    for (size_t i = 0; i < offen.size() && !ganz; ++i)
    {
        double a0, b0, a1, b1;
        if (!bildBereichVon(offen[i].first, offen[i].second, a0, b0, a1, b1))
            ganz = true;
        else
        {
            x0 = std::min(x0, a0);
            y0 = std::min(y0, b0);
            x1 = std::max(x1, a1);
            y1 = std::max(y1, b1);
        }
    }
    if (!ganz && x1 > x0 && y1 > y0)
    {
        // Rand: Schatten, Spiegelbilder und Kanten gleich mit
        const double rx = 0.15 * (x1 - x0) + 0.01, ry = 0.15 * (y1 - y0) + 0.01;
        x0 -= rx;
        x1 += rx;
        y0 -= ry;
        y1 += ry * 2.0; // Schatten fallen meist nach unten
    }
    else if (!ganz)
        x0 = y0 = x1 = y1 = 0.0; // nichts im Bild geaendert (main.cpp: nur ein ganzes Bild, falls noch keines da ist)
    if (gAutoRendernStarten && gAutoRendernStarten(x0, y0, x1, y1, ganz))
    {
        laufend.swap(offen);
        offen.clear();
        laufendGanz = ganz;
        offenGanz = false;
        ausstehend = false;
    }
}

static void autoRendernUmschalten()
{
    autoRendernAn = !autoRendernAn;
    if (autoRendernAn)
        autoRenderAktivitaet(); // nach der Wartezeit: ganzes Bild, falls noch keines fertig ist
    if (l.btnAutoRender)
        InvalidateRect(l.btnAutoRender, nullptr, FALSE);
    statusAktualisieren();
}

static void abgleichen()
{
    kantenAbgleichen();
    editorEinstellungenSpeichernFallsGeaendert();
    fortschrittAnzeigen();
    drehpunktAbgleichen();
    // Rauchboxen/Wolkenbereich als Objekte (VolumenBox): passend zu den Render-Einstellungen
    if (!volumenBoxenStimmen() && gSzeneAendern)
    {
        bool betroffen = dynamic_cast<VolumenBox *>(selectedObject) != nullptr;
        for (object *o : ausgewaehlteObjekte())
            betroffen = betroffen || dynamic_cast<VolumenBox *>(o) != nullptr;
        if (betroffen)
            auswahlSetzen({}); // die Platzhalter werden ersetzt
        gSzeneAendern([]
                      { volumenBoxenAbgleichen(); });
    }
    else
        volumenBoxenNachfuehren(); // Boxen koennen sich im Fenster Render-Einstellungen geaendert haben

    if (scene.size() != bekannteObjektAnzahl || lights.size() != bekannteLichtAnzahl)
        UpdateObjectList();

    if (aktuelleAuswahl() != angezeigteAuswahl)
        listenAuswahlAbgleichen();

    // Werte koennen sich auch durch Ziehen in den Ansichten aendern
    eigenschaftenAnzeigen();
    if (reiter == 1)
        materialAnzeigen();

    // Mehrfachauswahl geaendert: Liste neu zeichnen (blaue Eintraege)
    std::vector<object *> mehrfach = ausgewaehlteObjekte();
    if (mehrfach != angezeigteMehrfachauswahl)
    {
        angezeigteMehrfachauswahl = mehrfach;
        InvalidateRect(hTreeObjects, nullptr, FALSE);
    }

    statusAktualisieren();
    einstellungenAnzeigen();
    zeitleisteAbgleichen();
    einpassKnoepfeSetzen(); // Ansichten koennen ihre Groesse aendern
    autoRendernPruefen();   // Autom. Rendern: Wartezeit, Bereich starten

    // "Stop" nur, solange gerendert wird: Render-Start laeuft noch (gRenderLaeufe) oder der
    // Render-Code meldet laufende Threads (rendernRunning). Vorher nur rendernRunning: das ist
    // nicht waehrend des ganzen Renderns gesetzt.
    static bool stopSichtbar = true;
    const bool rendert = gRenderLaeufe > 0 || rendernRunning;
    if (rendert != stopSichtbar)
    {
        stopSichtbar = rendert;
        ShowWindow(l.btnStop, stopSichtbar ? SW_SHOWNA : SW_HIDE);
    }

    // Rendering-Fenster kann auch durch "Render" (Y) wieder erscheinen
    bool sichtbar = hRenderfenster && IsWindowVisible(hRenderfenster);
    if (sichtbar != renderfensterSichtbar)
    {
        renderfensterSichtbar = sichtbar;
        InvalidateRect(l.btnRenderfenster, nullptr, FALSE);
    }

    blockAuswahlAnzeigen(); // Blockgroesse geaendert (Laden einer Szene)

    if (useFlat != angezeigtFlat)
    {
        angezeigtFlat = useFlat;
        SetWindowTextW(l.btnFlat, useFlat ? L"Flat" : L"Draht");
        InvalidateRect(l.btnFlat, nullptr, FALSE);
    }

    if (selectedAction != angezeigterModus)
    {
        angezeigterModus = selectedAction;
        schrittAnzeigen();
        InvalidateRect(g.btnMove, nullptr, FALSE);
        InvalidateRect(g.btnRotate, nullptr, FALSE);
        InvalidateRect(g.btnScale, nullptr, FALSE);
        InvalidateRect(g.btnKanten, nullptr, FALSE);
    }
}

static void auswahlAusListe(LPARAM wert)
{
    if (wert == EINTRAG_GRUPPE)
        return;

    if (wert == EINTRAG_KAMERA)
    {
        selectedObject = nullptr;
        selectedLight = nullptr;
        selectedType = SelectedType::CAMERA;
    }
    else if (wert >= EINTRAG_KIND)
    {
        size_t i = size_t(wert - EINTRAG_KIND);
        if (i >= eintraegeKinder.size())
            return;
        auswahlSetzen({eintraegeKinder[i].first}); // einzeln bearbeiten (Material, Farbe, Name ...)
    }
    else if (wert >= EINTRAG_LICHT)
    {
        size_t i = size_t(wert - EINTRAG_LICHT);
        if (i >= lights.size())
            return;

        selectionIndex = int(scene.size() + i);
        selectedObject = nullptr;
        selectedLight = lights[i].get();
        selectedType = SelectedType::LIGHT;
    }
    else if (wert >= 0 && size_t(wert) < scene.size())
    {
        auswahlSetzen({scene[size_t(wert)].get()}); // Klick ohne Strg: nur dieses Objekt
    }

    angezeigteAuswahl = aktuelleAuswahl();
    eigenschaftenAnzeigen();
}

// Objektfenster aus-/einblenden; ausgeblendet bleibt nur der Button in der Leiste
static void einklappen(bool zu)
{
    eingeklappt = zu;
    ShowWindow(hObjektFenster, eingeklappt ? SW_HIDE : SW_SHOWNOACTIVATE);
    hauptfensterAnordnen(); // Ansichten nutzen die freie Breite
    InvalidateRect(l.btnObjekte, nullptr, FALSE);
}

//--------------------------------------------------
// Layout
//--------------------------------------------------

static void platz(HWND hwnd, int x, int y, int w, int h)
{
    MoveWindow(hwnd, x, y, w, h, TRUE);
}


// ---------- Blockgroesse Schnell-Rendering (Auswahlliste in der Leiste) ----------
static const int BLOCK_GROESSEN[] = {1, 2, 3, 4, 6, 8, 12, 16, 32};
static int angezeigterBlock = -1;

// Auswahl passend zu schnellBlock (auch nach Laden einer Szene); Zwischenwerte: naechstgroesserer Eintrag
static void blockAuswahlAnzeigen()
{
    if (!l.cmbBlock || angezeigterBlock == schnellBlock || SendMessageW(l.cmbBlock, CB_GETDROPPEDSTATE, 0, 0))
        return;
    angezeigterBlock = schnellBlock;
    int index = 0;
    for (int i = 0; i < int(sizeof(BLOCK_GROESSEN) / sizeof(BLOCK_GROESSEN[0])); ++i)
        if (BLOCK_GROESSEN[i] <= std::max(1, schnellBlock))
            index = i;
    SendMessageW(l.cmbBlock, CB_SETCURSEL, index, 0);
}

static void blockAuswahlUebernehmen()
{
    int i = int(SendMessageW(l.cmbBlock, CB_GETCURSEL, 0, 0));
    if (i < 0 || i >= int(sizeof(BLOCK_GROESSEN) / sizeof(BLOCK_GROESSEN[0])))
        return;
    schnellBlock = BLOCK_GROESSEN[i];
    angezeigterBlock = schnellBlock;
    szeneGeaendertMarkieren(); // wird mit der Szene gespeichert
}

// ---------- Menueleiste des Hauptfensters (ersetzt den Knopf "Datei ▾") ----------
// Datei | Bearbeiten | Ansicht | Rendern. Die Eintraege werden beim Aufklappen neu gefuellt (Haekchen, Namen der
// Rueckgaengig-Schritte, Liste "Zuletzt geoeffnet").
enum MenueLeisteId
{
    MB_NEU = 30001,
    MB_LADEN,
    MB_SPEICHERN,
    MB_SPEICHERN_UNTER,
    MB_OBJ_IMPORT,
    MB_OBJ_EXPORT,
    MB_BEENDEN,
    MB_ZULETZT_LEEREN,
    MB_UNDO,
    MB_REDO,
    MB_KOPIEREN,
    MB_EINFUEGEN,
    MB_DUPLIZIEREN,
    MB_INSTANZ,
    MB_LOESCHEN,
    MB_ALLES,
    MB_GRUPPIEREN,
    MB_AUFHEBEN,
    MB_MAT_AUFNEHMEN,
    MB_MAT_UEBERTRAGEN,
    MB_MAT_PINSEL_AUS,
    MB_FLAT,
    MB_ZENTRIEREN,
    MB_FOKUS,
    MB_OBJEKTFENSTER,
    MB_REITER_ANSICHTEN,
    MB_REITER_RENDERING,
    MB_RENDERN,
    MB_STOP,
    MB_AUTO,
    MB_EINSTELLUNGEN,
    MB_ZULETZT = 30200 // + Nummer in der Liste
};
static HMENU menueLeiste = nullptr, menueDatei = nullptr, menueBearbeiten = nullptr, menueAnsicht = nullptr,
             menueRendern = nullptr, menueZuletzt = nullptr, menueAnimation = nullptr;

static void menueLeeren(HMENU m)
{
    while (GetMenuItemCount(m) > 0)
        RemoveMenu(m, 0, MF_BYPOSITION); // Untermenue "Zuletzt geoeffnet" bleibt erhalten (nur abgehaengt)
}

// Dateiname vorne, Ordner dahinter: "szene.xml   (C:\...\szenen)"
static std::wstring zuletztText(size_t i, const std::string &pfad)
{
    std::wstring w = ausUtf8(pfad);
    size_t schnitt = w.find_last_of(L"/\\");
    std::wstring name = schnitt == std::wstring::npos ? w : w.substr(schnitt + 1);
    std::wstring ordner = schnitt == std::wstring::npos ? std::wstring() : w.substr(0, schnitt);
    if (ordner.size() > 60)
        ordner = L"\u2026" + ordner.substr(ordner.size() - 59);
    std::wstring t = (i < 9 ? L"&" + std::to_wstring(i + 1) + L"  " : L"    ") + name;
    if (!ordner.empty())
        t += L"   (" + ordner + L")";
    // "&" im Pfad nicht als Kuerzel lesen
    std::wstring sicher;
    for (size_t k = 0; k < t.size(); ++k)
    {
        sicher += t[k];
        if (t[k] == L'&' && !(k == 0 && i < 9))
            sicher += L'&';
    }
    return sicher;
}

static void menueFuellen(HMENU m)
{
    const std::vector<object *> alle = ausgewaehlteObjekte();
    const UINT mitAuswahl = alle.empty() ? MF_GRAYED : 0;
    if (!m || (m != menueDatei && m != menueBearbeiten && m != menueAnsicht && m != menueRendern && m != menueAnimation))
        return; // Untermenues (z.B. "Zuletzt geoeffnet") nicht leeren: sie werden mit ihrem Hauptmenue gefuellt
    menueLeeren(m);
    if (m == menueAnimation)
        ablaufMenueLeisteFuellen(m);
    else if (m == menueDatei)
    {
        AppendMenuW(m, MF_STRING, MB_NEU, L"&Neue Szene (leeren)\tC");
        AppendMenuW(m, MF_STRING, MB_LADEN, L"Szene ö&ffnen (XML) …\tL");
        menueLeeren(menueZuletzt);
        for (size_t i = 0; i < zuletztGeoeffnet.size(); ++i)
            AppendMenuW(menueZuletzt, MF_STRING, MB_ZULETZT + UINT(i), zuletztText(i, zuletztGeoeffnet[i]).c_str());
        if (zuletztGeoeffnet.empty())
            AppendMenuW(menueZuletzt, MF_STRING | MF_GRAYED, 0, L"(noch keine)");
        else
        {
            AppendMenuW(menueZuletzt, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(menueZuletzt, MF_STRING, MB_ZULETZT_LEEREN, L"Liste leeren");
        }
        AppendMenuW(m, MF_POPUP, reinterpret_cast<UINT_PTR>(menueZuletzt), L"&Zuletzt geöffnet");
        AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(m, MF_STRING, MB_SPEICHERN, L"&Speichern\tStrg+S");
        AppendMenuW(m, MF_STRING, MB_SPEICHERN_UNTER, L"Speichern &unter (XML) …\tStrg+Umschalt+S");
        AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(m, MF_STRING, MB_OBJ_IMPORT, L"Objekt &importieren (OBJ) …\tI");
        AppendMenuW(m, MF_STRING, MB_OBJ_EXPORT,
                    alle.empty() ? L"Objekt &exportieren (OBJ): ganze Szene …\tE" : L"Objekt &exportieren (OBJ): Auswahl …\tE");
        AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(m, MF_STRING, MB_BEENDEN, L"&Beenden\tQ");
    }
    else if (m == menueBearbeiten)
    {
        const std::string u = naechstesRueckgaengig(), r = naechstesWiederholen();
        const std::wstring ut = u.empty() ? std::wstring(L"&Rückgängig\tStrg+Z") : L"&Rückgängig: " + ausUtf8(u) + L"\tStrg+Z";
        const std::wstring rt = r.empty() ? std::wstring(L"&Wiederholen\tStrg+Y") : L"&Wiederholen: " + ausUtf8(r) + L"\tStrg+Y";
        AppendMenuW(m, MF_STRING | (u.empty() ? MF_GRAYED : 0), MB_UNDO, ut.c_str());
        AppendMenuW(m, MF_STRING | (r.empty() ? MF_GRAYED : 0), MB_REDO, rt.c_str());
        AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(m, MF_STRING | mitAuswahl, MB_KOPIEREN, L"&Kopieren\tStrg+C");
        AppendMenuW(m, MF_STRING, MB_EINFUEGEN, L"&Einfügen\tStrg+V");
        AppendMenuW(m, MF_STRING | mitAuswahl, MB_DUPLIZIEREN, L"&Duplizieren\tStrg+D");
        AppendMenuW(m, MF_STRING | mitAuswahl, MB_INSTANZ, L"&Instanz erstellen (verknüpfte Kopie)\tStrg+Umschalt+D");
        AppendMenuW(m, MF_STRING | mitAuswahl, MB_LOESCHEN, L"&Löschen\tEntf");
        AppendMenuW(m, MF_STRING, MB_ALLES, L"&Alles auswählen\tStrg+A");
        AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(m, MF_STRING | (alle.size() >= 2 ? 0 : MF_GRAYED), MB_GRUPPIEREN, L"&Gruppieren\tStrg+G");
        AppendMenuW(m, MF_STRING | mitAuswahl, MB_AUFHEBEN, L"Gruppe &aufheben\tStrg+Umschalt+G");
        AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(m, MF_STRING | mitAuswahl, MB_MAT_AUFNEHMEN, L"&Material aufnehmen (Pinsel)");
        const std::wstring mt = materialAblage.da ? L"Material von \u201E" + materialAblage.quelle + L"\u201C auf Auswahl &übertragen"
                                                  : std::wstring(L"Material auf Auswahl &übertragen");
        AppendMenuW(m, MF_STRING | (materialAblage.da && !alle.empty() ? 0 : MF_GRAYED), MB_MAT_UEBERTRAGEN, mt.c_str());
        AppendMenuW(m, MF_STRING | (materialPinsel::gAktiv ? 0 : MF_GRAYED), MB_MAT_PINSEL_AUS, L"Material-Pinsel beenden\tEsc");
    }
    else if (m == menueAnsicht)
    {
        AppendMenuW(m, MF_STRING | (aktiverReiter == 0 ? MF_CHECKED : 0), MB_REITER_ANSICHTEN, L"&Ansichten (2 × 2)");
        AppendMenuW(m, MF_STRING | (aktiverReiter == 1 ? MF_CHECKED : 0), MB_REITER_RENDERING, L"&Rendering-Bild\tW");
        AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(m, MF_STRING | (useFlat ? MF_CHECKED : 0), MB_FLAT, L"&Flächen statt Drahtgitter\tF");
        AppendMenuW(m, MF_STRING, MB_ZENTRIEREN, L"Ganze Szene &zeigen (Zentrieren)\tZ");
        AppendMenuW(m, MF_STRING | mitAuswahl, MB_FOKUS, L"Auf Auswahl &fokussieren\tKomma (Ziffernblock)");
        AppendMenuW(m, MF_STRING | (eingeklappt ? 0 : MF_CHECKED), MB_OBJEKTFENSTER, L"&Objektfenster");
        kamerasichtMenueEintraege(m); // Render-Bild in der Kamerasicht, Kamerasicht auf anderen Bildschirm
    }
    else if (m == menueRendern)
    {
        AppendMenuW(m, MF_STRING, MB_RENDERN, L"&Rendern\tY");
        AppendMenuW(m, MF_STRING, MB_STOP, L"&Stop\tX");
        AppendMenuW(m, MF_STRING | (autoRendernAn ? MF_CHECKED : 0), MB_AUTO, L"&Autom. Rendern (geänderter Bereich)");
        AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(m, MF_STRING, MB_EINSTELLUNGEN, L"Render-&Einstellungen …");
    }
}

static void menueLeisteAnlegen()
{
    if (!hHauptfenster || menueLeiste)
        return;
    menueLeiste = CreateMenu();
    menueDatei = CreatePopupMenu();
    menueBearbeiten = CreatePopupMenu();
    menueAnsicht = CreatePopupMenu();
    menueRendern = CreatePopupMenu();
    menueZuletzt = CreatePopupMenu();
    menueAnimation = CreatePopupMenu();
    AppendMenuW(menueLeiste, MF_POPUP, reinterpret_cast<UINT_PTR>(menueDatei), L"&Datei");
    AppendMenuW(menueLeiste, MF_POPUP, reinterpret_cast<UINT_PTR>(menueBearbeiten), L"&Bearbeiten");
    AppendMenuW(menueLeiste, MF_POPUP, reinterpret_cast<UINT_PTR>(menueAnsicht), L"&Ansicht");
    AppendMenuW(menueLeiste, MF_POPUP, reinterpret_cast<UINT_PTR>(menueAnimation), L"A&nimation");
    AppendMenuW(menueLeiste, MF_POPUP, reinterpret_cast<UINT_PTR>(menueRendern), L"&Rendern");
    for (HMENU m : {menueDatei, menueBearbeiten, menueAnsicht, menueAnimation, menueRendern})
        menueFuellen(m); // auch fuer Tastenkuerzel-Anzeige vor dem ersten Aufklappen
    SetMenu(hHauptfenster, menueLeiste);
    if (l.btnDatei)
        ShowWindow(l.btnDatei, SW_HIDE); // der Knopf "Datei ▾" entfaellt
}

static void menueLeisteAktualisieren()
{
    if (menueLeiste && hHauptfenster)
        DrawMenuBar(hHauptfenster);
}

// true: Befehl aus der Menueleiste ausgefuehrt
static bool menueLeisteBefehl(int id)
{
    if (ablaufMenueLeisteBefehl(hHauptfenster, id))
    {
        abgleichen();
        return true;
    }
    if (id < MB_NEU || id >= MB_ZULETZT + int(ZULETZT_ANZAHL))
        return kamerasichtMenueWahl(id);
    auto taste = [](WPARAM k)
    {
        if (gHandleKeys)
            gHandleKeys(k);
    };
    if (id >= MB_ZULETZT)
    {
        const size_t i = size_t(id - MB_ZULETZT);
        if (i < zuletztGeoeffnet.size() && gSzeneOeffnen)
        {
            const std::string pfad = zuletztGeoeffnet[i]; // Kopie: die Liste aendert sich beim Laden
            gSzeneOeffnen(pfad);
        }
        abgleichen();
        return true;
    }
    switch (id)
    {
    case MB_NEU: taste('C'); break;
    case MB_LADEN: taste('L'); break;
    case MB_SPEICHERN: taste(WPARAM(0x10001)); break; // wie Strg+S (main.cpp)
    case MB_SPEICHERN_UNTER: taste('O'); break;
    case MB_OBJ_IMPORT: taste('I'); break;
    case MB_OBJ_EXPORT: taste('E'); break;
    case MB_BEENDEN: taste('Q'); return true; // Fenster wird geschlossen: kein Abgleich mehr
    case MB_ZULETZT_LEEREN:
        zuletztGeoeffnet.clear();
        editorEinstellungenSpeichernFallsGeaendert();
        break;
    case MB_UNDO: Rueckgaengig(); break;
    case MB_REDO: Wiederholen(); break;
    case MB_KOPIEREN: AuswahlKopieren(); break;
    case MB_EINFUEGEN: ZwischenablageEinfuegen(); break;
    case MB_DUPLIZIEREN: AuswahlDuplizieren(); break;
    case MB_INSTANZ: AuswahlInstanzErstellen(); break;
    case MB_LOESCHEN: AusgewaehltesObjektLoeschen(); break;
    case MB_ALLES: AllesAuswaehlen(); break;
    case MB_GRUPPIEREN: AuswahlGruppieren(); break;
    case MB_AUFHEBEN: GruppeAufheben(); break;
    case MB_MAT_AUFNEHMEN:
    {
        std::vector<object *> alle = ausgewaehlteObjekte();
        if (!alle.empty() && materialAufnehmen(selectedObject && istAusgewaehlt(selectedObject) ? selectedObject : alle.front()))
            materialPinselSetzen(true);
        break;
    }
    case MB_MAT_UEBERTRAGEN: materialUebertragen(ausgewaehlteObjekte()); break;
    case MB_MAT_PINSEL_AUS: materialPinselSetzen(false); break;
    case MB_FLAT: taste('F'); break;
    case MB_ZENTRIEREN: taste('Z'); break;
    case MB_FOKUS: taste(VK_DECIMAL); break;
    case MB_OBJEKTFENSTER: einklappen(!eingeklappt); break;
    case MB_REITER_ANSICHTEN: RenderReiterZeigen(false); break;
    case MB_REITER_RENDERING: RenderReiterZeigen(true); break;
    case MB_RENDERN: taste('Y'); break;
    case MB_STOP: taste('X'); break;
    case MB_AUTO: autoRendernUmschalten(); break;
    case MB_EINSTELLUNGEN: EinstellungenZeigen(); break;
    default: return false;
    }
    abgleichen();
    return true;
}

// ---------- Menue "Datei" ----------
static void dateiMenue()
{
    enum
    {
        M_SZENE_LADEN = 1,
        M_SZENE_SPEICHERN,
        M_SZENE_STRG_S,
        M_OBJ_IMPORT,
        M_OBJ_EXPORT,
        M_LEEREN,
        M_BEENDEN
    };
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, M_SZENE_LADEN, L"Import Szene (XML) …\tL");
    AppendMenuW(m, MF_STRING, M_SZENE_SPEICHERN, L"Export Szene (XML) …\tO");
    AppendMenuW(m, MF_STRING, M_SZENE_STRG_S, L"Szene speichern\tStrg+S");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, M_OBJ_IMPORT, L"Import Objekt (OBJ) …\tI");
    std::wstring export_ = ausgewaehlteObjekte().empty() ? L"Export Objekt (OBJ): ganze Szene …\tE"
                                                         : L"Export Objekt (OBJ): Auswahl …\tE";
    AppendMenuW(m, MF_STRING, M_OBJ_EXPORT, export_.c_str());
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, M_LEEREN, L"Szene leeren\tC");
    kamerasichtMenueEintraege(m); // Render-Bild in der Kamerasicht, Kamerasicht auf anderen Bildschirm
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, M_BEENDEN, L"Beenden\tQ");
    RECT r;
    GetWindowRect(l.btnDatei, &r);
    int wahl = int(TrackPopupMenu(m, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN | TPM_NONOTIFY, r.left, r.bottom, 0,
                                  hToolWindow, nullptr));
    DestroyMenu(m);
    if (kamerasichtMenueWahl(wahl))
        return;
    if (!gHandleKeys)
        return;
    switch (wahl)
    {
    case M_SZENE_LADEN: gHandleKeys('L'); break;
    case M_SZENE_SPEICHERN: gHandleKeys('O'); break;
    case M_SZENE_STRG_S: gHandleKeys(WPARAM(0x10001)); break; // wie Strg+S (main.cpp)
    case M_OBJ_IMPORT: gHandleKeys('I'); break;
    case M_OBJ_EXPORT: gHandleKeys('E'); break;
    case M_LEEREN: gHandleKeys('C'); break;   // fragt bei ungespeicherten Aenderungen nach
    case M_BEENDEN: gHandleKeys('Q'); return; // Fenster wird geschlossen: kein Abgleich mehr
    default: return;
    }
    abgleichen();
}

// Leiste, eine Zeile:
//  [Datei v] | [Undo][Redo] | [Zahnrad][Block v][Render][Stop] | [Flat][Zentrieren][Fokus]
// gibt die Breite zurueck, die die Knoepfe brauchen (ohne den Objekte-Button rechts)
static int leisteLayout(int w, int /*h*/)
{
    trennlinien.clear();
    int x = MARGIN;
    const int y = (LEISTE_H - BTN_H) / 2;

    auto knopf = [&](HWND b, int breite)
    {
        platz(b, x, y, breite, BTN_H);
        x += breite + GAP;
    };

    auto trennen = [&]()
    {
        x -= GAP;
        trennlinien.push_back(x + GRUPPE / 2);
        x += GRUPPE;
    };

    if (!menueLeiste) // mit Menueleiste (Hauptfenster) entfaellt der Knopf "Datei"
    {
        knopf(l.btnDatei, 104);
        trennen();
    }
    knopf(l.btnUndo, 40);
    knopf(l.btnRedo, 40);
    trennen();
    knopf(l.btnEinstellungen, 40); // Render-Einstellungen (Zahnrad), vor dem Render-Knopf
    if (l.cmbBlock)
    {
        // Auswahlliste: Hoehe inkl. aufgeklappter Liste; Feld selbst so hoch wie die Schrift
        platz(l.cmbBlock, x, y + 2, 112, 300);
        x += 112 + GAP;
    }
    knopf(l.btnRender, 100);
    knopf(l.btnStop, 86);
    knopf(l.btnAutoRender, 160);
    // "Rendering Fenster" entfaellt: Wechsel ueber die Reiter "Ansichten | Rendering" (Taste W)
    trennen();
    knopf(l.btnFlat, 90);
    knopf(l.btnZentrieren, 120);
    knopf(l.btnFokus, 96);
    // "Clear Scene" und "Exit" stehen jetzt im Menue "Datei"
    int ende = x - GAP;

    // quadratischer Objekte-Button ganz rechts, volle Hoehe der Leiste; davor Zoom - / +
    platz(l.btnObjekte, w - LEISTE_H, 0, LEISTE_H, LEISTE_H);
    const int zoomBreite = 40;
    platz(l.btnZoomPlus, w - LEISTE_H - GAP - zoomBreite, y, zoomBreite, BTN_H);
    platz(l.btnZoomMinus, w - LEISTE_H - 2 * (GAP + zoomBreite), y, zoomBreite, BTN_H);

    return ende + GRUPPE + 2 * (zoomBreite + GAP) + MARGIN;
}

// Objektfenster, senkrecht:
//  [OBJEKTE              ^]   <- Kopf, blendet das Fenster aus
//  Baumliste (waechst mit)
//  [Neu][Delete][Next]
//  [Move][Rotate][Scale]
//  EIGENSCHAFTEN, Name, Typ, Mitte/Position, Farbe/LookAt + Farbfeld
// kleinste sinnvolle Hoehe des Objektfensters (Liste mindestens 150 Pixel); darunter wird gerollt
// Hoehe des Reiters "Manipulation": Symbole (in Reihen), Name, Objekt, zwei Wertzeilen, Knopf, Hinweis
static int manipReiterHoehe(int breite)
{
    const int jeReihe = std::max(1, (breite + 4) / 44);
    const int reihen = (ANZAHL_MANIP_ARTEN + jeReihe - 1) / jeReihe;
    return reihen * 44 + 2 + (LABEL_H + GAP) + 3 * (ZEILE_H + GAP) + (LABEL_H + 2) + (BTN_H + GAP) + 11 * LABEL_H;
}

static int objektfensterMindesthoehe()
{
    const int untenHoehe = 3 * (BTN_H + GAP) + (ZEILE_H + ABSCHNITT) + (BTN_H + GAP) +
                           std::max({(ZEILE_H + GAP) + (LABEL_H + GAP) + 6 * (ZEILE_H + GAP),
                                     (1 + MAT_FARBEN + MAT_WERTE / 2 + 7 + (texturKugelZeile ? 1 : 0)) * (ZEILE_H + GAP),
                                     manipReiterHoehe(PANEL_W - 2 * MARGIN)}) +
                           MARGIN - GAP;
    return KOPF_H + GAP + 150 + GAP + untenHoehe;
}

static void objektfensterLayout(int w, int h)
{
    platz(g.btnKopf, 0, 0, w, KOPF_H);

    const int x = MARGIN;
    const int breite = std::max(60, w - 2 * MARGIN);
    int listeOben = KOPF_H + GAP;

    const int untenHoehe =
        (BTN_H + GAP) +       // Neu / Delete / Next
        (BTN_H + GAP) +       // Move / Rotate / Scale
        (BTN_H + GAP) +       // -X +X -Y +Y -Z +Z
        (ZEILE_H + ABSCHNITT) + // Schritt
        (BTN_H + GAP) +       // Reiter
        std::max({(ZEILE_H + GAP) + (LABEL_H + GAP) + 6 * (ZEILE_H + GAP), // Eigenschaften (inkl. Unterster/Oberster)
                  (1 + MAT_FARBEN + MAT_WERTE / 2 + 7 + (texturKugelZeile ? 1 : 0)) * (ZEILE_H + GAP), // Material (inkl. Textur-Ausrichtung)
                  manipReiterHoehe(std::max(60, w - 2 * MARGIN))}) +      // Manipulation (Symbole, Hinweis)
        MARGIN - GAP;

    int untenOben = std::max(listeOben + 80 + GAP, h - untenHoehe);
    platz(hTreeObjects, x, listeOben, breite, untenOben - GAP - listeOben);

    int y = untenOben;
    int viertel = (breite - 3 * GAP) / 4;
    platz(g.btnNeu, x, y, viertel, BTN_H);
    platz(g.btnKopie, x + viertel + GAP, y, viertel, BTN_H);
    platz(g.btnLoeschen, x + 2 * (viertel + GAP), y, viertel, BTN_H);
    platz(g.btnNext, x + 3 * (viertel + GAP), y, breite - 3 * (viertel + GAP), BTN_H);
    y += BTN_H + GAP;

    {
        const int v4 = (breite - 3 * GAP) / 4;
        platz(g.btnMove, x, y, v4, BTN_H);
        platz(g.btnRotate, x + v4 + GAP, y, v4, BTN_H);
        platz(g.btnScale, x + 2 * (v4 + GAP), y, v4, BTN_H);
        platz(g.btnKanten, x + 3 * (v4 + GAP), y, breite - 3 * (v4 + GAP), BTN_H);
    }
    y += BTN_H + GAP;

    int sechstel = (breite - 5 * GAP) / 6;
    for (int i = 0; i < 6; ++i)
    {
        int links = x + i * (sechstel + GAP);
        int rechts = i == 5 ? x + breite : links + sechstel;
        platz(g.btnSchritt[i], links, y, rechts - links, BTN_H);
    }
    y += BTN_H + GAP;

    platz(g.lblSchritt, x, y + 4, ZEILEN_LABEL_W, LABEL_H);
    platz(g.editSchritt, x + ZEILEN_LABEL_W + GAP, y, 90, ZEILE_H);
    platz(g.lblEinheit, x + ZEILEN_LABEL_W + GAP + 90 + GAP, y + 4, breite - ZEILEN_LABEL_W - 90 - 2 * GAP, LABEL_H);
    y += ZEILE_H + ABSCHNITT;

    // Reiter
    const int drittel3 = (breite - 2 * GAP) / 3;
    platz(g.btnReiter[0], x, y, drittel3, BTN_H);
    platz(g.btnReiter[1], x + drittel3 + GAP, y, drittel3, BTN_H);
    platz(g.btnReiter[2], x + 2 * (drittel3 + GAP), y, breite - 2 * (drittel3 + GAP), BTN_H);
    y += BTN_H + GAP;
    const int inhaltOben = y;

    // ---------- Reiter "Manipulation" (liegt an derselben Stelle) ----------
    {
        int ym = inhaltOben;
        const int labelB = MAT_LABEL_W + 30;
        // Symbole der Aktionen in Reihen, darunter der Name der gewaehlten
        {
            const int gross = 40, abstand = 4;
            const int jeReihe = std::max(1, (breite + abstand) / (gross + abstand));
            const int reihen = (ANZAHL_MANIP_ARTEN + jeReihe - 1) / jeReihe;
            for (int i = 0; i < ANZAHL_MANIP_ARTEN; ++i)
                platz(g.btnManipIcon[i], x + (i % jeReihe) * (gross + abstand), ym + (i / jeReihe) * (gross + abstand), gross, gross);
            ym += reihen * (gross + abstand) + 2;
        }
        platz(g.lblManipArt, x, ym, breite, LABEL_H);
        platz(g.cmbManipArt, x, ym, 10, 10);
        ym += LABEL_H + GAP;
        platz(g.lblManipObjekt, x, ym + 4, labelB, LABEL_H);
        platz(g.cmbManipObjekt, x + labelB + GAP, ym, breite - labelB - GAP, ZEILE_H + 300);
        platz(g.lblManipZiel, x + labelB + GAP, ym + 4, breite - labelB - GAP, LABEL_H);
        ym += ZEILE_H + GAP;
        // Felder je nach Aktion: eine Wertzeile (Beschriftung links, so breit wie noetig) und/oder drei Felder
        // nebeneinander mit der Beschriftung darueber (die langen Texte wie "Max. Winkel X / Y / Z (°)" passen so ganz)
        const int art = g.cmbManipArt ? int(SendMessageW(g.cmbManipArt, CB_GETCURSEL, 0, 0)) : 0;
        const bool wertZeile = art == 2 || art == MANIP_GLATT || art == MANIP_DICKE || art == MANIP_FAECHERN ||
                               art == MANIP_VIELFACH || art == MANIP_SUBTRAHIEREN || art == MANIP_FELL;
        const bool dreiFelder = art == MANIP_VERTEILEN || art == MANIP_DREHEN || art == MANIP_GROESSEN || art == MANIP_VIELFACH ||
                                art == MANIP_ABSETZEN || art == MANIP_TERRAIN || art == MANIP_PINSEL || art == MANIP_FELL;
        int wertLabelB = labelB;
        {
            HDC dc = GetDC(hObjektFenster);
            HGDIOBJ alt = SelectObject(dc, gFont);
            for (HWND l : {g.lblManipTiefe, g.lblManipKnick, g.lblManipWert, g.lblManipModus, g.lblManipKopien, g.lblManipLage,
                           g.lblManipPinsel})
            {
                wchar_t t[96] = {};
                GetWindowTextW(l, t, 96);
                RECT r = {0, 0, 0, 0};
                DrawTextW(dc, t, -1, &r, DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
                wertLabelB = std::max(wertLabelB, int(r.right - r.left) + 10);
            }
            SelectObject(dc, alt);
            ReleaseDC(hObjektFenster, dc);
            wertLabelB = std::min(wertLabelB, breite / 2);
        }
        platz(g.lblManipTiefe, x, ym + 4, wertLabelB, LABEL_H);
        platz(g.editManipTiefe, x + wertLabelB + GAP, ym, 70, ZEILE_H);
        platz(g.lblManipKnick, x, ym + 4, wertLabelB, LABEL_H);
        platz(g.editManipKnick, x + wertLabelB + GAP, ym, 70, ZEILE_H);
        platz(g.lblManipWert, x, ym + 4, wertLabelB, LABEL_H);
        platz(g.editManipWert, x + wertLabelB + GAP, ym, 90, ZEILE_H);
        platz(g.lblManipModus, x, ym + 4, wertLabelB, LABEL_H);
        platz(g.cmbManipModus, x + wertLabelB + GAP, ym, breite - wertLabelB - GAP, ZEILE_H + 200);
        if (wertZeile)
            ym += ZEILE_H + GAP;
        platz(g.lblManipAus, x, ym, breite, LABEL_H);
        {
            const int feldB = std::max(30, (breite - 2 * GAP) / 3);
            for (int i = 0; i < 3; ++i)
                platz(g.editManipAus[i], x + i * (feldB + GAP), ym + LABEL_H + 2, i == 2 ? breite - 2 * (feldB + GAP) : feldB, ZEILE_H);
        }
        if (dreiFelder)
            ym += LABEL_H + 2 + ZEILE_H + GAP;
        // Vervielfaeltigen: echte Kopien oder Instanzen
        platz(g.lblManipKopien, x, ym + 4, wertLabelB, LABEL_H);
        platz(g.cmbManipKopien, x + wertLabelB + GAP, ym, breite - wertLabelB - GAP, ZEILE_H + 200);
        platz(g.lblManipLage, x, ym + 4, wertLabelB, LABEL_H);
        platz(g.cmbManipLage, x + wertLabelB + GAP, ym, breite - wertLabelB - GAP, ZEILE_H + 200);
        platz(g.lblManipPinsel, x, ym + 4, wertLabelB, LABEL_H);
        platz(g.cmbManipPinsel, x + wertLabelB + GAP, ym, breite - wertLabelB - GAP, ZEILE_H + 200);
        if (art == MANIP_VIELFACH || art == MANIP_ABSETZEN || art == MANIP_PINSEL)
            ym += ZEILE_H + GAP;
        platz(g.btnManipAusfuehren, x, ym, breite, BTN_H);
        ym += BTN_H + GAP;
        platz(g.lblManipInfo, x, ym, breite, std::max(11 * LABEL_H, h - MARGIN - ym));
    }

    // ---------- Reiter "Material" (liegt an derselben Stelle) ----------
    {
        int ym = inhaltOben;
        int feldB = (breite - MAT_LABEL_W - ZEILE_H - 4 * GAP) / 3;

        platz(g.lblVorlage, x, ym + 4, MAT_LABEL_W, LABEL_H);
        platz(g.cmbVorlage, x + MAT_LABEL_W + GAP, ym, breite - MAT_LABEL_W - GAP, ZEILE_H + 450); // mit Liste (viele Vorlagen)
        ym += ZEILE_H + GAP;

        for (int i = 0; i < MAT_FARBEN; ++i)
        {
            platz(g.lblMatFarbe[i], x, ym + 4, MAT_LABEL_W, LABEL_H);
            for (int k = 0; k < 3; ++k)
                platz(g.editMatFarbe[i * 3 + k], x + MAT_LABEL_W + GAP + k * (feldB + GAP), ym, feldB, ZEILE_H);
            platz(g.btnMatFarbe[i], x + breite - ZEILE_H, ym, ZEILE_H, ZEILE_H);
            ym += ZEILE_H + GAP;
        }

        // zwei Werte je Zeile, jedes Feld direkt nach seiner Beschriftung:
        // links  [Name (wie "Umgebung")][Feld unter der 1. Farbspalte]
        // rechts [Name (so breit wie links)][Feld direkt danach bis zum rechten Rand]
        const int feldLinks = x + MAT_LABEL_W + GAP;
        const int rechtsLabel = feldLinks + feldB + GAP;
        const int rechtsFeld = rechtsLabel + MAT_LABEL_W + GAP;
        for (int i = 0; i < MAT_WERTE; ++i)
        {
            if (i % 2 == 0)
            {
                platz(g.lblMatWert[i], x, ym + 4, MAT_LABEL_W, LABEL_H);
                platz(g.editMatWert[i], feldLinks, ym, feldB, ZEILE_H);
            }
            else
            {
                platz(g.lblMatWert[i], rechtsLabel, ym + 4, rechtsFeld - GAP - rechtsLabel, LABEL_H);
                platz(g.editMatWert[i], rechtsFeld, ym, std::max(30, x + breite - rechtsFeld), ZEILE_H);
                ym += ZEILE_H + GAP;
            }
        }

        // Textur: Dateiname und Abbildungsart gleich breit, dazwischen der Knopf "…"
        platz(g.lblTextur, x, ym + 4, MAT_LABEL_W, LABEL_H);
        const int dateiB = ZEILE_H + 4;
        const int texturB = std::max(40, (breite - MAT_LABEL_W - dateiB - 3 * GAP) / 2);
        platz(g.editTextur, feldLinks, ym, texturB, ZEILE_H);
        platz(g.btnTexturDatei, feldLinks + texturB + GAP, ym, dateiB, ZEILE_H);
        platz(g.cmbTexturModus, feldLinks + texturB + GAP + dateiB + GAP, ym,
              x + breite - (feldLinks + texturB + 2 * GAP + dateiB), ZEILE_H + 160);
        ym += ZEILE_H + GAP;
        {
            // Ausrichtung der Textur: drei Knoepfe nebeneinander
            const int kb = (breite - MAT_LABEL_W - 3 * GAP) / 3;
            platz(g.btnTexturU, x + MAT_LABEL_W + GAP, ym, kb, ZEILE_H);
            platz(g.btnTexturV, x + MAT_LABEL_W + 2 * GAP + kb, ym, kb, ZEILE_H);
            platz(g.btnTexturDrehen, x + MAT_LABEL_W + 3 * GAP + 2 * kb, ym, breite - MAT_LABEL_W - 3 * GAP - 2 * kb, ZEILE_H);
        }
        ym += ZEILE_H + GAP;
        // Kachel (m) | Winkel (Grad): wie die Wertezeilen oben
        platz(g.lblTexturKachel, x, ym + 4, MAT_LABEL_W, LABEL_H);
        platz(g.editTexturKachel, feldLinks, ym, feldB, ZEILE_H);
        platz(g.lblTexturWinkel, rechtsLabel, ym + 4, rechtsFeld - GAP - rechtsLabel, LABEL_H);
        platz(g.editTexturWinkel, rechtsFeld, ym, std::max(30, x + breite - rechtsFeld), ZEILE_H);
        ym += ZEILE_H + GAP;
        // Versatz U, V | haftet am Objekt
        platz(g.lblTexturVersatz, x, ym + 4, MAT_LABEL_W, LABEL_H);
        for (int k = 0; k < 2; ++k)
            platz(g.editTexturVersatz[k], feldLinks + k * (feldB + GAP), ym, feldB, ZEILE_H);
        platz(g.btnTexturFest, feldLinks + 2 * (feldB + GAP), ym, x + breite - (feldLinks + 2 * (feldB + GAP)), ZEILE_H);
        ym += ZEILE_H + GAP;
        // Kugel-Drehung X, Y, Z: nur bei der Abbildung Kugel (spart sonst eine Zeile)
        if (texturKugelZeile)
        {
            platz(g.lblTexturKugel, x, ym + 4, MAT_LABEL_W, LABEL_H);
            for (int k = 0; k < 3; ++k)
                platz(g.editTexturKugel[k], feldLinks + k * (feldB + GAP), ym, feldB, ZEILE_H);
            ym += ZEILE_H + GAP;
        }
        texturKugelSichtbar();
        // Zusatzbild in einer Zeile: Art | Datei | ... | Staerke
        platz(g.lblTexturBild, x, ym + 4, MAT_LABEL_W, LABEL_H);
        const int artB = std::max(56, feldB - 10);
        const int dateiB2 = ZEILE_H + 4;
        const int staerkeB = std::max(40, feldB * 2 / 3);
        platz(g.cmbTexturBild, feldLinks, ym, artB, ZEILE_H + 160);
        const int dateiX = feldLinks + artB + GAP;
        const int dateiW = std::max(30, x + breite - staerkeB - GAP - dateiB2 - GAP - dateiX);
        platz(g.editTexturBild, dateiX, ym, dateiW, ZEILE_H);
        platz(g.btnTexturBild, dateiX + dateiW + GAP, ym, dateiB2, ZEILE_H);
        platz(g.editTexturStaerke, x + breite - staerkeB, ym, staerkeB, ZEILE_H);
        ym += ZEILE_H + GAP;
        // Regen: Wellenringe | nass (wie Kachel | Winkel)
        platz(g.lblTexturRinge, x, ym + 4, MAT_LABEL_W, LABEL_H);
        platz(g.editTexturRinge, feldLinks, ym, feldB, ZEILE_H);
        platz(g.lblTexturNass, rechtsLabel, ym + 4, rechtsFeld - GAP - rechtsLabel, LABEL_H);
        platz(g.editTexturNass, rechtsFeld, ym, std::max(30, x + breite - rechtsFeld), ZEILE_H);
    }

    platz(g.lblName, x, y + 4, ZEILEN_LABEL_W, LABEL_H);
    platz(g.info, x + ZEILEN_LABEL_W + GAP, y, breite - ZEILEN_LABEL_W - GAP, ZEILE_H);
    y += ZEILE_H + GAP;

    platz(g.lblDetails, x + ZEILEN_LABEL_W + GAP, y, breite - ZEILEN_LABEL_W - GAP, LABEL_H);
    y += LABEL_H + GAP;

    // rechts neben den Farbwerten das Farbfeld (beide Zeilen gleich breit, damit die Spalten fluchten)
    int feldBreite = (breite - ZEILEN_LABEL_W - ZEILE_H - 4 * GAP) / 3;
    HWND zeilenLabel[6] = {g.lblZeile1, g.lblZeile2, g.lblZeile3, g.lblZeile4, g.lblZeile5, g.lblZeile6};
    zielKnopfX = x + breite - ZEILE_H;
    for (int zeile = 0; zeile < 6; ++zeile)
    {
        zeilenY[zeile] = y;
        platz(zeilenLabel[zeile], x, y + 4, ZEILEN_LABEL_W, LABEL_H);
        for (int k = 0; k < 3; ++k)
            platz(g.edit[zeile * 3 + k], x + ZEILEN_LABEL_W + GAP + k * (feldBreite + GAP), y, feldBreite, ZEILE_H);
        if (zeile == 1)
            platz(g.btnFarbe, x + breite - ZEILE_H, y, ZEILE_H, ZEILE_H);
        y += ZEILE_H + GAP;
    }
    if (g.btnZiel && zielZeile >= 0) // Ziel-Knopf mit der Zeile verschieben
        platz(g.btnZiel, zielKnopfX, zeilenY[zielZeile], ZEILE_H, ZEILE_H);
}

//--------------------------------------------------
// Darstellung
//--------------------------------------------------

// Farbfeld: aktuelle Farbe mit Rahmen, ohne aenderbare Farbe nur ein leerer Rahmen
static void farbfeldZeichnen(const DRAWITEMSTRUCT *d)
{
    FillRect(d->hDC, &d->rcItem, pinselHintergrund);

    RECT rc = d->rcItem;
    bool hover = d->hwndItem == hoverButton;

    // Farbfeld der Eigenschaften oder eines der Material-Farbfelder
    bool aktiv = farbeAktiv;
    Farbe farbe = angezeigteFarbe;
    if (d->CtlID >= ID_BTN_MATFARBE_ERSTES && d->CtlID < ID_BTN_MATFARBE_ERSTES + MAT_FARBEN)
    {
        aktiv = materialAktiv;
        farbe = angezeigteMatFarben[d->CtlID - ID_BTN_MATFARBE_ERSTES];
    }

    HBRUSH fuellung = aktiv
                          ? CreateSolidBrush(RGB(kanal(std::min(1.0, farbe.r)), kanal(std::min(1.0, farbe.g)), kanal(std::min(1.0, farbe.b))))
                          : CreateSolidBrush(FARBE_FLAECHE);
    HPEN rahmen = CreatePen(PS_SOLID, 1, aktiv && hover ? FARBE_TEXT : FARBE_BAUMLINIEN);

    HGDIOBJ altPinsel = SelectObject(d->hDC, fuellung);
    HGDIOBJ altStift = SelectObject(d->hDC, rahmen);
    RoundRect(d->hDC, rc.left, rc.top, rc.right, rc.bottom, 4, 4);
    SelectObject(d->hDC, altStift);
    SelectObject(d->hDC, altPinsel);

    DeleteObject(rahmen);
    DeleteObject(fuellung);
}

// ---------- Symbole der Manipulationen (statt Auswahlliste) ----------
// Jede Aktion ein kleines Bild aus Linien und Flaechen im Einheitsquadrat (0..1, y nach unten)
static void manipIconZeichnen(const DRAWITEMSTRUCT *d, int art)
{
    const int gewaehlt = int(SendMessageW(g.cmbManipArt, CB_GETCURSEL, 0, 0));
    const bool aktiv = art == gewaehlt, hover = d->hwndItem == hoverButton, gedrueckt = (d->itemState & ODS_SELECTED) != 0;
    HDC dc = d->hDC;
    const RECT &rc = d->rcItem;
    FillRect(dc, &rc, pinselHintergrund);
    {
        HBRUSH b = CreateSolidBrush(gedrueckt ? FARBE_BUTTON_GEDRUECKT : aktiv ? (hover ? FARBE_AKTIV_HOVER : FARBE_AKTIV)
                                                     : hover ? FARBE_BUTTON_HOVER
                                                             : FARBE_BUTTON);
        HGDIOBJ ab = SelectObject(dc, b), ap = SelectObject(dc, GetStockObject(NULL_PEN));
        RoundRect(dc, rc.left, rc.top, rc.right + 1, rc.bottom + 1, 7, 7);
        SelectObject(dc, ab);
        SelectObject(dc, ap);
        DeleteObject(b);
    }
    // Zeichenflaeche: Quadrat mit Rand
    const int w = rc.right - rc.left, h = rc.bottom - rc.top, s = std::max(8, std::min(w, h) - 10);
    const int x0 = rc.left + (w - s) / 2, y0 = rc.top + (h - s) / 2;
    auto P = [&](double x, double y) { return POINT{x0 + int(std::lround(x * s)), y0 + int(std::lround(y * s))}; };
    const COLORREF hell = RGB(225, 232, 242), mittel = RGB(150, 160, 175);
    const COLORREF akzent = aktiv ? RGB(255, 255, 255) : RGB(95, 175, 245), akzent2 = aktiv ? RGB(200, 225, 255) : RGB(60, 120, 185),
                   akzent3 = aktiv ? RGB(150, 190, 240) : RGB(40, 85, 140), grund = aktiv ? FARBE_AKTIV : FARBE_BUTTON;
    const int dick = std::max(1, s / 14);
    auto stift = [&](COLORREF f, int breite = -1) { return CreatePen(PS_SOLID, breite < 0 ? dick : breite, f); };
    auto vieleck = [&](std::initializer_list<std::pair<double, double>> pk, COLORREF fuellung, COLORREF rand, bool fuellen = true)
    {
        std::vector<POINT> pt;
        for (auto &q : pk)
            pt.push_back(P(q.first, q.second));
        HPEN pe = stift(rand);
        HBRUSH br = fuellen ? CreateSolidBrush(fuellung) : nullptr;
        HGDIOBJ ap = SelectObject(dc, pe), ab = SelectObject(dc, fuellen ? (HGDIOBJ)br : GetStockObject(NULL_BRUSH));
        Polygon(dc, pt.data(), int(pt.size()));
        SelectObject(dc, ap);
        SelectObject(dc, ab);
        DeleteObject(pe);
        if (br)
            DeleteObject(br);
    };
    auto linie = [&](double xa, double ya, double xb, double yb, COLORREF f, int breite = -1)
    {
        HPEN pe = stift(f, breite);
        HGDIOBJ ap = SelectObject(dc, pe);
        POINT a = P(xa, ya), b = P(xb, yb);
        MoveToEx(dc, a.x, a.y, nullptr);
        LineTo(dc, b.x, b.y);
        SelectObject(dc, ap);
        DeleteObject(pe);
    };
    auto kreis = [&](double cx, double cy, double r, COLORREF fuellung, COLORREF rand, bool fuellen = true)
    {
        HPEN pe = rand == CLR_NONE ? nullptr : stift(rand);
        HBRUSH br = fuellen ? CreateSolidBrush(fuellung) : nullptr;
        HGDIOBJ ap = SelectObject(dc, pe ? (HGDIOBJ)pe : GetStockObject(NULL_PEN));
        HGDIOBJ ab = SelectObject(dc, fuellen ? (HGDIOBJ)br : GetStockObject(NULL_BRUSH));
        POINT a = P(cx - r, cy - r), b = P(cx + r, cy + r);
        Ellipse(dc, a.x, a.y, b.x + 1, b.y + 1);
        SelectObject(dc, ap);
        SelectObject(dc, ab);
        if (pe)
            DeleteObject(pe);
        if (br)
            DeleteObject(br);
    };

    switch (art)
    {
    case 0: // in Ebenen zerlegen: auseinandergezogener Wuerfel
        vieleck({{0.10, 0.42}, {0.56, 0.42}, {0.56, 0.88}, {0.10, 0.88}}, akzent2, hell);
        vieleck({{0.18, 0.32}, {0.64, 0.32}, {0.84, 0.12}, {0.38, 0.12}}, akzent, hell);
        vieleck({{0.66, 0.44}, {0.88, 0.24}, {0.88, 0.68}, {0.66, 0.88}}, akzent3, hell);
        break;
    case 1: // Delaunay: Kante umklappen
        vieleck({{0.10, 0.72}, {0.45, 0.12}, {0.90, 0.30}, {0.62, 0.90}}, akzent3, hell);
        linie(0.10, 0.72, 0.90, 0.30, mittel, std::max(1, dick / 2));
        linie(0.45, 0.12, 0.62, 0.90, akzent, dick + 1);
        break;
    case 2: // Porath: Dreieck vom Mittelpunkt aus geteilt
    {
        vieleck({{0.50, 0.08}, {0.08, 0.88}, {0.92, 0.88}}, akzent3, hell);
        const double mx = 0.5, my = 0.613;
        for (auto &e : {std::pair<double, double>{0.50, 0.08}, {0.08, 0.88}, {0.92, 0.88}})
            linie(mx, my, e.first, e.second, akzent, std::max(1, dick - 1));
        kreis(mx, my, 0.06, akzent, CLR_NONE);
        break;
    }
    case 3: // glatt schattieren: Kugel mit weichem Licht
        for (int i = 0; i < 6; ++i)
        {
            const double t = i / 5.0, r = 0.42 * (1.0 - 0.8 * t);
            const int c = int(50 + 190 * t);
            kreis(0.5 - 0.12 * t, 0.5 - 0.12 * t, r,
                  aktiv ? RGB(c, c, 255) : RGB(std::min(255, c / 2 + 30), std::min(255, c * 3 / 4 + 30), std::min(255, c + 15)), CLR_NONE);
        }
        break;
    case 4: // Loop: eckig -> rund
        vieleck({{0.10, 0.10}, {0.90, 0.10}, {0.90, 0.90}, {0.10, 0.90}}, grund, mittel, false);
        {
            HPEN pe = stift(hell);
            HBRUSH br = CreateSolidBrush(akzent2);
            HGDIOBJ ap = SelectObject(dc, pe), ab = SelectObject(dc, br);
            POINT a = P(0.18, 0.18), b = P(0.82, 0.82);
            RoundRect(dc, a.x, a.y, b.x, b.y, s / 2, s / 2);
            SelectObject(dc, ap);
            SelectObject(dc, ab);
            DeleteObject(pe);
            DeleteObject(br);
        }
        break;
    case 5: // Dicke: Flaeche wird Platte
        linie(0.08, 0.30, 0.70, 0.30, mittel, std::max(1, dick / 2));
        vieleck({{0.10, 0.52}, {0.72, 0.52}, {0.72, 0.72}, {0.10, 0.72}}, akzent2, hell);
        vieleck({{0.10, 0.52}, {0.72, 0.52}, {0.90, 0.38}, {0.28, 0.38}}, akzent, hell);
        vieleck({{0.72, 0.52}, {0.90, 0.38}, {0.90, 0.58}, {0.72, 0.72}}, akzent3, hell);
        break;
    case 6: // Faechern: Pyramide auf der Flaeche
        vieleck({{0.10, 0.78}, {0.56, 0.90}, {0.90, 0.70}, {0.44, 0.60}}, akzent3, hell);
        vieleck({{0.10, 0.78}, {0.56, 0.90}, {0.50, 0.10}}, akzent2, hell);
        vieleck({{0.56, 0.90}, {0.90, 0.70}, {0.50, 0.10}}, akzent, hell);
        break;
    case 7: // zufaellige Verteilung: verstreute Punkte
        for (auto &q : {std::pair<double, double>{0.18, 0.22}, {0.62, 0.15}, {0.40, 0.45}, {0.82, 0.42}, {0.15, 0.70}, {0.55, 0.78}, {0.86, 0.84}})
            kreis(q.first, q.second, 0.08, akzent, hell);
        break;
    case 8: // zufaellige Drehung: gedrehtes Quadrat mit Drehpfeil
    {
        vieleck({{0.50, 0.28}, {0.72, 0.50}, {0.50, 0.72}, {0.28, 0.50}}, akzent2, hell);
        HPEN pe = stift(akzent, dick + 1);
        HGDIOBJ ap = SelectObject(dc, pe), ab = SelectObject(dc, GetStockObject(NULL_BRUSH));
        // Dreiviertelkreis gegen den Uhrzeigersinn von oben ueber links und unten nach rechts, Pfeil nach oben
        POINT a = P(0.10, 0.10), b = P(0.90, 0.90), st = P(0.50, 0.10), en = P(0.90, 0.40);
        Arc(dc, a.x, a.y, b.x, b.y, st.x, st.y, en.x, en.y);
        SelectObject(dc, ap);
        SelectObject(dc, ab);
        DeleteObject(pe);
        vieleck({{0.76, 0.46}, {1.0, 0.46}, {0.88, 0.26}}, akzent, akzent);
        break;
    }
    case MANIP_VIELFACH: // vervielfaeltigen: Reihe von Kopien
        vieleck({{0.04, 0.34}, {0.30, 0.34}, {0.30, 0.66}, {0.04, 0.66}}, akzent, hell);
        vieleck({{0.37, 0.34}, {0.63, 0.34}, {0.63, 0.66}, {0.37, 0.66}}, akzent2, hell);
        vieleck({{0.70, 0.34}, {0.96, 0.34}, {0.96, 0.66}, {0.70, 0.66}}, akzent3, hell);
        break;
    case MANIP_ADDIEREN: // addieren: zwei Koerper verschmolzen
        kreis(0.36, 0.52, 0.28, akzent2, hell);
        kreis(0.64, 0.48, 0.28, akzent2, hell);
        kreis(0.36, 0.52, 0.26, akzent2, CLR_NONE);
        kreis(0.64, 0.48, 0.26, akzent2, CLR_NONE);
        linie(0.42, 0.50, 0.58, 0.50, hell, dick);
        linie(0.50, 0.42, 0.50, 0.58, hell, dick);
        break;
    case MANIP_SUBTRAHIEREN: // subtrahieren: Wand mit Loch
        vieleck({{0.08, 0.12}, {0.92, 0.12}, {0.92, 0.88}, {0.08, 0.88}}, akzent2, hell);
        vieleck({{0.36, 0.40}, {0.64, 0.40}, {0.64, 0.88}, {0.36, 0.88}}, grund, hell);
        linie(0.42, 0.64, 0.58, 0.64, hell, dick);
        break;
    case MANIP_ABSETZEN: // auf Oberflaeche absetzen: Kasten faellt auf einen Huegel
        vieleck({{0.0, 0.92}, {0.0, 0.80}, {0.25, 0.70}, {0.55, 0.66}, {0.80, 0.74}, {1.0, 0.84}, {1.0, 0.92}}, akzent3, hell);
        vieleck({{0.38, 0.30}, {0.70, 0.30}, {0.70, 0.62}, {0.38, 0.62}}, akzent, hell);
        linie(0.54, 0.02, 0.54, 0.22, hell, dick);
        vieleck({{0.46, 0.14}, {0.62, 0.14}, {0.54, 0.26}}, hell, hell);
        break;
    case MANIP_TERRAIN: // Terrain unter Objekt absenken: Huegel mit Mulde, Platte darin
        vieleck({{0.0, 0.94}, {0.0, 0.66}, {0.22, 0.56}, {0.30, 0.70}, {0.70, 0.70}, {0.78, 0.54}, {1.0, 0.62}, {1.0, 0.94}},
                akzent3, hell);
        vieleck({{0.30, 0.60}, {0.70, 0.60}, {0.70, 0.70}, {0.30, 0.70}}, akzent, hell);
        linie(0.50, 0.16, 0.50, 0.44, hell, dick);
        vieleck({{0.40, 0.36}, {0.60, 0.36}, {0.50, 0.50}}, hell, hell);
        break;
    case MANIP_PINSEL: // Terrain-Pinsel: Huegel und Pinselkreis
        vieleck({{0.0, 0.94}, {0.0, 0.72}, {0.30, 0.50}, {0.55, 0.62}, {0.80, 0.40}, {1.0, 0.56}, {1.0, 0.94}}, akzent3, hell);
        kreis(0.55, 0.36, 0.20, CLR_NONE, hell);
        linie(0.62, 0.20, 0.92, 0.04, hell, dick + 1);
        break;
    case MANIP_FELL: // Fell-Imitation: Koerper mit abstehenden Haaren
        for (int i = 0; i < 16; ++i)
        {
            const double w = i * 2.0 * M_PI / 16.0;
            linie(0.5 + 0.30 * std::cos(w), 0.55 + 0.26 * std::sin(w), 0.5 + 0.44 * std::cos(w + 0.15),
                  0.55 + 0.38 * std::sin(w + 0.15), hell, std::max(1, dick / 2));
        }
        kreis(0.5, 0.55, 0.30, akzent3, hell);
        break;
    case MANIP_GROESSEN: // zufaellige Groessen: verschieden grosse Kaesten auf einer Linie
        linie(0.04, 0.88, 0.96, 0.88, mittel, std::max(1, dick / 2));
        vieleck({{0.06, 0.66}, {0.28, 0.66}, {0.28, 0.88}, {0.06, 0.88}}, akzent3, hell);
        vieleck({{0.34, 0.30}, {0.66, 0.30}, {0.66, 0.88}, {0.34, 0.88}}, akzent, hell);
        vieleck({{0.72, 0.52}, {0.94, 0.52}, {0.94, 0.88}, {0.72, 0.88}}, akzent2, hell);
        break;
    default:
        break;
    }
}

static void buttonZeichnen(const DRAWITEMSTRUCT *d)
{
    if (d->CtlID >= ID_BTN_MANIP_ICON && d->CtlID < UINT(ID_BTN_MANIP_ICON + ANZAHL_MANIP_ARTEN))
    {
        manipIconZeichnen(d, int(d->CtlID - ID_BTN_MANIP_ICON));
        return;
    }
    if (d->CtlID == ID_BTN_FARBE || (d->CtlID >= ID_BTN_MATFARBE_ERSTES && d->CtlID < ID_BTN_MATFARBE_ERSTES + MAT_FARBEN))
    {
        farbfeldZeichnen(d);
        return;
    }

    const bool kopf = d->CtlID == ID_BTN_ZUKLAPPEN;
    const bool objekteKnopf = d->CtlID == ID_BTN_KOPF;

    bool aktiv =
        (!kanten::gModus && d->CtlID == ID_BTN_MOVE && selectedAction == SelectedAction::MOVE) ||
        (!kanten::gModus && d->CtlID == ID_BTN_ROTATE && selectedAction == SelectedAction::ROTATE) ||
        (!kanten::gModus && d->CtlID == ID_BTN_SCALE && selectedAction == SelectedAction::SCALE) ||
        (d->CtlID == ID_BTN_KANTEN && kanten::gModus);

    aktiv = aktiv || d->CtlID == ID_BTN_RENDER;     // Hauptaktion immer hervorgehoben
    aktiv = aktiv || (d->CtlID == ID_BTN_AUTO_RENDER && autoRendernAn); // Schalter eingeschaltet
    aktiv = aktiv || (d->CtlID == ID_BTN_REITER_EIGENSCHAFTEN && reiter == 0) ||
            (d->CtlID == ID_BTN_REITER_MATERIAL && reiter == 1) ||
            (d->CtlID == ID_BTN_REITER_MANIPULATION && reiter == 2);
    aktiv = aktiv || (objekteKnopf && !eingeklappt); // Objektfenster sichtbar
    aktiv = aktiv || (d->CtlID == ID_BTN_RENDERFENSTER && hRenderfenster && IsWindowVisible(hRenderfenster));
    // Ausrichtung der Textur: hervorgehoben, wenn beim Hauptobjekt eingeschaltet
    if (d->CtlID == ID_BTN_TEXTUR_SPIEGEL_U || d->CtlID == ID_BTN_TEXTUR_SPIEGEL_V || d->CtlID == ID_BTN_TEXTUR_FEST)
    {
        std::vector<object *> mo = materialObjekte();
        if (!mo.empty())
            aktiv = aktiv || (d->CtlID == ID_BTN_TEXTUR_SPIEGEL_U   ? mo.front()->texturSpiegelU
                              : d->CtlID == ID_BTN_TEXTUR_SPIEGEL_V ? mo.front()->texturSpiegelV
                                                                    : mo.front()->texturPar().objektfest);
    }

    bool gedrueckt = (d->itemState & ODS_SELECTED) != 0;
    bool hover = d->hwndItem == hoverButton;

    COLORREF flaeche = kopf      ? (hover ? FARBE_BUTTON_HOVER : FARBE_KOPF)
                       : gedrueckt ? FARBE_BUTTON_GEDRUECKT
                       : aktiv     ? (hover ? FARBE_AKTIV_HOVER : FARBE_AKTIV)
                       : hover     ? FARBE_BUTTON_HOVER
                                   : FARBE_BUTTON;

    HBRUSH pinsel = CreateSolidBrush(flaeche);
    if (kopf || objekteKnopf)
        FillRect(d->hDC, &d->rcItem, pinsel); // ohne Rundung, fuellt die ganze Flaeche
    else
    {
        // Flaeche mit leicht abgerundeten Ecken
        FillRect(d->hDC, &d->rcItem, pinselHintergrund);
        HGDIOBJ altPinsel = SelectObject(d->hDC, pinsel);
        HGDIOBJ altStift = SelectObject(d->hDC, GetStockObject(NULL_PEN));
        RoundRect(d->hDC, d->rcItem.left, d->rcItem.top, d->rcItem.right + 1, d->rcItem.bottom + 1, 6, 6);
        SelectObject(d->hDC, altStift);
        SelectObject(d->hDC, altPinsel);
    }
    DeleteObject(pinsel);

    wchar_t text[128] = {};
    GetWindowTextW(d->hwndItem, text, 128);

    wchar_t symbol = 0;
    if (symbolFont)
    {
        if (kopf)
            symbol = 0xE70E; // ChevronUp: Fenster ausblenden
        else if (objekteKnopf)
            symbol = 0xE8FD; // BulletedList
        else
        {
            auto it = buttonSymbole.find(int(d->CtlID));
            if (it != buttonSymbole.end())
                symbol = it->second;
        }
    }

    SetBkMode(d->hDC, TRANSPARENT);
    HGDIOBJ alt = SelectObject(d->hDC, gFont);

    // Breite von Symbol + Abstand + Text, damit beides zusammen zentriert ist
    RECT textRc = {0, 0, 0, 0};
    DrawTextW(d->hDC, text, -1, &textRc, DT_CALCRECT | DT_SINGLELINE);
    int textBreite = textRc.right - textRc.left;

    int symbolBreite = 0;
    if (symbol)
    {
        SelectObject(d->hDC, symbolFont);
        RECT symRc = {0, 0, 0, 0};
        DrawTextW(d->hDC, &symbol, 1, &symRc, DT_CALCRECT | DT_SINGLELINE);
        symbolBreite = symRc.right - symRc.left + 8;
    }

    // zu schmal fuer Symbol und Text: nur den Text (z.B. "Kopie", "Delete" bei grosser Schrift)
    if (symbol && text[0] && !kopf && symbolBreite + textBreite + 8 > int(d->rcItem.right - d->rcItem.left))
        symbolBreite = 0, symbol = 0;

    RECT rc = d->rcItem;
    if (gedrueckt && !kopf)
        OffsetRect(&rc, 0, 1);

    // Kopfzeile linksbuendig, alle anderen zentriert
    int links = kopf ? rc.left + MARGIN
                     : rc.left + std::max(4, (int(rc.right - rc.left) - symbolBreite - textBreite) / 2);

    if (symbol)
    {
        RECT symRc = {links, rc.top, links + symbolBreite, rc.bottom};
        SetTextColor(d->hDC, d->CtlID == ID_BTN_CLEAR ? FARBE_GEFAHR : FARBE_TEXT);
        DrawTextW(d->hDC, &symbol, 1, &symRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        SelectObject(d->hDC, gFont);
    }

    RECT textZiel = {links + symbolBreite, rc.top, rc.right - 2, rc.bottom};
    SetTextColor(d->hDC, kopf ? FARBE_UEBERSCHRIFT : FARBE_TEXT);
    DrawTextW(d->hDC, text, -1, &textZiel, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    SelectObject(d->hDC, alt);
}

static bool istGedimmt(HWND h)
{
    // Ueberschriften im Einstellungsfenster sind mit GWLP_USERDATA = 1 markiert
    return h == g.lblDetails || h == g.lblEigenschaften || GetWindowLongPtrW(h, GWLP_USERDATA) == 1;
}

//--------------------------------------------------
// Window Proc (fuer Leiste und Objektfenster)
//--------------------------------------------------

LRESULT CALLBACK ToolWndProc(
    HWND hwnd,
    UINT msg,
    WPARAM wParam,
    LPARAM lParam)
{
    switch (msg)
    {
    case WM_CTLCOLORSTATIC:
    {
        HDC hdc = (HDC)wParam;
        HWND ctrl = (HWND)lParam;
        int id = GetDlgCtrlID(ctrl);

        // deaktivierte Eingabefelder kommen ebenfalls hier an
        if ((id >= ID_EDIT_ERSTES && id < ID_EDIT_ERSTES + ANZAHL_EDITS) || id == ID_EDIT_NAME ||
            (id >= ID_EDIT_MATFARBE_ERSTES && id <= ID_EDIT_TEXTUR_LETZTES) || id == ID_EDIT_TEXTUR_RINGE || id == ID_EDIT_TEXTUR_NASS)
        {
            SetTextColor(hdc, FARBE_TEXT_GEDIMMT);
            SetBkColor(hdc, FARBE_FLAECHE);
            return (INT_PTR)pinselFlaeche;
        }

        SetTextColor(hdc, istGedimmt(ctrl) ? FARBE_UEBERSCHRIFT : FARBE_TEXT);
        SetBkColor(hdc, FARBE_HINTERGRUND);
        return (INT_PTR)pinselHintergrund;
    }

    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX: // aufgeklappte Auswahllisten (Materialvorlage, Texturmodus)
    {
        HDC hdc = (HDC)wParam;
        SetTextColor(hdc, FARBE_TEXT);
        SetBkColor(hdc, FARBE_FLAECHE);
        return (INT_PTR)pinselFlaeche;
    }

    case WM_ERASEBKGND:
    {
        RECT rc;
        GetClientRect(hwnd, &rc);
        FillRect((HDC)wParam, &rc, pinselHintergrund);
        return TRUE;
    }

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        RECT rc;
        GetClientRect(hwnd, &rc);

        HPEN stift = CreatePen(PS_SOLID, 1, FARBE_LINIEN);
        HGDIOBJ alt = SelectObject(hdc, stift);

        if (hwnd == hToolWindow)
        {
            for (int x : trennlinien)
            {
                MoveToEx(hdc, x, MARGIN + 2, nullptr);
                LineTo(hdc, x, rc.bottom - MARGIN - 2);
            }

            // Unterkante zur Abgrenzung von den Ansichten
            MoveToEx(hdc, 0, rc.bottom - 1, nullptr);
            LineTo(hdc, rc.right, rc.bottom - 1);
        }
        else
        {
            // Rahmen um das Objektfenster
            HGDIOBJ altPinsel = SelectObject(hdc, GetStockObject(NULL_BRUSH));
            Rectangle(hdc, rc.left, rc.top, rc.right, rc.bottom);
            SelectObject(hdc, altPinsel);
        }

        SelectObject(hdc, alt);
        DeleteObject(stift);

        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_DRAWITEM:
    {
        buttonZeichnen(reinterpret_cast<const DRAWITEMSTRUCT *>(lParam));
        return TRUE;
    }

    case WM_SIZE:
    {
        // Zeichnen aus, solange verschoben wird, danach EIN Neuzeichnen von allem (ohne Zwischenstaende,
        // die man als Flackern sah); nur bei echter Groessenaenderung
        static std::map<HWND, LPARAM> letzteGroesse;
        if (letzteGroesse[hwnd] == lParam)
            return 0;
        letzteGroesse[hwnd] = lParam;
        SendMessageW(hwnd, WM_SETREDRAW, FALSE, 0);
        if (hwnd == hToolWindow)
            leisteLayout(LOWORD(lParam), HIWORD(lParam));
        else
            objektfensterLayout(LOWORD(lParam), HIWORD(lParam));
        SendMessageW(hwnd, WM_SETREDRAW, TRUE, 0);
        // mit allen Steuerelementen: verschobene Beschriftungen hinterlassen sonst Reste
        RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_FRAME);
        return 0;
    }

    case WM_TIMER:
    {
        if (wParam == TIMER_ABGLEICH)
            abgleichen();
        return 0;
    }

    case WM_LISTE_AKTUALISIEREN:
    {
        UpdateObjectList();
        return 0;
    }

    case WM_HAUPTTHREAD:
    {
        (*reinterpret_cast<const std::function<void()> *>(lParam))();
        return 0;
    }

    case WM_KEYDOWN: // Leiste oder Objektfenster selbst hat den Fokus
    {
        if (tasteWeitergeben(wParam))
            return 0;
        break;
    }

    case WM_EINGABE_FERTIG:
    {
        eingabeUebernehmen(int(wParam));
        return 0;
    }

    // Im Vordergrund nur, solange eines unserer Fenster aktiv ist
    // (das Objektfenster gehoert der Leiste und folgt ihr automatisch)
    case WM_ACTIVATEAPP:
    {
        if (hwnd != hToolWindow || hHauptfenster)
            break;

        if (wParam)
            SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        else
        {
            SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

            HWND vorne = GetForegroundWindow();
            if (vorne && vorne != hwnd)
                SetWindowPos(hwnd, vorne, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        }
        return 0;
    }

    case WM_COMMAND:
    {
        int id = LOWORD(wParam);

        if ((id >= ID_EDIT_ERSTES && id < ID_EDIT_ERSTES + ANZAHL_EDITS) || id == ID_EDIT_NAME || id == ID_EDIT_SCHRITT ||
            (id >= ID_EDIT_MATFARBE_ERSTES && id <= ID_EDIT_TEXTUR_LETZTES) || id == ID_EDIT_TEXTUR_RINGE || id == ID_EDIT_TEXTUR_NASS)
        {
            if (HIWORD(wParam) == EN_KILLFOCUS)
                eingabeUebernehmen(id);
            return 0;
        }

        if (id == ID_CMB_BLOCK)
        {
            if (HIWORD(wParam) == CBN_SELCHANGE)
                blockAuswahlUebernehmen();
            return 0;
        }
        if (id == ID_BTN_DATEI)
        {
            dateiMenue();
            return 0;
        }

        if (id == ID_CMB_VORLAGE || id == ID_CMB_TEXTURMODUS)
        {
            if (HIWORD(wParam) == CBN_SELCHANGE)
            {
                if (id == ID_CMB_VORLAGE)
                    vorlageUebernehmen();
                else
                    texturModusUebernehmen();
                abgleichen();
            }
            return 0;
        }

        if (id == ID_BTN_MANIP_AUSFUEHREN)
        {
            manipAusfuehren();
            return 0;
        }
        if (id == ID_CMB_MANIP_ART)
        {
            if (HIWORD(wParam) == CBN_SELCHANGE)
                manipAnzeigen();
            return 0;
        }
        if (id >= ID_BTN_MANIP_ICON && id < ID_BTN_MANIP_ICON + ANZAHL_MANIP_ARTEN)
        {
            SendMessageW(g.cmbManipArt, CB_SETCURSEL, WPARAM(id - ID_BTN_MANIP_ICON), 0);
            manipAnzeigen();
            for (int i = 0; i < ANZAHL_MANIP_ARTEN; ++i)
                InvalidateRect(g.btnManipIcon[i], nullptr, FALSE); // Hervorhebung
            return 0;
        }
        if (id == ID_CMB_MANIP_OBJEKT)
        {
            if (HIWORD(wParam) == CBN_DROPDOWN)
                manipObjekteFuellen(); // aktuelle Szene
            return 0;
        }
        if (id == ID_EDIT_MANIP_TIEFE || id == ID_EDIT_MANIP_KNICK || id == ID_EDIT_MANIP_WERT || id == ID_EDIT_MANIP_AUS_X ||
            id == ID_EDIT_MANIP_AUS_Y || id == ID_EDIT_MANIP_AUS_Z)
            return 0; // Wert wird beim Ausfuehren gelesen

        if (id == ID_BTN_REITER_EIGENSCHAFTEN || id == ID_BTN_REITER_MATERIAL || id == ID_BTN_REITER_MANIPULATION)
        {
            reiterZeigen(id == ID_BTN_REITER_MANIPULATION ? 2 : id == ID_BTN_REITER_MATERIAL ? 1 : 0);
            materialAnzeigen();
            return 0;
        }

        if (id >= ID_BTN_MATFARBE_ERSTES && id < ID_BTN_MATFARBE_ERSTES + MAT_FARBEN)
        {
            materialFarbdialog(id - ID_BTN_MATFARBE_ERSTES);
            return 0;
        }

        if (id == ID_BTN_EINSTELLUNGEN)
        {
            EinstellungenZeigen();
            return 0;
        }

        if (id == ID_BTN_UNDO || id == ID_BTN_REDO || id == ID_BTN_KOPIE)
        {
            if (id == ID_BTN_UNDO)
                Rueckgaengig();
            else if (id == ID_BTN_REDO)
                Wiederholen();
            else
                AuswahlDuplizieren();
            return 0;
        }

        if (id >= ID_BTN_SCHRITT_ERSTES && id < ID_BTN_SCHRITT_ERSTES + 6)
        {
            int i = id - ID_BTN_SCHRITT_ERSTES;
            SchrittAusfuehren(i / 2, (i % 2) ? +1 : -1);
            return 0;
        }

        if (id == ID_BTN_KOPF || id == ID_BTN_ZUKLAPPEN)
        {
            einklappen(id == ID_BTN_KOPF ? !eingeklappt : true);
            return 0;
        }

        if (id == ID_BTN_FARBE)
        {
            farbdialog();
            return 0;
        }

        if (id == ID_BTN_ZIEL)
        {
            zielMenueZeigen();
            return 0;
        }

        if (id == ID_BTN_LOESCHEN)
        {
            AusgewaehltesObjektLoeschen();
            return 0;
        }

        if (id == ID_BTN_NEU)
        {
            neuMenue();
            return 0;
        }
        if (id == ID_BTN_TEXTUR_DATEI)
        {
            texturDateiWaehlen();
            return 0;
        }
        if (id == ID_BTN_TEXTUR_SPIEGEL_U || id == ID_BTN_TEXTUR_SPIEGEL_V || id == ID_BTN_TEXTUR_DREHEN)
        {
            texturAusrichten(id);
            return 0;
        }
        if (id == ID_BTN_TEXTUR_FEST)
        {
            texturFestUmschalten();
            return 0;
        }
        if (id == ID_BTN_TEXTUR_BILD)
        {
            texturBildDateiWaehlen();
            return 0;
        }
        if (id == ID_CMB_TEXTUR_BILD)
        {
            if (HIWORD(wParam) == CBN_SELCHANGE)
                materialAnzeigen(); // Datei und Staerke des gewaehlten Bilds zeigen
            return 0;
        }

        static const std::unordered_map<int, char> commandMap =
            {
                {ID_BTN_IMPORT, 'I'},
                {ID_BTN_RENDER, 'Y'},
                {ID_BTN_STOP, 'X'},
                {ID_BTN_SAVE, 'O'},
                {ID_BTN_LADEN, 'L'},
                {ID_BTN_EXPORT, 'E'},
                {ID_BTN_RENDERFENSTER, 'W'},
                {ID_BTN_FLAT, 'F'},
                {ID_BTN_ZENTRIEREN, 'Z'},
                {ID_BTN_FOKUS, char(VK_DECIMAL)},
                {ID_BTN_ZOOM_PLUS, char(VK_ADD)},
                {ID_BTN_ZOOM_MINUS, char(VK_SUBTRACT)},
                {ID_BTN_MOVE, 'M'},
                {ID_BTN_ROTATE, 'R'},
                {ID_BTN_SCALE, 'S'},
                {ID_BTN_CLEAR, 'C'},
                {ID_BTN_QUIT, 'Q'},
                {ID_BTN_NEXT, 'N'}};

        if (id == ID_BTN_KANTEN)
        {
            kantenModusSetzen(!kanten::gModus);
            return 0;
        }
        if (id == ID_BTN_AUTO_RENDER)
        {
            autoRendernUmschalten();
            return 0;
        }
        if ((id == ID_BTN_MOVE || id == ID_BTN_ROTATE || id == ID_BTN_SCALE) && kanten::gModus)
            kantenModusSetzen(false);

        auto it = commandMap.find(id);

        if (it != commandMap.end() && gHandleKeys)
        {
            gHandleKeys(it->second);
            abgleichen();
        }

        return 0;
    }

    case WM_SICHTBAR_PRUEFEN:
        if (lParam)
            sichtbarkeitAusEintrag(reinterpret_cast<HTREEITEM>(lParam));
        return 0;

    case WM_NOTIFY:
    {
        NMHDR *hdr = reinterpret_cast<NMHDR *>(lParam);
        if (!hdr || hdr->idFrom != ID_LIST_OBJECTS)
            return 0;

        // Ausgewaehlter Eintrag blau statt weiss (Windows zeichnet ihn ohne Fokus hell)
        if (hdr->code == NM_CUSTOMDRAW)
        {
            NMTVCUSTOMDRAW *cd = reinterpret_cast<NMTVCUSTOMDRAW *>(lParam);

            static std::vector<object *> markiert; // einmal je Neuzeichnen bestimmen
            if (cd->nmcd.dwDrawStage == CDDS_PREPAINT)
            {
                markiert = ausgewaehlteObjekte();
                return CDRF_NOTIFYITEMDRAW;
            }

            // Objekte: alle der Mehrfachauswahl blau; sonst der in der Liste gewaehlte Eintrag
            LPARAM wert = cd->nmcd.lItemlParam;
            object *eintragObjekt = nullptr;
            if (wert >= 0 && wert < EINTRAG_LICHT && size_t(wert) < scene.size())
                eintragObjekt = scene[size_t(wert)].get();
            else if (wert >= EINTRAG_KIND && size_t(wert - EINTRAG_KIND) < eintraegeKinder.size())
                eintragObjekt = eintraegeKinder[size_t(wert - EINTRAG_KIND)].first;
            bool objektEintrag = eintragObjekt != nullptr;
            bool blau = objektEintrag ? std::find(markiert.begin(), markiert.end(), eintragObjekt) != markiert.end()
                                      : (cd->nmcd.uItemState & CDIS_SELECTED) != 0;

            if (cd->nmcd.dwDrawStage == CDDS_ITEMPREPAINT && objektEintrag && !blau)
                cd->nmcd.uItemState &= ~(CDIS_SELECTED | CDIS_FOCUS);
            if (cd->nmcd.dwDrawStage == CDDS_ITEMPREPAINT && objektEintrag && eintragObjekt->versteckt && !blau)
                cd->clrText = FARBE_TEXT_GEDIMMT; // ausgeblendet: grau

            if (cd->nmcd.dwDrawStage == CDDS_ITEMPREPAINT && blau)
            {
                cd->nmcd.uItemState &= ~(CDIS_SELECTED | CDIS_FOCUS); // eigene Farben statt Design
                cd->clrText = RGB(255, 255, 255);
                cd->clrTextBk = FARBE_AKTIV;
            }
            return CDRF_DODEFAULT;
        }

        // Rechtsklick: Kontextmenue (vorher den Eintrag unter der Maus waehlen, wenn er nicht dazugehoert)
        if (hdr->code == NM_RCLICK)
        {
            TVHITTESTINFO info = {};
            GetCursorPos(&info.pt);
            POINT bildschirm = info.pt;
            ScreenToClient(hTreeObjects, &info.pt);
            if (HTREEITEM eintrag = TreeView_HitTest(hTreeObjects, &info))
            {
                TVITEMW item = {};
                item.mask = TVIF_PARAM;
                item.hItem = eintrag;
                SendMessageW(hTreeObjects, TVM_GETITEMW, 0, reinterpret_cast<LPARAM>(&item));
                object *o = nullptr;
                if (item.lParam >= 0 && item.lParam < EINTRAG_LICHT && size_t(item.lParam) < scene.size())
                    o = scene[size_t(item.lParam)].get();
                else if (item.lParam >= EINTRAG_KIND && size_t(item.lParam - EINTRAG_KIND) < eintraegeKinder.size())
                    o = eintraegeKinder[size_t(item.lParam - EINTRAG_KIND)].first;
                if (o && !istAusgewaehlt(o))
                {
                    auswahlSetzen({o});
                    abgleichen();
                }
            }

            objektKontextMenue(bildschirm);
            return 1;
        }

        // Klick auf das Kaestchen: ein-/ausblenden (der Haken wechselt erst nach dieser Meldung)
        if (hdr->code == NM_CLICK)
        {
            TVHITTESTINFO info = {};
            GetCursorPos(&info.pt);
            ScreenToClient(hTreeObjects, &info.pt);
            HTREEITEM eintrag = TreeView_HitTest(hTreeObjects, &info);
            if (eintrag && (info.flags & TVHT_ONITEMSTATEICON))
            {
                PostMessageW(hwnd, WM_SICHTBAR_PRUEFEN, 0, reinterpret_cast<LPARAM>(eintrag));
                return 0;
            }
        }

        // Klick auf ein Objekt: Anker fuer Shift+Klick merken; Shift+Klick: alle Objekte dazwischen auswaehlen
        if (hdr->code == NM_CLICK)
        {
            static object *anker = nullptr;
            TVHITTESTINFO info = {};
            GetCursorPos(&info.pt);
            ScreenToClient(hTreeObjects, &info.pt);
            HTREEITEM eintrag = TreeView_HitTest(hTreeObjects, &info);
            object *geklickt = nullptr; // Objekt direkt in der Szene oder Teil einer Gruppe
            if (eintrag && (info.flags & TVHT_ONITEM))
            {
                TVITEMW item = {};
                item.mask = TVIF_PARAM;
                item.hItem = eintrag;
                SendMessageW(hTreeObjects, TVM_GETITEMW, 0, reinterpret_cast<LPARAM>(&item));
                geklickt = eintragsObjekt(item.lParam);
            }
            if (geklickt && (GetKeyState(VK_SHIFT) & 0x8000))
            {
                // sichtbare Eintraege der Liste der Reihe nach (aufgeklappte Gruppen mit ihren Teilen)
                std::vector<object *> reihe;
                for (HTREEITEM e = TreeView_GetRoot(hTreeObjects); e; e = TreeView_GetNextVisible(hTreeObjects, e))
                {
                    TVITEMW item = {};
                    item.mask = TVIF_PARAM;
                    item.hItem = e;
                    SendMessageW(hTreeObjects, TVM_GETITEMW, 0, reinterpret_cast<LPARAM>(&item));
                    if (object *o = eintragsObjekt(item.lParam))
                        reihe.push_back(o);
                }
                object *start = anker ? anker // nur gueltig, wenn noch in der Liste (siehe unten)
                                : selectedType == SelectedType::OBJECT ? selectedObject // Anker aus den Ansichten
                                                                       : nullptr;
                auto posStart = std::find(reihe.begin(), reihe.end(), start);
                auto posZiel = std::find(reihe.begin(), reihe.end(), geklickt);
                std::vector<object *> bereich;
                if (posStart == reihe.end() || posZiel == reihe.end())
                    bereich.push_back(geklickt);
                else
                    bereich.assign(std::min(posStart, posZiel), std::max(posStart, posZiel) + 1);
                // Strg+Shift: zur bisherigen Auswahl dazu
                if (GetKeyState(VK_CONTROL) & 0x8000)
                    for (object *o : ausgewaehlteObjekte())
                        if (std::find(bereich.begin(), bereich.end(), o) == bereich.end())
                            bereich.push_back(o);
                // eine Gruppe und ihre eigenen Teile nicht zugleich (sonst bewegt sich ein Teil doppelt): nur die Gruppe
                std::vector<object *> ohneDoppelte;
                for (object *o : bereich)
                {
                    bool gruppeDabei = false;
                    for (Gruppe *gr = gruppeVon(o); gr && !gruppeDabei; gr = gruppeVon(gr))
                        gruppeDabei = std::find(bereich.begin(), bereich.end(), static_cast<object *>(gr)) != bereich.end();
                    if (!gruppeDabei)
                        ohneDoppelte.push_back(o);
                }
                // das angeklickte Objekt wird Hauptobjekt (Eigenschaften), der Anker bleibt
                auto hk = std::find(ohneDoppelte.begin(), ohneDoppelte.end(), geklickt);
                if (hk != ohneDoppelte.end())
                    std::rotate(ohneDoppelte.begin(), hk, hk + 1);
                auswahlSetzen(ohneDoppelte);
                abgleichen();
                return 1; // keine normale Auswahl in der Liste
            }
            if (geklickt)
                anker = geklickt;
        }

        // Strg+Klick auf ein Objekt: zur Auswahl hinzufuegen bzw. entfernen
        if (hdr->code == NM_CLICK && (GetKeyState(VK_CONTROL) & 0x8000))
        {
            TVHITTESTINFO info = {};
            GetCursorPos(&info.pt);
            ScreenToClient(hTreeObjects, &info.pt);
            HTREEITEM eintrag = TreeView_HitTest(hTreeObjects, &info);
            if (eintrag)
            {
                TVITEMW item = {};
                item.mask = TVIF_PARAM;
                item.hItem = eintrag;
                SendMessageW(hTreeObjects, TVM_GETITEMW, 0, reinterpret_cast<LPARAM>(&item));
                // Objekt direkt in der Szene oder Teil einer Gruppe (Arm, Bein einer Figur ...)
                if (object *o = eintragsObjekt(item.lParam))
                {
                    auswahlUmschalten(o);
                    abgleichen();
                    return 1; // keine normale Auswahl in der Liste
                }
            }
        }

        if (hdr->code == TVN_SELCHANGEDW && !auswahlWirdGesetzt)
        {
            NMTREEVIEWW *tv = reinterpret_cast<NMTREEVIEWW *>(lParam);
            auswahlAusListe(tv->itemNew.lParam);
        }

        // Entf loescht, F2 springt ins Namensfeld
        if (hdr->code == TVN_KEYDOWN)
        {
            WORD taste = reinterpret_cast<NMTVKEYDOWN *>(lParam)->wVKey;

            if (taste == VK_DELETE)
                AusgewaehltesObjektLoeschen();
            else if (taste == VK_SPACE) // Leertaste schaltet den Haken des gewaehlten Eintrags
                PostMessageW(hwnd, WM_SICHTBAR_PRUEFEN, 0, reinterpret_cast<LPARAM>(TreeView_GetSelection(hTreeObjects)));
            else if (taste == VK_F2 && IsWindowEnabled(g.info))
            {
                SetFocus(g.info);
                SendMessageW(g.info, EM_SETSEL, 0, -1);
            }
        }

        return 0;
    }

    case WM_CLOSE:
    {
        ShowWindow(hwnd, SW_HIDE);
        return 0;
    }
    }

    return DefWindowProcW(
        hwnd,
        msg,
        wParam,
        lParam);
}

//--------------------------------------------------
// Create Tool Window
//--------------------------------------------------

//--------------------------------------------------
// Tooltips: kurze Erklaerung und Taste zu jedem Button
//--------------------------------------------------

static HWND hTooltip = nullptr;

// Ein Tooltip je Fenster (Leiste, Objektfenster, Einstellungen), der diesem Fenster gehoert:
// so erscheint er immer davor. Vorher gehoerten alle der Leiste und lagen hinter dem
// Objektfenster bzw. dem Einstellungsfenster.
static HWND tooltipFuer(HWND hauptfenster)
{
    static std::unordered_map<HWND, HWND> tips;
    auto it = tips.find(hauptfenster);
    if (it != tips.end())
        return it->second;

    HWND tip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
                               CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                               hauptfenster, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (tip)
    {
        SendMessageW(tip, TTM_SETMAXTIPWIDTH, 0, 380); // lange Texte umbrechen (\n erlaubt)
        SendMessageW(tip, TTM_SETDELAYTIME, TTDT_AUTOPOP, 20000);
    }
    tips[hauptfenster] = tip;
    return tip;
}

// Tooltip eines Fensters nachtraeglich aendern (Eigenschaftszeilen: Bedeutung haengt von der Auswahl ab)
static bool tooltipAendern(HWND fenster, const wchar_t *text)
{
    if (!hTooltip || !fenster)
        return false;
    HWND tip = tooltipFuer(GetAncestor(fenster, GA_ROOT));
    if (!tip)
        return false;
    TOOLINFOW ti = {};
    ti.cbSize = TTTOOLINFOW_V2_SIZE;
    ti.hwnd = GetParent(fenster);
    ti.uId = reinterpret_cast<UINT_PTR>(fenster);
    ti.lpszText = const_cast<LPWSTR>(text);
    SendMessageW(tip, TTM_UPDATETIPTEXTW, 0, reinterpret_cast<LPARAM>(&ti));
    return true;
}


static void tooltip(HWND fenster, const wchar_t *text)
{
    if (!hTooltip || !fenster) // hTooltip: Tooltips sind angelegt
        return;
    HWND tip = tooltipFuer(GetAncestor(fenster, GA_ROOT));
    if (!tip)
        return;
    TOOLINFOW ti = {};
    ti.cbSize = TTTOOLINFOW_V2_SIZE; // passt zu alten und neuen Common Controls
    ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
    ti.hwnd = GetParent(fenster);
    ti.uId = reinterpret_cast<UINT_PTR>(fenster);
    ti.lpszText = const_cast<LPWSTR>(text); // wird kopiert
    SendMessageW(tip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&ti));
}

static void tooltipsAnlegen(HINSTANCE hInst)
{
    hTooltip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
                               CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                               hToolWindow, nullptr, hInst, nullptr);
    if (!hTooltip)
        return;
    SendMessageW(hTooltip, TTM_SETMAXTIPWIDTH, 0, 380); // lange Texte umbrechen (\n erlaubt)
    SendMessageW(hTooltip, TTM_SETDELAYTIME, TTDT_AUTOPOP, 15000);

    // Leiste
    tooltip(l.btnDatei, L"Datei: Szene als XML laden (L) / speichern (O, Strg+S),\n"
                        L"Objekt aus OBJ importieren (I) / als OBJ exportieren (E: Auswahl, ohne Auswahl die ganze Szene),\n"
                        L"Szene leeren (C), Beenden (Q)");
    tooltip(l.cmbBlock, L"Schnell-Rendering: ein Strahl-Ergebnis je Block aus n × n Pixeln.\n"
                        L"Block 3 × 3 ist etwa 9-mal schneller (gröberes Bild, Licht und Farben bleiben richtig).\n"
                        L"\"Voll\" = jeder Pixel einzeln (fertiges Bild). Wird mit der Szene gespeichert.");
    tooltip(l.btnUndo, L"Rückgängig (Strg+Z)");
    tooltip(l.btnRedo, L"Wiederholen (Strg+Y)");
    tooltip(l.btnRender, L"Raytracing im Rendering-Fenster starten (Y)");
    tooltip(l.btnStop, L"Raytracing anhalten (X)");
    tooltip(l.btnAutoRender, L"Autom. Rendern (Schalter): nach jeder Änderung und einer Wartezeit ohne weitere Eingabe\n"
                             L"(Render-Einstellungen → Qualität, Vorgabe 3 s) wird der geänderte Bildbereich im Hintergrund\n"
                             L"neu gerendert, z.B. nur der Würfel, der eine neue Farbe bekam. Kamera, Licht und Einstellungen:\n"
                             L"ganzes Bild. Eine neue Änderung hält das Rendern an und zeigt wieder das vorige Bild.");
    tooltip(l.btnFlat, L"Ansichten: gefüllte Flächen oder Drahtgitter (F)");
    tooltip(l.btnZentrieren, L"Ganze Szene zeigen: Kamera schräg von oben, Ansichten zentriert und passend gezoomt (Z)");
    tooltip(l.btnFokus, L"Ansichten und Kamera auf die Auswahl ausrichten (Punkt bzw. Ziffernblock-Komma)");
    tooltip(l.btnZoomMinus, L"Ortho-Ansichten verkleinern (− oder Mausrad)");
    tooltip(l.btnZoomPlus, L"Ortho-Ansichten vergrößern (+ oder Mausrad)");
    tooltip(l.btnObjekte, L"Objektfenster ein- oder ausblenden");
    tooltip(l.btnEinstellungen, L"Render-Einstellungen: Umgebungsbild, Belichtung, Qualität, Nebel, Rauch, Wolken");

    // Objektfenster
    tooltip(g.btnKopf, L"Objektfenster ausblenden");
    tooltip(g.btnNeu, L"Neues Grundobjekt, Text oder Licht einfügen");
    tooltip(g.btnKopie, L"Auswahl duplizieren, etwas versetzt (Strg+D)\nStrg+C / Strg+V: kopieren und einfügen");
    tooltip(g.btnLoeschen, L"Auswahl löschen (Entf)");
    tooltip(g.btnNext, L"Nächstes Objekt bzw. Licht auswählen (N)");
    tooltip(g.btnMove, L"Modus Verschieben (M): Ziehen verschiebt die Auswahl");
    tooltip(g.btnRotate, L"Modus Drehen (R): Ziehen dreht um die Mitte der Auswahl\nShift: 15°-Raster");
    tooltip(g.btnScale, L"Modus Skalieren (S): von der Mitte weg ziehen vergrößert, zur Mitte hin verkleinert\nOrtho-Ansichten: waagrecht und senkrecht getrennt; Shift: gleichmäßig");
    tooltip(g.btnKanten, L"Kanten-Modus: die Kanten des gewählten Netzes erscheinen gelb in den Ansichten.\n"
                         L"Klick auf eine Kante wählt sie (rot), Strg+Klick weitere. Dann Rechtsklick in der Ansicht oder Taste:\n"
                         L"Entf = löschen (Loch: Kante mit ihren Dreiecken), K = zusammenziehen, U = umklappen.\n"
                         L"Esc leert die Kantenauswahl bzw. beendet den Modus; Move/Rotate/Scale ebenso. Strg+Z = rückgängig.");

    static const wchar_t *schrittTipps[6] = {
        L"Ein Schritt entlang −X: verschieben, drehen oder skalieren je nach Modus (Ziffernblock 4)",
        L"Ein Schritt entlang +X: verschieben, drehen oder skalieren je nach Modus (Ziffernblock 6)",
        L"Ein Schritt entlang −Y: verschieben, drehen oder skalieren je nach Modus (Ziffernblock 8)",
        L"Ein Schritt entlang +Y: verschieben, drehen oder skalieren je nach Modus (Ziffernblock 2)",
        L"Ein Schritt entlang −Z: verschieben, drehen oder skalieren je nach Modus (Ziffernblock 1)",
        L"Ein Schritt entlang +Z: verschieben, drehen oder skalieren je nach Modus (Ziffernblock 9)"};
    for (int i = 0; i < 6; ++i)
        tooltip(g.btnSchritt[i], schrittTipps[i]);

    tooltip(g.editSchritt, L"Schrittweite für die Buttons −X … +Z und den Ziffernblock\n(je Modus: Einheiten, Grad oder Prozent)");
    tooltip(g.btnFarbe, L"Farbe im Farbdialog wählen. Objekt: Diffus-Farbe (Grundfarbe im Licht).\n"
                         L"Licht: Lichtfarbe; multipliziert alles, was dieses Licht beleuchtet (Stärke separat in \"Leistung\").");
    tooltip(g.info, L"Name (beim 3D-Text: der Text); Enter übernimmt");

    tooltip(g.btnReiter[0], L"Lage, Größe, Drehung, Farbe und Name der Auswahl");
    tooltip(g.btnReiter[1], L"Material der Auswahl: Vorlage, Farben, Spiegelung, Transparenz, Textur …");
    tooltip(g.btnReiter[2], L"Netz-Operationen: Objekte in Flächen zerlegen, Dreiecke optimieren, verfeinern, glätten");
    tooltip(g.cmbManipArt, L" "); // Text je Aktion (manipAnzeigen)
    tooltip(g.lblManipZiel, L"Die Aktion wirkt auf die ausgewählten Objekte (in der Liste oder einer Ansicht anklicken, Strg = mehrere).\n"
                            L"Nur Objekte aus Dreiecken: Würfel, Wand, Körper, Terrain, OBJ, Rotationsobjekt; Kugeln und Stäbe sind exakt gerechnet.");
    tooltip(g.editManipTiefe, L"Porath-Triangulation: wie oft jedes Dreieck noch von seinem Mittelpunkt aus in drei geteilt wird\n"
                              L"(0 = nur der Fächer vom Flächenmittelpunkt; jede Stufe verdreifacht die Dreiecke, höchstens 6).");
    tooltip(g.editManipKnick, L"Glatt schattieren: Nachbarflächen, deren Normale um mehr als diesen Winkel abweicht, werden nicht\n"
                              L"gemittelt – dort bleibt die Kante scharf. 30–45 = Kanten scharf, Rundungen weich; 180 = alles weich;\n"
                              L"0 = Ecknormalen entfernen (wieder flach/facettiert).");
    tooltip(g.btnManipAusfuehren, L"Gewählte Aktion auf die Auswahl anwenden (Rückgängig mit Strg+Z)");
    for (int i = 0; i < ANZAHL_MANIP_ARTEN && i < 24; ++i)
        tooltip(g.btnManipIcon[i], manipSymbolTipps[i].c_str());
    // Beschriftungen zeigen denselben Tooltip wie ihr Feld (dafuer muessen sie Mausereignisse melden)
    for (HWND l : {g.lblManipTiefe, g.lblManipKnick, g.lblManipWert, g.lblManipModus, g.lblManipKopien, g.lblManipLage,
                   g.lblManipPinsel, g.lblManipAus, g.lblManipZiel, g.lblManipArt})
    {
        SetWindowLongPtrW(l, GWL_STYLE, GetWindowLongPtrW(l, GWL_STYLE) | SS_NOTIFY);
        tooltip(l, L" ");
    }
    tooltipAendern(g.lblManipTiefe, L"Porath-Triangulation: wie oft jedes Dreieck noch von seinem Mittelpunkt aus in drei geteilt wird");
    tooltipAendern(g.lblManipKnick, L"Glatt schattieren: Kanten, deren Flächen stärker als dieser Winkel abknicken, bleiben scharf");
    tooltipAendern(g.lblManipModus, L"Öffnung: Umriss-Quader des Schneiders ganz durch die Wand (Tür-/Fensterrahmen);\n"
                                    L"Form: genau die Form des Schneiders");
    tooltipAendern(g.lblManipKopien, L"Echte Kopien oder Instanzen (verknüpfte Kopien, die die Geometrie teilen)");
    tooltipAendern(g.lblManipLage, L"Stabil hinlegen: Objekt vor dem Absetzen auf seine nächste flache Seite kippen");
    tooltipAendern(g.lblManipPinsel, L"Art des Terrain-Pinsels");
    tooltipAendern(g.lblManipZiel, L"Die Aktion wirkt auf die ausgewählten Objekte (Liste oder Ansicht; Strg/Shift = mehrere)");
    {
        static const wchar_t *ausTipps[3] = {
            L"Zufällige Verteilung: Breite des Verteilungsgebiets in X (links–rechts), zentriert um die Mitte der Auswahl.\n"
            L"0 = in X nicht verteilen. Die Größe der Szene steht im Hinweistext darunter.",
            L"Zufällige Verteilung: Höhe des Verteilungsgebiets in Y (unten–oben).\n"
            L"0 = nur in der Ebene verteilen; jedes Objekt behält seine Höhe (z.B. Steine bleiben auf dem Boden).",
            L"Zufällige Verteilung: Tiefe des Verteilungsgebiets in Z (vorne–hinten).\n"
            L"0 = in Z nicht verteilen (z.B. nur entlang einer Linie in X)."};
        for (int i = 0; i < 3; ++i)
            tooltip(g.editManipAus[i], ausTipps[i]);
    }
    tooltip(g.btnZiel, L"Auf ein Objekt richten: Kamera (LookAt = Objektmitte) bzw. Richtungslicht (Licht läuft vom Symbol zum Objekt)");
    tooltip(hTreeObjects, L"Haken: Objekt in den Ansichten zeigen / ausblenden (Leertaste schaltet den gewählten Eintrag).\n"
                          L"Ausgeblendete Objekte werden trotzdem gerendert.");
    tooltip(g.cmbVorlage, L"Materialvorlage übernehmen (danach weiter anpassbar): Metalle, Kunststoff, Glas, Edelsteine,\n"
                          L"Holz, Stein, Stoff, Leuchtendes … Glas-Farben hängen von der Objektgröße ab (Absorption je Einheit).");
    static const wchar_t *farbTipps[MAT_FARBEN] = {
        L"Umgebung (ambient): Grundhelligkeit der Fläche ohne jedes Licht, mal kA. Wirkt nur, wenn in den\n"
        L"Render-Einstellungen \"Indirektes Licht\" aus ist; mit indirektem Licht kommt die Grundhelligkeit\n"
        L"von Himmel und Umgebung (dann ohne Wirkung, sonst wäre sie doppelt). Mit Textur: in deren Farbe.",
        L"Diffus: Grundfarbe der matten Fläche. Wirkt im direkten Licht (mal kD) und im indirekten Licht:\n"
        L"bestimmt auch, welche Farbe die Fläche an ihre Umgebung weitergibt (Farbbluten, z.B. rote Wand).\n"
        L"Bei Metallen ein dunkler Grundton, bei klarem Glas schwarz.",
        L"Glanz (specular): Farbe der Glanzpunkte von Lichtern (mit Glanzstärke und Glanzschärfe).\n"
        L"Ist Spiegelung > 0, gibt es keine Glanzpunkte; dann ist \"Glanz\" die Farbe der Spiegelung (Metall).",
        L"Leuchten (emission): eigene Helligkeit, unabhängig von Licht, Spiegelung und Transparenz.\n"
        L"Werte über 1 leuchten kräftig (Neon). Beleuchtet die Umgebung nur über das indirekte Licht\n"
        L"(begrenzt durch die Ausreißer-Grenze); für echte Lampen besser ein Flächen- oder Mesh-Licht.",
        L"Absorption: nur bei Transparenz. Wie stark Rot, Grün, Blau je Einheit Weg im Material geschluckt werden\n"
        L"(e hoch −Absorption · Weg): färbt Glas, Wasser, Wein, Edelsteine; dicke Stellen werden dunkler.\n"
        L"Hängt von der Objektgröße ab: bei großen Objekten kleinere Werte."};
    for (int i = 0; i < MAT_FARBEN; ++i)
        tooltip(g.btnMatFarbe[i], farbTipps[i]);
    static const wchar_t *wertTipps[MAT_WERTE] = {
        L"Spiegelung (0 bis 1): Anteil, der wie ein Spiegel die Umgebung zeigt; der Rest ist matte Fläche.\n"
        L"1 = perfekter Spiegel. Mit Glanz-Farbe getönt (Metall). Mit Brechung > 0 kommt die Fresnel-Spiegelung dazu\n"
        L"(flach betrachtet stärker). Größer als 0: keine Glanzpunkte von Punkt- und Richtungslichtern.",
        L"Transparenz (0 bis 1): Anteil, der durch das Material hindurchgeht und dabei gebrochen wird (Brechung).\n"
        L"1 = klares Glas/Wasser: die Fläche selbst wird nicht beleuchtet, nur Spiegelung und Durchsicht.\n"
        L"Transparente Objekte lassen auch Schattenstrahlen anteilig durch (heller Schatten).",
        L"Brechung (Brechungsindex, IOR): wie stark durchsichtiges Material die Strahlen knickt und wie stark es\n"
        L"flach betrachtet spiegelt (Fresnel). 0 = keine, Luft 1, Wasser 1.33, Glas 1.5, Diamant 2.42.\n"
        L"Auch bei undurchsichtigen Materialien: > 1 gibt Lack/Kunststoff eine Spiegelung an flachen Stellen.",
        L"Rauheit (0 bis 1): streut die Spiegelung. 0 = gestochen scharf, 0.2 = gebürstet/unscharf, 1 = sehr diffus.\n"
        L"Größer als 0 rauscht: braucht mehr Abtastungen (adaptiv).",
        L"Glanzschärfe (shininess): Größe der Glanzpunkte von Lichtern. Klein (5-20) = breiter, matter Schimmer,\n"
        L"groß (200-1000) = kleiner, scharfer Punkt (Lack, Keramik). Nur bei Spiegelung = 0.",
        L"Glanzstärke (specularStrength): Helligkeit der Glanzpunkte von Lichtern (0 = keine). Nur bei Spiegelung = 0.",
        L"kA (Anteil Umgebungslicht): Faktor für die Farbe \"Umgebung\" (Umgebung × kA wird addiert).\n"
        L"Wirkt nur bei ausgeschaltetem \"Indirektes Licht\" (Render-Einstellungen).",
        L"kD (Anteil diffuses Licht): Faktor für das direkte Licht auf der matten Fläche (Vorgabe 0.8-0.9).\n"
        L"Kleiner = Fläche erscheint im direkten Licht dunkler; das indirekte Licht nutzt nur die Diffus-Farbe."};
    for (int i = 0; i < MAT_WERTE; ++i)
        tooltip(g.editMatWert[i], wertTipps[i]);
    tooltip(g.editTextur, L"Textur: angezeigt wird der Dateiname. Eingeben: Name aus textures.txt oder Pfad einer BMP-Datei;\n"
                          L"leer = keine Textur; Enter übernimmt. Bequemer: Knopf \u2026");
    tooltip(g.btnTexturDatei, L"Bilddatei als Textur wählen (BMP, PNG, JPG, GIF, TIFF; PNG mit Transparenz schneidet aus)");
    tooltip(g.editTexturKachel, L"Größe einer Bildkachel in Metern: das Bild wiederholt sich (z.B. Fliesen 0,3, Parkett 1).\n"
                                L"0 = das Bild einmal über das ganze Objekt (wie bisher). Bei Kugel und Zylinder wird auf ganze\n"
                                L"Wiederholungen gerundet (keine Naht). Gilt nicht für die Abbildung UV.");
    tooltip(g.editTexturWinkel, L"Bild frei drehen (Grad, um die Bildmitte), zusätzlich zu \"Drehen 90°\"");
    tooltip(g.editTexturVersatz[0], L"Bild nach links/rechts verschieben, in Bildbreiten (0,5 = halbes Bild)");
    tooltip(g.editTexturVersatz[1], L"Bild nach oben/unten verschieben, in Bildhöhen");
    tooltip(g.btnTexturFest, L"Haftet am Objekt: die Textur dreht und bewegt sich mit dem Objekt (auch in Animationen und\n"
                             L"bei Instanzen). Einschalten macht die jetzige Lage zur Ausgangslage. Aus: Projektion in der Welt\n"
                             L"(die Textur bleibt stehen, wenn das Objekt gedreht wird). Neue Texturen haften.");
    tooltip(g.editTexturKugel[0], L"Kugel-Abbildung drehen: Grad um X (nur bei Abbildung Kugel)");
    tooltip(g.editTexturKugel[1], L"Kugel-Abbildung drehen: Grad um Y (z.B. Weltkarte nach Osten drehen)");
    tooltip(g.editTexturKugel[2], L"Kugel-Abbildung drehen: Grad um Z");
    tooltip(g.cmbTexturBild, L"Weiteres Bild mit derselben Abbildung wie die Textur:\n"
                             L"Relief = Höhenbild in Graustufen (hell erhaben), wirkt auf das Licht (Fugen, Rinde, Mauerwerk)\n"
                             L"Alpha = dunkle Stellen werden ausgeschnitten (Blätter, Zaun, Gitter; auch für Schatten)\n"
                             L"Glanz = hell glänzend/spiegelnd, dunkel matt\n"
                             L"Leuchten = leuchtende Stellen (Fenster bei Nacht, Bildschirm)\n"
                             L"Unterseite = Farbe unterhalb einer Höhe (weißer Bauch, helle Brust; Stärke = Höhe der Grenze\n"
                             L"0 bis 1 von unten, z.B. 0,35), mit weichem, fransigem Übergang. Sichtbar im Render-Bild\n"
                             L"Ohne Textur-Abbildung gilt \"Würfel weich\".");
    tooltip(g.editTexturBild, L"Datei des gewählten weiteren Bilds; leeren = entfernen");
    tooltip(g.btnTexturBild, L"Datei für das gewählte weitere Bild wählen");
    tooltip(g.editTexturStaerke, L"Relief: Tiefe in Metern (z.B. 0,01 für Fugen); Glanz: 0 bis 1; Leuchten: Helligkeit;\n"
                                 L"Unterseite: Höhe der Grenze von unten, 0 bis 1 (0,35 = unteres gutes Drittel)");
    tooltip(g.btnTexturU, L"Textur links/rechts spiegeln");
    tooltip(g.btnTexturV, L"Textur oben/unten spiegeln");
    tooltip(g.btnTexturDrehen, L"Textur um 90 Grad drehen (viermal = wieder wie vorher)");
    {
        static const wchar_t *unten = L"Unterster Punkt der Auswahl (Mitte der Unterseite): Y = tiefste Stelle.\n"
                                      L"Y ändern verschiebt senkrecht, z.B. Y = 0 setzt das Objekt auf den Boden.";
        static const wchar_t *oben = L"Oberster Punkt der Auswahl (Mitte der Oberseite): Y = höchste Stelle.\n"
                                     L"Ändern verschiebt das Objekt so, dass dieser Punkt dort liegt.";
        tooltip(g.lblZeile5, unten);
        tooltip(g.lblZeile6, oben);
        for (int k = 0; k < 3; ++k)
        {
            tooltip(g.edit[12 + k], unten);
            tooltip(g.edit[15 + k], oben);
        }
        // uebrige Zeilen: Tooltip anlegen (Text setzt zeilenTippsSetzen je nach Auswahl)
        for (HWND h : {g.lblZeile1, g.lblZeile2, g.lblZeile3, g.lblZeile4})
        {
            SetWindowLongPtrW(h, GWL_STYLE, GetWindowLongPtrW(h, GWL_STYLE) | SS_NOTIFY);
            tooltip(h, L" ");
        }
        for (HWND h : {g.lblZeile5, g.lblZeile6})
            SetWindowLongPtrW(h, GWL_STYLE, GetWindowLongPtrW(h, GWL_STYLE) | SS_NOTIFY);
        for (int i = 0; i < 12; ++i)
            tooltip(g.edit[i], L" ");
        zeilenTippsSetzen();
    }
    tooltip(g.cmbTexturModus, L"Wie die Textur aufs Objekt gelegt wird: Kugel = rundherum, Ebene = von der flachsten Seite her\n"
                              L"(Boden von oben, Wand von vorne), UV = Koordinaten aus der OBJ-Datei, Zylinder = um die senkrechte Achse,\n"
                              L"Würfel = jede Seite aus ihrer eigenen Richtung (gerade, ohne Verzerrung),\n"
                              L"Würfel weich = wie Würfel, an den Kanten weich gemischt (Steine, Terrain, Felsen ohne Nähte)");
}

void CreateToolWindow(HINSTANCE hInst)
{
    INITCOMMONCONTROLSEX icc{};
    icc.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icc.dwICC = ICC_TREEVIEW_CLASSES | ICC_BAR_CLASSES | ICC_PROGRESS_CLASS; // Baumliste, Tooltips, Fortschrittsbalken

    InitCommonControlsEx(&icc);

    // Dunkle Kontextmenues im dunklen Windows-Modus (uxtheme, ab Windows 10 1903; sonst ohne Wirkung)
    if (HMODULE ux = LoadLibraryW(L"uxtheme.dll"))
    {
        using SetPreferredAppModeFn = int(WINAPI *)(int);
        using FlushMenuThemesFn = void(WINAPI *)();
        auto modus = (SetPreferredAppModeFn)(void *)GetProcAddress(ux, MAKEINTRESOURCEA(135));
        auto flush = (FlushMenuThemesFn)(void *)GetProcAddress(ux, MAKEINTRESOURCEA(136));
        if (modus)
            modus(1); // AllowDark
        if (flush)
            flush();
    }

    pinselHintergrund = CreateSolidBrush(FARBE_HINTERGRUND);
    pinselFlaeche = CreateSolidBrush(FARBE_FLAECHE);

    editorEinstellungenLaden(); // zuletzt benutzte Bildgroesse
    hauptfensterAnlegen(hInst); // Rahmen fuer Leiste, Ansichten, Objekt-, Zeit- und Statusleiste
    if (hHauptfenster)
    {
        WNDCLASSW wcr{};
        wcr.lpfnWndProc = ReiterProc;
        wcr.hInstance = hInst;
        wcr.hCursor = LoadCursor(nullptr, IDC_HAND);
        wcr.lpszClassName = L"SZENEN_REITER";
        RegisterClassW(&wcr);
        hReiter = CreateWindowExW(0, L"SZENEN_REITER", L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, 0, 0, 400,
                                  REITER_H, hHauptfenster, nullptr, hInst, nullptr);
    }

    //--------------------------------------------------
    // Font
    //--------------------------------------------------

    gFont =
        CreateFontW(
            -15,
            0,
            0,
            0,
            FW_NORMAL,
            FALSE,
            FALSE,
            FALSE,
            DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY,
            DEFAULT_PITCH,
            L"Segoe UI");

    // Symbole der Windows-Symbolschrift (Windows 11: Fluent, Windows 10: MDL2); fehlt beides,
    // zeigen die Buttons nur Text
    for (const wchar_t *name : {L"Segoe Fluent Icons", L"Segoe MDL2 Assets"})
    {
        if (!schriftVorhanden(name))
            continue;

        symbolFont = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                 OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                 DEFAULT_PITCH, name);
        break;
    }

    //--------------------------------------------------
    // Window Class (fuer beide Fenster)
    //--------------------------------------------------

    WNDCLASSW wc{};
    wc.lpfnWndProc = ToolWndProc;
    wc.hInstance = hInst;
    wc.hbrBackground = pinselHintergrund;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"MY_TOOL_WINDOW";

    RegisterClassW(&wc);

    //--------------------------------------------------
    // Leiste ohne Titel; Position und Breite setzt WerkzeugleisteAndocken()
    //--------------------------------------------------

    hToolWindow =
        CreateWindowExW(
            0,
            L"MY_TOOL_WINDOW",
            L"Scene Tools",
            WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, // im Hauptfenster
            0,
            0,
            1200,
            LEISTE_H,
            hHauptfenster,
            nullptr,
            hInst,
            nullptr);

    if (!hToolWindow)
    {
        MessageBoxW(
            nullptr,
            L"Tool Window konnte nicht erstellt werden.",
            L"Fehler",
            MB_OK | MB_ICONERROR);

        return;
    }

    // Import/Export: ein Menue "Datei" statt vier Knoepfen
    l.btnImport = l.btnExport = l.btnLaden = l.btnSave = nullptr;
    l.btnDatei = CreateStyledButton(hToolWindow, hInst, ID_BTN_DATEI, L"Datei \u25BE", 0, 0, 10, 10);
    // Blockgroesse fuer das Schnell-Rendering (vorher im Fenster Render-Einstellungen)
    l.cmbBlock = CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_TABSTOP | CBS_DROPDOWNLIST,
                                 0, 0, 10, 10, hToolWindow, (HMENU)(INT_PTR)ID_CMB_BLOCK, hInst, nullptr);
    SendMessageW(l.cmbBlock, WM_SETFONT, (WPARAM)gFont, TRUE);
    SetWindowTheme(l.cmbBlock, L"DarkMode_CFD", nullptr);
    for (int b : BLOCK_GROESSEN)
    {
        std::wstring t = b == 1 ? std::wstring(L"Voll (1 × 1)") : L"Block " + std::to_wstring(b) + L" × " + std::to_wstring(b);
        SendMessageW(l.cmbBlock, CB_ADDSTRING, 0, (LPARAM)t.c_str());
    }
    blockAuswahlAnzeigen();
    l.btnRender = CreateStyledButton(hToolWindow, hInst, ID_BTN_RENDER, L"Render", 0, 0, 10, 10);
    l.btnStop = CreateStyledButton(hToolWindow, hInst, ID_BTN_STOP, L"Stop", 0, 0, 10, 10);
    l.btnAutoRender = CreateStyledButton(hToolWindow, hInst, ID_BTN_AUTO_RENDER, L"Autom. Rendern", 0, 0, 10, 10);
    menueLeisteAnlegen(); // echte Menueleiste am Hauptfenster (statt "Datei ▾")
    l.btnRenderfenster = nullptr; // entfaellt: Reiter "Ansichten | Rendering"
    l.btnFlat = CreateStyledButton(hToolWindow, hInst, ID_BTN_FLAT, useFlat ? L"Flat" : L"Draht", 0, 0, 10, 10);
    angezeigtFlat = useFlat;
    l.btnZentrieren = CreateStyledButton(hToolWindow, hInst, ID_BTN_ZENTRIEREN, L"Zentrieren", 0, 0, 10, 10);
    l.btnFokus = CreateStyledButton(hToolWindow, hInst, ID_BTN_FOKUS, L"Fokus", 0, 0, 10, 10);
    l.btnUndo = CreateStyledButton(hToolWindow, hInst, ID_BTN_UNDO, L"", 0, 0, 10, 10);
    l.btnEinstellungen = CreateStyledButton(hToolWindow, hInst, ID_BTN_EINSTELLUNGEN, L"", 0, 0, 10, 10);
    l.btnRedo = CreateStyledButton(hToolWindow, hInst, ID_BTN_REDO, L"", 0, 0, 10, 10);
    l.btnZoomMinus = CreateStyledButton(hToolWindow, hInst, ID_BTN_ZOOM_MINUS, L"", 0, 0, 10, 10);
    l.btnZoomPlus = CreateStyledButton(hToolWindow, hInst, ID_BTN_ZOOM_PLUS, L"", 0, 0, 10, 10);
    l.btnClear = l.btnQuit = nullptr; // jetzt im Menue "Datei"
    l.btnObjekte = CreateStyledButton(hToolWindow, hInst, ID_BTN_KOPF, L"", 0, 0, 10, 10);

    //--------------------------------------------------
    // Objektfenster: gehoert der Leiste (liegt immer darueber, folgt ihr in der Z-Reihenfolge)
    //--------------------------------------------------

    hObjektFenster =
        CreateWindowExW(
            0,
            L"MY_TOOL_WINDOW",
            L"Objekte",
            WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, // im Hauptfenster, rechts
            0,
            0,
            PANEL_W,
            panelHoehe,
            hHauptfenster,
            nullptr,
            hInst,
            nullptr);

    if (hHauptfenster && hObjektFenster)
    {
        WNDCLASSW wcr{};
        wcr.lpfnWndProc = ObjektRahmenProc;
        wcr.hInstance = hInst;
        wcr.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wcr.lpszClassName = L"SZENEN_OBJEKT_ROLLEN";
        RegisterClassW(&wcr);
        hObjektRahmen = CreateWindowExW(0, L"SZENEN_OBJEKT_ROLLEN", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL |
                                                                          WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
                                        0, 0, PANEL_W, 400, hHauptfenster, nullptr, hInst, nullptr);
        SetParent(hObjektFenster, hObjektRahmen); // Objektfenster rollt darin
        SetWindowTheme(hObjektRahmen, L"DarkMode_Explorer", nullptr); // dunkle Bildlaufleisten (ab Windows 10)

        WNDCLASSW wct{};
        wct.style = CS_DBLCLKS;
        wct.lpfnWndProc = ObjektTeilerProc;
        wct.hInstance = hInst;
        wct.hCursor = LoadCursor(nullptr, IDC_SIZEWE);
        wct.lpszClassName = L"SZENEN_OBJEKT_TEILER";
        RegisterClassW(&wct);
        hObjektTeiler = CreateWindowExW(0, L"SZENEN_OBJEKT_TEILER", L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, 0, 0,
                                        TEILER_B, 400, hHauptfenster, nullptr, hInst, nullptr);
    }

    g.btnKopf = CreateStyledButton(hObjektFenster, hInst, ID_BTN_ZUKLAPPEN, L"OBJEKTE", 0, 0, 10, 10);
    g.btnNeu = CreateStyledButton(hObjektFenster, hInst, ID_BTN_NEU, L"Neu", 0, 0, 10, 10);
    g.btnKopie = CreateStyledButton(hObjektFenster, hInst, ID_BTN_KOPIE, L"Kopie", 0, 0, 10, 10);
    g.btnLoeschen = CreateStyledButton(hObjektFenster, hInst, ID_BTN_LOESCHEN, L"Delete", 0, 0, 10, 10);
    g.btnNext = CreateStyledButton(hObjektFenster, hInst, ID_BTN_NEXT, L"Next", 0, 0, 10, 10);
    g.btnMove = CreateStyledButton(hObjektFenster, hInst, ID_BTN_MOVE, L"Move", 0, 0, 10, 10);
    g.btnRotate = CreateStyledButton(hObjektFenster, hInst, ID_BTN_ROTATE, L"Rotate", 0, 0, 10, 10);
    g.btnScale = CreateStyledButton(hObjektFenster, hInst, ID_BTN_SCALE, L"Scale", 0, 0, 10, 10);
    g.btnKanten = CreateStyledButton(hObjektFenster, hInst, ID_BTN_KANTEN, L"Kanten", 0, 0, 10, 10);

    const wchar_t *schrittText[6] = {L"\u2212X", L"+X", L"\u2212Y", L"+Y", L"\u2212Z", L"+Z"};
    for (int i = 0; i < 6; ++i)
        g.btnSchritt[i] = CreateStyledButton(hObjektFenster, hInst, ID_BTN_SCHRITT_ERSTES + i, schrittText[i], 0, 0, 10, 10);
    g.lblSchritt = CreateLabel(hObjektFenster, hInst, L"Schritt", 0, 0, 10, 10);
    g.editSchritt = CreateEingabefeld(hObjektFenster, hInst, ID_EDIT_SCHRITT, ES_RIGHT);
    g.lblEinheit = CreateLabel(hObjektFenster, hInst, L"", 0, 0, 10, 10);

    hTreeObjects =
        CreateWindowExW(
            0,
            WC_TREEVIEWW,
            L"",
            WS_VISIBLE |
                WS_CHILD |
                WS_BORDER |
                WS_TABSTOP |
                TVS_HASLINES |
                TVS_LINESATROOT |
                TVS_HASBUTTONS |
                TVS_SHOWSELALWAYS |
                TVS_FULLROWSELECT,
            0,
            0,
            10,
            10,
            hObjektFenster,
            (HMENU)(INT_PTR)ID_LIST_OBJECTS,
            hInst,
            nullptr);

    SetWindowSubclass(hTreeObjects, ListeTastenProc, 2, 0); // Pfeile/Bild-Tasten bewegen die Auswahl

    // dunkle Bildlaufleisten ab Windows 10, sonst bleibt das normale Explorer-Design
    if (FAILED(SetWindowTheme(hTreeObjects, L"DarkMode_Explorer", nullptr)))
        SetWindowTheme(hTreeObjects, L"Explorer", nullptr);

    // Haken je Objekt: sichtbar in den Ansichten (Stil erst nach dem Anlegen setzen, sonst fehlen die Bilder)
    SetWindowLongPtrW(hTreeObjects, GWL_STYLE, GetWindowLongPtrW(hTreeObjects, GWL_STYLE) | TVS_CHECKBOXES);

    TreeView_SetBkColor(hTreeObjects, FARBE_FLAECHE);
    TreeView_SetTextColor(hTreeObjects, FARBE_TEXT);
    TreeView_SetLineColor(hTreeObjects, FARBE_BAUMLINIEN);
    schriftSetzen(hTreeObjects);

    g.lblEigenschaften = CreateLabel(hObjektFenster, hInst, L"EIGENSCHAFTEN", 0, 0, 10, 10);
    g.lblName = CreateLabel(hObjektFenster, hInst, L"Name", 0, 0, 10, 10);
    g.info = CreateEingabefeld(hObjektFenster, hInst, ID_EDIT_NAME, ES_LEFT);
    SendMessageW(g.info, EM_LIMITTEXT, 200, 0);
    g.lblDetails = CreateLabel(hObjektFenster, hInst, L"", 0, 0, 10, 10);

    g.lblZeile1 = CreateLabel(hObjektFenster, hInst, L"Position", 0, 0, 10, 10);
    g.lblZeile2 = CreateLabel(hObjektFenster, hInst, L"Farbe", 0, 0, 10, 10);
    g.lblZeile3 = CreateLabel(hObjektFenster, hInst, L"Drehung", 0, 0, 10, 10);
    g.lblZeile4 = CreateLabel(hObjektFenster, hInst, L"Größe", 0, 0, 10, 10);
    g.lblZeile5 = CreateLabel(hObjektFenster, hInst, L"Unterster", 0, 0, 10, 10);
    g.lblZeile6 = CreateLabel(hObjektFenster, hInst, L"Oberster", 0, 0, 10, 10);
    for (HWND l : {g.lblZeile5, g.lblZeile6})
        SetWindowLongPtrW(l, GWL_STYLE, GetWindowLongPtrW(l, GWL_STYLE) | SS_NOTIFY); // Tooltip ueber der Beschriftung
    for (int i = 0; i < ANZAHL_EDITS; ++i)
        g.edit[i] = CreateEingabefeld(hObjektFenster, hInst, ID_EDIT_ERSTES + i, ES_RIGHT);
    g.btnFarbe = CreateStyledButton(hObjektFenster, hInst, ID_BTN_FARBE, L"", 0, 0, 10, 10);
    g.btnZiel = CreateStyledButton(hObjektFenster, hInst, ID_BTN_ZIEL, L"\u25BE", 0, 0, 10, 10);
    ShowWindow(g.btnZiel, SW_HIDE);
    ShowWindow(g.lblEigenschaften, SW_HIDE); // ersetzt durch die Reiter

    // ---------- Reiter und Material ----------
    g.btnReiter[0] = CreateStyledButton(hObjektFenster, hInst, ID_BTN_REITER_EIGENSCHAFTEN, L"Eigenschaften", 0, 0, 10, 10);
    g.btnReiter[1] = CreateStyledButton(hObjektFenster, hInst, ID_BTN_REITER_MATERIAL, L"Material", 0, 0, 10, 10);
    g.btnReiter[2] = CreateStyledButton(hObjektFenster, hInst, ID_BTN_REITER_MANIPULATION, L"Manipulation", 0, 0, 10, 10);

    auto auswahlListe = [&](int id)
    {
        HWND h = CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | WS_VSCROLL | WS_TABSTOP | CBS_DROPDOWNLIST,
                                 0, 0, 10, 10, hObjektFenster, (HMENU)(INT_PTR)id, hInst, nullptr);
        SendMessageW(h, WM_SETFONT, (WPARAM)gFont, TRUE);
        SetWindowTheme(h, L"DarkMode_CFD", nullptr); // dunkel ab Windows 10 1809, sonst ohne Wirkung
        return h;
    };

    // Reiter "Manipulation"
    g.lblManipArt = CreateLabel(hObjektFenster, hInst, L"Aktion", 0, 0, 10, 10);
    g.cmbManipArt = auswahlListe(ID_CMB_MANIP_ART);
    for (const auto &a : MANIP_ARTEN)
        SendMessageW(g.cmbManipArt, CB_ADDSTRING, 0, (LPARAM)a.name);
    SendMessageW(g.cmbManipArt, CB_SETCURSEL, 0, 0);
    ShowWindow(g.cmbManipArt, SW_HIDE); // Auswahl ueber die Symbole
    {
        std::wstring *tipps = manipSymbolTipps;
        for (int i = 0; i < ANZAHL_MANIP_ARTEN && i < 24; ++i)
        {
            g.btnManipIcon[i] = CreateStyledButton(hObjektFenster, hInst, ID_BTN_MANIP_ICON + i, L"", 0, 0, 10, 10);
            // Name und der erste Satz der Beschreibung
            std::wstring info = MANIP_ARTEN[i].info;
            size_t ende = info.find_first_of(L".\n");
            while (ende != std::wstring::npos && ende + 1 < info.size() && info[ende] == L'.' && info[ende + 1] != L' ' &&
                   info[ende + 1] != L'\n')
                ende = info.find_first_of(L".\n", ende + 1); // z.B. "z.B." ueberspringen
            tipps[i] = std::wstring(MANIP_ARTEN[i].name) + L"\n" + info.substr(0, ende == std::wstring::npos ? info.size() : ende + 1);
            // angemeldet wird in tooltipsAnlegen (das Tooltip-Fenster entsteht erst nach dem Objektfenster)
        }
    }
    g.lblManipObjekt = CreateLabel(hObjektFenster, hInst, L"Objekt", 0, 0, 10, 10);
    g.cmbManipObjekt = auswahlListe(ID_CMB_MANIP_OBJEKT);
    ShowWindow(g.cmbManipObjekt, SW_HIDE);
    g.lblManipZiel = CreateLabel(hObjektFenster, hInst, L"", 0, 0, 10, 10);
    g.lblManipTiefe = CreateLabel(hObjektFenster, hInst, L"Rekursionstiefe", 0, 0, 10, 10);
    g.editManipTiefe = CreateEingabefeld(hObjektFenster, hInst, ID_EDIT_MANIP_TIEFE, ES_RIGHT);
    textSetzen(g.editManipTiefe, L"1");
    g.lblManipKnick = CreateLabel(hObjektFenster, hInst, L"Knickwinkel (°)", 0, 0, 10, 10);
    g.editManipKnick = CreateEingabefeld(hObjektFenster, hInst, ID_EDIT_MANIP_KNICK, ES_RIGHT);
    textSetzen(g.editManipKnick, L"40");
    g.lblManipWert = CreateLabel(hObjektFenster, hInst, L"Dicke (Einheiten)", 0, 0, 10, 10);
    g.editManipWert = CreateEingabefeld(hObjektFenster, hInst, ID_EDIT_MANIP_WERT, ES_RIGHT);
    g.lblManipModus = CreateLabel(hObjektFenster, hInst, L"Ausschneiden", 0, 0, 10, 10);
    g.cmbManipModus = auswahlListe(ID_CMB_MANIP_MODUS);
    for (const wchar_t *t : {L"Öffnung (Umriss-Quader), Schneider bleibt", L"Form, Schneider bleibt", L"Form, Schneider entfernen"})
        SendMessageW(g.cmbManipModus, CB_ADDSTRING, 0, (LPARAM)t);
    SendMessageW(g.cmbManipModus, CB_SETCURSEL, 0, 0);
    tooltip(g.cmbManipModus, L"Öffnung: Loch in der Größe des Umriss-Quaders des Schneiders, ganz durch die Wand – für Tür- und\n"
                             L"Fensterrahmen (der Rahmen bleibt und sitzt genau im Loch).\n"
                             L"Form: genau die Form des Schneiders wird entfernt (Kugel = runde Mulde, Zylinder = Bohrung).");
    g.lblManipKopien = CreateLabel(hObjektFenster, hInst, L"Art der Kopien", 0, 0, 10, 10);
    g.cmbManipKopien = auswahlListe(ID_CMB_MANIP_KOPIEN);
    for (const wchar_t *t : {L"Echte Kopien (eigenständig)", L"Instanzen (verknüpft, sparsam)"})
        SendMessageW(g.cmbManipKopien, CB_ADDSTRING, 0, (LPARAM)t);
    SendMessageW(g.cmbManipKopien, CB_SETCURSEL, 0, 0);
    tooltip(g.cmbManipKopien, L"Echte Kopien: jede Kopie hat ihre eigenen Dreiecke und lässt sich einzeln verformen.\n"
                              L"Instanzen: alle teilen die Geometrie des Originals (100 Steine brauchen den Speicher von einem).\n"
                              L"Jede Instanz hat eigene Lage, Drehung und Größe; Material und Netz-Operationen wirken auf alle.\n"
                              L"Gruppen bleiben Gruppen mit allen Teilen; jedes Teil darin wird eine Instanz.\n"
                              L"Einzeln ändern: Rechtsklick in der Liste → \"Instanz lösen\".");
    g.lblManipLage = CreateLabel(hObjektFenster, hInst, L"Lage", 0, 0, 10, 10);
    g.cmbManipLage = auswahlListe(ID_CMB_MANIP_LAGE);
    for (const wchar_t *t : {L"Ausrichtung behalten", L"Stabil hinlegen (flache Seite unten)"})
        SendMessageW(g.cmbManipLage, CB_ADDSTRING, 0, (LPARAM)t);
    SendMessageW(g.cmbManipLage, CB_SETCURSEL, 0, 0);
    tooltip(g.cmbManipLage, L"Ausrichtung behalten: das Objekt wird nur verschoben (und mit „Neigung“ dem Hang angepasst).\n"
                            L"Stabil hinlegen: vorher so gedreht, wie es von seiner jetzigen Lage aus zur Ruhe käme –\n"
                            L"es kippt auf die nächste flache Seite, der Schwerpunkt liegt darüber. Steine liegen so nicht\n"
                            L"auf ihren Spitzen. Gut nach „Zufällige Drehung“ (jeder Stein findet eine andere Seite).");
    g.lblManipPinsel = CreateLabel(hObjektFenster, hInst, L"Pinsel", 0, 0, 10, 10);
    g.cmbManipPinsel = auswahlListe(ID_CMB_MANIP_PINSEL);
    for (const wchar_t *t : {L"Anheben", L"Absenken", L"Glätten", L"Ebnen (auf Höhe des Startpunkts)"})
        SendMessageW(g.cmbManipPinsel, CB_ADDSTRING, 0, (LPARAM)t);
    SendMessageW(g.cmbManipPinsel, CB_SETCURSEL, 0, 0);
    tooltip(g.cmbManipPinsel, L"Anheben / Absenken: Hügel bzw. Senke formen. Glätten: Buckel und Kanten ausgleichen.\n"
                              L"Ebnen: auf die Höhe ziehen, an der der Strich begonnen hat (Bauplatz, Weg).\n"
                              L"Die Art lässt sich auch bei eingeschaltetem Pinsel wechseln.");
    g.lblManipAus = CreateLabel(hObjektFenster, hInst, L"Ausdehnung X / Y / Z", 0, 0, 10, 10);
    for (int i = 0; i < 3; ++i)
        g.editManipAus[i] = CreateEingabefeld(hObjektFenster, hInst, ID_EDIT_MANIP_AUS_X + i, ES_RIGHT);
    g.btnManipAusfuehren = CreateStyledButton(hObjektFenster, hInst, ID_BTN_MANIP_AUSFUEHREN, L"Ausführen", 0, 0, 10, 10);
    g.lblManipInfo = CreateLabel(hObjektFenster, hInst, L"", 0, 0, 10, 10);
    SetWindowLongPtrW(g.lblManipInfo, GWLP_USERDATA, 1); // gedimmt wie Hinweise
    // mehrzeilig umbrechen statt mit "..." abschneiden
    SetWindowLongPtrW(g.lblManipInfo, GWL_STYLE, GetWindowLongPtrW(g.lblManipInfo, GWL_STYLE) & ~LONG_PTR(SS_ELLIPSISMASK));

    g.lblVorlage = CreateLabel(hObjektFenster, hInst, L"Vorlage", 0, 0, 10, 10);
    g.cmbVorlage = auswahlListe(ID_CMB_VORLAGE);
    SendMessageW(g.cmbVorlage, CB_ADDSTRING, 0, (LPARAM)L"(eigenes Material)");
    for (const auto &v : MAT_VORLAGEN)
        SendMessageW(g.cmbVorlage, CB_ADDSTRING, 0, (LPARAM)v.name);
    for (const auto &v : weitereVorlagen())
        SendMessageW(g.cmbVorlage, CB_ADDSTRING, 0, (LPARAM)v.name);

    for (int i = 0; i < MAT_FARBEN; ++i)
    {
        g.lblMatFarbe[i] = CreateLabel(hObjektFenster, hInst, MAT_FARB_NAMEN[i], 0, 0, 10, 10);
        for (int k = 0; k < 3; ++k)
            g.editMatFarbe[i * 3 + k] = CreateEingabefeld(hObjektFenster, hInst, ID_EDIT_MATFARBE_ERSTES + i * 3 + k, ES_RIGHT);
        g.btnMatFarbe[i] = CreateStyledButton(hObjektFenster, hInst, ID_BTN_MATFARBE_ERSTES + i, L"", 0, 0, 10, 10);
    }
    for (int i = 0; i < MAT_WERTE; ++i)
    {
        g.lblMatWert[i] = CreateLabel(hObjektFenster, hInst, MAT_WERT_NAMEN[i], 0, 0, 10, 10);
        g.editMatWert[i] = CreateEingabefeld(hObjektFenster, hInst, ID_EDIT_MATWERT_ERSTES + i, ES_RIGHT);
    }
    g.lblTextur = CreateLabel(hObjektFenster, hInst, L"Textur", 0, 0, 10, 10);
    g.editTextur = CreateEingabefeld(hObjektFenster, hInst, ID_EDIT_TEXTUR, ES_LEFT);
    g.btnTexturDatei = CreateStyledButton(hObjektFenster, hInst, ID_BTN_TEXTUR_DATEI, L"\u2026", 0, 0, 10, 10);
    g.btnTexturU = CreateStyledButton(hObjektFenster, hInst, ID_BTN_TEXTUR_SPIEGEL_U, L"Spiegeln \u2194", 0, 0, 10, 10);
    g.btnTexturV = CreateStyledButton(hObjektFenster, hInst, ID_BTN_TEXTUR_SPIEGEL_V, L"Spiegeln \u2195", 0, 0, 10, 10);
    g.btnTexturDrehen = CreateStyledButton(hObjektFenster, hInst, ID_BTN_TEXTUR_DREHEN, L"Drehen 90\u00B0", 0, 0, 10, 10);
    g.cmbTexturModus = auswahlListe(ID_CMB_TEXTURMODUS);
    for (const wchar_t *m : TEXTUR_MODI)
        SendMessageW(g.cmbTexturModus, CB_ADDSTRING, 0, (LPARAM)m);
    g.lblTexturKachel = CreateLabel(hObjektFenster, hInst, L"Kachel (m)", 0, 0, 10, 10);
    g.editTexturKachel = CreateEingabefeld(hObjektFenster, hInst, ID_EDIT_TEXTUR_KACHEL, ES_RIGHT);
    g.lblTexturWinkel = CreateLabel(hObjektFenster, hInst, L"Winkel (\u00B0)", 0, 0, 10, 10);
    g.editTexturWinkel = CreateEingabefeld(hObjektFenster, hInst, ID_EDIT_TEXTUR_WINKEL, ES_RIGHT);
    g.lblTexturVersatz = CreateLabel(hObjektFenster, hInst, L"Versatz", 0, 0, 10, 10);
    for (int k = 0; k < 2; ++k)
        g.editTexturVersatz[k] = CreateEingabefeld(hObjektFenster, hInst, ID_EDIT_TEXTUR_VERSATZ_U + k, ES_RIGHT);
    g.btnTexturFest = CreateStyledButton(hObjektFenster, hInst, ID_BTN_TEXTUR_FEST, L"Haftet am Objekt", 0, 0, 10, 10);
    g.lblTexturKugel = CreateLabel(hObjektFenster, hInst, L"Kugel (\u00B0)", 0, 0, 10, 10);
    for (int k = 0; k < 3; ++k)
        g.editTexturKugel[k] = CreateEingabefeld(hObjektFenster, hInst, ID_EDIT_TEXTUR_KUGEL_X + k, ES_RIGHT);
    g.lblTexturBild = CreateLabel(hObjektFenster, hInst, L"Zusatzbild", 0, 0, 10, 10);
    g.cmbTexturBild = auswahlListe(ID_CMB_TEXTUR_BILD);
    for (const wchar_t *m : {L"Relief", L"Alpha", L"Glanz", L"Leuchten", L"Unterseite"})
        SendMessageW(g.cmbTexturBild, CB_ADDSTRING, 0, (LPARAM)m);
    SendMessageW(g.cmbTexturBild, CB_SETCURSEL, 0, 0);
    g.editTexturBild = CreateEingabefeld(hObjektFenster, hInst, ID_EDIT_TEXTUR_BILD, ES_LEFT);
    g.btnTexturBild = CreateStyledButton(hObjektFenster, hInst, ID_BTN_TEXTUR_BILD, L"\u2026", 0, 0, 10, 10);
    g.editTexturStaerke = CreateEingabefeld(hObjektFenster, hInst, ID_EDIT_TEXTUR_STAERKE, ES_RIGHT);
    g.lblTexturRinge = CreateLabel(hObjektFenster, hInst, L"Regenringe", 0, 0, 10, 10);
    g.editTexturRinge = CreateEingabefeld(hObjektFenster, hInst, ID_EDIT_TEXTUR_RINGE, ES_RIGHT);
    g.lblTexturNass = CreateLabel(hObjektFenster, hInst, L"Nass", 0, 0, 10, 10);
    g.editTexturNass = CreateEingabefeld(hObjektFenster, hInst, ID_EDIT_TEXTUR_NASS, ES_RIGHT);
    tooltip(g.editTexturRinge, L"Wellenringe der Regentropfen auf dieser Fläche (Wasser, Pfütze): 0 = aus, 1 = normal, 2 = stark.\n"
                               L"Nur bei Regen (Render-Einstellungen → Regen) und nur auf nach oben zeigenden Flächen.\n"
                               L"Die Ringe kippen die Normale: Spiegelung und Brechung zeigen sie von selbst.");
    tooltip(g.editTexturNass, L"Nasse Oberfläche 0..1: dunkler und glänzender, spiegelt etwas (wirkt auch ohne Regen).\n"
                              L"Bei Regen mit \"Alle Flächen nass\" gilt der größere Wert.");

    reiterZeigen(0);

    leisteLayout(1200, LEISTE_H);
    objektfensterLayout(PANEL_W, panelHoehe);
    schrittAnzeigen();

    UpdateObjectList();
    SetTimer(hToolWindow, TIMER_ABGLEICH, 150, nullptr);

    //--------------------------------------------------

    //--------------------------------------------------
    // Statusleiste: gehoert ebenfalls der Leiste; Position setzt WerkzeugleisteAndocken()
    //--------------------------------------------------
    WNDCLASSW wcStatus{};
    wcStatus.lpfnWndProc = StatusProc;
    wcStatus.hInstance = hInst;
    wcStatus.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcStatus.lpszClassName = L"SZENEN_STATUSLEISTE";
    RegisterClassW(&wcStatus);

    hStatusleiste = CreateWindowExW(0, L"SZENEN_STATUSLEISTE", L"Status", WS_CHILD | WS_CLIPSIBLINGS, 0, 0, 400,
                                    STATUS_H, hHauptfenster, nullptr, hInst, nullptr);

    tooltipsAnlegen(hInst);
    tooltip(hObjektTeiler, L"Ziehen: Objekte-Fenster breiter/schmaler. Doppelklick: Standardbreite");

    ShowWindow(hToolWindow, SW_SHOWNOACTIVATE);
    ShowWindow(hObjektFenster, SW_SHOWNOACTIVATE);
    ShowWindow(hStatusleiste, SW_SHOWNOACTIVATE);
}

//--------------------------------------------------
// Zeitleiste ueber der Statusleiste (sichtbar mit "Animation" in den Render-Einstellungen)
//
//  [+ Snapshot][Ersetzen][Löschen][|◀][▶][▶|]  Zeit [____]  Label [______]  |---◆Start------◆Ende---|
//
// Klick/Ziehen in die Spur: Zeitpunkt waehlen, die Szene zeigt den gemischten Zustand.
// Klick auf eine Raute: Snapshot waehlen; Ziehen einer Raute verschiebt den Snapshot zeitlich.
//--------------------------------------------------

constexpr int ZEIT_H = 86; // mit Spur fuer die Ablaeufe
#define ID_ZEIT_NEU 4300
#define ID_ZEIT_ERSETZEN 4301
#define ID_ZEIT_LOESCHEN 4302
#define ID_ZEIT_ABSPIELEN 4303
#define ID_ZEIT_FELD 4304
#define ID_ZEIT_VORHER 4305
#define ID_ZEIT_NACHHER 4306
#define ID_ZEIT_LABEL 4307
#define ID_ZEIT_WIEDERHOLUNG 4308
#define TIMER_ABSPIELEN 2
constexpr int ZEIT_KNOEPFE = 7; // + Snapshot, Ersetzen, Löschen, |◀, ▶, ▶|, ↻

static HWND hZeitleiste = nullptr;
static HWND zeitKnoepfe[ZEIT_KNOEPFE] = {};
static HWND zeitFeld = nullptr, zeitBeschriftung = nullptr;
static HWND zeitLabelFeld = nullptr, zeitLabelBeschriftung = nullptr; // Beschriftung des Snapshots
static int zeitAuswahl = -1;       // gewaehlter Snapshot
static double zeitleisteKlickZeit = 0; // Zeit unter dem letzten Rechtsklick
static std::vector<std::pair<RECT, size_t>> ablaufBalken; // gezeichnete Ablauf-Balken (Rechtsklick)
static int spurLinks = 0;          // x der Spur im Fenster
static bool zeitZiehen = false;    // Maus gedrueckt in der Spur
static int rauteZiehen = -1;       // Snapshot, dessen Raute gezogen wird
static double rauteZeit = 0;       // Zeit der gezogenen Raute
static bool rauteBewegt = false;
static int zeitDruckX = 0;
static bool abspielen = false;
static ULONGLONG abspielStart = 0;
static double abspielStartZeit = 0;

// angedockte Ansichten (fuer das Ein-/Ausblenden der Zeitleiste)
static std::vector<HWND> angedockteAnsichten;
static LONG ansichtenOben = 0, ansichtenUnten = 0, statusOben = 0, dockLinks = 0;
static int dockBreite = 0;

std::atomic<int> gAnimationBild{0}, gAnimationBilder{0};
std::atomic<unsigned long long> gAnimationStartMs{0};
std::atomic<int> gAnimationAbBild{1};
bool gAnimationVideo = true;
std::function<void(const std::wstring &)> gAnimationRendern;

// sichtbarer Zeitbereich der Spur
static void zeitBereich(double &von, double &bis)
{
    von = std::min(0.0, animationsBeginn());
    bis = std::max({animationsEnde(), aktuelleZeit(), von + 1.0});
    if (rauteZiehen >= 0)
        bis = std::max(bis, rauteZeit);
    bis += std::max(0.5, (bis - von) * 0.08);
}

static RECT spurRechteck()
{
    RECT r;
    GetClientRect(hZeitleiste, &r);
    r.left = spurLinks;
    r.right -= 14;
    r.top += 6;
    r.bottom -= 8;
    return r;
}

static int zeitZuX(double t)
{
    double von, bis;
    zeitBereich(von, bis);
    RECT r = spurRechteck();
    return int(r.left + (t - von) / (bis - von) * (r.right - r.left) + 0.5);
}

static double xZuZeit(int x)
{
    double von, bis;
    zeitBereich(von, bis);
    RECT r = spurRechteck();
    double t = von + double(x - r.left) / std::max(1, int(r.right - r.left)) * (bis - von);
    t = std::max(0.0, t);
    return std::round(t * bilderProSekunde()) / bilderProSekunde(); // auf ganze Bilder
}

// Abschnitt (Snapshot davor, wenn danach noch einer kommt) zur Zeit t; -1: ausserhalb
static int abschnittBei(double t)
{
    for (int i = 0; i + 1 < snapshotAnzahl(); ++i)
        if (t >= snapshotZeit(i) - 1e-9 && t < snapshotZeit(i + 1) - 1e-9)
            return i;
    return -1;
}

static void wiederholungMenue(HWND fenster, int abschnitt, POINT bildschirm); // weiter unten (Dialog)

static int snapshotBei(double t)
{
    for (int i = 0; i < snapshotAnzahl(); ++i)
        if (std::fabs(snapshotZeit(i) - t) < 1e-6)
            return i;
    return -1;
}

static void zeitleisteAnzeigen()
{
    if (!hZeitleiste)
        return;
    if (zeitAuswahl >= snapshotAnzahl())
        zeitAuswahl = -1;
    textSetzen(zeitBeschriftung, zeitAuswahl >= 0 ? L"Snapshot " + std::to_wstring(zeitAuswahl + 1) + L" (s)"
                                                  : std::wstring(L"Zeit (s)"));
    textSetzen(zeitFeld, zahlText(zeitAuswahl >= 0 ? snapshotZeit(zeitAuswahl) : aktuelleZeit(), 3));
    EnableWindow(zeitKnoepfe[1], zeitAuswahl >= 0);
    EnableWindow(zeitKnoepfe[2], zeitAuswahl >= 0);
    // vorheriger/naechster Snapshot: gibt es einen vor bzw. nach der aktuellen Zeit?
    const double jetzt = aktuelleZeit();
    bool davor = false, danach = false;
    for (int i = 0; i < snapshotAnzahl(); ++i)
    {
        davor = davor || snapshotZeit(i) < jetzt - 1e-9;
        danach = danach || snapshotZeit(i) > jetzt + 1e-9;
    }
    EnableWindow(zeitKnoepfe[3], davor);
    EnableWindow(zeitKnoepfe[4], snapshotAnzahl() >= 2);
    EnableWindow(zeitKnoepfe[5], danach);
    EnableWindow(zeitKnoepfe[6], snapshotAnzahl() >= 2);
    SetWindowTextW(zeitKnoepfe[4], abspielen ? L"■" : L"▶");
    // Beschriftung des gewaehlten Snapshots (nicht ueberschreiben, waehrend man tippt)
    if (zeitLabelFeld)
    {
        EnableWindow(zeitLabelFeld, zeitAuswahl >= 0);
        if (GetFocus() != zeitLabelFeld)
        {
            textSetzen(zeitLabelFeld, zeitAuswahl >= 0 ? ausUtf8(snapshotLabel(zeitAuswahl)) : std::wstring());
            SendMessageW(zeitLabelFeld, EM_SETMODIFY, FALSE, 0);
        }
    }
    InvalidateRect(hZeitleiste, nullptr, FALSE);
}

// Zeit setzen und die Szene dazu berechnen; ein Snapshot genau dort wird gewaehlt
static void zeitWaehlen(double t)
{
    zeitpunktAnwenden(t);
    zeitAuswahl = snapshotBei(t);
    zeitleisteAnzeigen();
    einstellungenAnzeigen();
}

static void abspielenBeenden()
{
    if (!abspielen)
        return;
    abspielen = false;
    KillTimer(hZeitleiste, TIMER_ABSPIELEN);
    zeitleisteAnzeigen();
}

static void zeitleisteMalen(HDC hdc)
{
    RECT client;
    GetClientRect(hZeitleiste, &client);
    FillRect(hdc, &client, pinselHintergrund);

    HPEN linie = CreatePen(PS_SOLID, 1, FARBE_LINIEN);
    HGDIOBJ altStift = SelectObject(hdc, linie);
    MoveToEx(hdc, client.left, client.top, nullptr);
    LineTo(hdc, client.right, client.top);

    RECT spur = spurRechteck();
    FillRect(hdc, &spur, pinselFlaeche);
    SetBkMode(hdc, TRANSPARENT);
    HGDIOBJ altSchrift = SelectObject(hdc, gFont);

    // Bereich vom ersten bis zum letzten Snapshot (wird gerendert)
    if (snapshotAnzahl() >= 2)
    {
        RECT band = spur;
        band.left = std::max(spur.left, LONG(zeitZuX(animationsBeginn())));
        band.right = std::min(spur.right, LONG(zeitZuX(animationsEnde())));
        band.top = (spur.top + spur.bottom) / 2 - 3;
        band.bottom = band.top + 6;
        HBRUSH b = CreateSolidBrush(RGB(0, 70, 120));
        FillRect(hdc, &band, b);
        DeleteObject(b);
    }

    // Sekundenstriche: Abstand so, dass die Beschriftungen nicht ueberlappen
    double von, bis;
    zeitBereich(von, bis);
    const double schritte[] = {0.04, 0.1, 0.2, 0.25, 0.5, 1, 2, 5, 10, 15, 30, 60, 120, 300, 600};
    double schritt = 600;
    for (double s : schritte)
        if ((spur.right - spur.left) * s / (bis - von) >= 55)
        {
            schritt = s;
            break;
        }
    SetTextColor(hdc, FARBE_TEXT_GEDIMMT);
    for (double t = std::ceil(von / schritt) * schritt; t <= bis + 1e-9; t += schritt)
    {
        int x = zeitZuX(t);
        MoveToEx(hdc, x, spur.top, nullptr);
        LineTo(hdc, x, spur.top + 6);
        MoveToEx(hdc, x, spur.bottom - 6, nullptr);
        LineTo(hdc, x, spur.bottom);
        std::wstring text = zahlText(std::fabs(t) < 1e-9 ? 0.0 : t, schritt < 1 ? 2 : 0) + L" s";
        RECT tr = {x + 3, spur.top + 2, x + 60, spur.top + 2 + LABEL_H};
        DrawTextW(hdc, text.c_str(), -1, &tr, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);
    }

    // Rauten der Snapshots
    const int mitte = (spur.top + spur.bottom) / 2 + 6;
    for (int i = 0; i < snapshotAnzahl(); ++i)
    {
        double t = (i == rauteZiehen && rauteBewegt) ? rauteZeit : snapshotZeit(i);
        int x = zeitZuX(t);
        const int g = 7;
        POINT p[4] = {{x, mitte - g}, {x + g, mitte}, {x, mitte + g}, {x - g, mitte}};
        bool gewaehlt = i == zeitAuswahl;
        HBRUSH fuellung = CreateSolidBrush(gewaehlt ? RGB(255, 200, 60) : RGB(80, 170, 240));
        HPEN rand = CreatePen(PS_SOLID, 1, gewaehlt ? RGB(255, 240, 200) : RGB(200, 230, 255));
        HGDIOBJ ap = SelectObject(hdc, fuellung);
        HGDIOBJ as = SelectObject(hdc, rand);
        Polygon(hdc, p, 4);
        SelectObject(hdc, ap);
        SelectObject(hdc, as);
        DeleteObject(fuellung);
        DeleteObject(rand);

        // Beschriftung rechts neben der Raute (abgeschnitten vor der naechsten)
        std::wstring label = ausUtf8(snapshotLabel(i));
        if (!label.empty())
        {
            int rechts = spur.right;
            if (i + 1 < snapshotAnzahl())
                rechts = std::min(rechts, zeitZuX(snapshotZeit(i + 1)) - g - 2);
            RECT tr = {x + g + 3, mitte - LABEL_H / 2, rechts, mitte + LABEL_H / 2 + 1};
            if (tr.right - tr.left > 12)
            {
                SetTextColor(hdc, gewaehlt ? RGB(255, 220, 130) : RGB(170, 210, 245));
                DrawTextW(hdc, label.c_str(), -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
            }
        }
    }

    // Abschnitte mit Wiederholungen: orange Leiste unten und Text rechts im Abschnitt
    for (int i = 0; i + 1 < snapshotAnzahl(); ++i)
    {
        std::vector<Wiederholung> wd = wiederholungen(i);
        if (wd.empty())
            continue;
        const int x0 = zeitZuX(snapshotZeit(i)) + 8, x1 = zeitZuX(snapshotZeit(i + 1)) - 8;
        if (x1 <= x0)
            continue;
        RECT leiste = {x0, spur.bottom - 4, x1, spur.bottom - 1};
        HBRUSH orange = CreateSolidBrush(RGB(235, 150, 40));
        FillRect(hdc, &leiste, orange);
        DeleteObject(orange);
        std::wstring text = L"\u21BB ";
        if (wd.size() == 1)
        {
            static const wchar_t *arten[] = {L"", L"Drehen", L"Pendeln", L"Wippen", L"Abrollen"};
            const int n = wiederholungZyklen(i, wd[0]);
            text += ausUtf8(wd[0].o->name.empty() ? std::string("Objekt") : wd[0].o->name) + L": " +
                    arten[std::clamp(wd[0].art, 0, 4)] + L" ×" + std::to_wstring(n);
        }
        else
            text += std::to_wstring(wd.size()) + L" Wiederholungen";
        SetTextColor(hdc, RGB(240, 175, 90));
        RECT tr = {x0, spur.bottom - 4 - LABEL_H, x1, spur.bottom - 4};
        DrawTextW(hdc, text.c_str(), -1, &tr, DT_RIGHT | DT_BOTTOM | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    }

    // Ablaeufe: gruene Balken unter den Sekundenzahlen, Name darin
    ablaufBalken.clear();
    {
        const auto &liste = ablaufEinsaetze();
        const int y0 = spur.top + LABEL_H + 2;
        for (size_t i = 0; i < liste.size(); ++i)
        {
            const AblaufEinsatz &e = liste[i];
            if (!ablaufGueltig(e))
                continue;
            const bool gewaehlt = istAusgewaehlt(e.o);
            for (const auto &vk : ablaufZeiten(e))
            {
                RECT b = {std::max(spur.left, LONG(zeitZuX(vk.first))), y0, std::min(spur.right, LONG(zeitZuX(vk.second))), y0 + 13};
                if (b.right - b.left < 3)
                    continue;
                HBRUSH f = CreateSolidBrush(gewaehlt ? RGB(80, 190, 110) : RGB(40, 125, 75));
                FillRect(hdc, &b, f);
                DeleteObject(f);
                ablaufBalken.push_back({b, i});
                std::wstring t = ausUtf8(e.v.name) + L" – " + ausUtf8(e.o->name);
                RECT tr = b;
                tr.left += 3;
                SetTextColor(hdc, RGB(235, 255, 240));
                DrawTextW(hdc, t.c_str(), -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
            }
        }
    }

    // Abspielkopf
    {
        int x = zeitZuX(aktuelleZeit());
        HPEN rot = CreatePen(PS_SOLID, 2, RGB(230, 70, 70));
        HGDIOBJ as = SelectObject(hdc, rot);
        MoveToEx(hdc, x, spur.top, nullptr);
        LineTo(hdc, x, spur.bottom);
        SelectObject(hdc, as);
        DeleteObject(rot);
        std::wstring text = zahlText(aktuelleZeit(), 2) + L" s";
        SetTextColor(hdc, RGB(240, 140, 140));
        RECT tr = {x + 4, spur.bottom - LABEL_H, x + 90, spur.bottom};
        if (tr.right > spur.right)
            OffsetRect(&tr, -(tr.right - tr.left) - 8, 0);
        DrawTextW(hdc, text.c_str(), -1, &tr, DT_LEFT | DT_BOTTOM | DT_SINGLELINE | DT_NOPREFIX);
    }
    if (snapshotAnzahl() == 0)
    {
        SetTextColor(hdc, FARBE_UEBERSCHRIFT);
        RECT tr = spur;
        tr.left += 80;
        DrawTextW(hdc, L"Noch keine Snapshots: Szene aufbauen und „+ Snapshot“ drücken, dann Zeit wählen, ändern, wieder „+ Snapshot“.",
                  -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    }

    SelectObject(hdc, altSchrift);
    SelectObject(hdc, altStift);
    DeleteObject(linie);
}

static void zeitFeldUebernehmen()
{
    if (!SendMessageW(zeitFeld, EM_GETMODIFY, 0, 0))
        return;
    SendMessageW(zeitFeld, EM_SETMODIFY, FALSE, 0);
    double v;
    if (!zahlLesen(zeitFeld, v) || v < 0 || v > 1e6)
    {
        MessageBeep(MB_ICONWARNING);
        return;
    }
    if (zeitAuswahl >= 0)
    {
        if (snapshotBei(v) >= 0 && snapshotBei(v) != zeitAuswahl)
        {
            MessageBeep(MB_ICONWARNING); // dort liegt schon ein Snapshot
            zeitleisteAnzeigen();
            return;
        }
        zeitAuswahl = snapshotZeitSetzen(zeitAuswahl, v);
        zeitWaehlen(v);
    }
    else
        zeitWaehlen(v);
}

static void zeitLabelUebernehmen()
{
    if (!zeitLabelFeld || !SendMessageW(zeitLabelFeld, EM_GETMODIFY, 0, 0))
        return;
    SendMessageW(zeitLabelFeld, EM_SETMODIFY, FALSE, 0);
    if (zeitAuswahl < 0)
        return;
    wchar_t puffer[256] = {};
    GetWindowTextW(zeitLabelFeld, puffer, 256);
    std::wstring t = puffer;
    while (!t.empty() && (t.back() == L' ' || t.back() == L'\t'))
        t.pop_back();
    snapshotLabelSetzen(zeitAuswahl, zuUtf8(t));
    InvalidateRect(hZeitleiste, nullptr, FALSE);
}

// zum vorherigen (richtung < 0) bzw. naechsten Snapshot springen
static void snapshotSpringen(int richtung)
{
    abspielenBeenden();
    const double jetzt = aktuelleZeit();
    int ziel = -1;
    for (int i = 0; i < snapshotAnzahl(); ++i)
    {
        double t = snapshotZeit(i);
        if (richtung < 0 && t < jetzt - 1e-9)
            ziel = i; // sortiert: der letzte davor
        if (richtung > 0 && t > jetzt + 1e-9)
        {
            ziel = i; // der erste danach
            break;
        }
    }
    if (ziel < 0)
    {
        MessageBeep(MB_OK);
        return;
    }
    zeitWaehlen(snapshotZeit(ziel)); // waehlt den Snapshot dort
}

static LRESULT CALLBACK ZeitleisteProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        // doppelt gepuffert: kein Flackern beim Ziehen
        RECT r;
        GetClientRect(hwnd, &r);
        HDC mem = CreateCompatibleDC(hdc);
        HBITMAP bild = CreateCompatibleBitmap(hdc, std::max(1, int(r.right)), std::max(1, int(r.bottom)));
        HGDIOBJ alt = SelectObject(mem, bild);
        zeitleisteMalen(mem);
        BitBlt(hdc, 0, 0, r.right, r.bottom, mem, 0, 0, SRCCOPY);
        SelectObject(mem, alt);
        DeleteObject(bild);
        DeleteDC(mem);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_LBUTTONDOWN:
    {
        int x = GET_X_LPARAM(lParam), y = GET_Y_LPARAM(lParam);
        RECT spur = spurRechteck();
        if (x < spur.left - 8 || x > spur.right + 8 || y < spur.top || y > spur.bottom)
            break;
        abspielenBeenden();
        SetFocus(hwnd);
        SetCapture(hwnd);
        zeitDruckX = x;
        // Raute getroffen?
        rauteZiehen = -1;
        for (int i = 0; i < snapshotAnzahl(); ++i)
            if (std::abs(zeitZuX(snapshotZeit(i)) - x) <= 7)
                rauteZiehen = i;
        if (rauteZiehen >= 0)
        {
            rauteZeit = snapshotZeit(rauteZiehen);
            rauteBewegt = false;
            zeitAuswahl = rauteZiehen;
            zeitWaehlen(rauteZeit);
        }
        else
        {
            zeitZiehen = true;
            zeitWaehlen(xZuZeit(x));
        }
        return 0;
    }
    case WM_MOUSEMOVE:
    {
        int x = GET_X_LPARAM(lParam);
        if (rauteZiehen >= 0)
        {
            if (!rauteBewegt && std::abs(x - zeitDruckX) < 4)
                return 0;
            rauteBewegt = true;
            rauteZeit = xZuZeit(x);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        else if (zeitZiehen)
        {
            double t = xZuZeit(x);
            if (std::fabs(t - aktuelleZeit()) > 1e-9)
                zeitWaehlen(t);
        }
        return 0;
    }
    case WM_LBUTTONUP:
    case WM_CAPTURECHANGED:
    {
        if (rauteZiehen >= 0 && rauteBewegt)
        {
            int i = rauteZiehen;
            rauteZiehen = -1;
            int dort = snapshotBei(rauteZeit);
            if (dort >= 0 && dort != i)
                MessageBeep(MB_ICONWARNING); // dort liegt schon ein Snapshot: bleibt, wo er war
            else
            {
                zeitAuswahl = snapshotZeitSetzen(i, rauteZeit);
                zeitWaehlen(rauteZeit);
            }
        }
        rauteZiehen = -1;
        zeitZiehen = false;
        if (msg == WM_LBUTTONUP && GetCapture() == hwnd)
            ReleaseCapture();
        zeitleisteAnzeigen();
        return 0;
    }

    case WM_RBUTTONUP:
    {
        int x = GET_X_LPARAM(lParam), y = GET_Y_LPARAM(lParam);
        RECT spur = spurRechteck();
        if (x < spur.left || x > spur.right || y < spur.top || y > spur.bottom)
            break;
        abspielenBeenden();
        double von, bis;
        zeitBereich(von, bis);
        const double t = von + double(x - spur.left) / std::max(1, int(spur.right - spur.left)) * (bis - von);
        zeitleisteKlickZeit = t;
        for (const auto &[r, i] : ablaufBalken) // Rechtsklick auf einen Ablauf-Balken
        {
            POINT q = {x, y};
            if (PtInRect(&r, q))
            {
                POINT p = {x, y};
                ClientToScreen(hwnd, &p);
                ablaufMenue(hwnd, i, p);
                zeitleisteAnzeigen();
                return 0;
            }
        }
        const int abschnitt = abschnittBei(t);
        if (abschnitt < 0)
        {
            MessageBeep(MB_OK); // vor dem ersten oder nach dem letzten Snapshot
            return 0;
        }
        POINT p = {x, y};
        ClientToScreen(hwnd, &p);
        wiederholungMenue(hwnd, abschnitt, p);
        return 0;
    }

    case WM_TIMER:
        if (wParam == TIMER_ABSPIELEN)
        {
            double t = abspielStartZeit + double(GetTickCount64() - abspielStart) / 1000.0;
            if (t >= animationsEnde())
            {
                t = animationsEnde();
                abspielenBeenden();
            }
            zeitWaehlen(t);
        }
        return 0;

    case WM_EINGABE_FERTIG:
        if (int(wParam) == ID_ZEIT_FELD)
            zeitFeldUebernehmen();
        else if (int(wParam) == ID_ZEIT_LABEL)
            zeitLabelUebernehmen();
        return 0;

    case WM_COMMAND:
    {
        int id = LOWORD(wParam), code = HIWORD(wParam);
        if (id == ID_ZEIT_FELD)
        {
            if (code == EN_KILLFOCUS)
                zeitFeldUebernehmen();
            return 0;
        }
        if (id == ID_ZEIT_LABEL)
        {
            if (code == EN_KILLFOCUS)
                zeitLabelUebernehmen();
            return 0;
        }
        if (code != BN_CLICKED)
            break;
        if (id == ID_ZEIT_NEU)
        {
            abspielenBeenden();
            // an der aktuellen Zeit; liegt dort schon einer, eine Sekunde nach dem letzten
            double t = aktuelleZeit();
            if (snapshotAnzahl() > 0 && snapshotBei(t) >= 0)
                t = animationsEnde() + 1.0;
            zeitAuswahl = snapshotAufnehmen(t);
            zeitWaehlen(t);
        }
        else if (id == ID_ZEIT_ERSETZEN && zeitAuswahl >= 0)
        {
            snapshotErsetzen(zeitAuswahl);
            zeitleisteAnzeigen();
        }
        else if (id == ID_ZEIT_LOESCHEN && zeitAuswahl >= 0)
        {
            std::wstring frage = L"Snapshot " + std::to_wstring(zeitAuswahl + 1) + L" bei " +
                                 zahlText(snapshotZeit(zeitAuswahl), 3) + L" s löschen?";
            if (MessageBoxW(hwnd, frage.c_str(), L"Snapshot löschen", MB_YESNO | MB_ICONQUESTION) == IDYES)
            {
                snapshotLoeschen(zeitAuswahl);
                zeitAuswahl = -1;
                zeitWaehlen(aktuelleZeit());
            }
        }
        else if (id == ID_ZEIT_VORHER)
            snapshotSpringen(-1);
        else if (id == ID_ZEIT_WIEDERHOLUNG)
        {
            abspielenBeenden();
            int abschnitt = abschnittBei(aktuelleZeit());
            if (abschnitt < 0 && snapshotAnzahl() >= 2) // am Ende: letzter Abschnitt, davor: erster
                abschnitt = aktuelleZeit() >= animationsEnde() - 1e-9 ? snapshotAnzahl() - 2 : 0;
            if (abschnitt < 0)
                MessageBeep(MB_OK);
            else
            {
                RECT r;
                GetWindowRect(zeitKnoepfe[6], &r);
                wiederholungMenue(hwnd, abschnitt, POINT{r.left, r.bottom});
            }
        }
        else if (id == ID_ZEIT_NACHHER)
            snapshotSpringen(+1);
        else if (id == ID_ZEIT_ABSPIELEN)
        {
            if (abspielen)
                abspielenBeenden();
            else if (snapshotAnzahl() >= 2)
            {
                abspielen = true;
                abspielStartZeit = (aktuelleZeit() >= animationsEnde() - 1e-9 || aktuelleZeit() < animationsBeginn())
                                       ? animationsBeginn()
                                       : aktuelleZeit();
                abspielStart = GetTickCount64();
                SetTimer(hwnd, TIMER_ABSPIELEN, 40, nullptr);
                zeitleisteAnzeigen();
            }
        }
        einstellungenAnzeigen(); // Dauer und Anzahl Bilder
        return 0;
    }

    case WM_KEYDOWN:
        if (tasteWeitergeben(wParam))
            return 0;
        break;

    case WM_DRAWITEM:
        buttonZeichnen(reinterpret_cast<const DRAWITEMSTRUCT *>(lParam));
        return TRUE;

    case WM_CTLCOLORSTATIC:
    {
        HDC hdc = (HDC)wParam;
        SetTextColor(hdc, FARBE_UEBERSCHRIFT);
        SetBkColor(hdc, FARBE_HINTERGRUND);
        return (LRESULT)pinselHintergrund;
    }
    case WM_CTLCOLOREDIT:
    {
        HDC hdc = (HDC)wParam;
        SetTextColor(hdc, FARBE_TEXT);
        SetBkColor(hdc, FARBE_FLAECHE);
        return (LRESULT)pinselFlaeche;
    }
    case WM_CLOSE:
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void zeitleisteAnlegen()
{
    HINSTANCE hInst = GetModuleHandleW(nullptr);
    WNDCLASSW wc{};
    wc.lpfnWndProc = ZeitleisteProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"SZENEN_ZEITLEISTE";
    RegisterClassW(&wc);

    hZeitleiste = CreateWindowExW(0, L"SZENEN_ZEITLEISTE", L"Zeitleiste", WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
                                  0, 0, 800, ZEIT_H, hHauptfenster, nullptr, hInst, nullptr);

    const int y = (ZEIT_H - BTN_H) / 2 + 2;
    int x = 10;
    struct
    {
        int id;
        const wchar_t *text, *tipp;
        int breite;
    } knoepfe[ZEIT_KNOEPFE] = {
        {ID_ZEIT_NEU, L"+ Snapshot", L"Aktuelle Szene als Snapshot zur gewählten Zeit festhalten\n(liegt dort schon einer: eine Sekunde nach dem letzten)", 104},
        {ID_ZEIT_ERSETZEN, L"Ersetzen", L"Gewählten Snapshot mit der aktuellen Szene überschreiben", 84},
        {ID_ZEIT_LOESCHEN, L"Löschen", L"Gewählten Snapshot löschen", 80},
        {ID_ZEIT_VORHER, L"|\u25C0", L"Zum vorherigen Snapshot", 40},
        {ID_ZEIT_ABSPIELEN, L"▶", L"Animation abspielen / anhalten (vom ersten bis zum letzten Snapshot)", 40},
        {ID_ZEIT_NACHHER, L"\u25B6|", L"Zum nächsten Snapshot", 40},
        {ID_ZEIT_WIEDERHOLUNG, L"\u21BB", L"Wiederholung im Abschnitt der aktuellen Zeit (zwischen zwei Snapshots):\n"
                                         L"ausgewählte Objekte drehen, pendeln, wippen oder abrollen lassen.\n"
                                         L"Auch mit Rechtsklick in die Zeitleiste zwischen zwei Rauten.", 40},
    };
    for (int i = 0; i < ZEIT_KNOEPFE; ++i)
    {
        zeitKnoepfe[i] = CreateStyledButton(hZeitleiste, hInst, knoepfe[i].id, knoepfe[i].text, x, y, knoepfe[i].breite, BTN_H);
        tooltip(zeitKnoepfe[i], knoepfe[i].tipp);
        x += knoepfe[i].breite + GAP;
    }
    x += 8;
    // Breite nach der Schrift: laengster Text "Snapshot 99 (s)"
    int beschriftungB = 92;
    {
        HDC dc = GetDC(nullptr);
        HGDIOBJ alt = SelectObject(dc, gFont);
        RECT r = {0, 0, 0, 0};
        DrawTextW(dc, L"Snapshot 99 (s)", -1, &r, DT_CALCRECT | DT_SINGLELINE);
        SelectObject(dc, alt);
        ReleaseDC(nullptr, dc);
        beschriftungB = std::max(beschriftungB, int(r.right - r.left) + 10);
    }
    zeitBeschriftung = CreateLabel(hZeitleiste, hInst, L"Zeit (s)", x, y + 5, beschriftungB, LABEL_H);
    x += beschriftungB;
    zeitFeld = CreateEingabefeld(hZeitleiste, hInst, ID_ZEIT_FELD, ES_RIGHT);
    MoveWindow(zeitFeld, x, y + 2, 70, ZEILE_H, FALSE);
    tooltip(zeitFeld, L"Zeit in Sekunden. Mit gewähltem Snapshot: dessen Zeit ändern; sonst zu dieser Zeit springen (Enter)");
    x += 70 + 12;

    // Beschriftung des gewaehlten Snapshots
    zeitLabelBeschriftung = CreateLabel(hZeitleiste, hInst, L"Label", x, y + 5, 44, LABEL_H);
    x += 44;
    zeitLabelFeld = CreateEingabefeld(hZeitleiste, hInst, ID_ZEIT_LABEL, ES_LEFT | ES_AUTOHSCROLL);
    MoveWindow(zeitLabelFeld, x, y + 2, 150, ZEILE_H, FALSE);
    SendMessageW(zeitLabelFeld, EM_LIMITTEXT, 200, 0);
    tooltip(zeitLabelFeld, L"Beschriftung des gewählten Snapshots (z.B. \"Start\", \"Lampe an\"); erscheint in der Zeitleiste\n"
                           L"neben der Raute und wird mit der Szene gespeichert. Enter oder Verlassen übernimmt.");
    EnableWindow(zeitLabelFeld, FALSE);
    spurLinks = x + 150 + 16;
}

// Ansichten senkrecht zwischen Leiste und (Zeitleiste +) Statusleiste verteilen
static void ansichtenEinpassen(LONG neuUnten)
{
    if (angedockteAnsichten.empty() || ansichtenUnten <= ansichtenOben || neuUnten <= ansichtenOben)
        return;
    const double faktor = double(neuUnten - ansichtenOben) / double(ansichtenUnten - ansichtenOben);
    for (HWND h : angedockteAnsichten)
    {
        RECT r;
        if (!GetWindowRect(h, &r))
            continue;
        int oben = int(ansichtenOben + (r.top - ansichtenOben) * faktor + 0.5);
        int unten = int(ansichtenOben + (r.bottom - ansichtenOben) * faktor + 0.5);
        SetWindowPos(h, nullptr, r.left, oben, r.right - r.left, unten - oben, SWP_NOZORDER | SWP_NOACTIVATE);
    }
    ansichtenUnten = neuUnten;
}

static void zeitleisteUmschalten(bool an)
{
    if (an && !hZeitleiste)
        zeitleisteAnlegen();
    if (!hZeitleiste || (IsWindowVisible(hZeitleiste) != FALSE) == an)
        return;
    if (!an)
        abspielenBeenden();

    if (hHauptfenster)
    {
        ShowWindow(hZeitleiste, an ? SW_SHOWNOACTIVATE : SW_HIDE);
        hauptfensterAnordnen();
        zeitleisteAnzeigen();
        return;
    }
    if (statusOben > 0)
    {
        LONG unten = statusOben - (an ? ZEIT_H : 0);
        ansichtenEinpassen(unten);
        SetWindowPos(hZeitleiste, nullptr, dockLinks, statusOben - ZEIT_H, dockBreite, ZEIT_H, SWP_NOZORDER | SWP_NOACTIVATE);
    }
    ShowWindow(hZeitleiste, an ? SW_SHOWNOACTIVATE : SW_HIDE);
    zeitleisteAnzeigen();
}

// alle 150 ms: Zeitleiste neu zeichnen, wenn sich etwas geaendert hat (z.B. Szene geladen)
static void zeitleisteAbgleichen()
{
    if (animationAktiv() != (hZeitleiste && IsWindowVisible(hZeitleiste)))
        zeitleisteUmschalten(animationAktiv());
    if (!hZeitleiste || !IsWindowVisible(hZeitleiste))
        return;
    static int anzahl = -1;
    static double ende = -1, zeit = -1;
    if (anzahl != snapshotAnzahl() || ende != animationsEnde() || zeit != aktuelleZeit())
    {
        anzahl = snapshotAnzahl();
        ende = animationsEnde();
        zeit = aktuelleZeit();
        zeitleisteAnzeigen();
    }
}

// Code auf dem Hauptthread ausfuehren (Animation rendern: Szene je Bild vorbereiten)
void AufHauptthread(const std::function<void()> &f)
{
    if (hToolWindow)
        SendMessageW(hToolWindow, WM_HAUPTTHREAD, 0, (LPARAM)&f);
    else
        f();
}


//--------------------------------------------------
// Knopf "Einpassen" unten rechts in jeder Ansicht (eigenes kleines Kindfenster)
//--------------------------------------------------

struct EinpassKnopf
{
    HWND knopf = nullptr;
    HWND ansicht = nullptr;
    std::function<void()> aktion;
    bool ueber = false, gedrueckt = false;
};
static std::vector<std::unique_ptr<EinpassKnopf>> einpassKnoepfe;
constexpr int EINPASS_GROESSE = 28, EINPASS_RAND = 8;

// Massstab unten rechts in jeder Ansicht (links neben "Alle Objekte ins Bild"), wie auf einer Karte:
// ein Balken mit runder Laenge (1, 2, 5 mal Zehnerpotenz) in Einheiten der Szene
struct Massstab
{
    HWND fenster = nullptr;
    HWND ansicht = nullptr;
    std::function<double()> einheitenProPixel; // 0: unbekannt
    double angezeigt = -1;
};
static std::vector<std::unique_ptr<Massstab>> massstaebe;
constexpr int MASSSTAB_B = 150;

static void massstaebeSetzen()
{
    for (auto &m : massstaebe)
    {
        RECT r;
        if (!GetClientRect(m->ansicht, &r))
            continue;
        const int x = r.right - EINPASS_GROESSE - EINPASS_RAND - 6 - MASSSTAB_B, y = r.bottom - EINPASS_GROESSE - EINPASS_RAND;
        RECT jetzt;
        GetWindowRect(m->fenster, &jetzt);
        POINT p = {jetzt.left, jetzt.top};
        ScreenToClient(m->ansicht, &p);
        if (p.x != x || p.y != y)
            SetWindowPos(m->fenster, HWND_TOP, x, y, MASSSTAB_B, EINPASS_GROESSE, SWP_NOACTIVATE);
        ShowWindow(m->fenster, x > 0 ? SW_SHOWNA : SW_HIDE); // zu schmale Ansicht: weglassen
    }
}

// runde Laenge <= hoechstens: 1, 2 oder 5 mal Zehnerpotenz
static double rundeMassstabLaenge(double hoechstens)
{
    if (!(hoechstens > 0) || !std::isfinite(hoechstens))
        return 0;
    const double zehn = std::pow(10.0, std::floor(std::log10(hoechstens)));
    for (double f : {5.0, 2.0, 1.0})
        if (f * zehn <= hoechstens * (1.0 + 1e-9))
            return f * zehn;
    return zehn;
}

static std::wstring massstabText(double v)
{
    wchar_t t[64];
    if (v >= 1e6 || v < 1e-4)
        swprintf(t, 64, L"%g", v);
    else if (v >= 1)
        swprintf(t, 64, L"%.0f", v);
    else
    {
        const int stellen = std::max(1, int(std::ceil(-std::log10(v) - 1e-9)));
        swprintf(t, 64, L"%.*f", stellen, v);
    }
    std::wstring r = t;
    for (auto &c : r)
        if (c == L'.')
            c = L','; // wie die anderen Zahlen der Oberflaeche
    return r;
}

static LRESULT CALLBACK MassstabProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    auto *m = reinterpret_cast<Massstab *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg)
    {
    case WM_ERASEBKGND:
        return 1;
    case WM_TIMER: // Zoom geaendert? (Mausrad, Einpassen, Kamera)
        if (m && m->einheitenProPixel)
        {
            const double e = m->einheitenProPixel();
            if (std::fabs(e - m->angezeigt) > 1e-12 * std::max(1.0, std::fabs(e)))
            {
                m->angezeigt = e;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
        }
        return 0;
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT r;
        GetClientRect(hwnd, &r);
        HBRUSH grund = CreateSolidBrush(FARBE_BUTTON);
        FillRect(hdc, &r, grund);
        DeleteObject(grund);
        const double e = m && m->einheitenProPixel ? m->einheitenProPixel() : 0.0;
        if (m)
            m->angezeigt = e;
        const int rand = 8, maxPixel = r.right - 2 * rand - 10;
        const double laenge = rundeMassstabLaenge(e * maxPixel);
        if (e > 0 && laenge > 0)
        {
            const int px = std::max(4, int(std::lround(laenge / e)));
            const int x0 = rand + 3, y0 = 5, hoehe = 7;
            RECT balken = {x0, y0, x0 + px, y0 + hoehe};
            HBRUSH rot = CreateSolidBrush(RGB(150, 25, 25));
            FillRect(hdc, &balken, rot);
            DeleteObject(rot);
            HBRUSH rahmen = CreateSolidBrush(FARBE_TEXT);
            FrameRect(hdc, &balken, rahmen);
            DeleteObject(rahmen);
            HGDIOBJ altFont = SelectObject(hdc, gFont);
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, FARBE_TEXT);
            RECT links = {0, y0 + hoehe, x0 + 30, r.bottom};
            links.left = x0 - 30;
            DrawTextW(hdc, L"0", -1, &links, DT_CENTER | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);
            const std::wstring t = massstabText(laenge);
            RECT rechts = {x0 + px - 60, y0 + hoehe, x0 + px + 60, r.bottom};
            if (rechts.right > r.right) // Text nicht abschneiden
                OffsetRect(&rechts, r.right - rechts.right, 0);
            DrawTextW(hdc, t.c_str(), -1, &rechts, DT_CENTER | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);
            SelectObject(hdc, altFont);
        }
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_DESTROY:
        KillTimer(hwnd, 1);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void MassstabAnlegen(HWND ansicht, std::function<double()> einheitenProPixel, const wchar_t *tipp)
{
    if (!ansicht)
        return;
    HINSTANCE hInst = GetModuleHandleW(nullptr);
    static bool registriert = false;
    if (!registriert)
    {
        WNDCLASSW wc{};
        wc.lpfnWndProc = MassstabProc;
        wc.hInstance = hInst;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.lpszClassName = L"SZENEN_MASSSTAB";
        RegisterClassW(&wc);
        registriert = true;
    }
    SetWindowLongPtrW(ansicht, GWL_STYLE, GetWindowLongPtrW(ansicht, GWL_STYLE) | WS_CLIPCHILDREN);
    auto m = std::make_unique<Massstab>();
    m->ansicht = ansicht;
    m->einheitenProPixel = std::move(einheitenProPixel);
    m->fenster = CreateWindowExW(0, L"SZENEN_MASSSTAB", L"", WS_CHILD | WS_VISIBLE, 0, 0, MASSSTAB_B, EINPASS_GROESSE, ansicht, nullptr,
                                 hInst, nullptr);
    SetWindowLongPtrW(m->fenster, GWLP_USERDATA, (LONG_PTR)m.get());
    SetTimer(m->fenster, 1, 150, nullptr);
    tooltip(m->fenster, tipp);
    massstaebe.push_back(std::move(m));
    massstaebeSetzen();
}

static void einpassKnoepfeSetzen()
{
    massstaebeSetzen();
    for (auto &k : einpassKnoepfe)
    {
        RECT r;
        if (!GetClientRect(k->ansicht, &r))
            continue;
        int x = r.right - EINPASS_GROESSE - EINPASS_RAND, y = r.bottom - EINPASS_GROESSE - EINPASS_RAND;
        RECT jetzt;
        GetWindowRect(k->knopf, &jetzt);
        POINT p = {jetzt.left, jetzt.top};
        ScreenToClient(k->ansicht, &p);
        if (p.x != x || p.y != y)
            SetWindowPos(k->knopf, HWND_TOP, x, y, EINPASS_GROESSE, EINPASS_GROESSE, SWP_NOACTIVATE);
    }
    bildKnopfSetzen(); // Kamerasicht: "Bild/Editor" oben rechts, Render-Bild ueber die ganze Ansicht
}

static LRESULT CALLBACK EinpassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    auto *k = reinterpret_cast<EinpassKnopf *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg)
    {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT r;
        GetClientRect(hwnd, &r);
        COLORREF grund = k && k->gedrueckt ? FARBE_BUTTON_GEDRUECKT : k && k->ueber ? FARBE_BUTTON_HOVER : FARBE_BUTTON;
        HBRUSH pinsel = CreateSolidBrush(grund);
        FillRect(hdc, &r, pinsel);
        DeleteObject(pinsel);

        // vier Ecken (Symbol "alles einpassen") und ein kleines Quadrat in der Mitte
        HPEN stift = CreatePen(PS_SOLID, 2, FARBE_TEXT);
        HGDIOBJ alt = SelectObject(hdc, stift);
        const int a = 6, l = 6, w = r.right, h = r.bottom;
        auto ecke = [&](int x, int y, int dx, int dy)
        {
            MoveToEx(hdc, x + dx * l, y, nullptr);
            LineTo(hdc, x, y);
            LineTo(hdc, x, y + dy * l);
        };
        ecke(a, a, 1, 1);
        ecke(w - a - 1, a, -1, 1);
        ecke(a, h - a - 1, 1, -1);
        ecke(w - a - 1, h - a - 1, -1, -1);
        SelectObject(hdc, alt);
        DeleteObject(stift);
        RECT mitte = {w / 2 - 3, h / 2 - 3, w / 2 + 3, h / 2 + 3};
        HBRUSH weiss = CreateSolidBrush(FARBE_TEXT);
        FillRect(hdc, &mitte, weiss);
        DeleteObject(weiss);

        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_MOUSEMOVE:
        if (k && !k->ueber)
        {
            k->ueber = true;
            TRACKMOUSEEVENT t = {sizeof(t), TME_LEAVE, hwnd, 0};
            TrackMouseEvent(&t);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_MOUSELEAVE:
        if (k)
        {
            k->ueber = false;
            k->gedrueckt = false;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_LBUTTONDOWN:
        if (k)
        {
            k->gedrueckt = true;
            SetCapture(hwnd);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_LBUTTONUP:
        if (k && k->gedrueckt)
        {
            k->gedrueckt = false;
            ReleaseCapture();
            InvalidateRect(hwnd, nullptr, FALSE);
            POINT p = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            RECT r;
            GetClientRect(hwnd, &r);
            if (PtInRect(&r, p) && k->aktion)
            {
                k->aktion();
                abgleichen();
            }
        }
        return 0;
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE; // Ansicht behaelt den Fokus nicht wegen des Knopfs
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void EinpassKnopfAnlegen(HWND ansicht, std::function<void()> aktion, const wchar_t *tipp)
{
    if (!ansicht)
        return;
    HINSTANCE hInst = GetModuleHandleW(nullptr);
    static bool registriert = false;
    if (!registriert)
    {
        WNDCLASSW wc{};
        wc.lpfnWndProc = EinpassProc;
        wc.hInstance = hInst;
        wc.hCursor = LoadCursor(nullptr, IDC_HAND);
        wc.lpszClassName = L"SZENEN_EINPASSEN";
        RegisterClassW(&wc);
        registriert = true;
    }
    // das GPU-Bild der Ansicht soll den Knopf nicht uebermalen
    SetWindowLongPtrW(ansicht, GWL_STYLE, GetWindowLongPtrW(ansicht, GWL_STYLE) | WS_CLIPCHILDREN);

    auto k = std::make_unique<EinpassKnopf>();
    k->ansicht = ansicht;
    k->aktion = std::move(aktion);
    k->knopf = CreateWindowExW(0, L"SZENEN_EINPASSEN", L"", WS_CHILD | WS_VISIBLE, 0, 0, EINPASS_GROESSE,
                               EINPASS_GROESSE, ansicht, nullptr, hInst, nullptr);
    SetWindowLongPtrW(k->knopf, GWLP_USERDATA, (LONG_PTR)k.get());
    tooltip(k->knopf, tipp);
    einpassKnoepfe.push_back(std::move(k));
    einpassKnoepfeSetzen();
}

//--------------------------------------------------
// Leiste ueber den Ansichten andocken, Objektfenster oben rechts
//--------------------------------------------------

void RenderfensterSetzen(HWND fenster)
{
    hRenderfenster = fenster;
    if (hHauptfenster && fenster) // als Reiter "Rendering" im Hauptfenster
    {
        einbetten(fenster);
        ShowWindow(fenster, aktiverReiter == 1 ? SW_SHOWNA : SW_HIDE);
        hauptfensterAnordnen();
    }

    wchar_t titel[512] = {};
    if (fenster)
        GetWindowTextW(fenster, titel, 512);
    renderTitelAnfang = titel;
}

//--------------------------------------------------
// Hauptfenster: Werkzeugleiste, vier Ansichten, Objektfenster, Zeitleiste und Statusleiste als
// Kindfenster in einem Rahmen. Minimieren, Maximieren und in den Hintergrund schicken wirken auf alles.
// Dialoge (Render-Einstellungen, Werte, Farben) gehoeren dem Hauptfenster; das Render-Fenster bleibt eigenstaendig.
//
//  +-------------------------------------------------+
//  | Werkzeugleiste                                  |
//  +----------------------------+--------------------+
//  | Ansicht      | Ansicht     | Objektfenster      |
//  |--------------+-------------| (ausblendbar)      |
//  | Ansicht      | Ansicht     |                    |
//  +----------------------------+--------------------+
//  | Zeitleiste (mit "Animation")                    |
//  | Statusleiste                                    |
//  +-------------------------------------------------+
//--------------------------------------------------

HWND hHauptfenster = nullptr;
static std::vector<std::pair<HWND, int>> hauptAnsichten; // Ansicht, Feld (0 oben links, 1 oben rechts, 2 unten links, 3 unten rechts)
constexpr int ANSICHT_FUGE = 2;                          // Abstand zwischen den Ansichten (dunkle Linie)



// ---------- ziehbarer Teiler zwischen Ansichten und Objektfenster ----------
static int objektBreiteVorgabe() { return PANEL_W + GetSystemMetrics(SM_CXVSCROLL); }

// tatsaechliche Breite der rechten Spalte: mindestens 160, Ansichten behalten mindestens 300 Pixel
static int objektBreiteAktuell(int W)
{
    int b = objektBreite > 0 ? objektBreite : objektBreiteVorgabe();
    b = std::min(b, W - TEILER_B - 300);
    return std::max(160, b);
}

static LRESULT CALLBACK ObjektTeilerProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    static int griffX = 0; // Abstand Maus - linker Rand des Teilers beim Anfassen
    switch (msg)
    {
    case WM_SETCURSOR:
        SetCursor(LoadCursor(nullptr, IDC_SIZEWE));
        return TRUE;
    case WM_LBUTTONDOWN:
        griffX = GET_X_LPARAM(lParam);
        SetCapture(hwnd);
        return 0;
    case WM_MOUSEMOVE:
        if (GetCapture() == hwnd && hHauptfenster)
        {
            POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            MapWindowPoints(hwnd, hHauptfenster, &pt, 1);
            RECT r;
            GetClientRect(hHauptfenster, &r);
            int neu = int(r.right) - (pt.x - griffX) - TEILER_B;
            neu = std::max(160, std::min(neu, int(r.right) - TEILER_B - 300));
            if (neu != objektBreite)
            {
                objektBreite = neu;
                hauptfensterAnordnen();
                UpdateWindow(hHauptfenster);
            }
        }
        return 0;
    case WM_LBUTTONUP:
        if (GetCapture() == hwnd)
            ReleaseCapture();
        editorEinstellungenSpeichernFallsGeaendert(); // Breite fuer den naechsten Start merken
        return 0;
    case WM_LBUTTONDBLCLK: // Doppelklick: zurueck auf die Vorgabe
        objektBreite = 0;
        hauptfensterAnordnen();
        editorEinstellungenSpeichernFallsGeaendert();
        return 0;
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        RECT r;
        GetClientRect(hwnd, &r);
        FillRect(dc, &r, pinselHintergrund);
        // Griff: drei Punkte in der Mitte
        HBRUSH punkt = CreateSolidBrush(RGB(110, 110, 120));
        const int mx = r.right / 2, my = r.bottom / 2;
        for (int i = -1; i <= 1; ++i)
        {
            RECT p = {mx - 1, my + i * 8 - 1, mx + 2, my + i * 8 + 2};
            FillRect(dc, &p, punkt);
        }
        DeleteObject(punkt);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ---------- Rollbereich um das Objektfenster (senkrechte und waagrechte Bildlaufleiste) ----------
// hObjektRahmen: oben definiert

static int objektRollX = 0, objektRollY = 0;

static void objektRahmenAnordnen()
{
    static bool aktiv = false; // Bildlaufleisten aendern die Groesse: nicht verschachtelt
    if (!hObjektRahmen || !hObjektFenster || aktiv)
        return;
    aktiv = true;
    RECT r;
    GetClientRect(hObjektRahmen, &r);
    const int cw = std::max(1, int(r.right)), ch = std::max(1, int(r.bottom));
    const int inhaltW = std::max(PANEL_W, cw);
    const int inhaltH = std::max(objektfensterMindesthoehe(), ch);
    objektRollX = std::max(0, std::min(objektRollX, inhaltW - cw));
    objektRollY = std::max(0, std::min(objektRollY, inhaltH - ch));

    SCROLLINFO si = {sizeof(si)};
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS | SIF_DISABLENOSCROLL; // senkrecht immer sichtbar
    si.nMin = 0;
    si.nMax = inhaltH - 1;
    si.nPage = UINT(ch);
    si.nPos = objektRollY;
    SetScrollInfo(hObjektRahmen, SB_VERT, &si, TRUE);
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS | SIF_DISABLENOSCROLL; // waagrecht: sichtbar, aktiv nur wenn noetig
    si.nMax = inhaltW - 1;
    si.nPage = UINT(cw);
    si.nPos = objektRollX;
    SetScrollInfo(hObjektRahmen, SB_HORZ, &si, TRUE);

    // Groesse kann sich durch die Leisten geaendert haben
    GetClientRect(hObjektRahmen, &r);
    panelHoehe = std::max(objektfensterMindesthoehe(), int(r.bottom));
    SetWindowPos(hObjektFenster, nullptr, -objektRollX, -objektRollY, std::max(PANEL_W, int(r.right)), panelHoehe,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    static int altX = -1, altY = -1; // gerollt: alles neu zeichnen (durchsichtige Beschriftungen)
    if (altX != objektRollX || altY != objektRollY)
    {
        altX = objektRollX;
        altY = objektRollY;
        // nur der neu sichtbare Teil (den zeichnet Windows ohnehin); kein Loeschen aller Elemente mehr
        UpdateWindow(hObjektFenster);
    }
    aktiv = false;
}

static void objektRollen(int dx, int dy)
{
    objektRollX += dx;
    objektRollY += dy;
    objektRahmenAnordnen();
}

static LRESULT CALLBACK ObjektRahmenProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_SIZE:
        objektRahmenAnordnen();
        return 0;
    case WM_VSCROLL:
    case WM_HSCROLL:
    {
        const int leiste = msg == WM_VSCROLL ? SB_VERT : SB_HORZ;
        SCROLLINFO si = {sizeof(si)};
        si.fMask = SIF_ALL;
        GetScrollInfo(hwnd, leiste, &si);
        int pos = si.nPos;
        switch (LOWORD(wParam))
        {
        case SB_LINEUP: pos -= 30; break;
        case SB_LINEDOWN: pos += 30; break;
        case SB_PAGEUP: pos -= int(si.nPage); break;
        case SB_PAGEDOWN: pos += int(si.nPage); break;
        case SB_THUMBTRACK:
        case SB_THUMBPOSITION: pos = si.nTrackPos; break;
        case SB_TOP: pos = si.nMin; break;
        case SB_BOTTOM: pos = si.nMax; break;
        }
        if (msg == WM_VSCROLL)
            objektRollY = pos;
        else
            objektRollX = pos;
        objektRahmenAnordnen();
        return 0;
    }
    case WM_MOUSEWHEEL: // auch aus dem Objektfenster weitergereicht (ausser in der Liste, die selbst rollt)
    {
        int rasten = GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA;
        if (GetKeyState(VK_SHIFT) & 0x8000)
            objektRollen(-rasten * 60, 0);
        else
            objektRollen(0, -rasten * 60);
        return 0;
    }
    case WM_ERASEBKGND:
    {
        RECT r;
        GetClientRect(hwnd, &r);
        FillRect((HDC)wParam, &r, pinselHintergrund);
        return 1;
    }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// Feld bekommt den Fokus (Tab): sichtbar rollen
static void objektFeldZeigen(HWND feld)
{
    if (!hObjektRahmen || !feld || GetParent(feld) != hObjektFenster)
        return;
    RECT f, r;
    GetWindowRect(feld, &f);
    GetWindowRect(hObjektRahmen, &r);
    if (f.top < r.top)
        objektRollen(0, int(f.top - r.top) - 8);
    else if (f.bottom > r.bottom - GetSystemMetrics(SM_CYHSCROLL))
        objektRollen(0, int(f.bottom - r.bottom) + 8 + GetSystemMetrics(SM_CYHSCROLL));
}

// ---------- Reiter "Ansichten | Rendering" unter der Werkzeugleiste ----------
static const wchar_t *const REITER_NAMEN[2] = {L"Ansichten", L"Rendering"};

static RECT reiterFlaeche(int i)
{
    const int b = 130;
    RECT r = {8 + i * (b + 4), 3, 8 + i * (b + 4) + b, REITER_H};
    return r;
}

static LRESULT CALLBACK ReiterProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT r;
        GetClientRect(hwnd, &r);
        FillRect(hdc, &r, pinselFlaeche);
        SetBkMode(hdc, TRANSPARENT);
        HGDIOBJ alt = SelectObject(hdc, gFont);
        for (int i = 0; i < 2; ++i)
        {
            RECT t = reiterFlaeche(i);
            HBRUSH p = CreateSolidBrush(i == aktiverReiter ? FARBE_HINTERGRUND : FARBE_BUTTON);
            FillRect(hdc, &t, p);
            DeleteObject(p);
            if (i == aktiverReiter) // aktiver Reiter: blauer Strich oben
            {
                RECT strich = {t.left, t.top, t.right, t.top + 2};
                HBRUSH b = CreateSolidBrush(FARBE_AKTIV);
                FillRect(hdc, &strich, b);
                DeleteObject(b);
            }
            SetTextColor(hdc, i == aktiverReiter ? FARBE_TEXT : FARBE_TEXT_GEDIMMT);
            DrawTextW(hdc, REITER_NAMEN[i], -1, &t, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
        SelectObject(hdc, alt);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_LBUTTONDOWN:
    {
        POINT p = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        for (int i = 0; i < 2; ++i)
        {
            RECT t = reiterFlaeche(i);
            if (PtInRect(&t, p))
                RenderReiterZeigen(i == 1);
        }
        return 0;
    }
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void RenderReiterZeigen(bool rendering)
{
    if (!hHauptfenster)
        return;
    aktiverReiter = rendering ? 1 : 0;
    for (auto &[h, feld] : hauptAnsichten)
        ShowWindow(h, rendering ? SW_HIDE : SW_SHOWNA);
    if (hRenderfenster)
        ShowWindow(hRenderfenster, rendering ? SW_SHOWNA : SW_HIDE);
    hauptfensterAnordnen();
    if (hReiter)
        InvalidateRect(hReiter, nullptr, FALSE);
    if (hRenderfenster && rendering)
        InvalidateRect(hRenderfenster, nullptr, FALSE);
}

bool RenderReiterAktiv() { return hHauptfenster && aktiverReiter == 1; }

// ---------- Beschriftung oben links in jeder Ansicht ("Top", "Front" ...) ----------
static LRESULT CALLBACK BeschriftungProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_NCHITTEST:
        return HTTRANSPARENT; // Klicks gehen an die Ansicht
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT r;
        GetClientRect(hwnd, &r);
        FillRect(hdc, &r, pinselFlaeche);
        wchar_t text[64] = {};
        GetWindowTextW(hwnd, text, 64);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, FARBE_UEBERSCHRIFT);
        HGDIOBJ alt = SelectObject(hdc, gFont);
        DrawTextW(hdc, text, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(hdc, alt);
        EndPaint(hwnd, &ps);
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void AnsichtBeschriftungAnlegen(HWND ansicht, const wchar_t *text)
{
    if (!ansicht)
        return;
    HINSTANCE hInst = GetModuleHandleW(nullptr);
    static bool registriert = false;
    if (!registriert)
    {
        WNDCLASSW wc{};
        wc.lpfnWndProc = BeschriftungProc;
        wc.hInstance = hInst;
        wc.lpszClassName = L"SZENEN_BESCHRIFTUNG";
        RegisterClassW(&wc);
        registriert = true;
    }
    SetWindowLongPtrW(ansicht, GWL_STYLE, GetWindowLongPtrW(ansicht, GWL_STYLE) | WS_CLIPCHILDREN);
    HDC dc = GetDC(nullptr);
    HGDIOBJ alt = SelectObject(dc, gFont);
    RECT t = {0, 0, 0, 0};
    DrawTextW(dc, text, -1, &t, DT_CALCRECT | DT_SINGLELINE);
    SelectObject(dc, alt);
    ReleaseDC(nullptr, dc);
    CreateWindowExW(0, L"SZENEN_BESCHRIFTUNG", text, WS_CHILD | WS_VISIBLE, 6, 6, int(t.right - t.left) + 16,
                    int(t.bottom - t.top) + 6, ansicht, nullptr, hInst, nullptr);
}

static void hauptfensterAnordnen()
{
    if (!hHauptfenster || IsIconic(hHauptfenster))
        return;
    RECT r;
    GetClientRect(hHauptfenster, &r);
    const int W = r.right, H = r.bottom;
    if (W <= 0 || H <= 0)
        return;

    HDWP dwp = BeginDeferWindowPos(12);
    auto setzen = [&](HWND h, int x, int y, int w, int hh)
    {
        if (h && dwp)
            dwp = DeferWindowPos(dwp, h, nullptr, x, y, std::max(1, w), std::max(1, hh), SWP_NOZORDER | SWP_NOACTIVATE);
    };

    int oben = 0, unten = H;
    setzen(hToolWindow, 0, 0, W, LEISTE_H);
    oben = LEISTE_H;
    if (hReiter)
    {
        setzen(hReiter, 0, oben, W, REITER_H);
        oben += REITER_H;
    }
    if (hStatusleiste)
    {
        unten -= STATUS_H;
        setzen(hStatusleiste, 0, unten, W, STATUS_H);
    }
    if (hZeitleiste && IsWindowVisible(hZeitleiste))
    {
        unten -= ZEIT_H;
        setzen(hZeitleiste, 0, unten, W, ZEIT_H);
    }

    int rechts = W;
    if (hObjektFenster && !eingeklappt)
    {
        // Objektfenster im Rollbereich; Breite mit dem Teiler links davon ziehbar
        const int rahmenB = hObjektRahmen ? objektBreiteAktuell(W) : PANEL_W;
        rechts = W - rahmenB;
        if (hObjektRahmen)
            setzen(hObjektRahmen, rechts, oben, rahmenB, std::max(1, unten - oben));
        else
        {
            panelHoehe = std::max(1, unten - oben);
            setzen(hObjektFenster, rechts, oben, PANEL_W, panelHoehe);
        }
        if (hObjektTeiler)
        {
            rechts -= TEILER_B;
            setzen(hObjektTeiler, rechts, oben, TEILER_B, std::max(1, unten - oben));
        }
        else
            rechts -= ANSICHT_FUGE;
    }

    // Reiter "Rendering": das Render-Bild an Stelle der vier Ansichten
    const int bw = std::max(2, rechts), bh = std::max(2, unten - oben);
    if (aktiverReiter == 1 && hRenderfenster && GetParent(hRenderfenster) == hHauptfenster)
        setzen(hRenderfenster, 0, oben, bw, bh);

    renderFlaecheB = bw;
    renderFlaecheH = bh;

    // 2 x 2 Ansichten; fehlt eine (Kamerasicht auf einem anderen Bildschirm), nimmt ihr Nachbar die ganze Breite
    const int w0 = (bw - ANSICHT_FUGE) / 2, h0 = (bh - ANSICHT_FUGE) / 2;
    for (auto &[h, feld] : hauptAnsichten)
    {
        const int sp = feld % 2, ze = feld / 2;
        bool nachbar = false;
        for (auto &[h2, feld2] : hauptAnsichten)
            nachbar = nachbar || (h2 != h && feld2 == (feld ^ 1));
        const int x = (sp == 0 || !nachbar) ? 0 : w0 + ANSICHT_FUGE;
        const int y = oben + (ze == 0 ? 0 : h0 + ANSICHT_FUGE);
        const int w = !nachbar ? bw : sp == 0 ? w0 : bw - w0 - ANSICHT_FUGE;
        const int hh = ze == 0 ? h0 : bh - h0 - ANSICHT_FUGE;
        setzen(h, x, y, w, hh);
    }
    if (dwp)
        EndDeferWindowPos(dwp);
    if (hObjektRahmen)
        ShowWindow(hObjektRahmen, eingeklappt ? SW_HIDE : SW_SHOWNA);
    if (hObjektTeiler)
        ShowWindow(hObjektTeiler, eingeklappt ? SW_HIDE : SW_SHOWNA);
    objektRahmenAnordnen();
    einpassKnoepfeSetzen();
}

static LRESULT CALLBACK HauptProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_SIZE:
        hauptfensterAnordnen();
        return 0;
    case WM_GETMINMAXINFO:
    {
        auto *mm = reinterpret_cast<MINMAXINFO *>(lParam);
        mm->ptMinTrackSize.x = 900;
        mm->ptMinTrackSize.y = 600;
        return 0;
    }
    case WM_ERASEBKGND:
    {
        RECT r;
        GetClientRect(hwnd, &r);
        FillRect((HDC)wParam, &r, pinselFlaeche); // Fugen zwischen den Ansichten
        return 1;
    }
    case WM_KEYDOWN: // Hauptfenster selbst hat den Fokus
        if (tasteWeitergeben(wParam))
            return 0;
        break;
    case WM_INITMENUPOPUP: // Menueleiste: Eintraege frisch (Haekchen, Rueckgaengig-Name, zuletzt geoeffnet)
        if (!HIWORD(lParam))
            menueFuellen(reinterpret_cast<HMENU>(wParam));
        return 0;
    case WM_COMMAND:
        if (HIWORD(wParam) == 0 && lParam == 0 && menueLeisteBefehl(LOWORD(wParam)))
            return 0;
        break;
    case WM_CLOSE:
        // Beenden wie bisher ueber das Render-Fenster (fragt nach dem Speichern, beendet die Schleife)
        if (hRenderfenster && IsWindow(hRenderfenster))
        {
            SendMessageW(hRenderfenster, WM_CLOSE, 0, 0);
            return 0;
        }
        break;
    case WM_DESTROY:
        hHauptfenster = nullptr;
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// Programm-Icon (Taskleiste, Titelleiste, Alt+Tab): aus der Ressource 1 (raytracer_editor.rc), sonst aus
// raytracer_editor.ico neben der .exe, sonst das Windows-Standardsymbol

// ---------- Fortschritt im Taskleisten-Symbol ----------
// Waehrend gerendert wird, zeigt das Symbol den Fortschritt in Prozent (Zahl und Ring) statt des Programmbilds;
// zusaetzlich fuellt Windows den Taskleisten-Knopf als gruenen Balken (ITaskbarList3).
static HICON iconNormalGross = nullptr, iconNormalKlein = nullptr;

static HICON fortschrittIcon(int groesse, int prozent)
{
    BITMAPV5HEADER bh = {};
    bh.bV5Size = sizeof(bh);
    bh.bV5Width = groesse;
    bh.bV5Height = -groesse; // von oben nach unten
    bh.bV5Planes = 1;
    bh.bV5BitCount = 32;
    bh.bV5Compression = BI_BITFIELDS;
    bh.bV5RedMask = 0x00FF0000;
    bh.bV5GreenMask = 0x0000FF00;
    bh.bV5BlueMask = 0x000000FF;
    bh.bV5AlphaMask = 0xFF000000;
    void *pixel = nullptr;
    HDC dc = GetDC(nullptr);
    HBITMAP farbe = CreateDIBSection(dc, reinterpret_cast<BITMAPINFO *>(&bh), DIB_RGB_COLORS, &pixel, nullptr, 0);
    ReleaseDC(nullptr, dc);
    if (!farbe || !pixel)
        return nullptr;

    HDC mem = CreateCompatibleDC(nullptr);
    HGDIOBJ alt = SelectObject(mem, farbe);
    RECT r = {0, 0, groesse, groesse};
    HBRUSH grund = CreateSolidBrush(RGB(30, 32, 44));
    FillRect(mem, &r, grund);
    DeleteObject(grund);

    // Ring: grau = Rest, orange = erledigt (wie die Kugel im Programm-Icon)
    const int rand = std::max(1, groesse / 16), dicke = std::max(2, groesse / 8);
    auto bogen = [&](COLORREF f, double von, double bis)
    {
        HPEN stift = CreatePen(PS_SOLID, dicke, f);
        HGDIOBJ as = SelectObject(mem, stift);
        HGDIOBJ ab = SelectObject(mem, GetStockObject(NULL_BRUSH));
        const int a = rand + dicke / 2, b = groesse - rand - dicke / 2;
        const double m = groesse * 0.5, rad = (b - a) * 0.5;
        // im Uhrzeigersinn ab 12 Uhr
        auto punkt = [&](double t) { return POINT{int(m + rad * std::sin(t * 2 * M_PI)), int(m - rad * std::cos(t * 2 * M_PI))}; };
        if (bis - von >= 0.999)
            Ellipse(mem, a, a, b, b);
        else if (bis > von)
        {
            POINT p0 = punkt(von), p1 = punkt(bis);
            SetArcDirection(mem, AD_CLOCKWISE);
            Arc(mem, a, a, b, b, p0.x, p0.y, p1.x, p1.y);
        }
        SelectObject(mem, as);
        SelectObject(mem, ab);
        DeleteObject(stift);
    };
    const double anteil = std::clamp(prozent / 100.0, 0.0, 1.0);
    bogen(RGB(70, 74, 90), anteil, 1.0);
    bogen(RGB(242, 110, 40), 0.0, anteil);

    // Zahl in der Mitte (ohne %-Zeichen: passt auch bei 16 Pixeln)
    HFONT schrift = CreateFontW(-int(groesse * (prozent >= 100 ? 0.36 : 0.46)), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
                                DEFAULT_PITCH, L"Segoe UI");
    HGDIOBJ af = SelectObject(mem, schrift);
    SetBkMode(mem, TRANSPARENT);
    SetTextColor(mem, RGB(255, 255, 255));
    std::wstring zahl = std::to_wstring(std::clamp(prozent, 0, 100));
    DrawTextW(mem, zahl.c_str(), -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(mem, af);
    DeleteObject(schrift);
    SelectObject(mem, alt);
    DeleteDC(mem);

    // GDI setzt kein Alpha: ganzes Quadrat deckend
    uint32_t *p = static_cast<uint32_t *>(pixel);
    for (int i = 0; i < groesse * groesse; ++i)
        p[i] |= 0xFF000000u;

    HBITMAP maske = CreateBitmap(groesse, groesse, 1, 1, nullptr);
    ICONINFO ii = {};
    ii.fIcon = TRUE;
    ii.hbmColor = farbe;
    ii.hbmMask = maske;
    HICON icon = CreateIconIndirect(&ii);
    DeleteObject(farbe);
    DeleteObject(maske);
    return icon;
}

// ITaskbarList3 ohne zusaetzliche Bibliothek (ole32 wird zur Laufzeit geladen)
struct TaskleistenFortschritt
{
    void *liste = nullptr;
    bool versucht = false;

    void anlegen()
    {
        if (versucht)
            return;
        versucht = true;
        HMODULE ole = LoadLibraryW(L"ole32.dll");
        if (!ole)
            return;
        using CoInit = HRESULT(WINAPI *)(LPVOID, DWORD);
        using CoCreate = HRESULT(WINAPI *)(REFCLSID, LPUNKNOWN, DWORD, REFIID, LPVOID *);
        auto init = (CoInit)(void *)GetProcAddress(ole, "CoInitializeEx");
        auto create = (CoCreate)(void *)GetProcAddress(ole, "CoCreateInstance");
        if (!init || !create)
            return;
        init(nullptr, 0x2 /* COINIT_APARTMENTTHREADED */);
        static const GUID CLSID_Taskleiste = {0x56FDF344, 0xFD6D, 0x11d0, {0x95, 0x8A, 0x00, 0x60, 0x97, 0xC9, 0xA0, 0x90}};
        static const GUID IID_Taskleiste3 = {0xEA1AFB91, 0x9E28, 0x4B86, {0x90, 0xE9, 0x9E, 0x9F, 0x8A, 0x5E, 0xEF, 0xAF}};
        if (FAILED(create(CLSID_Taskleiste, nullptr, 0x1 /* CLSCTX_INPROC_SERVER */, IID_Taskleiste3, &liste)))
            liste = nullptr;
        else
            methode<HRESULT(__stdcall *)(void *)>(3)(liste); // HrInit
    }
    // vtable: 0-2 IUnknown, 3 HrInit, 4 AddTab, 5 DeleteTab, 6 ActivateTab, 7 SetActiveAlt, 8 MarkFullscreenWindow,
    //         9 SetProgressValue, 10 SetProgressState
    template <class F>
    F methode(int nr) const { return reinterpret_cast<F>((*reinterpret_cast<void ***>(liste))[nr]); }

    // Tooltip ueber dem Vorschaubild des Taskleisten-Knopfs (nullptr: wieder der Fenstertitel)
    void tooltip(HWND h, const wchar_t *text)
    {
        anlegen();
        if (!liste || !h)
            return;
        using SetTip = HRESULT(__stdcall *)(void *, HWND, LPCWSTR);
        methode<SetTip>(19)(liste, h, text); // ITaskbarList3::SetThumbnailTooltip
    }

    void setzen(HWND h, int prozent) // prozent < 0: kein Fortschritt
    {
        anlegen();
        if (!liste || !h)
            return;
        using SetState = HRESULT(__stdcall *)(void *, HWND, int);
        using SetValue = HRESULT(__stdcall *)(void *, HWND, ULONGLONG, ULONGLONG);
        if (prozent < 0)
            methode<SetState>(10)(liste, h, 0 /* TBPF_NOPROGRESS */);
        else
        {
            methode<SetState>(10)(liste, h, 2 /* TBPF_NORMAL */);
            methode<SetValue>(9)(liste, h, ULONGLONG(prozent), 100);
        }
    }
};
static TaskleistenFortschritt taskleiste;

// im Abgleich (alle 150 ms): Fortschritt im Symbol und auf dem Taskleisten-Knopf
static void fortschrittAnzeigen()
{
    if (!hHauptfenster)
        return;
    int prozent = -1;
    if (gAnimationBilder > 0 && gAnimationBild > 0)
    {
        const int n = gAnimationBilder.load(), bild = gAnimationBild.load();
        const int imBild = std::max(0, gRenderProzent.load());
        prozent = int((double(bild - 1) + imBild / 100.0) * 100.0 / std::max(1, n));
    }
    else if (gAnimationBilder > 0) // Video wird erstellt
        prozent = 100;
    else if ((gRenderLaeufe > 0 || rendernRunning) && gRenderProzent >= 0)
        prozent = gRenderProzent.load();

    // Text fuer Tooltip und Fenstertitel (der Titel erscheint auch beim Ueberfahren des Taskleisten-Knopfs)
    std::wstring text;
    if (prozent >= 0)
    {
        if (gAnimationBilder > 0 && gAnimationBild < 0)
            text = L"Animation: Video wird erstellt";
        else if (gAnimationBilder > 0)
            text = L"Animation: " + std::to_wstring(prozent) + L" % (Bild " + std::to_wstring(gAnimationBild.load()) + L" von " +
                   std::to_wstring(gAnimationBilder.load()) + L")";
        else
            text = L"Rendering: " + std::to_wstring(prozent) + L" %";
    }
    static std::wstring angezeigterText = L"?";
    if (text != angezeigterText)
    {
        angezeigterText = text;
        SetWindowTextW(hHauptfenster, text.empty() ? L"3D-Editor und RayTracer" : (L"3D-Editor und RayTracer \u2013 " + text).c_str());
        taskleiste.tooltip(hHauptfenster, text.empty() ? nullptr : text.c_str());
    }

    static int angezeigt = -2;
    if (prozent == angezeigt)
        return;
    angezeigt = prozent;

    static HICON letztesGross = nullptr, letztesKlein = nullptr;
    if (prozent < 0)
    {
        SendMessageW(hHauptfenster, WM_SETICON, ICON_BIG, (LPARAM)iconNormalGross);
        SendMessageW(hHauptfenster, WM_SETICON, ICON_SMALL, (LPARAM)iconNormalKlein);
    }
    else
    {
        HICON g = fortschrittIcon(GetSystemMetrics(SM_CXICON), prozent);
        HICON k = fortschrittIcon(GetSystemMetrics(SM_CXSMICON), prozent);
        if (g)
            SendMessageW(hHauptfenster, WM_SETICON, ICON_BIG, (LPARAM)g);
        if (k)
            SendMessageW(hHauptfenster, WM_SETICON, ICON_SMALL, (LPARAM)k);
        if (letztesGross)
            DestroyIcon(letztesGross);
        if (letztesKlein)
            DestroyIcon(letztesKlein);
        letztesGross = g;
        letztesKlein = k;
    }
    taskleiste.setzen(hHauptfenster, prozent);
}

// ======================================================================
// Fortschrittsanzeige fuer laengere Arbeiten (fortschritt.h)
// Die Arbeit laeuft im Thread der Oberflaeche; das kleine Fenster wird darum direkt neu gezeichnet
// (RedrawWindow mit RDW_UPDATENOW), ohne Nachrichten zu verarbeiten - so kann waehrenddessen nichts anderes die
// Szene veraendern. PeekMessage ohne Entfernen haelt Windows davon ab, das Programm als "reagiert nicht" zu zeigen.
// ======================================================================
namespace fortschritt
{
struct Ebene
{
    double lo = 0, hi = 1, jetzt = 0, kindHi = 1;
    std::wstring titel, text;
};
static std::vector<Ebene> stapel;
static ULONGLONG beginn = 0, gezeichnet = 0;
static HWND fenster = nullptr, beschriftung = nullptr, balken = nullptr, prozentText = nullptr;
static HCURSOR alterCursor = nullptr;

static bool aufOberflaeche()
{
    HWND h = hHauptfenster ? hHauptfenster : hToolWindow;
    return h && GetWindowThreadProcessId(h, nullptr) == GetCurrentThreadId();
}

static LRESULT CALLBACK FortschrittProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_ERASEBKGND:
    {
        RECT r;
        GetClientRect(hwnd, &r);
        FillRect((HDC)wParam, &r, pinselHintergrund);
        return 1;
    }
    case WM_CTLCOLORSTATIC:
    {
        HDC hdc = (HDC)wParam;
        SetTextColor(hdc, FARBE_TEXT);
        SetBkColor(hdc, FARBE_HINTERGRUND);
        return (LRESULT)pinselHintergrund;
    }
    case WM_NCHITTEST:
        return HTCAPTION; // laesst sich verschieben
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void fensterAnlegen()
{
    if (fenster)
        return;
    HINSTANCE hInst = GetModuleHandleW(nullptr);
    static bool registriert = false;
    if (!registriert)
    {
        WNDCLASSW wc{};
        wc.lpfnWndProc = FortschrittProc;
        wc.hInstance = hInst;
        wc.hCursor = LoadCursor(nullptr, IDC_WAIT);
        wc.lpszClassName = L"SZENEN_FORTSCHRITT";
        RegisterClassW(&wc);
        registriert = true;
    }
    const int b = 440, h = 96;
    HWND bezug = hHauptfenster ? hHauptfenster : hToolWindow;
    RECT r = {0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
    if (bezug)
        GetWindowRect(bezug, &r);
    const int x = (r.left + r.right) / 2 - b / 2, y = (r.top + r.bottom) / 2 - h / 2;
    fenster = CreateWindowExW(WS_EX_TOOLWINDOW, L"SZENEN_FORTSCHRITT", L"", WS_POPUP | WS_BORDER, x, y, b, h,
                              bezug, nullptr, hInst, nullptr);
    if (!fenster)
        return;
    beschriftung = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_ENDELLIPSIS | SS_NOPREFIX, 16, 14, b - 32, 20,
                                   fenster, nullptr, hInst, nullptr);
    balken = CreateWindowExW(0, PROGRESS_CLASSW, L"", WS_CHILD | WS_VISIBLE | PBS_SMOOTH, 16, 42, b - 32 - 56, 18, fenster,
                             nullptr, hInst, nullptr);
    prozentText = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_RIGHT, b - 16 - 48, 42, 48, 20, fenster, nullptr,
                                  hInst, nullptr);
    SendMessageW(balken, PBM_SETRANGE32, 0, 1000);
    for (HWND k : {beschriftung, prozentText})
        SendMessageW(k, WM_SETFONT, (WPARAM)gFont, TRUE);
    ShowWindow(fenster, SW_SHOWNOACTIVATE);
}

static void zeigen(bool sofort)
{
    if (stapel.empty())
        return;
    const ULONGLONG jetzt = GetTickCount64();
    if (jetzt - beginn < 300) // kurze Arbeiten: nichts anzeigen
        return;
    if (!sofort && jetzt - gezeichnet < 60)
        return;
    gezeichnet = jetzt;
    fensterAnlegen();
    if (!fenster)
        return;
    if (!alterCursor)
        alterCursor = SetCursor(LoadCursor(nullptr, IDC_WAIT));
    const double anteil = std::clamp(stapel.back().jetzt, 0.0, 1.0);
    // Text: aeusserster Titel, dann die genaueste Angabe (innerster Abschnitt mit Text)
    std::wstring t = stapel.front().titel;
    for (size_t i = stapel.size(); i-- > 0;)
    {
        const Ebene &e = stapel[i];
        std::wstring teil = !e.text.empty() ? e.text : (i > 0 ? e.titel : std::wstring());
        if (!teil.empty())
        {
            t += L" \u2013 " + teil;
            break;
        }
    }
    SetWindowTextW(beschriftung, (t + L" \u2026").c_str());
    SendMessageW(balken, PBM_SETPOS, WPARAM(std::lround(anteil * 1000.0)), 0);
    const int prozent = int(std::lround(anteil * 100.0));
    SetWindowTextW(prozentText, (std::to_wstring(prozent) + L" %").c_str());
    RedrawWindow(fenster, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW | RDW_ALLCHILDREN);
    if (hHauptfenster)
        taskleiste.setzen(hHauptfenster, prozent);
    MSG m;
    PeekMessageW(&m, nullptr, 0, 0, PM_NOREMOVE); // "reagiert nicht" vermeiden
}

void beginnen(const std::wstring &titel)
{
    if (!aufOberflaeche())
        return;
    Ebene e;
    if (stapel.empty())
    {
        beginn = GetTickCount64();
        gezeichnet = 0;
    }
    else
    {
        const Ebene &p = stapel.back();
        e.lo = e.jetzt = p.jetzt;
        e.hi = e.kindHi = std::max(p.jetzt, p.kindHi);
    }
    e.titel = titel;
    stapel.push_back(e);
    zeigen(false);
}

void schritt(size_t i, size_t n, const std::wstring &text)
{
    if (stapel.empty() || !aufOberflaeche())
        return;
    Ebene &e = stapel.back();
    const double a = n ? double(std::min(i, n)) / double(n) : 0.0, b = n ? double(std::min(i + 1, n)) / double(n) : 1.0;
    e.jetzt = e.lo + a * (e.hi - e.lo);
    e.kindHi = e.lo + b * (e.hi - e.lo);
    if (!text.empty())
        e.text = text;
    zeigen(false);
}

void bereich(double von, double bis, const std::wstring &text)
{
    if (stapel.empty() || !aufOberflaeche())
        return;
    Ebene &e = stapel.back();
    von = std::clamp(von, 0.0, 1.0);
    bis = std::clamp(bis, von, 1.0);
    e.jetzt = e.lo + von * (e.hi - e.lo);
    e.kindHi = e.lo + bis * (e.hi - e.lo);
    if (!text.empty())
        e.text = text;
    zeigen(false);
}

void melden(double anteil, const std::wstring &text)
{
    if (stapel.empty() || !aufOberflaeche())
        return;
    Ebene &e = stapel.back();
    anteil = std::clamp(anteil, 0.0, 1.0);
    e.jetzt = e.kindHi = e.lo + anteil * (e.hi - e.lo);
    if (!text.empty())
        e.text = text;
    zeigen(false);
}

void ende()
{
    if (stapel.empty() || !aufOberflaeche())
        return;
    const double fertig = stapel.back().hi;
    stapel.pop_back();
    if (!stapel.empty())
    {
        stapel.back().jetzt = std::max(stapel.back().jetzt, fertig); // innerer Teil fertig
        zeigen(false);
        return;
    }
    if (fenster)
    {
        DestroyWindow(fenster);
        fenster = beschriftung = balken = prozentText = nullptr;
        if (hHauptfenster)
            taskleiste.setzen(hHauptfenster, -1);
    }
    if (alterCursor)
    {
        SetCursor(alterCursor);
        alterCursor = nullptr;
    }
}
} // namespace fortschritt

static HICON programmIcon(HINSTANCE hInst, int groesse)
{
    HICON h = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(1), IMAGE_ICON, groesse, groesse, LR_DEFAULTCOLOR);
    if (!h)
    {
        wchar_t pfad[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, pfad, MAX_PATH);
        std::wstring datei = pfad;
        size_t schnitt = datei.find_last_of(L"/\\");
        datei = (schnitt == std::wstring::npos ? std::wstring() : datei.substr(0, schnitt + 1)) + L"raytracer_editor.ico";
        h = (HICON)LoadImageW(nullptr, datei.c_str(), IMAGE_ICON, groesse, groesse, LR_LOADFROMFILE);
    }
    return h ? h : LoadIcon(nullptr, IDI_APPLICATION);
}

static void hauptfensterAnlegen(HINSTANCE hInst)
{
    HICON gross = programmIcon(hInst, GetSystemMetrics(SM_CXICON));
    HICON klein = programmIcon(hInst, GetSystemMetrics(SM_CXSMICON));
    iconNormalGross = gross; // nach dem Rendern wieder dieses Bild
    iconNormalKlein = klein;
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = HauptProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = gross;
    wc.hIconSm = klein;
    wc.lpszClassName = L"SZENEN_HAUPTFENSTER";
    RegisterClassExW(&wc);
    hHauptfenster = CreateWindowExW(WS_EX_APPWINDOW, L"SZENEN_HAUPTFENSTER", L"3D-Editor und RayTracer",
                                    WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, 1600, 1000,
                                    nullptr, nullptr, hInst, nullptr);
    if (hHauptfenster)
    {
        SendMessageW(hHauptfenster, WM_SETICON, ICON_BIG, (LPARAM)gross);
        SendMessageW(hHauptfenster, WM_SETICON, ICON_SMALL, (LPARAM)klein);
    }
}

// Fenster eines anderen Teils (Ansicht) zum Kindfenster des Hauptfensters machen
static void einbetten(HWND h)
{
    if (!hHauptfenster || !h)
        return;
    LONG_PTR stil = GetWindowLongPtrW(h, GWL_STYLE);
    stil &= ~LONG_PTR(WS_POPUP | WS_OVERLAPPEDWINDOW);
    stil |= WS_CHILD | WS_CLIPSIBLINGS | WS_CLIPCHILDREN;
    SetWindowLongPtrW(h, GWL_STYLE, stil); // erst Kind-Stil, dann neuer Elter (wie von SetParent verlangt)
    LONG_PTR ex = GetWindowLongPtrW(h, GWL_EXSTYLE);
    ex &= ~LONG_PTR(WS_EX_APPWINDOW | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE);
    SetWindowLongPtrW(h, GWL_EXSTYLE, ex);
    SetParent(h, hHauptfenster);
    SetWindowPos(h, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED | SWP_NOACTIVATE);
    ShowWindow(h, SW_SHOWNA);
}


//--------------------------------------------------
// Kamerasicht: Render-Bild statt Editor-Ansicht (Knopf "Bild/Editor", Taste B) und Kamerasicht auf einem
// externen Bildschirm (Menue "Datei", F11 = Vollbild/Fenster)
//--------------------------------------------------

static HWND hKamerasicht = nullptr;       // Perspektiv-Ansicht
static Fenster *renderQuelle = nullptr;   // Render-Fenster (Pixelpuffer)
static HWND hBildUeberlagerung = nullptr; // Kind der Kamerasicht: zeigt das Render-Bild
static HWND hBildKnopf = nullptr;         // "Bild" / "Editor"
static bool bildAnzeigen = false;
static bool bildKnopfUeber = false;
static unsigned angezeigterBildStand = ~0u;
static bool renderteZuletzt = false;
static double renderSignatur = 0;   // Zustand der Szene beim Start des Renderns
static bool renderSignaturGueltig = false;
static bool bildVeraltetAngezeigt = false;
static int kamerasichtFeld = 0;     // Feld im 2x2-Raster (zum Zurueckholen)
static bool kamerasichtAbgedockt = false, kamerasichtVollbild = true;
static RECT kamerasichtFensterRect = {0, 0, 0, 0}; // Lage als Fenster (F11)
constexpr int BILD_KNOPF_H = 24;
#define TIMER_KAMERASICHT 7

// einfache Kennzahl der Szene: Kamera, Lage/Groesse/Farbe der Objekte, Lichter (fuer "Bild veraltet")
static double szenenSignatur()
{
    double s = 0, k = 1;
    auto dazu = [&](double v)
    {
        s += v * k;
        k = k * 1.000003 + 0.618;
        if (k > 1e6)
            k = 1.0 + std::fmod(k, 7.0);
    };
    if (!cam.empty())
    {
        for (const Vector3D &v : {cam[0].position, cam[0].lookAt})
            dazu(v.x), dazu(v.y), dazu(v.z);
        dazu(cam[0].fov);
    }
    std::lock_guard<std::mutex> lock(sceneMutex);
    dazu(double(scene.size()));
    for (auto &o : scene)
    {
        for (const Vector3D &v : {o->minBound, o->maxBound})
            dazu(v.x), dazu(v.y), dazu(v.z);
        dazu(o->mat.diffuse.r), dazu(o->mat.diffuse.g), dazu(o->mat.diffuse.b), dazu(o->mat.transparency);
    }
    dazu(double(lights.size()));
    for (auto &l : lights)
    {
        const Vector3D p = l->getPosition();
        dazu(p.x), dazu(p.y), dazu(p.z);
    }
    return s;
}

static bool rendertGerade() { return gRenderLaeufe > 0 || rendernRunning; }

static void bildKnopfSetzen()
{
    if (!hKamerasicht || !hBildKnopf)
        return;
    RECT r;
    GetClientRect(hKamerasicht, &r);
    const int b = 78;
    SetWindowPos(hBildKnopf, HWND_TOP, r.right - b - EINPASS_RAND, EINPASS_RAND, b, BILD_KNOPF_H, SWP_NOACTIVATE);
    if (hBildUeberlagerung)
        SetWindowPos(hBildUeberlagerung, HWND_BOTTOM, 0, 0, std::max(1L, r.right), std::max(1L, r.bottom), SWP_NOACTIVATE);
}

static void bildZeigen(bool an)
{
    bildAnzeigen = an && hBildUeberlagerung;
    if (!hBildUeberlagerung)
        return;
    bildKnopfSetzen();
    ShowWindow(hBildUeberlagerung, bildAnzeigen ? SW_SHOWNA : SW_HIDE);
    angezeigterBildStand = ~0u;
    InvalidateRect(hBildUeberlagerung, nullptr, FALSE);
    InvalidateRect(hBildKnopf, nullptr, FALSE);
}

void KamerasichtBildUmschalten() { bildZeigen(!bildAnzeigen); }

static LRESULT CALLBACK BildUeberlagerungProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT r;
        GetClientRect(hwnd, &r);
        HDC mem = CreateCompatibleDC(hdc);
        HBITMAP bmp = CreateCompatibleBitmap(hdc, std::max(1L, r.right), std::max(1L, r.bottom));
        HGDIOBJ alt = SelectObject(mem, bmp);
        FillRect(mem, &r, (HBRUSH)GetStockObject(BLACK_BRUSH));

        static std::vector<uint32_t> pixel;
        int b = 0, h = 0;
        // ein Bild gibt es erst nach einem Rendering in dieser Sitzung (vorher ist der Puffer leer)
        const bool da = renderQuelle && (renderSignaturGueltig || rendertGerade()) && renderQuelle->bildKopie(pixel, b, h);
        if (da)
        {
            // eingepasst, Seitenverhaeltnis bleibt (schwarzer Rand)
            const double f = std::min(double(r.right) / b, double(r.bottom) / h);
            const int zb = std::max(1, int(b * f)), zh = std::max(1, int(h * f));
            const int zx = (r.right - zb) / 2, zy = (r.bottom - zh) / 2;
            BITMAPINFO bi = {};
            bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            bi.bmiHeader.biWidth = b;
            bi.bmiHeader.biHeight = -h; // von oben nach unten
            bi.bmiHeader.biPlanes = 1;
            bi.bmiHeader.biBitCount = 32;
            bi.bmiHeader.biCompression = BI_RGB;
            SetStretchBltMode(mem, f < 1.0 ? HALFTONE : COLORONCOLOR);
            SetBrushOrgEx(mem, 0, 0, nullptr);
            StretchDIBits(mem, zx, zy, zb, zh, 0, 0, b, h, pixel.data(), &bi, DIB_RGB_COLORS, SRCCOPY);
        }
        // Hinweise
        std::wstring hinweis;
        if (!da)
            hinweis = L"Noch kein Render-Bild – Render-Knopf oder Taste Y";
        else if (rendertGerade())
            hinweis = L"Rendering läuft …";
        else if (bildVeraltetAngezeigt)
            hinweis = L"Bild veraltet (Szene oder Kamera geändert) – Y = neu rendern";
        if (!hinweis.empty())
        {
            SetBkMode(mem, TRANSPARENT);
            HGDIOBJ af = SelectObject(mem, gFont);
            RECT t = {0, 0, 0, 0};
            DrawTextW(mem, hinweis.c_str(), -1, &t, DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
            const int tb = t.right - t.left + 20, th = t.bottom - t.top + 10;
            RECT feld = {(r.right - tb) / 2, da ? r.bottom - th - 10 : (r.bottom - th) / 2, 0, 0};
            feld.right = feld.left + tb;
            feld.bottom = feld.top + th;
            HBRUSH grund = CreateSolidBrush(RGB(30, 33, 38));
            FillRect(mem, &feld, grund);
            DeleteObject(grund);
            SetTextColor(mem, rendertGerade() || !da ? RGB(220, 225, 235) : RGB(255, 200, 90));
            DrawTextW(mem, hinweis.c_str(), -1, &feld, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            SelectObject(mem, af);
        }
        BitBlt(hdc, 0, 0, r.right, r.bottom, mem, 0, 0, SRCCOPY);
        SelectObject(mem, alt);
        DeleteObject(bmp);
        DeleteDC(mem);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
    case WM_MBUTTONDOWN:
        bildZeigen(false); // Klick ins Bild: zurueck zum Editor
        if (hKamerasicht)
            SetFocus(hKamerasicht);
        return 0;
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static LRESULT CALLBACK BildKnopfProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT r;
        GetClientRect(hwnd, &r);
        HBRUSH grund = CreateSolidBrush(bildKnopfUeber ? FARBE_AKTIV_HOVER : bildAnzeigen ? FARBE_AKTIV : RGB(45, 49, 56));
        FillRect(hdc, &r, grund);
        DeleteObject(grund);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, FARBE_TEXT);
        HGDIOBJ af = SelectObject(hdc, gFont);
        DrawTextW(hdc, bildAnzeigen ? L"◧ Editor" : L"◨ Bild", -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        SelectObject(hdc, af);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_MOUSEMOVE:
        if (!bildKnopfUeber)
        {
            bildKnopfUeber = true;
            TRACKMOUSEEVENT t = {sizeof(t), TME_LEAVE, hwnd, 0};
            TrackMouseEvent(&t);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_MOUSELEAVE:
        bildKnopfUeber = false;
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_LBUTTONUP:
        KamerasichtBildUmschalten();
        return 0;
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_TIMER:
        if (wParam == TIMER_KAMERASICHT)
        {
            // Rendering begonnen: Zustand merken (fuer "veraltet"); neues Bild: neu zeichnen
            const bool rendert = rendertGerade();
            if (rendert && !renderteZuletzt)
            {
                renderSignatur = szenenSignatur();
                renderSignaturGueltig = true;
            }
            renderteZuletzt = rendert;
            if (bildAnzeigen && hBildUeberlagerung)
            {
                const bool veraltet = renderSignaturGueltig && !rendert && szenenSignatur() != renderSignatur;
                const unsigned stand = renderQuelle ? unsigned(renderQuelle->bildStand) : 0u;
                if (stand != angezeigterBildStand || veraltet != bildVeraltetAngezeigt)
                {
                    angezeigterBildStand = stand;
                    bildVeraltetAngezeigt = veraltet;
                    InvalidateRect(hBildUeberlagerung, nullptr, FALSE);
                }
            }
        }
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ---------- Kamerasicht auf einem anderen Bildschirm ----------

struct Bildschirm
{
    std::wstring geraet;
    RECT flaeche;
    bool haupt;
};
static BOOL CALLBACK bildschirmGefunden(HMONITOR m, HDC, LPRECT, LPARAM daten)
{
    MONITORINFOEXW mi = {};
    mi.cbSize = sizeof(mi);
    if (GetMonitorInfoW(m, &mi))
        reinterpret_cast<std::vector<Bildschirm> *>(daten)->push_back({mi.szDevice, mi.rcMonitor, (mi.dwFlags & MONITORINFOF_PRIMARY) != 0});
    return TRUE;
}
static std::vector<Bildschirm> bildschirme()
{
    std::vector<Bildschirm> r;
    EnumDisplayMonitors(nullptr, nullptr, bildschirmGefunden, reinterpret_cast<LPARAM>(&r));
    return r;
}

static void kamerasichtLageSetzen()
{
    if (!kamerasichtAbgedockt || !hKamerasicht)
        return;
    RECT ziel = {0, 0, 0, 0};
    for (const Bildschirm &b : bildschirme())
        if (b.geraet == kamerasichtGeraet)
            ziel = b.flaeche;
    if (ziel.right <= ziel.left)
        return;
    LONG_PTR stil = GetWindowLongPtrW(hKamerasicht, GWL_STYLE);
    stil &= ~LONG_PTR(WS_CHILD | WS_POPUP | WS_OVERLAPPEDWINDOW);
    if (kamerasichtVollbild)
        stil |= WS_POPUP; // randlos
    else
        stil |= WS_OVERLAPPED | WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX; // ohne Schliessen-Knopf
    stil |= WS_CLIPCHILDREN | WS_VISIBLE;
    SetWindowLongPtrW(hKamerasicht, GWL_STYLE, stil);
    RECT r = ziel;
    if (!kamerasichtVollbild)
    {
        if (kamerasichtFensterRect.right > kamerasichtFensterRect.left)
            r = kamerasichtFensterRect;
        else
        {
            const int b = (ziel.right - ziel.left) * 3 / 4, h = (ziel.bottom - ziel.top) * 3 / 4;
            r = {ziel.left + (ziel.right - ziel.left - b) / 2, ziel.top + (ziel.bottom - ziel.top - h) / 2, 0, 0};
            r.right = r.left + b;
            r.bottom = r.top + h;
        }
    }
    SetWindowPos(hKamerasicht, HWND_TOP, r.left, r.top, r.right - r.left, r.bottom - r.top,
                 SWP_FRAMECHANGED | SWP_SHOWWINDOW | SWP_NOACTIVATE);
    bildKnopfSetzen();
    einpassKnoepfeSetzen();
}

static void kamerasichtAbdocken(const std::wstring &geraet)
{
    if (!hKamerasicht || !hHauptfenster)
        return;
    if (!kamerasichtAbgedockt)
    {
        for (auto it = hauptAnsichten.begin(); it != hauptAnsichten.end(); ++it)
            if (it->first == hKamerasicht)
            {
                kamerasichtFeld = it->second;
                hauptAnsichten.erase(it);
                break;
            }
        SetWindowLongPtrW(hKamerasicht, GWL_STYLE, (GetWindowLongPtrW(hKamerasicht, GWL_STYLE) & ~LONG_PTR(WS_CHILD)) | WS_POPUP);
        SetParent(hKamerasicht, nullptr);
        SetWindowTextW(hKamerasicht, L"Kamerasicht");
        kamerasichtAbgedockt = true;
    }
    kamerasichtGeraet = geraet;
    kamerasichtLageSetzen();
    hauptfensterAnordnen(); // die anderen drei Ansichten nehmen den Platz ein
}

static void kamerasichtZurueck()
{
    if (!kamerasichtAbgedockt || !hKamerasicht)
        return;
    kamerasichtAbgedockt = false;
    kamerasichtGeraet.clear();
    einbetten(hKamerasicht);
    hauptAnsichten.push_back({hKamerasicht, kamerasichtFeld});
    hauptfensterAnordnen();
    bildKnopfSetzen();
}

// Fenster der Kamerasicht: B = Bild/Editor, F11 = Vollbild/Fenster; Schliessen (Alt+F4) holt sie zurueck
static LRESULT CALLBACK KamerasichtUnterklasse(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR)
{
    switch (msg)
    {
    case WM_KEYDOWN:
        if (wParam == 'B' && !(GetKeyState(VK_CONTROL) & 0x8000))
        {
            KamerasichtBildUmschalten();
            return 0;
        }
        if (wParam == VK_F11 && kamerasichtAbgedockt)
        {
            if (kamerasichtVollbild == false)
                GetWindowRect(hwnd, &kamerasichtFensterRect);
            kamerasichtVollbild = !kamerasichtVollbild;
            kamerasichtLageSetzen();
            return 0;
        }
        break;
    case WM_CLOSE:
        if (kamerasichtAbgedockt)
        {
            kamerasichtZurueck();
            return 0;
        }
        break;
    case WM_SIZE:
    {
        LRESULT r = DefSubclassProc(hwnd, msg, wParam, lParam);
        bildKnopfSetzen();
        return r;
    }
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

void KamerasichtAnmelden(HWND kamerasicht, Fenster *render)
{
    hKamerasicht = kamerasicht;
    renderQuelle = render;
    if (!kamerasicht)
        return;
    HINSTANCE hInst = GetModuleHandleW(nullptr);
    WNDCLASSW wc{};
    wc.lpfnWndProc = BildUeberlagerungProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"SZENEN_RENDERBILD";
    RegisterClassW(&wc);
    WNDCLASSW wk{};
    wk.lpfnWndProc = BildKnopfProc;
    wk.hInstance = hInst;
    wk.hCursor = LoadCursor(nullptr, IDC_HAND);
    wk.lpszClassName = L"SZENEN_BILDKNOPF";
    RegisterClassW(&wk);
    SetWindowLongPtrW(kamerasicht, GWL_STYLE, GetWindowLongPtrW(kamerasicht, GWL_STYLE) | WS_CLIPCHILDREN);
    hBildUeberlagerung = CreateWindowExW(0, L"SZENEN_RENDERBILD", L"", WS_CHILD, 0, 0, 10, 10, kamerasicht, nullptr, hInst, nullptr);
    hBildKnopf = CreateWindowExW(0, L"SZENEN_BILDKNOPF", L"", WS_CHILD | WS_VISIBLE, 0, 0, 10, 10, kamerasicht, nullptr, hInst, nullptr);
    tooltip(hBildKnopf, L"Kamerasicht: zwischen der Editor-Ansicht und dem gerenderten Bild wechseln (Taste B).\n"
                        L"Während des Renderns entsteht das Bild hier live; die anderen Ansichten bleiben zum Arbeiten frei.\n"
                        L"Klick ins Bild: zurück zum Editor. Auf einen anderen Bildschirm: Menü \"Datei\".");
    SetTimer(hBildKnopf, TIMER_KAMERASICHT, 250, nullptr);
    SetWindowSubclass(kamerasicht, KamerasichtUnterklasse, 1, 0);
    bildKnopfSetzen();
    // zuletzt benutzter Bildschirm (ini), wenn er noch angeschlossen ist
    if (!kamerasichtGeraet.empty())
    {
        bool da = false;
        for (const Bildschirm &b : bildschirme())
            da = da || b.geraet == kamerasichtGeraet;
        if (da)
            kamerasichtAbdocken(kamerasichtGeraet);
        else
            kamerasichtGeraet.clear();
    }
}

// Render-Start: liegen die Ansichten vorne, erscheint das Bild in der Kamerasicht (statt Reiter "Rendering")
bool RenderStartInKamerasicht()
{
    if (!hHauptfenster || aktiverReiter != 0 || !hKamerasicht || !hBildUeberlagerung || !IsWindowVisible(hKamerasicht))
        return false;
    // Bildgroesse wie im Reiter "Rendering" (Flaeche der Ansichten), das Render-Fenster bleibt verborgen
    if (hRenderfenster && renderFlaecheB > 1 && renderFlaecheH > 1)
        SetWindowPos(hRenderfenster, nullptr, 0, 0, renderFlaecheB, renderFlaecheH, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    renderSignatur = szenenSignatur(); // Stand fuer "Bild veraltet"
    renderSignaturGueltig = true;
    bildZeigen(true);
    return true;
}

// Eintraege im Menue "Datei"; liefert true, wenn die Wahl hierher gehoerte
static constexpr int M_KAMERASICHT_BILD = 200, M_KAMERASICHT_ZURUECK = 201, M_KAMERASICHT_VOLLBILD = 202,
                     M_KAMERASICHT_AUFLOESUNG = 203, M_KAMERASICHT_SCHIRM = 210;
static void kamerasichtMenueEintraege(HMENU m)
{
    if (!hKamerasicht)
        return;
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING | (bildAnzeigen ? MF_CHECKED : 0), M_KAMERASICHT_BILD, L"Render-Bild in der Kamerasicht\tB");
    const auto liste = bildschirme();
    for (size_t i = 0; i < liste.size() && i < 9; ++i)
    {
        const Bildschirm &b = liste[i];
        std::wstring t = L"Kamerasicht auf Bildschirm " + std::to_wstring(i + 1) + L" (" +
                         std::to_wstring(b.flaeche.right - b.flaeche.left) + L" × " +
                         std::to_wstring(b.flaeche.bottom - b.flaeche.top) + L")" + (b.haupt ? L" – Hauptbildschirm" : L"");
        AppendMenuW(m, MF_STRING | (kamerasichtAbgedockt && b.geraet == kamerasichtGeraet ? MF_CHECKED : 0),
                    UINT(M_KAMERASICHT_SCHIRM + int(i)), t.c_str());
    }
    if (kamerasichtAbgedockt)
    {
        AppendMenuW(m, MF_STRING, M_KAMERASICHT_ZURUECK, L"Kamerasicht zurück ins Hauptfenster");
        AppendMenuW(m, MF_STRING | (kamerasichtVollbild ? MF_CHECKED : 0), M_KAMERASICHT_VOLLBILD, L"Kamerasicht als Vollbild\tF11");
        RECT r;
        GetClientRect(hKamerasicht, &r);
        std::wstring t = L"Bildgröße = Kamerasicht (" + std::to_wstring(r.right) + L" × " + std::to_wstring(r.bottom) + L")";
        AppendMenuW(m, MF_STRING, M_KAMERASICHT_AUFLOESUNG, t.c_str());
    }
}
static bool kamerasichtMenueWahl(int wahl)
{
    if (wahl == M_KAMERASICHT_BILD)
        KamerasichtBildUmschalten();
    else if (wahl == M_KAMERASICHT_ZURUECK)
        kamerasichtZurueck();
    else if (wahl == M_KAMERASICHT_VOLLBILD)
    {
        if (!kamerasichtVollbild)
            GetWindowRect(hKamerasicht, &kamerasichtFensterRect);
        kamerasichtVollbild = !kamerasichtVollbild;
        kamerasichtLageSetzen();
    }
    else if (wahl == M_KAMERASICHT_AUFLOESUNG)
    {
        // Render-Bild genau so gross wie die Kamerasicht (z.B. der externe Bildschirm): gestochen scharf
        RECT r;
        GetClientRect(hKamerasicht, &r);
        renderBreite = std::max(1, int(r.right));
        renderHoehe = std::max(1, int(r.bottom));
        einstellungenAnzeigen();
    }
    else if (wahl >= M_KAMERASICHT_SCHIRM && wahl < M_KAMERASICHT_SCHIRM + 9)
    {
        const auto liste = bildschirme();
        const size_t i = size_t(wahl - M_KAMERASICHT_SCHIRM);
        if (i < liste.size())
            kamerasichtAbdocken(liste[i].geraet);
    }
    else
        return false;
    return true;
}

// Die vier Ansichten ins Hauptfenster: Anordnung wie bisher auf dem Bildschirm (oben/unten, links/rechts)
void WerkzeugleisteAndocken(const std::vector<HWND> &ansichten)
{
    if (!hHauptfenster || ansichten.empty())
        return;
    // Mitte aller Ansichten: dient zur Einteilung in die vier Felder
    double mx = 0, my = 0;
    std::vector<POINT> mitten;
    for (HWND h : ansichten)
    {
        RECT r;
        GetWindowRect(h, &r);
        POINT p = {(r.left + r.right) / 2, (r.top + r.bottom) / 2};
        mitten.push_back(p);
        mx += p.x;
        my += p.y;
    }
    mx /= double(ansichten.size());
    my /= double(ansichten.size());

    hauptAnsichten.clear();
    std::vector<bool> belegt(4, false);
    for (size_t i = 0; i < ansichten.size(); ++i)
    {
        int feld = (mitten[i].x > mx ? 1 : 0) + (mitten[i].y > my ? 2 : 0);
        if (belegt[size_t(feld)]) // gleiche Stelle: naechstes freies Feld
            for (int f = 0; f < 4; ++f)
                if (!belegt[size_t(f)])
                {
                    feld = f;
                    break;
                }
        belegt[size_t(feld)] = true;
        hauptAnsichten.push_back({ansichten[i], feld});
        einbetten(ansichten[i]);
    }
    angedockteAnsichten = ansichten;

    ShowWindow(hHauptfenster, SW_SHOWMAXIMIZED);
    hauptfensterAnordnen();
    SetForegroundWindow(hHauptfenster);
}


//--------------------------------------------------
// Objektliste: Szene, Lichter, Kamera
//--------------------------------------------------

void UpdateObjectList()
{
    if (!hTreeObjects)
        return;

    // Wird auch aus dem Import-Thread aufgerufen: dann im Fenster-Thread ausfuehren
    if (GetWindowThreadProcessId(hToolWindow, nullptr) != GetCurrentThreadId())
    {
        PostMessageW(hToolWindow, WM_LISTE_AKTUALISIEREN, 0, 0);
        return;
    }

    auswahlWirdGesetzt = true;
    SendMessageW(hTreeObjects, WM_SETREDRAW, FALSE, 0);
    TreeView_DeleteAllItems(hTreeObjects);

    eintraegeObjekte.clear();
    eintraegeLichter.clear();
    eintraegeKinder.clear();

    HTREEITEM szeneWurzel = eintragEinfuegen(TVI_ROOT, L"Szene  (" + std::to_wstring(scene.size()) + L")", EINTRAG_GRUPPE);
    // Gruppen mit ihren Objekten darunter (zugeklappt); die Objekte lassen sich einzeln waehlen
    std::function<void(HTREEITEM, const Gruppe *)> kinderEinfuegen = [&](HTREEITEM eltern, const Gruppe *g)
    {
        for (size_t n = 0; n < g->kinder.size(); ++n)
        {
            object *k = g->kinder[n].get();
            HTREEITEM e = eintragEinfuegen(eltern, objektName(k, n), EINTRAG_KIND + LPARAM(eintraegeKinder.size()));
            eintraegeKinder.push_back({k, e});
            eintragHakenSetzen(e, k);
            if (auto *u = dynamic_cast<const Gruppe *>(k))
                kinderEinfuegen(e, u);
        }
    };
    for (size_t i = 0; i < scene.size(); ++i)
    {
        eintraegeObjekte.push_back(eintragEinfuegen(szeneWurzel, objektName(scene[i].get(), i), LPARAM(i)));
        eintragHakenSetzen(eintraegeObjekte.back(), scene[i].get());
        if (auto *g = dynamic_cast<const Gruppe *>(scene[i].get()))
            kinderEinfuegen(eintraegeObjekte.back(), g);
    }

    HTREEITEM lichtWurzel = eintragEinfuegen(TVI_ROOT, L"Lichter  (" + std::to_wstring(lights.size()) + L")", EINTRAG_GRUPPE);
    for (size_t i = 0; i < lights.size(); ++i)
        eintraegeLichter.push_back(eintragEinfuegen(lichtWurzel, lichtName(lights[i].get(), i), EINTRAG_LICHT + LPARAM(i)));

    eintragKamera = cam.empty() ? nullptr : eintragEinfuegen(TVI_ROOT, L"Kamera", EINTRAG_KAMERA);
    // Wurzeln, Lichter, Kamera: ohne Kaestchen
    eintragHakenSetzen(szeneWurzel, nullptr);
    eintragHakenSetzen(lichtWurzel, nullptr);
    for (HTREEITEM e : eintraegeLichter)
        eintragHakenSetzen(e, nullptr);
    if (eintragKamera)
        eintragHakenSetzen(eintragKamera, nullptr);

    TreeView_Expand(hTreeObjects, szeneWurzel, TVE_EXPAND);
    TreeView_Expand(hTreeObjects, lichtWurzel, TVE_EXPAND);

    SendMessageW(hTreeObjects, WM_SETREDRAW, TRUE, 0);
    auswahlWirdGesetzt = false;

    bekannteObjektAnzahl = scene.size();
    bekannteLichtAnzahl = lights.size();

    listenAuswahlAbgleichen();
    eigenschaftenAnzeigen();
}

//--------------------------------------------------
// Neue Grundobjekte und Lichter einfuegen
//--------------------------------------------------

enum NeuArt
{
    NEU_WUERFEL = 1,
    NEU_KUGEL,
    NEU_EBENE,
    NEU_WAND,
    NEU_ZYLINDER,
    NEU_PFEIL,
    NEU_TEXT,
    NEU_BAUM,
    NEU_TERRAIN,
    NEU_TERRAIN_FEIN,
    NEU_WASSER,
    NEU_WOLKE,
    NEU_KOORDINATEN,
    NEU_KUGEL_SPIEGEL,
    NEU_KUGEL_GOLD,
    NEU_KUGEL_GLAS,
    NEU_KUGEL_GRUENGLAS,
    NEU_KUGEL_PLASTIK,
    NEU_PUNKTLICHT,
    NEU_KUGELLICHT,
    NEU_RICHTUNGSLICHT,
    NEU_SPOTLICHT, // Spotlicht mit Lichtkegel
    NEU_FLAECHENLICHT,
    NEU_MESHLICHT,
    NEU_ROTATION, // Rotationsobjekt aus einem gezeichneten Profil
    NEU_STEIN,    // Stein aus zufaelligen Schnitten
    NEU_FIGUR,    // Figur (Gelenkkette) mit Bewegungen
    NEU_ELLIPSOID, // Ellipsoid (drei Radien)
    NEU_ROEHRE,    // Roehre entlang gezeichneter Punkte
    NEU_TIER,      // Tier (Katze, Hund) mit Bewegungen
    NEU_RING3D,
    NEU_RING2D,
    NEU_PYRAMIDE,
    NEU_ZYLINDER_NETZ,
    NEU_TETRAEDER,
    NEU_OKTAEDER,
    NEU_KREISFLAECHE,
    NEU_KEGEL,
    NEU_TORUS,
    NEU_KAPSEL,
    NEU_ROHR
};

// "runde" Groesse passend zur Szene: 1, 2 oder 5 mal Zehnerpotenz, etwa ein Zehntel der Szene
static double passendeGroesse()
{
    double ausdehnung = (sceneMax - sceneMin).length();
    if (!(ausdehnung > 1e-9))
        return 1.0;

    double ziel = ausdehnung * 0.1;
    double basis = std::pow(10.0, std::floor(std::log10(ziel)));
    for (double f : {1.0, 2.0, 5.0, 10.0})
        if (basis * f >= ziel * 0.7)
            return basis * f;
    return basis * 10.0;
}

//--------------------------------------------------
// Schritte -X..+Z (Buttons und Ziffernblock)
//--------------------------------------------------

static int modusIndex()
{
    return selectedAction == SelectedAction::MOVE ? 0 : selectedAction == SelectedAction::ROTATE ? 1 : 2;
}

static double schritt(int modus)
{
    if (modus == 0 && !(schrittWeite[0] > 0.0))
        schrittWeite[0] = passendeGroesse() * 0.1; // z.B. 1 bei einer Szene von etwa 100
    return schrittWeite[modus];
}

static void schrittAnzeigen()
{
    if (!g.editSchritt)
        return;
    int m = modusIndex();
    if (GetFocus() != g.editSchritt)
        SetWindowTextW(g.editSchritt, zahlText(schritt(m), m == 0 ? 3 : 1).c_str());
    textSetzen(g.lblEinheit, m == 0 ? L"Einheiten" : m == 1 ? L"Grad" : L"Prozent");
}

static void schrittUebernehmen()
{
    if (!SendMessageW(g.editSchritt, EM_GETMODIFY, 0, 0))
        return;
    SendMessageW(g.editSchritt, EM_SETMODIFY, FALSE, 0);

    double wert;
    if (!zahlLesen(g.editSchritt, wert) || !(wert > 0.0))
    {
        MessageBeep(MB_ICONWARNING);
        SendMessageW(g.editSchritt, EM_SETMODIFY, TRUE, 0);
        return;
    }
    schrittWeite[modusIndex()] = wert;
}

void SchrittAusfuehren(int achse, int vorzeichen)
{
    const int m = modusIndex();
    const double s = schritt(m);
    Vector3D richtung = drehAchse(achse) * double(vorzeichen);

    if (selectedType == SelectedType::OBJECT && selectedObject && objektIndex(selectedObject) >= 0)
    {
        // alle ausgewaehlten Objekte; Drehen/Skalieren um den Drehpunkt (Mitte oder verschobene Markierung)
        std::vector<object *> alle = ausgewaehlteObjekte();
        const Vector3D mitte = drehpunkt();

        aenderungBeginnen(m == 0 ? "Verschieben" : m == 1 ? "Drehen" : "Skalieren", alle);
        {
            std::lock_guard<std::mutex> lock(sceneMutex); // GPU-Ansichten lesen die Geometrie
            for (object *o : alle)
            {
                if (m == 0)
                    o->translate(richtung * s);
                else if (m == 1)
                    objektDrehen(o, achse, vorzeichen * s, mitte);
                else
                {
                    // nur entlang der Achse; "-" macht den Schritt rueckgaengig
                    double f = 1.0 + s / 100.0;
                    if (vorzeichen < 0)
                        f = 1.0 / f;
                    objektSkalieren(o, Vector3D(achse == 0 ? f : 1.0, achse == 1 ? f : 1.0, achse == 2 ? f : 1.0), mitte);
                }
            }
        }
        aenderungAbschliessen();
    }
    else if (selectedType == SelectedType::LIGHT && selectedLight && lichtIndex(selectedLight) >= 0)
    {
        auto *d = dynamic_cast<DirectionalLight *>(selectedLight);
        auto *sp = dynamic_cast<SpotLight *>(selectedLight);
        aenderungBeginnen((d || sp) && m == 1 ? "Lichtrichtung" : "Licht verschieben", {}, {selectedLight});
        {
            std::lock_guard<std::mutex> lock(sceneMutex);
            if (sp && m == 1)
                sp->richtung = drehenUmAchse(sp->richtung, drehAchse(achse), vorzeichen * s).normalized(); // Kegel schwenken
            else if (d && m == 1)
                d->direction = drehenUmAchse(d->direction, drehAchse(achse), vorzeichen * s); // Rotate: Richtung drehen
            else
                selectedLight->translate(-(richtung * schritt(0))); // verschieben; translate() verschiebt um -delta
        }
        aenderungAbschliessen();
    }
    else if ((selectedType == SelectedType::CAMERA || selectedType == SelectedType::LOOKAT) && !cam.empty())
    {
        // Kamera (rote Pyramide) bzw. Zielpunkt; wie beim Ziehen in den Ansichten bewegt sich nur der gewaehlte Punkt
        Camera &c = cam[0];
        const bool ziel = selectedType == SelectedType::LOOKAT;
        {
            std::lock_guard<std::mutex> lock(sceneMutex);
            if (m == 0) // Move: verschieben
                (ziel ? c.lookAt : c.position) += richtung * s;
            else if (m == 1) // Rotate: Kamera kreist um den Zielpunkt, Zielpunkt schwenkt um die Kamera
            {
                if (ziel)
                    c.lookAt = c.position + drehenUmAchse(c.lookAt - c.position, drehAchse(achse), vorzeichen * s);
                else
                    c.position = c.lookAt + drehenUmAchse(c.position - c.lookAt, drehAchse(achse), vorzeichen * s);
            }
            else // Scale: Abstand Kamera-Zielpunkt entlang der Achse vergroessern/verkleinern
            {
                double f = 1.0 + s / 100.0;
                if (vorzeichen < 0)
                    f = 1.0 / f;
                Vector3D d = c.position - c.lookAt;
                Vector3D neu(achse == 0 ? d.x * f : d.x, achse == 1 ? d.y * f : d.y, achse == 2 ? d.z * f : d.z);
                if (neu.length() > 1e-9)
                {
                    if (ziel)
                        c.lookAt = c.position - neu;
                    else
                        c.position = c.lookAt + neu;
                }
            }
            if ((c.position - c.lookAt).length() < 1e-9) // Kamera und Ziel nie am selben Punkt
                (ziel ? c.lookAt : c.position) -= richtung * s;
            c.update();
            kameraBahnAbgleichen(c);
        }
        szeneGeaendertMarkieren();
    }
    else
        return;

    computeSceneBounds();
    eigenschaftenAnzeigen();
}

// Einfuegepunkt: dort, wo die Kamera hinschaut
static Vector3D einfuegePunkt()
{
    if (!cam.empty())
        return cam[0].lookAt;
    return (sceneMin + sceneMax) * 0.5;
}

// naechster freier Name "Basis N"
static std::string freierName(const std::string &basis)
{
    for (int n = 1;; ++n)
    {
        std::string name = basis + " " + std::to_string(n);
        bool vergeben = false;
        for (const auto &o : scene)
            vergeben = vergeben || o->name == name;
        if (!vergeben)
            return name;
    }
}

//--------------------------------------------------
// Kleiner Dialog: Werte vor dem Einfuegen abfragen (Groessen, Rekursionstiefen ...)
//--------------------------------------------------

struct EingabeWert
{
    const wchar_t *text;
    double wert;
    bool ganzzahl;
    double minimum, maximum;
    const wchar_t *tipp = nullptr; // Tooltip fuer Beschriftung und Feld
    bool istFarbe = false;         // Feld nimmt "R G B" (je 0 bis 1) statt einer Zahl
    Farbe farbe = Farbe(0, 0, 0);
    const wchar_t *abschnitt = nullptr; // Ueberschrift ueber diesem Feld
    bool neueSpalte = false;            // dieses Feld beginnt eine neue Spalte
    const wchar_t *auswahl = nullptr;   // Auswahlliste "A|B|C" statt Zahl; wert = Nummer des Eintrags (ab 0)
};

static EingabeWert farbFeld(const wchar_t *text, const Farbe &f, const wchar_t *tipp)
{
    EingabeWert e{text, 0.0, false, 0.0, 1.0, tipp};
    e.istFarbe = true;
    e.farbe = f;
    return e;
}

static std::wstring farbFeldText(const Farbe &f)
{
    wchar_t t[96];
    swprintf(t, 96, L"%.3g %.3g %.3g", f.r, f.g, f.b);
    return t;
}

static bool farbFeldLesen(HWND edit, Farbe &f)
{
    wchar_t t[128] = {};
    GetWindowTextW(edit, t, 128);
    for (wchar_t *c = t; *c; ++c)
        if (*c == L',' || *c == L';')
            *c = L' '; // auch "0,5 0,5 0,5" geht nicht eindeutig: Kommas als Trenner
    double r, g, b;
    if (swscanf(t, L"%lf %lf %lf", &r, &g, &b) != 3)
        return false;
    for (double v : {r, g, b})
        if (!(v >= 0.0 && v <= 1.0))
            return false;
    f = Farbe(r, g, b);
    return true;
}

static bool dialogFertig = false;
static bool dialogOk = false;

// aufgeklappte Liste einer Auswahl so breit wie ihr laengster Eintrag (sonst abgeschnitten)
static void auswahlBreiteAnpassen(HWND cb)
{
    if (!cb)
        return;
    HDC dc = GetDC(cb);
    HGDIOBJ alt = SelectObject(dc, gFont);
    int breit = 0;
    const int n = int(SendMessageW(cb, CB_GETCOUNT, 0, 0));
    for (int i = 0; i < n; ++i)
    {
        const int len = int(SendMessageW(cb, CB_GETLBTEXTLEN, WPARAM(i), 0));
        if (len <= 0)
            continue;
        std::wstring t(size_t(len) + 1, L'\0');
        SendMessageW(cb, CB_GETLBTEXT, WPARAM(i), (LPARAM)t.data());
        RECT r = {0, 0, 0, 0};
        DrawTextW(dc, t.c_str(), -1, &r, DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
        breit = std::max(breit, int(r.right - r.left));
    }
    SelectObject(dc, alt);
    ReleaseDC(cb, dc);
    RECT r;
    GetWindowRect(cb, &r);
    SendMessageW(cb, CB_SETDROPPEDWIDTH, WPARAM(std::max(int(r.right - r.left), breit + 16 + GetSystemMetrics(SM_CXVSCROLL))), 0);
}

// Hinweis unter den Feldern, bei jeder Eingabe neu berechnet
using HinweisRechner = std::function<std::wstring(const std::vector<double> &)>;
static HWND dialogHinweis = nullptr;
static std::vector<HWND> dialogFelder;
static std::vector<double> dialogWerte; // letzte gueltige Werte (waehrend des Tippens)
static HinweisRechner dialogRechner;
static std::vector<bool> dialogIstAuswahl;    // Feld ist eine Auswahlliste
static const wchar_t *dialogOkText = nullptr; // Text des OK-Knopfs (nullptr: "Einfügen")
// Vorlagen unten links im Dialog: Auswahl setzt die Felder (leerer Text = Feld bleibt)
struct DialogVorlage
{
    std::wstring name;
    std::vector<std::wstring> texte; // je Feld; bei Auswahllisten die Nummer
    std::wstring tipp;
};
static std::vector<DialogVorlage> dialogVorlagen;
static HWND dialogVorlageListe = nullptr;
#define ID_DIALOG_VORLAGE 9150

static void dialogHinweisAktualisieren()
{
    if (!dialogHinweis || !dialogRechner)
        return;
    for (size_t i = 0; i < dialogFelder.size() && i < dialogWerte.size(); ++i)
    {
        double v;
        if (i < dialogIstAuswahl.size() && dialogIstAuswahl[i])
            dialogWerte[i] = double(std::max(0, int(SendMessageW(dialogFelder[i], CB_GETCURSEL, 0, 0))));
        else if (zahlLesen(dialogFelder[i], v))
            dialogWerte[i] = v;
    }
    SetWindowTextW(dialogHinweis, dialogRechner(dialogWerte).c_str());
}

static LRESULT CALLBACK WerteDialogProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
        {
            dialogOk = LOWORD(wParam) == IDOK;
            dialogFertig = true;
            return 0;
        }
        if (LOWORD(wParam) == ID_DIALOG_VORLAGE && HIWORD(wParam) == CBN_SELCHANGE)
        {
            const int k = int(SendMessageW(dialogVorlageListe, CB_GETCURSEL, 0, 0)) - 1; // 0 = "eigene Werte"
            if (k >= 0 && k < int(dialogVorlagen.size()))
            {
                const auto &v = dialogVorlagen[size_t(k)];
                for (size_t i = 0; i < dialogFelder.size() && i < v.texte.size(); ++i)
                {
                    if (v.texte[i].empty())
                        continue;
                    if (i < dialogIstAuswahl.size() && dialogIstAuswahl[i])
                        SendMessageW(dialogFelder[i], CB_SETCURSEL, WPARAM(_wtoi(v.texte[i].c_str())), 0);
                    else
                        SetWindowTextW(dialogFelder[i], v.texte[i].c_str());
                }
                if (!v.tipp.empty())
                    tooltipAendern(dialogVorlageListe, (v.name + L": " + v.tipp).c_str());
                dialogHinweisAktualisieren();
            }
            return 0;
        }
        if (HIWORD(wParam) == EN_CHANGE || HIWORD(wParam) == CBN_SELCHANGE)
        {
            dialogHinweisAktualisieren();
            return 0;
        }
        break;
    case WM_CLOSE:
        dialogOk = false;
        dialogFertig = true;
        return 0;
    case WM_CTLCOLORSTATIC:
    {
        HDC hdc = (HDC)wParam;
        SetTextColor(hdc, GetWindowLongPtrW((HWND)lParam, GWLP_USERDATA) == 1 ? FARBE_UEBERSCHRIFT : FARBE_TEXT);
        SetBkColor(hdc, FARBE_HINTERGRUND);
        return (LRESULT)pinselHintergrund;
    }
    case WM_CTLCOLOREDIT:
    {
        HDC hdc = (HDC)wParam;
        SetTextColor(hdc, FARBE_TEXT);
        SetBkColor(hdc, FARBE_FLAECHE);
        return (LRESULT)pinselFlaeche;
    }
    case WM_DRAWITEM:
        buttonZeichnen(reinterpret_cast<const DRAWITEMSTRUCT *>(lParam));
        return TRUE;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// Modaler Dialog; Tab wechselt die Felder, Enter = Einfuegen, Esc = Abbrechen.
// 'rechner' liefert den Hinweis unter den Feldern aus den aktuellen Werten (z.B. Anzahl Dreiecke).
// false bei Abbruch; sonst stehen die (geprueften) Werte in 'felder'
static bool werteAbfragen(const wchar_t *titel, std::vector<EingabeWert> &felder, const HinweisRechner &rechner)
{
    dialogWerte.clear();
    for (auto &f : felder)
        dialogWerte.push_back(f.wert);
    const std::wstring ersterHinweis = rechner ? rechner(dialogWerte) : std::wstring();
    const wchar_t *hinweis = ersterHinweis.empty() ? nullptr : ersterHinweis.c_str();

    HINSTANCE hInst = GetModuleHandleW(nullptr);
    static bool registriert = false;
    if (!registriert)
    {
        WNDCLASSW wc{};
        wc.lpfnWndProc = WerteDialogProc;
        wc.hInstance = hInst;
        wc.hbrBackground = pinselHintergrund;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.lpszClassName = L"SZENEN_WERTE_DIALOG";
        RegisterClassW(&wc);
        registriert = true;
    }

    // Groessen aus der tatsaechlichen Schrift (vorher feste Breiten: Texte wurden abgeschnitten)
    HDC dc = GetDC(nullptr);
    HGDIOBJ alteSchrift = SelectObject(dc, gFont);
    int labelB = 120;
    for (auto &f : felder)
    {
        RECT t = {0, 0, 0, 0};
        DrawTextW(dc, f.text, -1, &t, DT_CALCRECT | DT_SINGLELINE);
        labelB = std::max(labelB, int(t.right - t.left) + 16);
    }
    TEXTMETRICW tm;
    GetTextMetricsW(dc, &tm);
    const int feldB = 140, rand = 14, spaltenAbstand = 30;
    const int feldH = std::max(26, int(tm.tmHeight) + 8);
    const int zeile = feldH + 6;
    const int kopfH = int(tm.tmHeight) + 14; // Abschnitts-Ueberschrift

    // Spalten: bei neueSpalte; Hoehe je Spalte
    int spalten = 1, spaltenH = 0, h0 = 0;
    for (size_t i = 0; i < felder.size(); ++i)
    {
        if (felder[i].neueSpalte && i > 0)
        {
            ++spalten;
            spaltenH = std::max(spaltenH, h0);
            h0 = 0;
        }
        h0 += (felder[i].abschnitt ? kopfH : 0) + zeile;
    }
    spaltenH = std::max(spaltenH, h0);
    const int spalteB = labelB + feldB;
    const int breite = std::max(rand + spalten * spalteB + (spalten - 1) * spaltenAbstand + rand, 420);

    int hinweisH = 0;
    if (hinweis)
    {
        // Platz fuer mindestens fuenf Zeilen: der Text aendert sich beim Tippen
        RECT t = {0, 0, breite - 2 * rand, 0};
        DrawTextW(dc, hinweis, -1, &t, DT_CALCRECT | DT_WORDBREAK);
        // zwei Zeilen Reserve: der Text aendert sich beim Tippen, und das Feld bricht etwas anders um als DT_CALCRECT
        hinweisH = std::max(int(t.bottom - t.top) + 2 * int(tm.tmHeight), 5 * int(tm.tmHeight)) + 10;
    }
    SelectObject(dc, alteSchrift);
    ReleaseDC(nullptr, dc);

    // Vorlage-Auswahl neben den Knoepfen; ist das Fenster dafuer zu schmal, in einer eigenen Zeile darueber
    const bool vorlageEigeneZeile = !dialogVorlagen.empty() && breite - 2 * rand - 2 * 120 - GAP - 64 - 24 < 200;
    const int innenH = rand + spaltenH + hinweisH + 10 + BTN_H + rand + (vorlageEigeneZeile ? BTN_H + 8 : 0);

    DWORD stil = WS_POPUP | WS_CAPTION | WS_SYSMENU;
    RECT r = {0, 0, breite, innenH};
    AdjustWindowRectEx(&r, stil, FALSE, WS_EX_DLGMODALFRAME);
    int w = r.right - r.left, h = r.bottom - r.top;

    // mittig ueber dem Objektfenster bzw. der Leiste; nicht ueber den Bildschirmrand
    RECT bezug;
    GetWindowRect(hObjektFenster && IsWindowVisible(hObjektFenster) ? hObjektFenster : hToolWindow, &bezug);
    int x = (bezug.left + bezug.right) / 2 - w / 2, y = bezug.top + 80;
    MONITORINFO mon = {};
    mon.cbSize = sizeof(mon);
    if (GetMonitorInfoW(MonitorFromRect(&bezug, MONITOR_DEFAULTTONEAREST), &mon))
    {
        x = std::max(int(mon.rcWork.left), std::min(x, int(mon.rcWork.right) - w));
        y = std::max(int(mon.rcWork.top), std::min(y, int(mon.rcWork.bottom) - h));
    }

    HWND dlg = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_TOPMOST, L"SZENEN_WERTE_DIALOG", titel, stil,
                               x, y, w, h, hToolWindow, nullptr, hInst, nullptr);
    if (!dlg)
        return false;

    std::vector<HWND> edits;
    dialogHinweis = nullptr;
    dialogFelder.clear();
    dialogIstAuswahl.clear();
    dialogRechner = nullptr; // erst nach dem Anlegen (EN_CHANGE beim Setzen der Startwerte)
    int yy = rand, xx = rand;
    for (size_t i = 0; i < felder.size(); ++i)
    {
        auto &f = felder[i];
        if (f.neueSpalte && i > 0)
        {
            xx += spalteB + spaltenAbstand;
            yy = rand;
        }
        if (f.abschnitt)
        {
            HWND k = CreateWindowExW(0, L"STATIC", f.abschnitt, WS_CHILD | WS_VISIBLE | SS_NOPREFIX, xx, yy + 6, spalteB,
                                     kopfH - 6, dlg, nullptr, hInst, nullptr);
            SendMessageW(k, WM_SETFONT, (WPARAM)gFont, TRUE);
            SetWindowLongPtrW(k, GWLP_USERDATA, 1); // gedimmt (WM_CTLCOLORSTATIC)
            yy += kopfH;
        }
        HWND l = CreateWindowExW(0, L"STATIC", f.text, WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE | SS_NOTIFY, xx, yy, labelB, feldH, dlg, nullptr, hInst, nullptr);
        std::wstring start = f.istFarbe ? farbFeldText(f.farbe) : zahlText(f.wert, f.ganzzahl ? 0 : 3);
        HWND e;
        if (f.auswahl)
        {
            e = CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST,
                                xx + labelB, yy, feldB, feldH + 220, dlg, nullptr, hInst, nullptr);
            SetWindowTheme(e, L"DarkMode_CFD", nullptr);
            std::wstring alle = f.auswahl;
            size_t von = 0;
            for (;;)
            {
                size_t bis = alle.find(L'|', von);
                std::wstring eintrag = alle.substr(von, bis == std::wstring::npos ? std::wstring::npos : bis - von);
                SendMessageW(e, CB_ADDSTRING, 0, (LPARAM)eintrag.c_str());
                if (bis == std::wstring::npos)
                    break;
                von = bis + 1;
            }
            SendMessageW(e, CB_SETCURSEL, WPARAM(std::max(0, int(std::lround(f.wert)))), 0);
            SendMessageW(e, WM_SETFONT, (WPARAM)gFont, TRUE);
            auswahlBreiteAnpassen(e);
        }
        else
            e = CreateWindowExW(0, L"EDIT", start.c_str(),
                                WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_BORDER | ES_RIGHT | ES_AUTOHSCROLL,
                                xx + labelB, yy, feldB, feldH, dlg, nullptr, hInst, nullptr);
        dialogIstAuswahl.push_back(f.auswahl != nullptr);
        SendMessageW(l, WM_SETFONT, (WPARAM)gFont, TRUE);
        SendMessageW(e, WM_SETFONT, (WPARAM)gFont, TRUE);
        if (f.tipp)
        {
            tooltip(l, f.tipp);
            tooltip(e, f.tipp);
        }
        edits.push_back(e);
        yy += zeile;
    }
    yy = rand + spaltenH;
    if (hinweis)
    {
        HWND l = CreateWindowExW(0, L"STATIC", hinweis, WS_CHILD | WS_VISIBLE, rand, yy + 4, breite - 2 * rand, hinweisH - 4, dlg, nullptr, hInst, nullptr);
        SendMessageW(l, WM_SETFONT, (WPARAM)gFont, TRUE);
        dialogHinweis = l;
        yy += hinweisH;
    }
    yy += 10;
    const int knopfB = 120;
    const int yVorlage = yy, yKnopf = vorlageEigeneZeile ? yy + BTN_H + 8 : yy;
    // eigene Buttons ohne Weiterleitung von Tasten an den Editor (ButtonProc wuerde z.B. "C" weitergeben)
    HWND ok = CreateWindowExW(0, L"BUTTON", dialogOkText ? dialogOkText : L"Einfügen", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                              breite - rand - 2 * knopfB - GAP, yKnopf, knopfB, BTN_H, dlg, (HMENU)(INT_PTR)IDOK, hInst, nullptr);
    HWND abbrechen = CreateWindowExW(0, L"BUTTON", L"Abbrechen", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                                     breite - rand - knopfB, yKnopf, knopfB, BTN_H, dlg, (HMENU)(INT_PTR)IDCANCEL, hInst, nullptr);
    SendMessageW(ok, WM_SETFONT, (WPARAM)gFont, TRUE);
    SendMessageW(abbrechen, WM_SETFONT, (WPARAM)gFont, TRUE);
    dialogVorlageListe = nullptr;
    if (!dialogVorlagen.empty())
    {
        const int labelV = 64;
        const int listeB = vorlageEigeneZeile ? breite - 2 * rand - labelV : std::max(160, breite - 2 * rand - 2 * knopfB - GAP - labelV - 24);
        HWND l = CreateWindowExW(0, L"STATIC", L"Vorlage", WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE, rand, yVorlage, labelV,
                                 BTN_H, dlg, nullptr, hInst, nullptr);
        SendMessageW(l, WM_SETFONT, (WPARAM)gFont, TRUE);
        dialogVorlageListe = CreateWindowExW(0, L"COMBOBOX", L"",
                                             WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST,
                                             rand + labelV, yVorlage + 2, listeB, 400, dlg, (HMENU)(INT_PTR)ID_DIALOG_VORLAGE,
                                             hInst, nullptr);
        SetWindowTheme(dialogVorlageListe, L"DarkMode_CFD", nullptr);
        SendMessageW(dialogVorlageListe, WM_SETFONT, (WPARAM)gFont, TRUE);
        SendMessageW(dialogVorlageListe, CB_ADDSTRING, 0, (LPARAM)L"(eigene Werte)");
        for (const auto &v : dialogVorlagen)
            SendMessageW(dialogVorlageListe, CB_ADDSTRING, 0, (LPARAM)v.name.c_str());
        SendMessageW(dialogVorlageListe, CB_SETCURSEL, 0, 0);
        auswahlBreiteAnpassen(dialogVorlageListe);
        tooltip(dialogVorlageListe, L"Fertige Einstellungen übernehmen (alle Felder werden gesetzt und können danach\n"
                                    L"weiter angepasst werden). Die Größe richtet sich nach der Szene.");
    }

    dialogFelder = edits;
    dialogRechner = rechner;

    // modal: Leiste und Objektfenster sperren
    EnableWindow(hToolWindow, FALSE);
    EnableWindow(hObjektFenster, FALSE);
    ShowWindow(dlg, SW_SHOW);
    SetForegroundWindow(dlg);
    if (!edits.empty())
    {
        SetFocus(edits[0]);
        if (!dialogIstAuswahl[0])
            SendMessageW(edits[0], EM_SETSEL, 0, -1);
    }

    bool ergebnis = false;
    for (;;)
    {
        dialogFertig = false;
        dialogOk = false;
        MSG msg;
        while (!dialogFertig && GetMessageW(&msg, nullptr, 0, 0) > 0)
        {
            if (!IsDialogMessageW(dlg, &msg))
            {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        }
        if (!dialogOk)
            break;

        // Werte pruefen; bei Fehler zurueck ins Feld
        bool gueltig = true;
        for (size_t i = 0; i < felder.size(); ++i)
        {
            double v = 0;
            if (felder[i].auswahl)
            {
                felder[i].wert = double(std::max(0, int(SendMessageW(edits[i], CB_GETCURSEL, 0, 0))));
                continue;
            }
            if (felder[i].istFarbe)
            {
                Farbe fa;
                if (farbFeldLesen(edits[i], fa))
                {
                    felder[i].farbe = fa;
                    continue;
                }
                gueltig = false;
                MessageBeep(MB_ICONWARNING);
                SetFocus(edits[i]);
                SendMessageW(edits[i], EM_SETSEL, 0, -1);
                break;
            }
            if (!zahlLesen(edits[i], v) || v < felder[i].minimum || v > felder[i].maximum)
            {
                gueltig = false;
                MessageBeep(MB_ICONWARNING);
                SetFocus(edits[i]);
                SendMessageW(edits[i], EM_SETSEL, 0, -1);
                break;
            }
            felder[i].wert = felder[i].ganzzahl ? std::round(v) : v;
        }
        if (gueltig)
        {
            ergebnis = true;
            break;
        }
    }

    dialogRechner = nullptr;
    dialogHinweis = nullptr;
    dialogFelder.clear();
    dialogIstAuswahl.clear();
    dialogOkText = nullptr;
    dialogVorlagen.clear();
    dialogVorlageListe = nullptr;
    EnableWindow(hToolWindow, TRUE);
    EnableWindow(hObjektFenster, TRUE);
    DestroyWindow(dlg);
    return ergebnis;
}

// ---------- Wiederholungen in der Zeitleiste (Drehen, Pendeln, Wippen, Abrollen je Abschnitt) ----------

static std::wstring wiederholungKurztext(int abschnitt, const Wiederholung &w)
{
    static const wchar_t *arten[] = {L"keine", L"Drehen", L"Pendeln", L"Wippen", L"Abrollen"};
    const wchar_t achsen[] = {L'X', L'Y', L'Z', L'G'};
    double periode = 0, radius = 0;
    const int n = wiederholungZyklen(abschnitt, w, &periode, &radius);
    std::wstring t = arten[std::clamp(w.art, 0, 4)];
    if (w.art == WIEDERHOLUNG_ABROLLEN)
        return t + L", " + std::to_wstring(n) + (n == 1 ? L" Umdrehung" : L" Umdrehungen") + L" (Radius " +
               zahlText(radius, 3) + L")";
    t += std::wstring(L" um ") + (w.lokal ? L"eigene " : L"") + achsen[std::clamp(w.achse, 0, 3)];
    if (w.art == WIEDERHOLUNG_WIPPEN)
        t = std::wstring(L"Wippen entlang ") + (w.lokal ? L"eigener " : L"") + achsen[std::clamp(w.achse, 0, 3)];
    if (w.art == WIEDERHOLUNG_PENDELN)
        t += (w.mittel != 0 ? L" " + zahlText(w.mittel, 1) + L"°" : std::wstring()) + L" ±" + zahlText(w.weite, 1) + L"°";
    if (w.mitGelenk)
        t += L" am Gelenk";
    if (w.art == WIEDERHOLUNG_WIPPEN)
        t += L" ±" + zahlText(w.weite, 3);
    t += L", " + std::to_wstring(n) + (w.art == WIEDERHOLUNG_DREHEN ? L"× (je " : L" Schwingungen (je ") +
         zahlText(periode, 3) + L" s)";
    return t;
}

// Dialog fuer eine Wiederholung; 'w' enthaelt die Vorgabe und bekommt das Ergebnis.
// gelenkNeu: der Gelenkpunkt soll aus der Drehpunkt-Markierung neu bestimmt werden
static bool wiederholungAbfragen(int abschnitt, const std::vector<object *> &ziele, Wiederholung &w, bool &gelenkNeu)
{
    const double t0 = snapshotZeit(abschnitt), t1 = snapshotZeit(abschnitt + 1);
    enum
    {
        W_ART, W_ACHSE, W_BEZUG, W_DREHPUNKT, W_PERIODE, W_WEITE, W_MITTEL, W_PHASE, W_RICHTUNG, W_ANZAHL
    };
    std::vector<EingabeWert> felder = {
        {L"Art", double(std::max(0, w.art - 1)), true, 0, 3,
         L"Drehen: volle Umdrehungen (Rad, Propeller, Ventilator).\n"
         L"Pendeln: hin und her (Schaukel, Uhrpendel, Arme und Beine einer Figur).\n"
         L"Wippen: auf und ab entlang der Achse (Boje, Kolben, Körper beim Rennen).\n"
         L"Abrollen: dreht sich passend zur Strecke, die es im Abschnitt zurücklegt (Rad auf dem Boden, Y = oben);\n"
         L"Achse, Periode und Richtung ergeben sich dabei von selbst."},
        {L"Achse", double(std::clamp(w.achse, 0, 3)), true, 0, 3,
         L"Drehachse (Drehen, Pendeln) bzw. Bewegungsrichtung (Wippen).\n"
         L"Rad, das nach rechts (X) fährt: Achse Z. Bein einer Figur, die nach X läuft: Achse Z.\n"
         L"Gelenkachse: die gespeicherte freie Achse (z.B. von „+ Neu → Figur“ für eine gedrehte Figur)."},
        {L"Achse bezogen auf", w.lokal ? 1.0 : 0.0, true, 0, 1,
         L"Welt: feste Achse der Szene.\n"
         L"Objekt: Achse des Objekts mit seiner Drehung (z.B. ein schräg gestelltes Rad dreht um seine eigene Nabe).\n"
         L"In jedem Fall dreht die Achse mit, wenn sich das Objekt zwischen den Snapshots dreht oder ein\n"
         L"übergeordnetes Teil (Oberschenkel beim Unterschenkel) pendelt."},
        {L"Drehpunkt", w.mitGelenk ? 1.0 : 0.0, true, 0, 1,
         L"Objektmitte: dreht bzw. pendelt um die Mitte des Objekts.\n"
         L"Gelenk: um die orange Drehpunkt-Markierung (Modus Rotate, Markierung mit der Maus auf Schulter, Knie usw.\n"
         L"ziehen, bevor diese Wiederholung angelegt wird). Der Punkt bewegt sich danach mit dem Objekt mit."},
        {L"Periode (s)", w.periode, false, 0.001, 1e6,
         L"Dauer einer Umdrehung bzw. Schwingung in Sekunden. Wird so angepasst, dass im Abschnitt eine ganze Anzahl\n"
         L"liegt: am Ende steht das Objekt wieder wie am Anfang des Abschnitts (kein Sprung)."},
        {L"Ausschlag", w.weite, false, 0, 1e9,
         L"Pendeln: größter Ausschlag in Grad um den Mittelwert (z.B. 30). Wippen: größter Weg in Einheiten.\n"
         L"Bei Drehen und Abrollen ohne Bedeutung."},
        {L"Mittelwert", w.mittel, false, -1e9, 1e9,
         L"Pendeln: Grundwinkel in Grad, um den geschwungen wird (Knie: −50 = immer gebeugt, Ausschlag 40 →\n"
         L"zwischen −10 und −90 Grad). Wippen: Grundversatz in Einheiten. 0 = um die Lage im Snapshot."},
        {L"Phase (Grad)", w.phase, false, -3600, 3600,
         L"Versatz im Takt: 180 = genau gegenläufig (linkes und rechtes Bein), 90 = eine Viertelperiode später\n"
         L"(Knie gegenüber Oberschenkel). Bei Drehen: Startwinkel."},
        {L"Richtung", w.richtung < 0 ? 1.0 : 0.0, true, 0, 1,
         L"Drehsinn (rechte Hand: Daumen in Achsrichtung, Finger zeigen die Drehung) bzw. ob Pendeln/Wippen\n"
         L"zuerst in die positive oder negative Richtung geht"},
    };
    felder[W_ART].auswahl = L"Drehen|Pendeln|Wippen (auf und ab)|Abrollen (Rad auf dem Boden)";
    felder[W_ACHSE].auswahl = L"X|Y|Z|Gelenkachse (gespeichert)";
    felder[W_BEZUG].auswahl = L"Welt|Objekt (eigene Drehung)";
    felder[W_DREHPUNKT].auswahl = L"Objektmitte|Gelenk (Drehpunkt-Markierung)";
    felder[W_RICHTUNG].auswahl = L"vorwärts (+)|rückwärts (−)";
    const bool warGelenk = w.mitGelenk;

    auto ausWerten = [&](const std::vector<double> &v, Wiederholung &x)
    {
        x.art = int(std::lround(v[W_ART])) + 1;
        x.achse = int(std::lround(v[W_ACHSE]));
        x.lokal = v[W_BEZUG] > 0.5;
        x.mitGelenk = v[W_DREHPUNKT] > 0.5;
        x.periode = v[W_PERIODE];
        x.weite = v[W_WEITE];
        x.mittel = v[W_MITTEL];
        x.phase = v[W_PHASE];
        x.richtung = v[W_RICHTUNG] > 0.5 ? -1 : 1;
    };
    auto hinweis = [&](const std::vector<double> &v)
    {
        Wiederholung x = w;
        ausWerten(v, x);
        std::wstring t = L"Abschnitt " + zahlText(t0, 2) + L" s bis " + zahlText(t1, 2) + L" s (" + zahlText(t1 - t0, 2) +
                         L" s), " + (ziele.size() == 1 ? ausUtf8(ziele[0]->name.empty() ? std::string("Objekt") : ziele[0]->name)
                                                         : std::to_wstring(ziele.size()) + L" Objekte") +
                         L".\n";
        double periode = 0, radius = 0;
        x.o = ziele.empty() ? nullptr : ziele[0];
        const int n = wiederholungZyklen(abschnitt, x, &periode, &radius);
        if (x.art == WIEDERHOLUNG_ABROLLEN)
        {
            if (n <= 0)
                t += radius <= 0 ? L"Kein Radius erkennbar (Objekt im Snapshot nicht gefunden?) – keine Drehung."
                                 : L"Erkannter Radius " + zahlText(radius, 3) +
                                       L": die Strecke im Abschnitt reicht nicht für eine ganze Umdrehung – keine Drehung.";
            else
                t += L"Erkannter Radius " + zahlText(radius, 3) + L": " + std::to_wstring(n) +
                     (n == 1 ? L" Umdrehung" : L" Umdrehungen") + L", je " + zahlText(periode, 3) + L" s.";
        }
        else
        {
            t += std::to_wstring(n) + (x.art == WIEDERHOLUNG_DREHEN ? L" Umdrehungen" : L" Schwingungen") + L", je " +
                 zahlText(periode, 3) + L" s";
            if (std::fabs(periode - x.periode) > 1e-6)
                t += L" (gewünscht " + zahlText(x.periode, 3) + L" s, angepasst für ganze Anzahl)";
            t += L".";
            if (x.art == WIEDERHOLUNG_PENDELN)
                t += L" Winkel zwischen " + zahlText(x.mittel - x.weite, 1) + L"° und " + zahlText(x.mittel + x.weite, 1) + L"°.";
        }
        if (x.mitGelenk && x.art != WIEDERHOLUNG_ABROLLEN)
        {
            if (drehpunktEigen)
                t += L"\nGelenk: an der Drehpunkt-Markierung (" + zahlText(drehpunktPos.x, 3) + L" / " +
                     zahlText(drehpunktPos.y, 3) + L" / " + zahlText(drehpunktPos.z, 3) + L").";
            else if (warGelenk)
                t += L"\nGelenk: wie bisher gespeichert (Markierung verschieben, um es neu zu setzen).";
            else
                t += L"\nAchtung: Drehpunkt-Markierung nicht verschoben – das Gelenk liegt dann in der Objektmitte.\n"
                     L"Modus Rotate wählen und die orange Markierung auf das Gelenk ziehen.";
        }
        return t;
    };
    dialogOkText = L"Übernehmen";
    if (!werteAbfragen(L"Wiederholung im Abschnitt", felder, hinweis))
        return false;
    std::vector<double> v;
    for (auto &f : felder)
        v.push_back(f.wert);
    ausWerten(v, w);
    gelenkNeu = w.mitGelenk && (drehpunktEigen || !warGelenk);
    return true;
}

// Teil mit diesem Namen unter o (Tiefensuche)
static object *teilMitNamenUI(object *o, const std::string &name)
{
    if (!o)
        return nullptr;
    if (o->name == name)
        return o;
    if (auto *g = dynamic_cast<Gruppe *>(o))
        for (auto &k : g->kinder)
            if (object *t = teilMitNamenUI(k.get(), name))
                return t;
    return nullptr;
}

// ---------- kleiner Eingabedialog fuer einen Text (Name eines Ablaufs) ----------
static INT_PTR CALLBACK TextDialogProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    static std::wstring *ziel = nullptr;
    switch (m)
    {
    case WM_INITDIALOG:
        ziel = reinterpret_cast<std::wstring *>(l);
        SetDlgItemTextW(h, 101, ziel->c_str());
        SendDlgItemMessageW(h, 101, EM_SETSEL, 0, -1);
        SetFocus(GetDlgItem(h, 101));
        return FALSE;
    case WM_COMMAND:
        if (LOWORD(w) == IDOK)
        {
            wchar_t puffer[256] = {};
            GetDlgItemTextW(h, 101, puffer, 256);
            if (ziel)
                *ziel = puffer;
            EndDialog(h, IDOK);
            return TRUE;
        }
        if (LOWORD(w) == IDCANCEL)
        {
            EndDialog(h, IDCANCEL);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

static bool textAbfragen(HWND besitzer, const wchar_t *titel, const wchar_t *frage, std::wstring &text)
{
    std::vector<WORD> t;
    auto wort = [&](WORD x) { t.push_back(x); };
    auto zeichen = [&](const wchar_t *s)
    {
        while (*s)
            wort(WORD(*s++));
        wort(0);
    };
    auto ausrichten = [&]
    {
        if (t.size() % 2)
            wort(0);
    };
    const DWORD stil = DS_MODALFRAME | DS_CENTER | DS_SETFONT | WS_POPUP | WS_CAPTION | WS_SYSMENU;
    wort(LOWORD(stil));
    wort(HIWORD(stil));
    wort(0);
    wort(0);
    wort(4); // Steuerelemente
    wort(0);
    wort(0);
    wort(240);
    wort(68);
    wort(0); // kein Menue
    wort(0); // Standardklasse
    zeichen(titel);
    wort(9);
    zeichen(L"Segoe UI");
    auto element = [&](DWORD s, short x, short y, short cx, short cy, WORD id, WORD klasse, const wchar_t *text)
    {
        ausrichten();
        s |= WS_CHILD | WS_VISIBLE;
        wort(LOWORD(s));
        wort(HIWORD(s));
        wort(0);
        wort(0);
        wort(WORD(x));
        wort(WORD(y));
        wort(WORD(cx));
        wort(WORD(cy));
        wort(id);
        wort(0xFFFF);
        wort(klasse);
        zeichen(text);
        wort(0);
    };
    element(SS_LEFT, 7, 7, 226, 10, 100, 0x0082, frage);
    element(WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, 7, 20, 226, 14, 101, 0x0081, L"");
    element(BS_DEFPUSHBUTTON | WS_TABSTOP, 121, 44, 54, 14, IDOK, 0x0080, L"OK");
    element(BS_PUSHBUTTON | WS_TABSTOP, 179, 44, 54, 14, IDCANCEL, 0x0080, L"Abbrechen");
    return DialogBoxIndirectParamW(GetModuleHandleW(nullptr), reinterpret_cast<LPCDLGTEMPLATEW>(t.data()), besitzer, TextDialogProc,
                                   reinterpret_cast<LPARAM>(&text)) == IDOK;
}

// Vorlage unter einem Namen speichern (gleicher Name: ersetzen)
static void ablaufVorlageSpeichern(HWND fenster, AblaufVorlage v)
{
    std::wstring name = ausUtf8(v.name.empty() ? std::string("Ablauf") : v.name);
    if (!textAbfragen(fenster, L"Ablauf speichern", L"Name des Ablaufs (steht dann unter „Bewegung / Ablauf einsetzen …“):", name))
        return;
    while (!name.empty() && name.back() == L' ')
        name.pop_back();
    if (name.empty())
        return;
    v.name = zuUtf8(name);
    auto &liste = ablaufVorlagen();
    bool ersetzt = false;
    for (auto &x : liste)
        if (x.name == v.name)
        {
            x = v;
            ersetzt = true;
        }
    if (!ersetzt)
        liste.push_back(v);
    szeneGeaendertMarkieren();
}

static const wchar_t *const ABLAUF_BAUART_NAMEN = L"Rennen (Figur)|Gehen (Figur)|Winken (Figur)|Gehen (Tier)|Schwanz wedeln (Tier)|"
                                                  L"Springen (Tier, z.B. Katze)|Hinsetzen (Figur)|Tür öffnen|Schranke öffnen";

// Bewegung / Ablauf fuer o einsetzen (fertige Bewegung oder gespeicherte Vorlage), mit Zeitspanne und Muster
static void ablaufDialog(HWND fenster, object *o, double start)
{
    o = o ? obersteGruppe(o) : nullptr;
    if (!o)
    {
        MessageBoxW(fenster, L"Bitte zuerst eine Figur, ein Tier oder ein anderes Objekt wählen.", L"Bewegung / Ablauf",
                    MB_OK | MB_ICONINFORMATION);
        return;
    }
    static std::wstring auswahlText;
    auswahlText = ABLAUF_BAUART_NAMEN;
    for (const AblaufVorlage &v : ablaufVorlagen())
        auswahlText += L"|" + ausUtf8(v.name);
    const int anzahl = ABLAUF_BAUARTEN_ANZAHL + int(ablaufVorlagen().size());
    static double art = 0, dauer = 0, tempo = 1, alle = 0, bisAbstand = 0, rueckwaerts = 0;
    if (art >= anzahl)
        art = 0;
    if ((art < 3 || art == 6) && teilMitNamenUI(o, "Bein vorne links"))
        art = 3; // ein Tier: "Gehen (Tier)" vorschlagen
    if (teilMitNamenUI(o, "Schlagbaum"))
        art = ABLAUF_SCHRANKE - 1;
    else if (teilMitNamenUI(o, "Türblatt"))
        art = ABLAUF_TUER - 1;
    std::vector<EingabeWert> werte = {
        {L"Bewegung", art, true, 0, double(anzahl - 1),
         L"Fertige Bewegungen für Figuren und Tiere aus „+ Neu“ (Glieder werden an ihren Namen erkannt)\n"
         L"und gespeicherte Abläufe (Zeitleiste: Rechtsklick in einen Abschnitt → „Als Ablauf speichern …“\n"
         L"oder „Posen ab hier als Ablauf speichern …“)."},
        {L"Beginn (s)", start, false, -1e6, 1e6, L"Zeitpunkt, an dem die Bewegung beginnt (Vorgabe: aktuelle Zeit)"},
        {L"Dauer (s)", dauer, false, 0, 1e5,
         L"Länge der Bewegung (0 = wie die Bewegung selbst, z.B. 1,35 s für den Sprung).\n"
         L"Arme und Beine laufen weich an und wieder aus; Schlüsselposen werden auf die Dauer gestreckt."},
        {L"Tempo (Faktor)", tempo, false, 0.1, 5, L"1 = normal; 2 = doppelt so schnelle Schritte und doppelte Geschwindigkeit"},
        {L"Wiederholen alle (s)", alle, false, 0, 1e5,
         L"0 = nur einmal. Sonst beginnt die Bewegung alle so viele Sekunden neu (z.B. alle 15 s für 3 s rennen)"},
        {L"… bis (s)", bisAbstand > 0 ? start + bisAbstand : 0, false, 0, 1e6,
         L"Letzter möglicher Beginn des Musters (0 = nur einmal). Die Snapshots werden bei Bedarf bis zum Ende verlängert."},
        {L"Richtung", rueckwaerts, true, 0, 1,
         L"Rückwärts: die Bewegung läuft vom Ende zum Anfang – aus „Tür öffnen“ wird „Tür schließen“,\n"
         L"aus „Hinsetzen“ „Aufstehen“. Sie beginnt dort, wo der vorige Ablauf aufgehört hat."}};
    werte[0].auswahl = auswahlText.c_str();
    werte[6].auswahl = L"Vorwärts|Rückwärts (schließen, aufstehen …)";
    const std::wstring name = ausUtf8(o->name.empty() ? std::string("Objekt") : o->name);
    auto hinweis = [name](std::vector<double> w)
    {
        std::wstring t = L"Für „" + name + L"“. ";
        if (w[2] <= 0)
        {
            t += L"Dauer wie die Bewegung. ";
            w[2] = 2;
        }
        if (w[4] > 1e-6 && w[5] > w[1])
        {
            const int n = int(std::floor((w[5] - w[1]) / std::max(w[4], w[2] * 0.5) + 1e-9)) + 1;
            t += std::to_wstring(n) + L"-mal, zuletzt ab " + zahlText(w[1] + (n - 1) * std::max(w[4], w[2] * 0.5), 2) + L" s.";
        }
        else
            t += L"Einmal ab " + zahlText(w[1], 2) + L" s.";
        t += L"\nDie Fortbewegung bleibt nach dem Ende erhalten; in der Zeitleiste erscheint ein grüner Balken.";
        return t;
    };
    if (!werteAbfragen(L"Bewegung / Ablauf einsetzen", werte, hinweis))
        return;
    art = werte[0].wert;
    dauer = werte[2].wert;
    tempo = werte[3].wert;
    alle = werte[4].wert;
    bisAbstand = werte[5].wert > werte[1].wert ? werte[5].wert - werte[1].wert : 0;
    rueckwaerts = werte[6].wert;
    AblaufEinsatz e;
    std::string fehler;
    const int wahl = int(std::lround(werte[0].wert));
    const bool ok = wahl < ABLAUF_BAUARTEN_ANZAHL ? ablaufBauen(o, wahl + 1, e, fehler)
                                                  : ablaufAusVorlage(o, ablaufVorlagen()[size_t(wahl - ABLAUF_BAUARTEN_ANZAHL)], e);
    if (!ok)
    {
        MessageBoxW(fenster, ausUtf8(fehler).c_str(), L"Bewegung / Ablauf", MB_OK | MB_ICONWARNING);
        return;
    }
    e.start = werte[1].wert;
    if (werte[2].wert > 1e-9)
        e.dauer = werte[2].wert;
    else if (e.dauer <= 1e-9)
        e.dauer = std::max(0.1, e.v.dauer);
    if (werte[6].wert > 0.5)
        e.v = ablaufRueckwaerts(e.v);
    e.tempo = werte[3].wert;
    e.alle = werte[4].wert;
    e.bis = werte[5].wert;
    ablaufHinzufuegen(e);
    zeitleisteUmschalten(true);
    zeitWaehlen(aktuelleZeit());
}

// bestehenden Einsatz aendern (Zeiten, Tempo, Muster)
static void ablaufBearbeiten(HWND fenster, size_t i)
{
    auto &liste = ablaufEinsaetze();
    if (i >= liste.size())
        return;
    AblaufEinsatz &e = liste[i];
    std::vector<EingabeWert> werte = {
        {L"Beginn (s)", e.start, false, -1e6, 1e6, L"Zeitpunkt, an dem die Bewegung beginnt"},
        {L"Dauer (s)", e.dauer, false, 0.1, 1e5, L"Länge der Bewegung"},
        {L"Tempo (Faktor)", e.tempo, false, 0.1, 5, L"1 = normal"},
        {L"Wiederholen alle (s)", e.alle, false, 0, 1e5, L"0 = nur einmal"},
        {L"… bis (s)", e.bis, false, 0, 1e6, L"Letzter möglicher Beginn des Musters (0 = nur einmal)"},
        {L"Ein-/Auslauf (s)", e.v.einlauf, false, -1, 1e4,
         L"Weiches Anfahren und Anhalten (−1 = Vorgabe: 0,35 s; mit Schlüsselposen 25 % der Dauer, höchstens 0,6 s)"},
        {L"Richtung umkehren", 0, true, 0, 1, L"Ja: die Bewegung läuft rückwärts (öffnen ↔ schließen, hinsetzen ↔ aufstehen)"}};
    werte[6].auswahl = L"Nein|Ja";
    const std::wstring titel = L"Ablauf „" + ausUtf8(e.v.name) + L"“ ändern";
    if (!werteAbfragen(titel.c_str(), werte, [](const std::vector<double> &) { return std::wstring(); }))
        return;
    AblaufEinsatz neu = e;
    neu.start = werte[0].wert;
    neu.dauer = werte[1].wert;
    neu.tempo = werte[2].wert;
    neu.alle = werte[3].wert;
    neu.bis = werte[4].wert;
    neu.v.einlauf = werte[5].wert < 0 ? -1 : werte[5].wert;
    if (werte[6].wert > 0.5)
        neu.v = ablaufRueckwaerts(neu.v);
    liste.erase(liste.begin() + long(i));
    ablaufHinzufuegen(neu); // verlaengert die Snapshots bei Bedarf
    zeitWaehlen(aktuelleZeit());
}

// Rechtsklick auf einen Ablauf-Balken in der Zeitleiste
static void ablaufMenue(HWND fenster, size_t i, POINT bildschirm)
{
    auto &liste = ablaufEinsaetze();
    if (i >= liste.size())
        return;
    enum
    {
        M_AENDERN = 1,
        M_SPEICHERN,
        M_WAEHLEN,
        M_ENTFERNEN,
        M_WERTE,
        M_NAME
    };
    HMENU m = CreatePopupMenu();
    const AblaufEinsatz &e = liste[i];
    std::wstring kopf = L"Ablauf „" + ausUtf8(e.v.name) + L"“ – " +
                        ausUtf8(ablaufGueltig(e) && !e.o->name.empty() ? e.o->name : std::string("Objekt"));
    AppendMenuW(m, MF_STRING | MF_GRAYED, 0, kopf.c_str());
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, M_AENDERN, L"Zeit, Dauer, Tempo, Muster, Richtung ändern …");
    AppendMenuW(m, MF_STRING, M_WERTE, L"Bewegung bearbeiten (Glieder, Fortbewegung, Posen) …");
    AppendMenuW(m, MF_STRING, M_NAME, L"Umbenennen …");
    AppendMenuW(m, MF_STRING, M_WAEHLEN, L"Objekt auswählen");
    AppendMenuW(m, MF_STRING, M_SPEICHERN, L"Als Ablauf speichern (für andere Objekte) …");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, M_ENTFERNEN, L"Ablauf entfernen");
    const int wahl = int(TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, bildschirm.x, bildschirm.y, 0, fenster, nullptr));
    DestroyMenu(m);
    if (wahl == M_AENDERN)
        ablaufBearbeiten(fenster, i);
    else if (wahl == M_WERTE)
    {
        AblaufVorlage v = liste[i].v;
        if (ablaufVorlageBearbeiten(L"Ablauf „" + ausUtf8(v.name) + L"“ bearbeiten", v))
        {
            liste[i].v = v;
            szeneGeaendertMarkieren();
            zeitWaehlen(aktuelleZeit());
        }
    }
    else if (wahl == M_NAME)
    {
        if (ablaufNameAbfragen(fenster, liste[i].v.name))
        {
            szeneGeaendertMarkieren();
            zeitleisteAnzeigen();
        }
    }
    else if (wahl == M_SPEICHERN)
        ablaufVorlageSpeichern(fenster, liste[i].v);
    else if (wahl == M_WAEHLEN && ablaufGueltig(liste[i]))
    {
        auswahlSetzen({liste[i].o});
        abgleichen();
    }
    else if (wahl == M_ENTFERNEN)
    {
        liste.erase(liste.begin() + long(i));
        szeneGeaendertMarkieren();
        zeitWaehlen(aktuelleZeit());
    }
}

// ---------- Ablauf-Editor: Werte einer Vorlage (Fortbewegung, Ein-/Auslauf, Glieder, Zeiten der Posen) ----------
static bool ablaufVorlageBearbeiten(const std::wstring &titel, AblaufVorlage &v)
{
    std::deque<std::wstring> texte; // Beschriftungen leben bis zum Ende des Dialogs
    auto text = [&](const std::wstring &t) { texte.push_back(t); return texte.back().c_str(); };
    std::vector<EingabeWert> werte;
    struct Bezug
    {
        int art; // 0 Dauer, 1 Einlauf, 2..4 Fortbewegung, 10 weite, 11 mittel, 12 periode, 13 phase, 20 Posenzeit
        size_t i;
    };
    std::vector<Bezug> bezug;
    auto feld = [&](const wchar_t *t, double w, double mn, double mx, const wchar_t *tipp, Bezug b)
    {
        EingabeWert e{t, w, false, mn, mx, tipp};
        werte.push_back(e);
        bezug.push_back(b);
    };
    feld(L"Dauer (s)", v.dauer, 0.05, 1e5, L"Vorgabe für neue Einsätze. Bei Schlüsselposen werden ihre Zeiten mit gestreckt.", {0, 0});
    werte.back().abschnitt = L"Bewegung";
    feld(L"Ein-/Auslauf (s)", v.einlauf, -1, 1e4,
         L"Weiches Anfahren und Anhalten am Anfang und Ende.\n−1 = Vorgabe (0,35 s; mit Schlüsselposen 25 % der Dauer, höchstens 0,6 s).",
         {1, 0});
    feld(L"Fortbewegung vorwärts (Einh./s)", v.geschwindigkeit.x, -1e4, 1e4, L"Geradeaus in Blickrichtung (bei Tempo 1)", {2, 0});
    feld(L"Fortbewegung nach oben (Einh./s)", v.geschwindigkeit.y, -1e4, 1e4, nullptr, {3, 0});
    feld(L"Fortbewegung seitwärts (Einh./s)", v.geschwindigkeit.z, -1e4, 1e4, L"Nach rechts (quer zur Blickrichtung)", {4, 0});
    for (size_t k = 1; k < v.posen.size() && k <= 12; ++k)
    {
        feld(text(L"Pose " + std::to_wstring(k + 1) + L": Zeit (s)"), v.posen[k].zeit, 0, 1e5,
             L"Zeitpunkt dieser Schlüsselpose ab Beginn (Pose 1 liegt bei 0 s)", {20, k});
        if (k == 1)
            werte.back().abschnitt = L"Schlüsselposen";
    }
    int zeilen = int(werte.size());
    for (size_t g = 0; g < v.glieder.size() && g < 24; ++g)
    {
        const AblaufGlied &x = v.glieder[g];
        std::string kurz = x.pfad.empty() ? std::string("Ganzes Objekt") : x.pfad.substr(x.pfad.find_last_of('/') + 1);
        const std::wstring n = ausUtf8(kurz);
        const size_t start = werte.size();
        if (x.art == WIEDERHOLUNG_ABROLLEN)
            feld(text(n + L": Radius"), x.weite, 1e-4, 1e5, L"Rad: dreht sich passend zur gefahrenen Strecke", {10, g});
        else
        {
            if (x.art == WIEDERHOLUNG_PENDELN)
                feld(text(n + L": Ausschlag (°)"), x.weite, -360, 360, L"Pendeln hin und her um das Gelenk", {10, g});
            else if (x.art == WIEDERHOLUNG_WIPPEN)
                feld(text(n + L": Weg (Einh.)"), x.weite, -1e4, 1e4, L"Auf und ab entlang der Achse", {10, g});
            if (x.art != WIEDERHOLUNG_DREHEN)
                feld(text(n + L": Mitte (°/Einh.)"), x.mittel, -360, 360, L"Mittelstellung, um die geschwungen wird", {11, g});
            feld(text(n + L": Periode (s)"), x.periode, 0.01, 1e4,
                 x.art == WIEDERHOLUNG_DREHEN ? L"Zeit für eine volle Umdrehung" : L"Zeit für eine ganze Schwingung", {12, g});
            if (x.art != WIEDERHOLUNG_DREHEN)
                feld(text(n + L": Phase (°)"), x.phase, -360, 360, L"Versatz im Takt (180° = Gegentakt, z.B. linkes und rechtes Bein)", {13, g});
        }
        if (g == 0)
            werte[start].abschnitt = L"Glieder";
        if (zeilen + int(werte.size() - start) > 16)
        {
            werte[start].neueSpalte = true;
            zeilen = 0;
        }
        zeilen += int(werte.size() - start);
    }
    auto hinweis = [&v](const std::vector<double> &)
    {
        std::wstring t = std::to_wstring(v.glieder.size()) + L" Glieder, " + std::to_wstring(v.posen.size()) + L" Schlüsselposen.";
        if (v.glieder.size() > 24 || v.posen.size() > 13)
            t += L" (Es werden nicht alle Werte gezeigt.)";
        return t;
    };
    if (!werteAbfragen(titel.c_str(), werte, hinweis))
        return false;
    const double alteDauer = v.dauer;
    for (size_t k = 0; k < werte.size(); ++k)
    {
        const double w = werte[k].wert;
        const Bezug &b = bezug[k];
        switch (b.art)
        {
        case 0: v.dauer = w; break;
        case 1: v.einlauf = w < 0 ? -1 : w; break;
        case 2: v.geschwindigkeit.x = w; break;
        case 3: v.geschwindigkeit.y = w; break;
        case 4: v.geschwindigkeit.z = w; break;
        case 10: v.glieder[b.i].weite = w; break;
        case 11: v.glieder[b.i].mittel = w; break;
        case 12: v.glieder[b.i].periode = w; break;
        case 13: v.glieder[b.i].phase = w; break;
        case 20: v.posen[b.i].zeit = w; break;
        default: break;
        }
    }
    std::stable_sort(v.posen.begin(), v.posen.end(), [](const AblaufPose &a, const AblaufPose &b) { return a.zeit < b.zeit; });
    if (!v.posen.empty() && alteDauer > 1e-9 && std::fabs(v.dauer - alteDauer) > 1e-9)
        for (AblaufPose &p : v.posen)
            p.zeit *= v.dauer / alteDauer;
    return true;
}

static bool ablaufNameAbfragen(HWND fenster, std::string &name)
{
    std::wstring w = ausUtf8(name);
    if (!textAbfragen(fenster, L"Ablauf umbenennen", L"Neuer Name:", w))
        return false;
    while (!w.empty() && w.back() == L' ')
        w.pop_back();
    if (w.empty())
        return false;
    name = zuUtf8(w);
    return true;
}

// Posen aus Snapshots als Ablauf speichern (von: Snapshot-Nummer ab 0)
static void posenAlsAblauf(HWND fenster, object *o, int von)
{
    o = o ? obersteGruppe(o) : nullptr;
    const int n = snapshotAnzahl();
    if (!o || n < 2)
    {
        MessageBoxW(fenster, o ? L"Es braucht mindestens zwei Snapshots." : L"Bitte zuerst das Objekt wählen (z.B. das Auto).",
                    L"Posen als Ablauf speichern", MB_OK | MB_ICONINFORMATION);
        return;
    }
    von = std::clamp(von, 0, n - 2);
    std::vector<EingabeWert> werte = {
        {L"Von Snapshot Nr.", double(von + 1), true, 1, double(n - 1), L"Erste Schlüsselpose (die Ruhelage der Bewegung)"},
        {L"Bis Snapshot Nr.", double(n), true, 2, double(n), L"Letzte Schlüsselpose; alle Snapshots dazwischen werden Posen"}};
    auto hinweis = [](const std::vector<double> &w)
    {
        const int a = int(std::lround(w[0])) - 1, b = int(std::lround(w[1])) - 1;
        if (b <= a)
            return std::wstring(L"„Bis“ muss nach „Von“ liegen.");
        return std::to_wstring(b - a + 1) + L" Posen, " + zahlText(snapshotZeit(a), 2) + L" s bis " + zahlText(snapshotZeit(b), 2) +
               L" s.\nAufgenommen wird die Lage des ganzen Objekts und seiner Teile, relativ zum Objekt:\n"
               L"Eingesetzt bei einem anderen Objekt oder später, beginnt die Bewegung dort, wo es gerade steht.\n"
               L"Stäbe mit Namen „Rad …“ rollen dabei passend zur Strecke ab.";
    };
    if (!werteAbfragen(L"Posen als Ablauf speichern", werte, hinweis))
        return;
    const int a = int(std::lround(werte[0].wert)) - 1, b = int(std::lround(werte[1].wert)) - 1;
    AblaufVorlage v;
    std::string fehler;
    if (!ablaufAusPosen(a, b, o, v, fehler))
    {
        MessageBoxW(fenster, ausUtf8(fehler).c_str(), L"Posen als Ablauf speichern", MB_OK | MB_ICONINFORMATION);
        return;
    }
    ablaufVorlageSpeichern(fenster, v);
}

// ---------- Menue "Animation" in der Menueleiste ----------
enum
{
    MA_EINSETZEN = 31001,
    MA_POSEN,
    MA_VORLAGE = 31100, // + 4 * Nummer + Befehl (0 bearbeiten, 1 umbenennen, 2 rueckwaerts, 3 loeschen)
    MA_EINSATZ = 31600, // + Nummer
    MA_ENDE = 31800
};
static std::vector<HMENU> ablaufUntermenues;

static void ablaufMenueLeisteFuellen(HMENU m)
{
    for (HMENU u : ablaufUntermenues)
        DestroyMenu(u);
    ablaufUntermenues.clear();
    const std::vector<object *> auswahl = ausgewaehlteObjekte();
    const UINT mitAuswahl = auswahl.empty() ? MF_GRAYED : 0;
    AppendMenuW(m, MF_STRING | mitAuswahl, MA_EINSETZEN, L"Bewegung / Ablauf &einsetzen (ab aktueller Zeit) …");
    AppendMenuW(m, MF_STRING | (auswahl.empty() || snapshotAnzahl() < 2 ? MF_GRAYED : 0), MA_POSEN,
                L"&Posen aus Snapshots als Ablauf speichern …");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    HMENU vorlagen = CreatePopupMenu();
    ablaufUntermenues.push_back(vorlagen);
    const auto &liste = ablaufVorlagen();
    for (size_t i = 0; i < liste.size() && i < 120; ++i)
    {
        HMENU u = CreatePopupMenu();
        ablaufUntermenues.push_back(u);
        const UINT basis = MA_VORLAGE + UINT(4 * i);
        AppendMenuW(u, MF_STRING, basis + 0, L"Werte &bearbeiten …");
        AppendMenuW(u, MF_STRING, basis + 1, L"&Umbenennen …");
        AppendMenuW(u, MF_STRING, basis + 2, L"&Rückwärts-Version speichern (z.B. schließen, aufstehen)");
        AppendMenuW(u, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(u, MF_STRING, basis + 3, L"&Löschen");
        std::wstring t = ausUtf8(liste[i].name);
        if (!liste[i].posen.empty())
            t += L"   (" + std::to_wstring(liste[i].posen.size()) + L" Posen)";
        AppendMenuW(vorlagen, MF_POPUP, reinterpret_cast<UINT_PTR>(u), t.c_str());
    }
    if (liste.empty())
        AppendMenuW(vorlagen, MF_STRING | MF_GRAYED, 0, L"(noch keine – Zeitleiste: Rechtsklick in einen Abschnitt)");
    AppendMenuW(m, MF_POPUP, reinterpret_cast<UINT_PTR>(vorlagen), L"&Gespeicherte Abläufe");
    HMENU einsaetze = CreatePopupMenu();
    ablaufUntermenues.push_back(einsaetze);
    const auto &el = ablaufEinsaetze();
    for (size_t i = 0; i < el.size() && i < 200; ++i)
    {
        const AblaufEinsatz &e = el[i];
        std::wstring t = ausUtf8(e.v.name) + L" – " + ausUtf8(ablaufGueltig(e) && !e.o->name.empty() ? e.o->name : std::string("Objekt")) +
                         L"   (" + zahlText(e.start, 2) + L" s, " + zahlText(e.dauer, 2) + L" s) …";
        AppendMenuW(einsaetze, MF_STRING, MA_EINSATZ + UINT(i), t.c_str());
    }
    if (el.empty())
        AppendMenuW(einsaetze, MF_STRING | MF_GRAYED, 0, L"(keine)");
    AppendMenuW(m, MF_POPUP, reinterpret_cast<UINT_PTR>(einsaetze), L"Abläufe in der &Szene (ändern)");
}

static bool ablaufMenueLeisteBefehl(HWND fenster, int id)
{
    if (id < MA_EINSETZEN || id >= MA_ENDE)
        return false;
    const std::vector<object *> auswahl = ausgewaehlteObjekte();
    if (id == MA_EINSETZEN)
        ablaufDialog(fenster, auswahl.empty() ? nullptr : auswahl.front(), aktuelleZeit());
    else if (id == MA_POSEN)
    {
        int von = 0; // Snapshot an oder vor der aktuellen Zeit
        for (int i = 0; i < snapshotAnzahl(); ++i)
            if (snapshotZeit(i) <= aktuelleZeit() + 1e-9)
                von = i;
        posenAlsAblauf(fenster, auswahl.empty() ? nullptr : auswahl.front(), von);
    }
    else if (id >= MA_EINSATZ)
    {
        const size_t i = size_t(id - MA_EINSATZ);
        if (i < ablaufEinsaetze().size())
            ablaufBearbeiten(fenster, i);
    }
    else
    {
        auto &liste = ablaufVorlagen();
        const size_t i = size_t(id - MA_VORLAGE) / 4;
        const int befehl = (id - MA_VORLAGE) % 4;
        if (i >= liste.size())
            return true;
        if (befehl == 0)
        {
            AblaufVorlage v = liste[i];
            if (ablaufVorlageBearbeiten(L"Ablauf „" + ausUtf8(v.name) + L"“ bearbeiten", v))
            {
                liste[i] = v;
                szeneGeaendertMarkieren();
            }
        }
        else if (befehl == 1)
        {
            if (ablaufNameAbfragen(fenster, liste[i].name))
                szeneGeaendertMarkieren();
        }
        else if (befehl == 2)
            ablaufVorlageSpeichern(fenster, ablaufRueckwaerts(liste[i]));
        else if (befehl == 3)
        {
            const std::wstring frage = L"Ablauf „" + ausUtf8(liste[i].name) +
                                       L"“ aus der Liste löschen?\n(Schon eingesetzte Abläufe in der Zeitleiste bleiben erhalten.)";
            if (MessageBoxW(fenster, frage.c_str(), L"Ablauf löschen", MB_YESNO | MB_ICONQUESTION) == IDYES)
            {
                liste.erase(liste.begin() + long(i));
                szeneGeaendertMarkieren();
            }
        }
    }
    return true;
}

static void wiederholungMenue(HWND fenster, int abschnitt, POINT bildschirm)
{
    if (abschnitt < 0 || abschnitt + 1 >= snapshotAnzahl())
        return;
    // Gelenke werden aus der Ansicht bestimmt: die angezeigte Zeit muss im Abschnitt liegen
    if (aktuelleZeit() < snapshotZeit(abschnitt) - 1e-9 || aktuelleZeit() > snapshotZeit(abschnitt + 1) + 1e-9)
        zeitWaehlen(snapshotZeit(abschnitt));
    std::vector<object *> auswahl = ausgewaehlteObjekte(); // auch Teile in Gruppen (Glieder einer Figur)
    const std::vector<Wiederholung> vorhanden = wiederholungen(abschnitt);
    bool auswahlHat = false;
    for (const Wiederholung &w : vorhanden)
        auswahlHat = auswahlHat || std::find(auswahl.begin(), auswahl.end(), w.o) != auswahl.end();

    enum
    {
        M_NEU = 1,
        M_AUSWAHL_WEG,
        M_ALLE_WEG,
        M_KOPIEREN,
        M_EINFUEGEN,
        M_ALS_ABLAUF,
        M_ABLAUF_HIER,
        M_POSEN,
        M_BEARBEITEN = 100
    };
    HMENU m = CreatePopupMenu();
    std::wstring kopf = L"Abschnitt " + zahlText(snapshotZeit(abschnitt), 2) + L" s – " +
                        zahlText(snapshotZeit(abschnitt + 1), 2) + L" s";
    AppendMenuW(m, MF_STRING | MF_GRAYED, 0, kopf.c_str());
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    std::wstring neuText = auswahl.empty() ? std::wstring(L"Wiederholung für Auswahl … (zuerst ein Objekt wählen)")
                                           : L"Wiederholung für " +
                                                 (auswahl.size() == 1 ? ausUtf8(auswahl[0]->name.empty() ? std::string("Objekt") : auswahl[0]->name)
                                                                      : std::to_wstring(auswahl.size()) + L" Objekte") +
                                                 L" …";
    AppendMenuW(m, MF_STRING | (auswahl.empty() ? MF_GRAYED : 0), M_NEU, neuText.c_str());
    if (!vorhanden.empty())
    {
        AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
        for (size_t k = 0; k < vorhanden.size(); ++k)
        {
            const Wiederholung &w = vorhanden[k];
            std::wstring t = L"↻ " + ausUtf8(w.o->name.empty() ? std::string("Objekt") : w.o->name) + L": " +
                             wiederholungKurztext(abschnitt, w) + L" …";
            AppendMenuW(m, MF_STRING, M_BEARBEITEN + UINT(k), t.c_str());
        }
        AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(m, MF_STRING | (auswahlHat ? 0 : MF_GRAYED), M_AUSWAHL_WEG, L"Wiederholung der Auswahl entfernen");
        AppendMenuW(m, MF_STRING, M_ALLE_WEG, L"Alle Wiederholungen dieses Abschnitts entfernen");
    }
    // Kopieren / Einfuegen und Ablaeufe
    object *obenAuswahl = auswahl.empty() ? nullptr : obersteGruppe(auswahl[0]);
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING | (vorhanden.empty() ? MF_GRAYED : 0), M_KOPIEREN,
                auswahl.empty() ? L"Wiederholungen dieses Abschnitts kopieren"
                                : L"Wiederholungen der Auswahl (mit ihren Teilen) kopieren");
    {
        const int n = wiederholungenInAblage();
        std::wstring t = n == 0 ? std::wstring(L"Wiederholungen einfügen (zuerst kopieren)")
                                : L"Wiederholungen einfügen (" + std::to_wstring(n) + L")" +
                                      (obenAuswahl ? L" – auf „" + ausUtf8(obenAuswahl->name) + L"“" : std::wstring());
        AppendMenuW(m, MF_STRING | (n == 0 ? MF_GRAYED : 0), M_EINFUEGEN, t.c_str());
    }
    AppendMenuW(m, MF_STRING | (obenAuswahl ? 0 : MF_GRAYED), M_ALS_ABLAUF,
                L"Als Ablauf speichern … (Wiederholungen und Fortbewegung der gewählten Figur)");
    AppendMenuW(m, MF_STRING | (obenAuswahl ? 0 : MF_GRAYED), M_POSEN,
                L"Posen ab hier als Ablauf speichern … (Lage in den Snapshots, z.B. Auto fährt Kurve)");
    AppendMenuW(m, MF_STRING | (obenAuswahl ? 0 : MF_GRAYED), M_ABLAUF_HIER, L"Bewegung / Ablauf hier einsetzen …");
    const int wahl = int(TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, bildschirm.x, bildschirm.y, 0, fenster, nullptr));
    DestroyMenu(m);

    bool geaendert = false;
    if (wahl == M_NEU && !auswahl.empty())
    {
        Wiederholung w;
        w.art = WIEDERHOLUNG_DREHEN;
        w.periode = 0.5;
        Wiederholung alt;
        if (wiederholungVon(abschnitt, auswahl[0], alt))
            w = alt; // vorhandene Werte vorschlagen
        else
        {
            // Vorschlag aus einem anderen Abschnitt desselben Objekts (z.B. gleiche Achse)
            for (int i = 0; i + 1 < snapshotAnzahl(); ++i)
                if (wiederholungVon(i, auswahl[0], alt))
                {
                    w = alt;
                    break;
                }
        }
        bool gelenkNeu = false;
        if (wiederholungAbfragen(abschnitt, auswahl, w, gelenkNeu))
        {
            const Vector3D welt = drehpunkt(); // Markierung (oder Mitte der Auswahl)
            bool fehler = false;
            for (object *o : auswahl)
            {
                Wiederholung x = w;
                x.o = o;
                if (x.mitGelenk && gelenkNeu && !gelenkAusWelt(abschnitt, o, welt, x.gelenk))
                    fehler = true;
                wiederholungSetzen(abschnitt, x);
            }
            if (fehler)
                MessageBoxW(fenster, L"Das Gelenk konnte nicht aus der Drehpunkt-Markierung bestimmt werden\n"
                                     L"(liegt die aktuelle Zeit im Abschnitt, ist das Objekt im Snapshot enthalten?).",
                            L"Wiederholung", MB_OK | MB_ICONWARNING);
            geaendert = true;
        }
    }
    else if (wahl >= M_BEARBEITEN && wahl < M_BEARBEITEN + int(vorhanden.size()))
    {
        Wiederholung w = vorhanden[size_t(wahl - M_BEARBEITEN)];
        bool gelenkNeu = false;
        if (wiederholungAbfragen(abschnitt, {w.o}, w, gelenkNeu))
        {
            if (gelenkNeu && !gelenkAusWelt(abschnitt, w.o, drehpunkt(), w.gelenk))
                MessageBeep(MB_ICONWARNING);
            wiederholungSetzen(abschnitt, w);
            geaendert = true;
        }
    }
    else if (wahl == M_AUSWAHL_WEG)
    {
        for (object *o : auswahl)
        {
            Wiederholung w;
            w.o = o; // art 0 = entfernen
            wiederholungSetzen(abschnitt, w);
        }
        geaendert = true;
    }
    else if (wahl == M_ALLE_WEG)
    {
        wiederholungenEntfernen(abschnitt);
        geaendert = true;
    }
    else if (wahl == M_KOPIEREN)
    {
        if (wiederholungenKopieren(abschnitt, auswahl) == 0)
            MessageBeep(MB_ICONWARNING);
    }
    else if (wahl == M_EINFUEGEN)
    {
        // eine andere Figur gleichen Aufbaus gewaehlt: Teile ueber ihre Namen; sonst dieselben Objekte
        const int n = wiederholungenEinfuegen(abschnitt, obenAuswahl);
        if (n == 0)
            MessageBoxW(fenster, L"Keine Wiederholung eingefügt: Das Ziel enthält keine Teile mit denselben Namen\n"
                                 L"(oder steht nicht im Snapshot am Abschnittsbeginn).",
                        L"Wiederholungen einfügen", MB_OK | MB_ICONINFORMATION);
        geaendert = n > 0;
    }
    else if (wahl == M_ALS_ABLAUF && obenAuswahl)
    {
        AblaufVorlage v;
        std::string fehler;
        if (!ablaufAusAbschnitt(abschnitt, obenAuswahl, v, fehler))
            MessageBoxW(fenster, ausUtf8(fehler).c_str(), L"Als Ablauf speichern", MB_OK | MB_ICONINFORMATION);
        else
        {
            v.name = obenAuswahl->name + " " + std::to_string(abschnitt + 1);
            ablaufVorlageSpeichern(fenster, v);
        }
    }
    else if (wahl == M_POSEN && obenAuswahl)
        posenAlsAblauf(fenster, obenAuswahl, abschnitt);
    else if (wahl == M_ABLAUF_HIER && obenAuswahl)
        ablaufDialog(fenster, obenAuswahl, zeitleisteKlickZeit);
    if (geaendert)
    {
        zeitpunktAnwenden(aktuelleZeit()); // Szene zur aktuellen Zeit neu berechnen
        zeitleisteAnzeigen();
        einstellungenAnzeigen();
    }
}

// grosse Zahl mit Tausenderpunkten (gerundet)
static std::wstring anzahlText(double n)
{
    if (!(n < 9e18))
        return L"sehr viele";
    return zahlMitPunkten((long long)std::llround(n));
}

// Stammlaenge, fuer die Tree::build gemacht ist (addBaum in scene.cpp)
static const double BAUM_REFERENZ = 5.0;

// Erwartete Dreiecke eines Baums, nachgerechnet aus Tree::build: je Glied ein Ast aus 16 Dreiecken,
// in den letzten zwei Stufen Blaetter (je 2 Dreiecke), danach Hauptast mit Wahrscheinlichkeit p
// und im Mittel 3 Seitenaeste; Abbruch bei Holzradius < 0.01
// Dreiecke eines Baums wie Tree::build (Mittelwerte statt Zufall). Stufe fuer Stufe von unten nach oben
// gerechnet (vorher rekursiv: bei grosser Tiefe sehr langsam, der Hinweis rechnet bei jedem Tastendruck)
static double baumDreieckeSchaetzen(int tiefe, double radius, double p, const BaumParameter &bp)
{
    tiefe = std::min(tiefe, 30);
    const double dicke = std::clamp(bp.dickenFaktor, 0.05, 0.99);
    const double kinder = p + 0.5 * (bp.seitenMin + bp.seitenMax);
    const int ecken = std::max(3, bp.segmente);
    const double schwelle = 1.0 - 1.5 * std::clamp(bp.schnee, 0.0, 1.0);
    const double schneeAnteil = bp.schnee > 0 ? std::acos(std::clamp(schwelle, -1.0, 1.0)) / M_PI : 0.0;
    double summe = 0.0; // Dreiecke der Stufe darunter (tiefer im Baum) samt allem, was daran waechst
    for (int stufe = tiefe; stufe >= 1; --stufe)
    {
        // Stufe 'stufe' (von oben: 1 = Stamm) hat Restiefe depth = tiefe - stufe + 1
        int depth = tiefe - stufe + 1;
        double r = radius * std::pow(dicke, stufe - 1);
        if (r < bp.mindestDicke)
        {
            summe = 0.0;
            continue;
        }
        double n = 2.0 * ecken * (1.0 + schneeAnteil);
        if (depth <= bp.blattStufen)
            n += 2.0 * bp.blattBueschel * std::max(1, int(std::pow(r, 1.5) * bp.blattDichte));
        summe = n + kinder * summe;
    }
    return summe;
}

// "schoener" Teilstrich-Abstand: 1, 2 oder 5 mal Zehnerpotenz, etwa laenge / 8
static double schoenerSchritt(double laenge)
{
    double ziel = laenge / 8.0;
    double basis = std::pow(10.0, std::floor(std::log10(ziel)));
    for (double f : {1.0, 2.0, 5.0})
        if (basis * f >= ziel)
            return basis * f;
    return basis * 10.0;
}

// Zahl fuer die Beschriftung: ohne unnoetige Nachkommastellen ("-0" vermeiden)
static std::string zahlFuerText(double v)
{
    if (std::fabs(v) < 1e-9)
        v = 0.0;
    char puffer[32];
    std::snprintf(puffer, sizeof(puffer), "%.4g", v);
    return puffer;
}

// mehrere neue Objekte auf einmal (Wolke aus Kugeln, Koordinatensystem): zusammen als eine Gruppe,
// damit sie gemeinsam verschoben, gedreht und skaliert werden (Gruppe aufheben: Strg+Umschalt+G).
// Ein Rueckgaengig-Schritt; die Gruppe ist danach ausgewaehlt.
static void neueObjekteEinfuegen(std::vector<std::unique_ptr<object>> objekte, const std::string &name)
{
    if (objekte.empty() || !gSzeneAendern)
    {
        MessageBeep(MB_ICONWARNING);
        return;
    }

    std::unique_ptr<object> neu;
    if (objekte.size() == 1)
        neu = std::move(objekte.front());
    else
    {
        auto gruppe = std::make_unique<Gruppe>();
        for (auto &o : objekte)
            gruppe->kinder.push_back(std::move(o));
        gruppe->grenzenAktualisieren();
        neu = std::move(gruppe);
    }
    neu->name = freierName(name);
    object *zeiger = neu.get();

    gSzeneAendern([&]
                  { scene.push_back(std::move(neu)); });
    eingefuegtMerken(name, {zeiger});
    auswahlSetzen({zeiger});
    abgleichen();
}

static void einfuegen(int art)
{
    if (!gSzeneAendern)
        return;

    const double s = passendeGroesse();
    const Vector3D p = einfuegePunkt();
    const Material mat = makeMaterialSimple(Farbe(0.7, 0.7, 0.7));

    // ---------- Lichter ----------
    std::unique_ptr<light> licht;
    if (art == NEU_PUNKTLICHT)
        licht = std::make_unique<PointLight>(p + Vector3D(0, s, 0), Farbe(1, 1, 1), 300.0); // x pi seit 1/pi im Shading
    else if (art == NEU_KUGELLICHT)
        licht = std::make_unique<SphereLight>(p + Vector3D(0, s, 0), s * 0.1, Farbe(1, 1, 1), 150.0);
    else if (art == NEU_FLAECHENLICHT)
    {
        // waagrechtes Rechteck ueber dem Einfuegepunkt, leuchtet nach unten (U = +X, V = +Z)
        const double k = s * 0.5;
        auto a = std::make_unique<AreaLight>(Vector3D(0, 0, 0), Vector3D(k, 0, 0), Vector3D(0, 0, k), Farbe(1, 1, 1), 150.0);
        a->setPosition(p + Vector3D(0, s, 0));
        licht = std::move(a);
    }
    else if (art == NEU_MESHLICHT)
    {
        Material leucht = makeMaterialSimple(Farbe(1, 1, 1));
        leucht.emission = Farbe(1, 1, 1);
        std::unique_ptr<object> w = createCubeMesh(p + Vector3D(0, s * 0.5, 0), s * 0.3, leucht);
        if (auto *m = dynamic_cast<Mesh *>(w.get()))
        {
            w.release();
            licht = std::make_unique<MeshLight>(std::unique_ptr<Mesh>(m), Farbe(1, 1, 1), 150.0);
        }
    }
    else if (art == NEU_SPOTLICHT)
    {
        // schraeg oberhalb des Einfuegepunkts, auf ihn gerichtet; Kegel 60 Grad, weicher Rand
        const Vector3D pos = p + Vector3D(-0.6, 2.0, -0.6) * s;
        licht = std::make_unique<SpotLight>(pos, p - pos, Farbe(1, 1, 1), 600.0, 30.0, 0.25);
    }
    else if (art == NEU_RICHTUNGSLICHT)
    {
        auto d = std::make_unique<DirectionalLight>(Vector3D(-1, 2, -1).normalized(), Farbe(1, 1, 1), 3.0); // Richtung ZUM Licht: von oben; Staerke 3 (seit 1/pi im Shading)
        richtungslichtSymbolSetzen(d.get(), p + Vector3D(-1, 2, -1).normalized() * (s * 2.0)); // Symbol beim Einfuegepunkt
        licht = std::move(d);
    }

    if (licht)
    {
        light *neu = licht.get();
        gSzeneAendern([&]
                      { lights.push_back(std::move(licht)); });
        eingefuegtMerken("Neues Licht", {}, {neu});

        auswahlSetzen({});
        selectedLight = neu;
        selectedType = SelectedType::LIGHT;
        abgleichen();
        return;
    }

    // ---------- Grundobjekte ----------
    std::unique_ptr<object> obj;
    std::string name;

    switch (art)
    {
    case NEU_WUERFEL:
        obj = createCubeMesh(p, s, mat);
        name = "W\xC3\xBC" "rfel"; // UTF-8
        break;

    case NEU_KUGEL:
        obj = std::make_unique<Kugel>(p, s * 0.5, mat);
        name = "Kugel";
        break;

    case NEU_EBENE: // waagrecht, s*4 x s*4, Oberseite bei p.y
    {
        // als duenne Platte (Dicke 1 % der Kantenlaenge) statt zwei Dreiecken ohne Dicke:
        // so laesst sich auch die Dicke unter "Groesse" einstellen, und die Ebene hat eine
        // eindeutige Ober- und Unterseite
        const double h = s * 2.0, dicke = s * 0.04;
        Vector3D mn(p.x - h, p.y - dicke, p.z - h), mx(p.x + h, p.y, p.z + h);
        formen::Netz n(mat);
        auto e = [&](int i)
        { return Vector3D((i & 1) ? mx.x : mn.x, (i & 2) ? mx.y : mn.y, (i & 4) ? mx.z : mn.z); };
        n.viereck(e(2), e(6), e(7), e(3)); // oben (Normale +Y)
        n.viereck(e(0), e(1), e(5), e(4)); // unten
        n.viereck(e(0), e(2), e(3), e(1)); // vorne (z min)
        n.viereck(e(4), e(5), e(7), e(6)); // hinten
        n.viereck(e(0), e(4), e(6), e(2)); // links
        n.viereck(e(1), e(3), e(7), e(5)); // rechts
        auto netz = std::make_unique<Mesh>(n.d, Vector3D(0, 0, 0));
        netz->mat = mat;
        obj = std::move(netz);
        name = "Ebene";
        break;
    }

    case NEU_WAND: // senkrecht in der XY-Ebene, s*2 breit, s hoch
        obj = createWall(Vector3D(p.x - s, p.y, p.z), Vector3D(p.x + s, p.y, p.z),
                         Vector3D(p.x + s, p.y + s, p.z), Vector3D(p.x - s, p.y + s, p.z), mat, s * 0.05);
        name = "Wand";
        break;

    case NEU_ZYLINDER:
        obj = std::make_unique<Stab>(p, p + Vector3D(0, s, 0), s * 0.15, mat);
        name = "Zylinder";
        break;

    case NEU_PFEIL: // waagrecht in +X, Laenge s*2, mittig um den Einfuegepunkt
        obj = std::make_unique<Pfeil>(p - Vector3D(s, 0, 0), p + Vector3D(s, 0, 0), s * 0.05, mat);
        name = "Pfeil";
        break;

    // ---------- Kugeln mit Material (aus MaterialLibrary, wie addSphere in scene.cpp) ----------
    case NEU_KUGEL_SPIEGEL:
    case NEU_KUGEL_GOLD:
    case NEU_KUGEL_GLAS:
    case NEU_KUGEL_GRUENGLAS:
    case NEU_KUGEL_PLASTIK:
    {
        struct Vorgabe
        {
            int art;
            MaterialType typ;
            const char *id; // wie in der Szenen-XML (SceneLoader::stringToMaterialType)
            const char *name;
        };
        static const Vorgabe vorgaben[] = {
            {NEU_KUGEL_SPIEGEL, MaterialType::Mirror, "mirror", "Spiegelkugel"},
            {NEU_KUGEL_GOLD, MaterialType::Gold, "gold", "Goldkugel"},
            {NEU_KUGEL_GLAS, MaterialType::Glass, "glass", "Glaskugel"},
            {NEU_KUGEL_GRUENGLAS, MaterialType::GreenGlass, "greenGlass", "Grünglaskugel"},
            {NEU_KUGEL_PLASTIK, MaterialType::BluePlastic, "bluePlastic", "Plastikkugel"}};
        for (const auto &v : vorgaben)
            if (v.art == art)
            {
                // in die Materialliste, damit "Save XML" das Material (nicht nur die Farbe) speichert
                Material m = MaterialLibrary::get(v.typ);
                SceneLoader::materialMap[v.id] = m;
                auto k = std::make_unique<Kugel>(p, s * 0.5, m);
                k->materialName = v.id;
                obj = std::move(k);
                name = v.name;
            }
        break;
    }

    // ---------- Baum (wie addBaum in scene.cpp); Werte im Dialog, zuletzt benutzte bleiben ----------
    case NEU_BAUM:
    {
        // Felder (Nummern fuer werteZuParametern und den Hinweis)
        enum
        {
            B_LAENGE, B_RADIUS, B_TIEFE, B_WKEIT, B_DICKE, B_LMIN, B_LMAX, B_SMIN, B_SMAX, B_ANSATZ, B_SEITENLAENGE, B_STREU_H, B_STREU_S,
            B_SCHWER_H, B_SCHWER_S, B_SPITZE_MIN, B_SPITZE_MAX, B_MINDEST, B_SEGMENTE, B_HOLZFARBE,
            B_BLATTSTUFEN, B_BLATTDICHTE, B_BUESCHEL, B_BLATT_MIN, B_BLATT_MAX, B_BLATT_STREU, B_BLATTFARBE,
            B_SCHNEE, B_SCHNEEDICKE, B_SCHNEEFARBE, B_ZUFALL, B_ANZAHL
        };
        static std::vector<EingabeWert> werte;
        if (werte.size() != B_ANZAHL)
        {
            const BaumParameter v; // Vorgaben
            const double R = BAUM_REFERENZ;
            werte = {
                {L"Stammlänge", s * 0.5, false, 1e-6, 1e9,
                 L"Länge des untersten Stammstücks. Der ganze Baum wird etwa 4- bis 5-mal so hoch.\n"
                 L"Alle Angaben „× Stammlänge“ unten beziehen sich darauf."},
                {L"Radius des Holzes", s * 0.1, false, 1e-6, 1e9, L"Dicke des Stamms unten (in Einheiten der Szene)"},
                {L"Rekursionstiefe", 7, true, 1, 14,
                 L"Wie oft sich die Äste verzweigen. Jede Stufe hat etwa 3- bis 4-mal so viele Dreiecke wie die vorige."},
                {L"Wahrscheinlichkeit weiteres Glied", 0.9, false, 0, 1,
                 L"Wahrscheinlichkeit (0 bis 1), dass ein Ast in seiner Richtung weiterwächst.\nKleiner = lichterer, unregelmäßiger Baum."},
                {L"Dickenfaktor je Stufe", v.dickenFaktor, false, 0.05, 0.99,
                 L"Ein Ast ist so viel mal so dick wie der Ast, an dem er wächst (bisher 0,65).\n"
                 L"Größer = dickere Äste und mehr Stufen, bevor die Mindestdicke erreicht ist."},
                {L"Längenfaktor min", v.laengeMin, false, 0.01, 2,
                 L"Ein Ast ist zufällig zwischen min und max mal so lang wie der vorige (bisher 0,75 bis 0,9)"},
                {L"Längenfaktor max", v.laengeMax, false, 0.01, 2, L"siehe Längenfaktor min"},
                {L"Seitenäste min", double(v.seitenMin), true, 0, 20, L"Seitenäste je Glied, zufällig zwischen min und max (bisher 2 bis 4)"},
                {L"Seitenäste max", double(v.seitenMax), true, 0, 20, L"siehe Seitenäste min"},
                {L"Ansatz der Seitenäste ab", v.ansatzMin, false, 0, 1,
                 L"Seitenäste wachsen zwischen diesem Anteil der Astlänge und dem Astende (0 = ganz unten, bisher 0,2)"},
                {L"Länge der Seitenäste (Faktor)", v.seitenLaenge, false, 0.01, 5,
                 L"Seitenäste sind so viel mal so lang wie der weiterwachsende Ast (bisher 1).\n"
                 L"Kleiner (0,6 bis 0,8): kurze Seitenäste, die nach oben kürzer werden – Kegelform wie bei einer Tanne"},
                {L"Streuung Hauptast", v.streuungHaupt, false, 0, 5,
                 L"Wie stark der weiterwachsende Ast zufällig zur Seite abknickt (bisher 0,3)"},
                {L"Streuung Seitenäste", v.streuungSeiten, false, 0, 5,
                 L"Wie weit Seitenäste zur Seite abstehen (bisher 0,6). Größer = breitere Krone"},
                {L"Steigung Hauptast", v.schwereHaupt, false, 0, 5,
                 L"Faktor auf den nach oben zeigenden Anteil der Richtung (bisher 0,7).\nKleiner = Äste hängen mehr (Schwerkraft), größer = schlanker, aufrechter Baum"},
                {L"Steigung Seitenäste", v.schwereSeiten, false, 0, 5, L"wie Steigung Hauptast, für die Seitenäste (bisher 0,6)"},
                {L"Spitzenradius min", v.spitzeMin, false, 0.01, 1,
                 L"Radius am Ende eines Glieds als Anteil vom Anfang, zufällig zwischen min und max (bisher 0,6 bis 0,9)"},
                {L"Spitzenradius max", v.spitzeMax, false, 0.01, 1, L"siehe Spitzenradius min"},
                {L"Mindestdicke (× Stammlänge)", v.mindestDicke / R, false, 1e-6, 1,
                 L"Dünnere Äste wachsen nicht mehr, auch keine Blätter daran (bisher 0,002 = 1/500)"},
                {L"Ecken je Ast", double(v.segmente), true, 3, 32,
                 L"Ecken des Querschnitts der Äste (bisher 8). Weniger = eckiger, aber weniger Dreiecke"},
                farbFeld(L"Holzfarbe", v.holzFarbe, L"Farbe des Holzes als \"Rot Grün Blau\", je 0 bis 1 (bisher 0.36 0.22 0.12)"),

                {L"Blätter an den letzten Stufen", double(v.blattStufen), true, 0, 14,
                 L"An wie vielen der letzten Stufen Blätter wachsen (bisher 2). 0 = kahler Baum (Winter)"},
                {L"Blattdichte", v.blattDichte, false, 0, 1e6,
                 L"Blätter je Astende ≈ Dichte × (Astradius im Bezugsmaßstab)^1,5, mindestens 1 (bisher 50)"},
                {L"Blattbüschel je Astende", double(v.blattBueschel), true, 1, 100,
                 L"Faktor auf die Anzahl der Blätter je Astende (bisher 3)"},
                {L"Blattgröße min (× Stammlänge)", v.blattMin / R, false, 1e-6, 10,
                 L"Halbe Kantenlänge eines Blatts, zufällig zwischen min und max (bisher 0,02 bis 0,04)"},
                {L"Blattgröße max (× Stammlänge)", v.blattMax / R, false, 1e-6, 10, L"siehe Blattgröße min"},
                {L"Blattstreuung (× Stammlänge)", v.blattStreuung / R, false, 0, 10,
                 L"Radius der Kugel ums Astende, in der die Blätter liegen (bisher 0,02)"},
                farbFeld(L"Blattfarbe", v.blattFarbe, L"Farbe der Blätter als \"Rot Grün Blau\", je 0 bis 1 (bisher 0.2 0.55 0.2)"),

                {L"Schnee (0 bis 1)", 0.0, false, 0, 1,
                 L"0 = kein Schnee. Sonst liegt eine Schneeschicht auf der Oberseite der Äste (mehr = weiter um den Ast herum)\n"
                 L"und waagrechte Blätter werden weiß. Für einen Winterbaum: Blätter an 0 Stufen, Schnee 0,6"},
                {L"Schneedicke (× Astradius)", v.schneeDicke, false, 0, 5, L"Dicke der Schneeschicht im Verhältnis zur Astdicke"},
                farbFeld(L"Schneefarbe", v.schneeFarbe, L"Farbe des Schnees als \"Rot Grün Blau\" (z.B. leicht bläulich 0.9 0.93 1)"),
                {L"Zufallszahl", 0, true, 0, 1e9,
                 L"0 = jeder Baum anders. Eine feste Zahl ergibt bei gleichen Werten immer denselben Baum"}};
            werte[B_LAENGE].abschnitt = L"STAMM UND ÄSTE";
            werte[B_BLATTSTUFEN].abschnitt = L"BLÄTTER";
            werte[B_BLATTSTUFEN].neueSpalte = true;
            werte[B_SCHNEE].abschnitt = L"SCHNEE";
            werte[B_ZUFALL].abschnitt = L"ZUFALL";
        }

        // Werte -> Parameter im Bezugsmassstab (Stammlaenge BAUM_REFERENZ); Farben direkt aus den Feldern
        auto parameter = [](const std::vector<double> &w, const std::vector<EingabeWert> &felder)
        {
            const double R = BAUM_REFERENZ;
            BaumParameter p;
            p.dickenFaktor = w[B_DICKE];
            p.laengeMin = w[B_LMIN], p.laengeMax = w[B_LMAX];
            p.seitenMin = int(std::round(w[B_SMIN])), p.seitenMax = int(std::round(w[B_SMAX]));
            p.ansatzMin = w[B_ANSATZ];
            p.seitenLaenge = w[B_SEITENLAENGE];
            p.streuungHaupt = w[B_STREU_H], p.streuungSeiten = w[B_STREU_S];
            p.schwereHaupt = w[B_SCHWER_H], p.schwereSeiten = w[B_SCHWER_S];
            p.spitzeMin = w[B_SPITZE_MIN], p.spitzeMax = w[B_SPITZE_MAX];
            p.mindestDicke = w[B_MINDEST] * R;
            p.segmente = int(std::round(w[B_SEGMENTE]));
            p.holzFarbe = felder[B_HOLZFARBE].farbe;
            p.blattStufen = int(std::round(w[B_BLATTSTUFEN]));
            p.blattDichte = w[B_BLATTDICHTE];
            p.blattBueschel = int(std::round(w[B_BUESCHEL]));
            p.blattMin = w[B_BLATT_MIN] * R, p.blattMax = w[B_BLATT_MAX] * R;
            p.blattStreuung = w[B_BLATT_STREU] * R;
            p.blattFarbe = felder[B_BLATTFARBE].farbe;
            p.schnee = w[B_SCHNEE];
            p.schneeDicke = w[B_SCHNEEDICKE];
            p.schneeFarbe = felder[B_SCHNEEFARBE].farbe;
            p.zufall = unsigned(std::round(w[B_ZUFALL]));
            if (p.seitenMin > p.seitenMax)
                std::swap(p.seitenMin, p.seitenMax);
            return p;
        };

        auto baumHinweis = [&](const std::vector<double> &w)
        {
            const BaumParameter bp = parameter(w, werte);
            int tiefe = int(std::round(w[B_TIEFE]));
            // erzeugt wird im Massstab Stammlaenge 5 (siehe Einfuegen): Radius entsprechend umrechnen
            double radius = w[B_LAENGE] > 0 ? w[B_RADIUS] * BAUM_REFERENZ / w[B_LAENGE] : w[B_RADIUS];
            // Tree::build bricht ab, sobald das Holz duenner als die Mindestdicke ist:
            // tiefere Stufen entstehen dann gar nicht, auch nicht ihre Blaetter
            int stufen = 0;
            for (double r = radius; stufen < tiefe && r >= bp.mindestDicke; r *= std::clamp(bp.dickenFaktor, 0.05, 0.99))
                ++stufen;
            double L = 0.5 * (std::min(bp.laengeMin, bp.laengeMax) + std::max(bp.laengeMin, bp.laengeMax));
            double hoehe = std::fabs(L - 1.0) < 1e-9 ? w[B_LAENGE] * stufen : w[B_LAENGE] * (1.0 - std::pow(L, stufen)) / (1.0 - L);
            double n = baumDreieckeSchaetzen(tiefe, radius, std::clamp(w[B_WKEIT], 0.0, 1.0), bp);
            std::wstring t = L"Höhe etwa " + zahlText(hoehe, 2) + L" (Stamm entlang), ungefähr " + anzahlText(n) +
                             L" Dreiecke (Zufall: ± 20 %).";
            if (stufen < tiefe)
                t += L"\nDas Holz wird nach " + std::to_wstring(stufen) + L" Stufen zu dünn: tiefere Stufen und ihre "
                     L"Blätter fallen weg. Radius, Dickenfaktor oder Mindestdicke ändern oder Tiefe " + std::to_wstring(stufen) + L" wählen.";
            if (n > 300000)
                t += L"\nViele Dreiecke: die Ansichten werden träge.";
            return t;
        };
        // Vorlagen (Werte fuer Stammlaenge 1 bzw. Radius: mal Szenengroesse s); Reihenfolge wie die Felder
        // ohne Farben und Zufall
        struct BaumVorlage
        {
            const wchar_t *name;
            double w[27];
            Farbe holz, blatt, schnee;
        };
        static const BaumVorlage baumVorlagen[] = {
            {L"Laubbaum", {0.5, 0.1, 7, 0.9, 0.65, 0.75, 0.9, 2, 4, 0.2, 1, 0.3, 0.6, 0.7, 0.6, 0.6, 0.9, 0.002, 8, 2, 50, 3, 0.02, 0.04, 0.02, 0, 0.3}, Farbe(0.36, 0.22, 0.12), Farbe(0.2, 0.55, 0.2), Farbe(0.95, 0.95, 0.97)},
            {L"Laubbaum im Herbst", {0.5, 0.1, 7, 0.9, 0.65, 0.75, 0.9, 2, 4, 0.2, 1, 0.3, 0.6, 0.7, 0.6, 0.6, 0.9, 0.002, 8, 2, 50, 3, 0.02, 0.04, 0.02, 0, 0.3}, Farbe(0.36, 0.22, 0.12), Farbe(0.85, 0.42, 0.1), Farbe(0.95, 0.95, 0.97)},
            {L"Laubbaum mit Schnee (ohne Blätter)", {0.5, 0.1, 8, 0.9, 0.65, 0.75, 0.9, 2, 4, 0.2, 1, 0.3, 0.6, 0.7, 0.6, 0.6, 0.9, 0.0015, 8, 0, 50, 3, 0.02, 0.04, 0.02, 0.6, 0.35}, Farbe(0.36, 0.22, 0.12), Farbe(0.2, 0.55, 0.2), Farbe(0.95, 0.95, 0.97)},
            {L"Kahler Laubbaum (Winter)", {0.5, 0.1, 8, 0.9, 0.65, 0.75, 0.9, 2, 4, 0.2, 1, 0.3, 0.6, 0.7, 0.6, 0.6, 0.9, 0.0015, 8, 0, 50, 3, 0.02, 0.04, 0.02, 0, 0.3}, Farbe(0.36, 0.22, 0.12), Farbe(0.2, 0.55, 0.2), Farbe(0.95, 0.95, 0.97)},
            {L"Obstbaum blühend", {0.3, 0.06, 6, 0.9, 0.65, 0.75, 0.9, 2, 4, 0.2, 1, 0.3, 0.8, 0.5, 0.45, 0.6, 0.9, 0.002, 8, 2, 150, 3, 0.02, 0.04, 0.02, 0, 0.3}, Farbe(0.3, 0.2, 0.15), Farbe(1, 0.8, 0.88), Farbe(0.95, 0.95, 0.97)},
            {L"Birke", {0.5, 0.06, 7, 0.9, 0.7, 0.8, 0.92, 2, 4, 0.2, 1, 0.12, 0.5, 1, 0.35, 0.6, 0.9, 0.0015, 8, 2, 50, 3, 0.015, 0.03, 0.02, 0, 0.3}, Farbe(0.85, 0.84, 0.8), Farbe(0.45, 0.7, 0.25), Farbe(0.95, 0.95, 0.97)},
            {L"Pappel", {0.5, 0.07, 8, 0.9, 0.7, 0.8, 0.9, 2, 3, 0.3, 1, 0.08, 0.25, 1.2, 1.6, 0.6, 0.9, 0.0015, 8, 3, 50, 3, 0.02, 0.04, 0.02, 0, 0.3}, Farbe(0.36, 0.22, 0.12), Farbe(0.25, 0.5, 0.2), Farbe(0.95, 0.95, 0.97)},
            {L"Tannenbaum", {0.45, 0.045, 6, 1, 0.75, 0.8, 0.85, 4, 5, 0, 0.75, 0.02, 1.5, 0.9, 0.15, 0.85, 0.95, 0.0015, 4, 4, 120, 2, 0.02, 0.035, 0.035, 0, 0.3}, Farbe(0.3, 0.2, 0.12), Farbe(0.07, 0.28, 0.12), Farbe(0.95, 0.95, 0.97)},
            {L"Tannenbaum mit Schnee", {0.45, 0.045, 6, 1, 0.75, 0.8, 0.85, 4, 5, 0, 0.75, 0.02, 1.5, 0.9, 0.15, 0.85, 0.95, 0.0015, 4, 4, 120, 2, 0.02, 0.035, 0.035, 0.7, 0.4}, Farbe(0.3, 0.2, 0.12), Farbe(0.07, 0.28, 0.12), Farbe(0.95, 0.95, 0.97)},
            {L"Strauch", {0.12, 0.012, 5, 0.85, 0.75, 0.8, 0.95, 3, 5, 0, 1, 0.5, 1, 0.8, 0.7, 0.6, 0.9, 0.0015, 5, 3, 400, 3, 0.03, 0.055, 0.05, 0, 0.3}, Farbe(0.36, 0.22, 0.12), Farbe(0.18, 0.45, 0.15), Farbe(0.95, 0.95, 0.97)},
            {L"Blühender Strauch", {0.12, 0.012, 5, 0.85, 0.75, 0.8, 0.95, 3, 5, 0, 1, 0.5, 1, 0.8, 0.7, 0.6, 0.9, 0.0015, 5, 3, 400, 3, 0.03, 0.055, 0.05, 0, 0.3}, Farbe(0.36, 0.22, 0.12), Farbe(0.95, 0.6, 0.8), Farbe(0.95, 0.95, 0.97)},
            {L"Blume (rot)", {0.06, 0.0015, 2, 1, 0.9, 0.9, 1, 0, 0, 0.2, 1, 0.05, 0.6, 1, 0.6, 0.9, 1, 0.001, 5, 1, 1500, 1, 0.08, 0.12, 0.08, 0, 0.3}, Farbe(0.2, 0.5, 0.15), Farbe(0.9, 0.15, 0.25), Farbe(0.95, 0.95, 0.97)},
            {L"Blume (gelb)", {0.06, 0.0015, 2, 1, 0.9, 0.9, 1, 0, 0, 0.2, 1, 0.05, 0.6, 1, 0.6, 0.9, 1, 0.001, 5, 1, 1500, 1, 0.08, 0.12, 0.08, 0, 0.3}, Farbe(0.2, 0.5, 0.15), Farbe(1, 0.82, 0.1), Farbe(0.95, 0.95, 0.97)},
            {L"Grasbüschel", {0.05, 0.0025, 2, 0, 0.8, 1, 1.4, 10, 16, 0, 1, 0.3, 0.9, 0.7, 1, 0.05, 0.1, 0.0005, 3, 0, 50, 3, 0.02, 0.04, 0.02, 0, 0.3}, Farbe(0.3, 0.55, 0.15), Farbe(0.2, 0.55, 0.2), Farbe(0.95, 0.95, 0.97)},
            {L"Abgestorbener Baum", {0.5, 0.1, 6, 0.7, 0.65, 0.75, 0.9, 1, 3, 0.2, 1, 0.3, 0.9, 0.7, 0.4, 0.5, 0.75, 0.002, 8, 0, 50, 3, 0.02, 0.04, 0.02, 0, 0.3}, Farbe(0.42, 0.38, 0.33), Farbe(0.2, 0.55, 0.2), Farbe(0.95, 0.95, 0.97)},
        };
        {
            static const int feld[27] = {B_LAENGE, B_RADIUS, B_TIEFE, B_WKEIT, B_DICKE, B_LMIN, B_LMAX, B_SMIN, B_SMAX,
                                         B_ANSATZ, B_SEITENLAENGE, B_STREU_H, B_STREU_S, B_SCHWER_H, B_SCHWER_S,
                                         B_SPITZE_MIN, B_SPITZE_MAX, B_MINDEST, B_SEGMENTE, B_BLATTSTUFEN, B_BLATTDICHTE,
                                         B_BUESCHEL, B_BLATT_MIN, B_BLATT_MAX, B_BLATT_STREU, B_SCHNEE, B_SCHNEEDICKE};
            dialogVorlagen.clear();
            for (const auto &bv : baumVorlagen)
            {
                DialogVorlage d;
                d.name = bv.name;
                d.texte.assign(B_ANZAHL, std::wstring());
                for (int k = 0; k < 27; ++k)
                {
                    double x = bv.w[k];
                    if (feld[k] == B_LAENGE || feld[k] == B_RADIUS)
                        x *= s; // Stammlaenge und Radius als Vielfache der Szenengroesse (Laubbaum: 0,5 s wie die Vorgabe)
                    d.texte[size_t(feld[k])] = zahlText(x, werte[size_t(feld[k])].ganzzahl ? 0 : 4);
                }
                d.texte[B_HOLZFARBE] = farbFeldText(bv.holz);
                d.texte[B_BLATTFARBE] = farbFeldText(bv.blatt);
                d.texte[B_SCHNEEFARBE] = farbFeldText(bv.schnee);
                dialogVorlagen.push_back(std::move(d));
            }
        }
        if (!werteAbfragen(L"Neuer Baum", werte, baumHinweis))
            return;

        std::vector<double> w;
        for (auto &e : werte)
            w.push_back(e.wert);
        // Tree::build hat Groessen im Bezugsmassstab (Stammlaenge 5, wie in scene.cpp). Daher in diesem
        // Massstab erzeugen und danach auf die gewuenschte Groesse skalieren.
        const double f = werte[B_LAENGE].wert / BAUM_REFERENZ;
        auto baum = std::make_unique<Tree>(Vector3D(0, 0, 0), Vector3D(0.0, 1.0, 0.0), BAUM_REFERENZ, // senkrecht
                                           werte[B_RADIUS].wert / f, int(werte[B_TIEFE].wert), werte[B_WKEIT].wert,
                                           parameter(w, werte));
        object *b = baum.get();
        if (std::fabs(f - 1.0) > 1e-12)
            objektSkalieren(b, Vector3D(f, f, f), Vector3D(0, 0, 0)); // Fusspunkt bleibt im Ursprung
        b->translate(p);
        obj = std::move(baum);
        name = "Baum";
        break;
    }

    // ---------- Terrain und Wasser (wie addTerrain/addWasser, kleineres Raster) ----------
    case NEU_TERRAIN:
    case NEU_TERRAIN_FEIN:
    case NEU_WASSER:
    {
        Material m = art == NEU_WASSER
                         ? MaterialLibrary::get(MaterialType::Wasser)
                         : Material(Farbe(0, 1, 0), Farbe(0, 1, 0), Farbe(1, 1, 1), Farbe(0, 1, 0), 0.3, 0.7, 0.0, 0.0,
                                    0.0, 0.0, 0.0, 0.0, Farbe(0, 0, 0)); // wie addTerrain
        // Werte im Dialog, getrennt fuer Terrain und Wasser; zuletzt benutzte bleiben
        static std::vector<EingabeWert> terrainWerte, wasserWerte;
        std::vector<EingabeWert> &werte = art == NEU_WASSER ? wasserWerte : terrainWerte;
        if (werte.empty())
            werte = {{L"Größe (X und Z)", s * 8.0, false, 1e-6, 1e9,
                      L"Seitenlänge der quadratischen Fläche; mittig unter den Einfügepunkt (Kamera-Zielpunkt) gelegt"},
                     {L"Höhe", art == NEU_WASSER ? s * 0.1 : s * 0.6, false, 0, 1e9,
                      art == NEU_WASSER ? L"Höhe der Wellen (Unterschied zwischen Wellental und -berg)"
                                        : L"Höhenunterschied zwischen tiefster und höchster Stelle (Hügel)"},
                     {L"Raster (Punkte je Seite)", 128, true, 2, 1024,
                      L"Anzahl Gitterpunkte je Seite (2 bis 1024). Mehr = feinere Form, aber 2 × (Raster − 1)² Dreiecke"},
                     {L"Oktaven (Detailstufen)", art == NEU_WASSER ? 3.0 : 6.0, true, 1, 12,
                      L"Wie viele Rauschschichten übereinander liegen: jede weitere halb so groß und halb so hoch.\n"
                      L"Wenig = sanfte Wellen, viel = zerklüftet. Mehr Oktaven als Raster/4 bringen nichts."}};
        auto terrainHinweis = [](const std::vector<double> &w)
        {
            double raster = std::max(2.0, std::round(w[2]));
            double n = 2.0 * (raster - 1) * (raster - 1);
            std::wstring t = anzahlText(n) + L" Dreiecke, Punktabstand " + zahlText(w[0] / (raster - 1), 3) + L".";
            if (n > 300000)
                t += L"\nViele Dreiecke: die Ansichten werden träge.";
            return t;
        };
        if (!werteAbfragen(art == NEU_WASSER ? L"Neues Wasser" : L"Neues Terrain", werte, terrainHinweis))
            return;

        auto t = std::make_unique<Mesh>(m);
        t->generateTerrain(int(werte[2].wert), werte[0].wert, werte[1].wert, int(werte[3].wert));

        // erzeugt von (0,0,0) bis (groesse, h, groesse): mittig unter den Einfuegepunkt
        Vector3D mn, mx;
        t->getWorldAABB(mn, mx);
        t->translate(Vector3D(p.x - (mn.x + mx.x) * 0.5, p.y - mx.y, p.z - (mn.z + mx.z) * 0.5));
        obj = std::move(t);
        name = art == NEU_WASSER ? "Wasser" : "Terrain";
        break;
    }

    // ---------- Wolke aus vielen Kugeln (wie addWolkenAusKugeln) ----------
    case NEU_ELLIPSOID:
    {
        static std::vector<EingabeWert> werte;
        if (werte.empty())
        {
            werte = {{L"Radius X", s * 0.5, false, 1e-6, 1e9, L"Halbe Länge in X (links–rechts)"},
                     {L"Radius Y", s * 0.3, false, 1e-6, 1e9, L"Halbe Höhe (Y)"},
                     {L"Radius Z", s * 0.3, false, 1e-6, 1e9, L"Halbe Tiefe (Z)"},
                     {L"Segmente rundherum", 32, true, 6, 256, L"Ecken je Ring (die Hälfte davon als Ringe): mehr = runder, mehr Dreiecke"},
                     {L"Schattierung", 0, true, 0, 1, L"Glatt: weiche Rundung (Normalen der Ellipsoid-Fläche). Facettiert: sichtbare Flächen"}};
            werte[4].auswahl = L"glatt|facettiert";
        }
        auto hinweis = [](const std::vector<double> &w)
        {
            const int seg = std::max(6, int(std::round(w[3])));
            return L"Etwa " + std::to_wstring(seg * seg) + L" Dreiecke. Das Ellipsoid steht mit der Unterseite auf dem "
                   L"Einfügepunkt.\nGut für Körper und Köpfe von Tieren, Eier, Kissen, Linsen, Wolken, Blätter (flach).";
        };
        if (!werteAbfragen(L"Neues Ellipsoid", werte, hinweis))
            return;
        const double rx = werte[0].wert, ry = werte[1].wert, rz = werte[2].wert;
        std::vector<Dreieck> d = koerper::ellipsoid(p + Vector3D(0, ry, 0), rx, ry, rz, int(werte[3].wert), werte[4].wert < 0.5,
                                                    makeMaterialSimple(Farbe(0.7, 0.7, 0.7)));
        std::vector<std::unique_ptr<object>> teile;
        teile.push_back(std::make_unique<Mesh>(d, Vector3D(0, 0, 0)));
        neueObjekteEinfuegen(std::move(teile), "Ellipsoid");
        return;
    }

    case NEU_STEIN:
    {
        static std::vector<EingabeWert> werte;
        if (werte.empty())
            werte = {{L"Größe", s, false, 1e-6, 1e9,
                      L"Längste Ausdehnung des Steins (vor dem Abflachen) in Einheiten"},
                     {L"Anzahl Schnitte", 14, true, 1, 200,
                      L"Wie oft der Ausgangswürfel mit einer zufällig gedrehten Ebene beschnitten wird (Abschläge).\n"
                      L"Wenige (4–8): klotzig, noch würfelähnlich; 12–20: typischer Stein; viele (40+): fast rund wie ein Kiesel"},
                     {L"Schnitttiefe", 1.0, false, 0.05, 2.5,
                      L"Wie tief jeder Abschlag geht: 1 = normal (15–38 % der Ausdehnung in dieser Richtung),\n"
                      L"kleiner = nur Ecken angeschlagen, größer = grobe, große Bruchflächen"},
                     {L"Höhe (Faktor)", 0.7, false, 0.05, 5,
                      L"Höhe im Verhältnis zur Breite: < 1 flacher Stein (Kiesel, Platte), > 1 aufrecht (Felsnadel)"},
                     {L"Zufallswert", 1, true, 0, 1e9,
                      L"Startwert der Zufallsschnitte: gleicher Wert = gleiche Steine, anderer Wert = andere Formen"},
                     {L"Anzahl Steine", 1, true, 1, 500,
                      L"Wie viele Steine erzeugt werden. Jeder hat eine eigene Form und eine zufällige Drehung\n"
                      L"um alle drei Achsen; mehrere Steine werden ohne Überlappung um den Einfügepunkt verteilt\n"
                      L"und als Gruppe \"Steine\" eingefügt"},
                     {L"Streuradius", 0, false, 0, 1e9,
                      L"Radius der Fläche, auf der mehrere Steine verteilt werden.\n"
                      L"0 = automatisch (so dicht, dass die Steine knapp nebeneinander liegen)"}};
        werte[4].wert = double((GetTickCount() / 7) % 100000); // jedes Mal eine neue Form vorschlagen
        auto steinHinweis = [](const std::vector<double> &w)
        {
            int n = int(std::round(w[1]));
            int steine = std::max(1, int(std::round(w[5])));
            return std::to_wstring(steine) + (steine == 1 ? L" Stein" : L" Steine") + L", je etwa " + std::to_wstring(6 + n) + L" Bruchflächen, " + std::to_wstring(4 * (6 + n)) +
                   L" Dreiecke oder weniger.\nZufällig gedreht, mit der Unterseite auf der Höhe des Einfügepunkts.\n"
                   L"Danach z.B. Material \"Beton\" oder eine Textur; weicher mit Manipulation → Glatt schattieren oder Loop.";
        };
        if (!werteAbfragen(L"Neue Steine", werte, steinHinweis))
            return;
        const int anzahl = std::max(1, int(std::round(werte[5].wert)));
        const unsigned zufall = unsigned(werte[4].wert);
        const Material steinMat = makeMaterialSimple(Farbe(0.5, 0.48, 0.45));
        std::mt19937 rng(zufall * 2654435761u + 12345u);
        struct Liegend
        {
            double x, z, r;
        };
        std::vector<Liegend> liegen;
        std::vector<std::unique_ptr<object>> teile;
        for (int i = 0; i < anzahl; ++i)
        {
            std::vector<Dreieck> d = stein::erzeugen(werte[0].wert, int(werte[1].wert), werte[2].wert, werte[3].wert,
                                                     zufall + unsigned(i) * 7919u, steinMat);
            if (d.empty())
                continue;
            const double r = stein::zufaelligDrehen(d, rng);
            // Platz suchen: ein Stein in die Mitte, weitere zufaellig im Streukreis ohne Ueberlappung
            double x = 0, z = 0;
            if (!liegen.empty())
            {
                const double streu = werte[6].wert > 0 ? werte[6].wert
                                                       : werte[0].wert * 0.75 * std::sqrt(double(anzahl)) + r;
                std::uniform_real_distribution<double> gleich(0.0, 1.0);
                double besterAbstand = -1e300;
                for (int versuch = 0; versuch < 60; ++versuch)
                {
                    const double w = gleich(rng) * 2.0 * M_PI, rr = streu * std::sqrt(gleich(rng));
                    const double px = rr * std::cos(w), pz = rr * std::sin(w);
                    double luecke = 1e300; // kleinster Abstand zu den Nachbarn minus Radien
                    for (const Liegend &l : liegen)
                        luecke = std::min(luecke, std::hypot(px - l.x, pz - l.z) - l.r - r);
                    if (luecke > besterAbstand)
                    {
                        besterAbstand = luecke;
                        x = px;
                        z = pz;
                    }
                    if (luecke >= 0)
                        break;
                }
            }
            liegen.push_back({x, z, r});
            const Vector3D ort = p + Vector3D(x, 0, z);
            for (auto &t : d)
            {
                t.a += ort;
                t.b += ort;
                t.c += ort;
                t.rebuild();
            }
            auto m = std::make_unique<Mesh>(d, Vector3D(0, 0, 0));
            if (anzahl > 1)
                m->name = "Stein " + std::to_string(i + 1);
            teile.push_back(std::move(m));
        }
        if (teile.empty())
        {
            MessageBeep(MB_ICONWARNING);
            return;
        }
        neueObjekteEinfuegen(std::move(teile), anzahl > 1 ? "Steine" : "Stein");
        return;
    }

    case NEU_FIGUR:
    {
        // Figur aus Kugeln und Staeben als Gelenkkette (verschachtelte Gruppen), schaut nach +X (Laufrichtung).
        // Bewegungen: Pendeln der Glieder um ihre Gelenke (Wiederholungen) + Strecke ueber zwei Snapshots.
        enum
        {
            F_GROESSE, F_BEWEGUNG, F_RICHTUNG, F_TEMPO, F_DAUER, F_HAUT, F_OBEN, F_HOSE, F_SCHUHE, F_ANZAHL
        };
        static std::vector<EingabeWert> werte;
        if (werte.size() != F_ANZAHL)
        {
            const double h = (s >= 0.3 && s <= 6.0) ? 1.8 : s * 1.8; // Szene in Metern: 1,8 m
            werte = {
                {L"Körpergröße", h, false, 1e-6, 1e9, L"Höhe der Figur in Einheiten der Szene (Mensch: 1,8 bei Metern)"},
                {L"Bewegung", 1, true, 0, 3,
                 L"Rennen / Gehen: Arme und Beine pendeln um Schulter, Ellbogen, Hüfte, Knie und Knöchel, der Körper wippt,\n"
                 L"und die Figur läuft geradeaus. Winken: steht und winkt mit dem rechten Arm.\n"
                 L"Dafür werden Snapshots angelegt bzw. ergänzt und Wiederholungen eingetragen (Zeitleiste ↻)."},
                {L"Laufrichtung (Grad)", 0, false, -360, 360, L"Drehung um die senkrechte Achse: 0 = nach +X, 90 = nach −Z, 180 = nach −X"},
                {L"Tempo (Faktor)", 1, false, 0.1, 5, L"1 = normal; 2 = doppelt so schnelle Schritte und doppelte Geschwindigkeit"},
                {L"Dauer (s)", 4, false, 0.1, 1e5,
                 L"Länge der Bewegung, wenn es noch keine zwei Snapshots gibt (sonst: vom ersten bis zum letzten Snapshot)"},
                farbFeld(L"Haut", Farbe(0.85, 0.66, 0.52), L"Kopf und Hände (\"Rot Grün Blau\", je 0 bis 1)"),
                farbFeld(L"Oberteil", Farbe(0.15, 0.35, 0.75), L"Rumpf und Arme"),
                farbFeld(L"Hose", Farbe(0.18, 0.18, 0.22), L"Beine"),
                farbFeld(L"Schuhe", Farbe(0.85, 0.85, 0.85), L"Füße")};
            werte[F_BEWEGUNG].auswahl = L"keine (nur Figur)|Rennen|Gehen|Winken";
            werte[F_HAUT].abschnitt = L"FARBEN";
        }
        auto tempoDaten = [](int bewegung, double tempo, double groesse, double &periode, double &geschwindigkeit)
        {
            periode = bewegung == 1 ? 0.7 : 1.1; // Doppelschritt
            geschwindigkeit = bewegung == 1 ? 4.0 : bewegung == 2 ? 1.4 : 0.0;
            periode /= tempo;
            geschwindigkeit *= tempo * groesse / 1.8;
        };
        auto figurHinweis = [&](const std::vector<double> &v)
        {
            const int bw = int(std::lround(v[F_BEWEGUNG]));
            std::wstring t = L"Gruppe „Figur“ mit Armen und Beinen als Gelenkketten (Teile einzeln wählbar).";
            if (bw == 1 || bw == 2)
            {
                double p, g;
                tempoDaten(bw, v[F_TEMPO], v[F_GROESSE], p, g);
                t += L"\n" + std::wstring(bw == 1 ? L"Rennen" : L"Gehen") + L": Doppelschritt " + zahlText(p, 2) + L" s, " +
                     zahlText(g, 2) + L" Einheiten/s (" + zahlText(g * 3.6 * 1.8 / std::max(1e-9, v[F_GROESSE]), 1) +
                     L" km/h bei 1,8 m Größe).";
            }
            if (bw != 0)
                t += snapshotAnzahl() >= 2
                         ? L"\nDie Bewegung läuft vom ersten bis zum letzten vorhandenen Snapshot (" +
                               zahlText(animationsEnde() - animationsBeginn(), 2) + L" s)."
                         : L"\nEs werden Snapshots bei " + zahlText(snapshotAnzahl() ? snapshotZeit(0) : 0.0, 2) + L" s und " +
                               zahlText((snapshotAnzahl() ? snapshotZeit(0) : 0.0) + v[F_DAUER], 2) +
                               L" s angelegt; „Animation“ wird eingeschaltet.";
            if (bw != 0)
                t += L"\nStrg+Z entfernt nur die Figur, nicht die Snapshots und Wiederholungen.";
            return t;
        };
        if (!werteAbfragen(L"Neue Figur", werte, figurHinweis))
            return;

        const double H = werte[F_GROESSE].wert;
        const int bewegung = int(std::lround(werte[F_BEWEGUNG].wert));
        const double wr = werte[F_RICHTUNG].wert * M_PI / 180.0;
        const double cw = std::cos(wr), sw = std::sin(wr);
        // Figurkoordinaten (x vorne, y oben, z rechts; Einheit = Koerpergroesse) -> Szene
        auto P = [&](double x, double y, double z) { return p + Vector3D(H * (cw * x + sw * z), H * y, H * (-sw * x + cw * z)); };
        auto R = [&](double x, double y, double z) { return Vector3D(cw * x + sw * z, y, -sw * x + cw * z); };
        const Material haut = makeMaterialSimple(werte[F_HAUT].farbe), oben = makeMaterialSimple(werte[F_OBEN].farbe),
                       hose = makeMaterialSimple(werte[F_HOSE].farbe), schuhe = makeMaterialSimple(werte[F_SCHUHE].farbe);
        auto stab = [&](const char *name, Vector3D a, Vector3D b, double r, const Material &m)
        {
            auto o = std::make_unique<Stab>(a, b, r * H, m);
            o->name = name;
            return o;
        };
        auto kugel = [&](const char *name, Vector3D c, double r, const Material &m)
        {
            auto o = std::make_unique<Kugel>(c, r * H, m);
            o->name = name;
            return o;
        };
        auto gruppe = [](const char *name)
        {
            auto g = std::make_unique<Gruppe>();
            g->name = name;
            return g;
        };

        struct Glieder
        {
            Gruppe *arm, *unterarm, *bein, *unterschenkel;
            object *fuss;
            Vector3D schulter, ellbogen, huefte, knie, knoechel;
        } seite[2]; // 0 links (z < 0), 1 rechts

        auto figur = gruppe("Figur");
        figur->kinder.push_back(stab("Rumpf", P(0, 0.52, 0), P(0, 0.79, 0), 0.10, oben));
        figur->kinder.push_back(stab("Schultern", P(0, 0.80, -0.14), P(0, 0.80, 0.14), 0.05, oben));
        figur->kinder.push_back(stab("Becken", P(0, 0.52, -0.09), P(0, 0.52, 0.09), 0.068, hose));
        figur->kinder.push_back(stab("Hals", P(0, 0.80, 0), P(0, 0.88, 0), 0.03, haut));
        figur->kinder.push_back(kugel("Kopf", P(0.005, 0.93, 0), 0.066, haut));
        for (int i = 0; i < 2; ++i)
        {
            const double z = i == 0 ? -1.0 : 1.0;
            const char *sn = i == 0 ? " links" : " rechts";
            auto N = [&](const char *t) { return std::string(t) + sn; };
            Glieder &gl = seite[i];
            gl.schulter = P(0, 0.80, 0.175 * z);
            gl.ellbogen = P(0, 0.62, 0.185 * z);
            gl.huefte = P(0, 0.52, 0.085 * z);
            gl.knie = P(0, 0.285, 0.085 * z);
            gl.knoechel = P(0, 0.055, 0.085 * z);

            auto arm = gruppe(N("Arm").c_str());
            arm->kinder.push_back(kugel(N("Schulter").c_str(), gl.schulter, 0.045, oben));
            arm->kinder.push_back(stab(N("Oberarm").c_str(), gl.schulter, gl.ellbogen, 0.034, oben));
            auto unterarm = gruppe(N("Unterarm").c_str());
            unterarm->kinder.push_back(kugel(N("Ellbogen").c_str(), gl.ellbogen, 0.032, oben));
            unterarm->kinder.push_back(stab(N("Unterarm").c_str(), gl.ellbogen, P(0, 0.455, 0.185 * z), 0.027, haut));
            unterarm->kinder.push_back(kugel(N("Hand").c_str(), P(0, 0.425, 0.185 * z), 0.037, haut));
            gl.unterarm = unterarm.get();
            arm->kinder.push_back(std::move(unterarm));
            gl.arm = arm.get();

            auto bein = gruppe(N("Bein").c_str());
            bein->kinder.push_back(stab(N("Oberschenkel").c_str(), gl.huefte, gl.knie, 0.052, hose));
            auto unterschenkel = gruppe(N("Unterschenkel").c_str());
            unterschenkel->kinder.push_back(kugel(N("Knie").c_str(), gl.knie, 0.044, hose));
            unterschenkel->kinder.push_back(stab(N("Unterschenkel").c_str(), gl.knie, gl.knoechel, 0.039, hose));
            auto fuss = stab(N("Fuß").c_str(), P(-0.025, 0.035, 0.085 * z), P(0.13, 0.03, 0.085 * z), 0.032, schuhe);
            gl.fuss = fuss.get();
            unterschenkel->kinder.push_back(std::move(fuss));
            gl.unterschenkel = unterschenkel.get();
            bein->kinder.push_back(std::move(unterschenkel));
            gl.bein = bein.get();

            figur->kinder.push_back(std::move(arm));
            figur->kinder.push_back(std::move(bein));
        }
        // Gruppen-Grenzen von innen nach aussen
        for (Glieder &gl : seite)
        {
            gl.unterarm->grenzenAktualisieren();
            gl.arm->grenzenAktualisieren();
            gl.unterschenkel->grenzenAktualisieren();
            gl.bein->grenzenAktualisieren();
        }
        figur->grenzenAktualisieren();
        object *fig = figur.get();
        std::vector<std::unique_ptr<object>> teile;
        teile.push_back(std::move(figur));
        neueObjekteEinfuegen(std::move(teile), "Figur");
        if (bewegung == 0)
            return;

        // ---------- Bewegung: Snapshots und Wiederholungen ----------
        double periode = 1, v = 0;
        tempoDaten(bewegung, werte[F_TEMPO].wert, H, periode, v);
        const Vector3D vorne = R(1, 0, 0), quer = R(0, 0, 1);
        double t0, t1;
        if (snapshotAnzahl() >= 2)
        {
            t0 = snapshotZeit(0);
            t1 = snapshotZeit(snapshotAnzahl() - 1);
            for (int i = 0; i < snapshotAnzahl(); ++i)
                snapshotObjektEintragen(i, fig, vorne * (v * (snapshotZeit(i) - t0)));
        }
        else
        {
            t0 = snapshotAnzahl() == 1 ? snapshotZeit(0) : 0.0;
            t1 = t0 + werte[F_DAUER].wert;
            if (snapshotAnzahl() == 1)
                snapshotObjektEintragen(0, fig, Vector3D(0, 0, 0));
            else
                snapshotAufnehmen(t0);
            const Vector3D weg = vorne * (v * (t1 - t0));
            fig->translate(weg);
            snapshotAufnehmen(t1);
            fig->translate(-weg);
        }
        // Pendeln um ein Gelenk; Achse quer (Figur-Z) oder vorne (Figur-X)
        auto pendeln = [&](int abschnitt, object *o, const Vector3D &gelenk, const Vector3D &versatz, double mittel,
                           double ausschlag, double phase, double per, const Vector3D &achse)
        {
            Wiederholung w;
            w.o = o;
            w.art = WIEDERHOLUNG_PENDELN;
            w.achse = 3;
            w.achsVektor = achse;
            w.periode = per;
            w.mittel = mittel;
            w.weite = ausschlag;
            w.phase = phase;
            w.mitGelenk = true;
            w.gelenk = gelenk + versatz;
            wiederholungSetzen(abschnitt, w);
        };
        for (int i = 0; i + 1 < snapshotAnzahl(); ++i)
        {
            const Vector3D vs = vorne * (v * (snapshotZeit(i) - t0)); // Lage der Figur in diesem Snapshot
            if (bewegung == 1 || bewegung == 2)
            {
                const bool rennen = bewegung == 1;
                const double bein = rennen ? 35 : 22, knieM = rennen ? -50 : -12, knieA = rennen ? 40 : 12;
                const double fuss = rennen ? 12 : 8, arm = rennen ? 30 : 16, ellM = rennen ? 80 : 15, ellA = rennen ? 15 : 6;
                for (int s2 = 0; s2 < 2; ++s2)
                {
                    const Glieder &gl = seite[s2];
                    const double ph = s2 == 0 ? 0.0 : 180.0; // rechts gegenlaeufig
                    pendeln(i, gl.bein, gl.huefte, vs, 0, bein, ph, periode, quer);
                    pendeln(i, gl.unterschenkel, gl.knie, vs, knieM, knieA, ph - 90, periode, quer);
                    pendeln(i, gl.fuss, gl.knoechel, vs, 0, fuss, ph, periode, quer);
                    pendeln(i, gl.arm, gl.schulter, vs, 0, arm, ph + 180, periode, quer); // gegen das Bein derselben Seite
                    pendeln(i, gl.unterarm, gl.ellbogen, vs, ellM, ellA, ph + 180, periode, quer);
                }
                Wiederholung w; // Koerper wippt zweimal je Doppelschritt
                w.o = fig;
                w.art = WIEDERHOLUNG_WIPPEN;
                w.achse = 1;
                w.periode = periode / 2;
                w.weite = (rennen ? 0.025 : 0.012) * H;
                w.phase = 90;
                wiederholungSetzen(i, w);
            }
            else if (bewegung == 3) // Winken: rechter Arm seitlich hoch, Unterarm schwingt
            {
                const Glieder &gl = seite[1];
                pendeln(i, gl.arm, gl.schulter, vs, -145, 0, 0, 1.0, vorne);
                pendeln(i, gl.unterarm, gl.ellbogen, vs, -10, 28, 0, 0.6 / werte[F_TEMPO].wert, vorne);
            }
        }
        animationAktivSetzen(true);
        zeitleisteUmschalten(true);
        zeitWaehlen(t0);
        return;
    }

    case NEU_TIER:
    {
        // Tier aus Ellipsoiden, Roehre (Schwanz), Staeben und Kugeln als Gelenkkette (verschachtelte Gruppen),
        // schaut nach +X. Bewegungen: Pendeln der Beine, des Kopfs und des Schwanzes um ihre Gelenke + Strecke.
        enum
        {
            T_ART, T_LAENGE, T_BEWEGUNG, T_RICHTUNG, T_TEMPO, T_DAUER, T_FELL, T_AUGEN, T_NASE, T_ANZAHL
        };
        static std::vector<EingabeWert> werte;
        if (werte.size() != T_ANZAHL)
        {
            const double l = (s >= 0.3 && s <= 6.0) ? 0.5 : s * 0.5; // Szene in Metern: Katze 50 cm
            werte = {
                {L"Tier", 0, true, 0, 2, L"Katze: schlank, spitze Ohren, langer gebogener Schwanz, Schnurrhaare.\n"
                                         L"Hund: kräftiger, längere Schnauze, Schlappohren, kürzerer Schwanz.\n"
                                         L"Maus: rund, großer Kopf, große runde Ohren, Knopfaugen mit Glanzpunkt, langer rosa Schwanz."},
                {L"Länge (Rumpf + Kopf)", l, false, 1e-6, 1e9, L"Länge ohne Schwanz in Einheiten der Szene (Katze etwa 0,5 m, Hund 0,6 bis 1 m, Maus 0,1 m)"},
                {L"Bewegung", 0, true, 0, 3,
                 L"Gehen: Beine im Kreuzgang, Kopf nickt leicht, Schwanz schwingt mit, das Tier läuft geradeaus.\n"
                 L"Schwanzwedeln: steht und wedelt (Maus: Schwanz schwingt, Ohren zucken).\n"
                 L"Dafür werden Snapshots angelegt bzw. ergänzt (Zeitleiste ↻)."},
                {L"Laufrichtung (Grad)", 0, false, -360, 360, L"Drehung um die senkrechte Achse: 0 = nach +X, 90 = nach −Z"},
                {L"Tempo (Faktor)", 1, false, 0.1, 5, L"1 = gemütlich; 2 = doppelt so schnelle Schritte und Geschwindigkeit"},
                {L"Dauer (s)", 4, false, 0.1, 1e5, L"Länge der Bewegung, wenn es noch keine zwei Snapshots gibt"},
                farbFeld(L"Fellfarbe", Farbe(0.85, 0.55, 0.25), L"Rumpf, Kopf, Beine, Schwanz (\"Rot Grün Blau\", je 0 bis 1)"),
                farbFeld(L"Augenfarbe", Farbe(0.55, 0.8, 0.2), L"Iris (die Pupille ist schwarz)"),
                farbFeld(L"Nasenfarbe", Farbe(0.9, 0.55, 0.6), L"Nase")};
            werte[T_ART].auswahl = L"Katze|Hund|Maus";
            werte[T_BEWEGUNG].auswahl = L"keine (nur Tier)|Gehen|Schwanzwedeln (steht)|Gehen und Schwanzwedeln";
            werte[T_FELL].abschnitt = L"FARBEN";
        }
        // Vorlagen: Tierart und Farben
        {
            struct V
            {
                const wchar_t *name;
                int art;
                Farbe fell, augen, nase;
            };
            static const V vorlagen[] = {
                {L"Katze rot getigert", 0, Farbe(0.85, 0.55, 0.25), Farbe(0.55, 0.8, 0.2), Farbe(0.9, 0.55, 0.6)},
                {L"Katze grau", 0, Farbe(0.5, 0.5, 0.52), Farbe(0.85, 0.75, 0.2), Farbe(0.75, 0.5, 0.55)},
                {L"Katze schwarz", 0, Farbe(0.06, 0.06, 0.07), Farbe(0.9, 0.75, 0.1), Farbe(0.15, 0.12, 0.12)},
                {L"Katze weiß", 0, Farbe(0.92, 0.92, 0.9), Farbe(0.3, 0.55, 0.9), Farbe(0.95, 0.6, 0.65)},
                {L"Hund braun", 1, Farbe(0.55, 0.36, 0.2), Farbe(0.35, 0.22, 0.1), Farbe(0.06, 0.05, 0.05)},
                {L"Hund hellbeige", 1, Farbe(0.88, 0.75, 0.55), Farbe(0.3, 0.2, 0.1), Farbe(0.1, 0.08, 0.07)},
                {L"Hund schwarz", 1, Farbe(0.08, 0.08, 0.08), Farbe(0.35, 0.22, 0.1), Farbe(0.05, 0.05, 0.05)},
                {L"Maus graubraun", 2, Farbe(0.62, 0.55, 0.5), Farbe(0.04, 0.03, 0.03), Farbe(1.0, 0.62, 0.7)},
                {L"Maus grau", 2, Farbe(0.58, 0.58, 0.6), Farbe(0.04, 0.03, 0.03), Farbe(1.0, 0.62, 0.7)},
                {L"Maus weiß", 2, Farbe(0.95, 0.94, 0.92), Farbe(0.75, 0.1, 0.2), Farbe(1.0, 0.65, 0.72)},
                {L"Maus hellbraun", 2, Farbe(0.78, 0.6, 0.42), Farbe(0.04, 0.03, 0.03), Farbe(0.98, 0.6, 0.65)}};
            dialogVorlagen.clear();
            for (const V &v : vorlagen)
            {
                DialogVorlage d;
                d.name = v.name;
                d.texte.assign(T_ANZAHL, std::wstring());
                d.texte[T_ART] = std::to_wstring(v.art);
                {
                    const double s0 = passendeGroesse(), l0 = (s0 >= 0.3 && s0 <= 6.0) ? 0.5 : s0 * 0.5; // wie oben
                    d.texte[T_LAENGE] = zahlText(v.art == 0 ? l0 : v.art == 1 ? l0 * 1.4 : l0 * 0.2, 3);
                }
                d.texte[T_FELL] = farbFeldText(v.fell);
                d.texte[T_AUGEN] = farbFeldText(v.augen);
                d.texte[T_NASE] = farbFeldText(v.nase);
                dialogVorlagen.push_back(std::move(d));
            }
        }
        auto tierHinweis = [&](const std::vector<double> &v)
        {
            const int bw = int(std::lround(v[T_BEWEGUNG]));
            std::wstring t = L"Gruppe „Katze“, „Hund“ bzw. „Maus“: Rumpf, Kopf (mit Ohren, Augen, Nase), vier Beine als Gelenkketten "
                             L"und der Schwanz – alles einzeln wählbar und färbbar.";
            if (bw == 1 || bw == 3)
                t += L"\nGehen: " + zahlText(v[T_LAENGE] * v[T_TEMPO], 2) + L" Einheiten/s, Schrittfolge " + zahlText(1.0 / v[T_TEMPO], 2) + L" s.";
            if (bw != 0)
                t += snapshotAnzahl() >= 2 ? std::wstring(L"\nDie Bewegung läuft vom ersten bis zum letzten vorhandenen Snapshot.")
                                           : L"\nEs werden Snapshots bei 0 s und " + zahlText(v[T_DAUER], 2) + L" s angelegt.";
            return t;
        };
        if (!werteAbfragen(L"Neues Tier", werte, tierHinweis))
            return;

        const int tierArt = int(std::lround(werte[T_ART].wert));
        const bool maus = tierArt == 2;
        const bool katze = tierArt == 0;
        const double L = werte[T_LAENGE].wert;
        const int bewegung = int(std::lround(werte[T_BEWEGUNG].wert));
        const double wr = werte[T_RICHTUNG].wert * M_PI / 180.0, cw = std::cos(wr), sw = std::sin(wr);
        // Tierkoordinaten (x vorne, y oben, z rechts; Einheit = Laenge) -> Szene
        auto R = [&](const Vector3D &v) { return Vector3D(cw * v.x + sw * v.z, v.y, -sw * v.x + cw * v.z); };
        auto P = [&](double x, double y, double z) { return p + R(Vector3D(x, y, z)) * L; };
        const Material fell = makeMaterialSimple(werte[T_FELL].farbe), augeM = makeMaterialSimple(werte[T_AUGEN].farbe),
                       nase = makeMaterialSimple(werte[T_NASE].farbe), schwarz = makeMaterialSimple(Farbe(0.02, 0.02, 0.02)),
                       weiss = makeMaterialSimple(Farbe(0.95, 0.95, 0.95));
        Material glanz = augeM;
        glanz.specular = Farbe(1, 1, 1);
        glanz.shininess = 120;
        // Netz in Tierkoordinaten -> Szene (Lage, Drehung, Groesse)
        auto netz = [&](std::vector<Dreieck> d, const char *name)
        {
            for (Dreieck &t : d)
            {
                t.a = P(t.a.x, t.a.y, t.a.z);
                t.b = P(t.b.x, t.b.y, t.b.z);
                t.c = P(t.c.x, t.c.y, t.c.z);
                if (t.hasVertexNormals)
                {
                    t.nA = R(t.nA);
                    t.nB = R(t.nB);
                    t.nC = R(t.nC);
                }
                t.rebuild();
            }
            auto m = std::make_unique<Mesh>(d, Vector3D(0, 0, 0));
            m->name = name;
            m->mat = d.empty() ? fell : d.front().mat;
            return m;
        };
        auto ell = [&](const char *name, double x, double y, double z, double rx, double ry, double rz, const Material &m, int seg = 28)
        { return netz(koerper::ellipsoid(Vector3D(x, y, z), rx, ry, rz, seg, true, m), name); };
        auto stab = [&](const char *name, Vector3D a, Vector3D b, double r, const Material &m)
        {
            auto o = std::make_unique<Stab>(a, b, r * L, m);
            o->name = name;
            return o;
        };
        auto kugel = [&](const char *name, Vector3D c, double r, const Material &m)
        {
            auto o = std::make_unique<Kugel>(c, r * L, m);
            o->name = name;
            return o;
        };
        auto gruppe = [](const char *name)
        {
            auto g = std::make_unique<Gruppe>();
            g->name = name;
            return g;
        };

        // Masse (Einheit = Laenge): Katze schlank und tief, Hund kraeftig und hoeher
        // Maus: rund und tief, grosser Kopf (Kindchenschema), ohne sichtbaren Hals
        const double rumpfY = maus ? 0.27 : katze ? 0.40 : 0.50, rumpfX = maus ? 0.33 : katze ? 0.30 : 0.32,
                     rumpfH = maus ? 0.21 : katze ? 0.12 : 0.15, rumpfB = maus ? 0.22 : katze ? 0.11 : 0.13;
        const double schulterY = rumpfY - rumpfH * 0.55, knieY = maus ? 0.08 : katze ? 0.17 : 0.22;
        const Vector3D kopfM = maus ? Vector3D(0.42, 0.40, 0) : katze ? Vector3D(0.36, 0.58, 0) : Vector3D(0.40, 0.70, 0);
        const Vector3D halsA(0.22, rumpfY + 0.03, 0), halsB(kopfM.x - 0.06, kopfM.y - 0.05, 0);
        const Vector3D halsGelenk = maus ? Vector3D(0.28, 0.36, 0) : halsA * 0.5 + halsB * 0.5;
        const char *tierName = maus ? "Maus" : katze ? "Katze" : "Hund";
        std::vector<object *> mausOhren; // zucken beim "Schwanzwedeln"
        std::vector<Vector3D> mausOhrGelenke;

        auto tier = gruppe(tierName);
        tier->kinder.push_back(ell("Rumpf", 0, rumpfY, 0, rumpfX, rumpfH, rumpfB, fell, 36));
        if (maus) // hellerer Bauch
        {
            Farbe hell = werte[T_FELL].farbe * 0.35 + Farbe(0.95, 0.9, 0.82) * 0.65;
            tier->kinder.push_back(ell("Bauch", 0.10, rumpfY - 0.06, 0, rumpfX * 0.8, rumpfH * 0.82, rumpfB * 0.88, makeMaterialSimple(hell), 32));
        }
        else
            tier->kinder.push_back(stab("Hals", P(halsA.x, halsA.y, 0), P(halsB.x, halsB.y, 0), katze ? 0.065 : 0.08, fell));

        // Kopf
        auto kopf = gruppe("Kopf");
        Gruppe *kopfZ = kopf.get();
        if (maus)
        {
            Material nasenGlanz = nase;
            nasenGlanz.specular = Farbe(1, 1, 1);
            nasenGlanz.shininess = 80;
            Material augenGlanz = augeM;
            augenGlanz.specular = Farbe(1, 1, 1);
            augenGlanz.shininess = 150;
            Material glanzPunkt = makeMaterialSimple(Farbe(1, 1, 1));
            glanzPunkt.emission = Farbe(0.6, 0.6, 0.6); // leuchtet etwas: der Blick wirkt lebendig
            const Material backe = makeMaterialSimple(werte[T_NASE].farbe * 0.6 + werte[T_FELL].farbe * 0.4);
            kopf->kinder.push_back(ell("Kopf", kopfM.x, kopfM.y, 0, 0.20, 0.18, 0.18, fell, 32));
            kopf->kinder.push_back(ell("Schnauze", 0.58, 0.36, 0, 0.12, 0.085, 0.095, fell, 24));
            kopf->kinder.push_back(kugel("Nase", P(0.70, 0.37, 0), 0.033, nasenGlanz));
            // Laecheln: duenne, dunkle Roehre unter der Schnauze
            kopf->kinder.push_back(netz(koerper::roehre({{Vector3D(0.62, 0.293, -0.045), 0.006}, {Vector3D(0.648, 0.283, 0), 0.006},
                                                         {Vector3D(0.62, 0.293, 0.045), 0.006}},
                                                        8, 6, true, true, schwarz),
                                        "Mund"));
            for (int seite = 0; seite < 2; ++seite)
            {
                const double z = seite == 0 ? -1.0 : 1.0;
                const char *sn = seite == 0 ? " links" : " rechts";
                auto N = [&](const char *t) { return std::string(t) + sn; };
                // grosse Knopfaugen mit Glanzpunkt
                kopf->kinder.push_back(kugel(N("Auge").c_str(), P(0.57, 0.46, 0.09 * z), 0.042, augenGlanz));
                kopf->kinder.push_back(kugel(N("Glanzpunkt").c_str(), P(0.605, 0.485, 0.083 * z), 0.012, glanzPunkt));
                kopf->kinder.push_back(ell(N("Bäckchen").c_str(), 0.55, 0.34, 0.115 * z, 0.035, 0.028, 0.02, backe, 16));
                // grosse runde Ohren, leicht nach aussen geneigt (um X um die Ohrmitte)
                const Vector3D ohrM(0.34, 0.60, 0.15 * z);
                const double neig = 0.3 * z, cn = std::cos(neig), sn2 = std::sin(neig);
                auto ohrNetz = [&](double dx, double rx, double r, const Material &m)
                {
                    std::vector<Dreieck> d = koerper::ellipsoid(Vector3D(0, 0, 0), rx, r, r, 24, true, m);
                    for (Dreieck &t : d)
                    {
                        for (Vector3D *v : {&t.a, &t.b, &t.c})
                            *v = Vector3D(v->x + dx, cn * v->y - sn2 * v->z, sn2 * v->y + cn * v->z) + ohrM;
                        for (Vector3D *v : {&t.nA, &t.nB, &t.nC})
                            *v = Vector3D(v->x, cn * v->y - sn2 * v->z, sn2 * v->y + cn * v->z);
                        t.rebuild();
                    }
                    return d;
                };
                auto ohr = gruppe(N("Ohr").c_str());
                ohr->kinder.push_back(netz(ohrNetz(0.0, 0.025, 0.13, fell), "Ohrmuschel"));
                ohr->kinder.push_back(netz(ohrNetz(0.02, 0.012, 0.095, nase), "Innenohr"));
                ohr->grenzenAktualisieren();
                mausOhren.push_back(ohr.get());
                mausOhrGelenke.push_back(Vector3D(0.34, 0.50, 0.12 * z));
                kopf->kinder.push_back(std::move(ohr));
                // Schnurrhaare
                for (int k = 0; k < 3; ++k)
                    kopf->kinder.push_back(stab(N("Schnurrhaar").c_str(), P(0.66, 0.35, 0.03 * z),
                                                P(0.71, 0.385 - 0.035 * k, 0.26 * z), 0.0022, makeMaterialSimple(Farbe(0.2, 0.18, 0.18))));
            }
        }
        else
        {
        kopf->kinder.push_back(ell("Kopf", kopfM.x, kopfM.y, 0, katze ? 0.10 : 0.11, katze ? 0.09 : 0.10, katze ? 0.10 : 0.10, fell));
        const double schnX = katze ? 0.09 : 0.13, schnL = katze ? 0.045 : 0.10;
        kopf->kinder.push_back(ell("Schnauze", kopfM.x + schnX, kopfM.y - 0.035, 0, schnL, katze ? 0.035 : 0.05, katze ? 0.05 : 0.055, fell));
        const double naseX = kopfM.x + schnX + schnL - 0.005;
        kopf->kinder.push_back(kugel("Nase", P(naseX, kopfM.y - (katze ? 0.02 : 0.005), 0), katze ? 0.012 : 0.02, nase));
        for (int seite = 0; seite < 2; ++seite)
        {
            const double z = seite == 0 ? -1.0 : 1.0;
            const char *sn = seite == 0 ? " links" : " rechts";
            auto N = [&](const char *t) { return std::string(t) + sn; };
            const Vector3D auge(kopfM.x + (katze ? 0.075 : 0.085), kopfM.y + 0.025, 0.045 * z);
            kopf->kinder.push_back(kugel(N("Auge").c_str(), P(auge.x, auge.y, auge.z), katze ? 0.022 : 0.018, glanz));
            kopf->kinder.push_back(kugel(N("Pupille").c_str(), P(auge.x + 0.015, auge.y, auge.z * 1.06), katze ? 0.011 : 0.011, schwarz));
            if (katze)
            {
                // spitzes Ohr: dreieckige Pyramide, nach aussen geneigt
                std::vector<Dreieck> ohr = rotation::koerper({{0.045, 0.0}, {0.0, 0.10}}, 3, false, fell);
                const double neig = 0.35 * z, cn = std::cos(neig), sn2 = std::sin(neig);
                for (Dreieck &t : ohr)
                {
                    for (Vector3D *v : {&t.a, &t.b, &t.c})
                        *v = Vector3D(v->x, cn * v->y - sn2 * v->z, sn2 * v->y + cn * v->z) + Vector3D(kopfM.x - 0.01, kopfM.y + 0.06, 0.055 * z);
                    t.rebuild();
                }
                kopf->kinder.push_back(netz(ohr, N("Ohr").c_str()));
                // Schnurrhaare
                for (int k = 0; k < 3; ++k)
                    kopf->kinder.push_back(stab(N("Schnurrhaar").c_str(), P(naseX - 0.02, kopfM.y - 0.04, 0.03 * z),
                                                P(naseX + 0.03, kopfM.y - 0.02 - 0.02 * k, 0.13 * z), 0.0018, weiss));
            }
            else // Schlappohr
                kopf->kinder.push_back(ell(N("Ohr").c_str(), kopfM.x - 0.03, kopfM.y - 0.01, 0.10 * z, 0.035, 0.075, 0.015, fell, 20));
        }

        } // Katze/Hund

        // Beine: vorne (x > 0) und hinten, je Oberbein, Knie, Unterbein, Pfote
        struct Bein
        {
            Gruppe *bein, *unter;
            Vector3D huefte, knie;
        } beine[4]; // 0 vorne links, 1 vorne rechts, 2 hinten links, 3 hinten rechts
        std::vector<std::unique_ptr<object>> beinGruppen;
        for (int i = 0; i < 4; ++i)
        {
            const bool vorne = i < 2;
            const double z = (i % 2 == 0 ? -1.0 : 1.0) * (rumpfB * 0.6);
            const double x = vorne ? rumpfX * 0.62 : -rumpfX * 0.62;
            static const char *namen[4] = {"Bein vorne links", "Bein vorne rechts", "Bein hinten links", "Bein hinten rechts"};
            Bein &b = beine[i];
            b.huefte = Vector3D(x, schulterY + 0.04, z);
            b.knie = Vector3D(x + (vorne ? 0.0 : -0.02), knieY, z);
            const Vector3D pfote(x + 0.015, 0.025, z);
            const double r1 = maus ? 0.04 : katze ? 0.035 : 0.045, r2 = maus ? 0.028 : katze ? 0.025 : 0.032;
            auto bein = gruppe(namen[i]);
            bein->kinder.push_back(stab("Oberbein", P(b.huefte.x, b.huefte.y, b.huefte.z), P(b.knie.x, b.knie.y, b.knie.z), r1, fell));
            auto unter = gruppe("Unterbein");
            unter->kinder.push_back(kugel("Knie", P(b.knie.x, b.knie.y, b.knie.z), r2 * 1.1, fell));
            unter->kinder.push_back(stab("Unterbein", P(b.knie.x, b.knie.y, b.knie.z), P(pfote.x - 0.01, pfote.y + 0.01, pfote.z), r2, fell));
            unter->kinder.push_back(ell("Pfote", pfote.x + 0.01 + (maus ? 0.01 : 0.0), pfote.y, pfote.z, r2 * (maus ? 1.9 : 1.5),
                                        r2 * 1.0, r2 * 1.25, maus ? nase : fell, 16)); // Maus: rosa Fuesschen
            b.unter = unter.get();
            bein->kinder.push_back(std::move(unter));
            b.bein = bein.get();
            beinGruppen.push_back(std::move(bein));
        }
        // Schwanz: Roehre vom Hinterteil nach oben gebogen
        std::vector<koerper::PfadPunkt> pfad;
        const Vector3D wurzel(-rumpfX * 0.92, rumpfY + 0.04, 0);
        if (maus) // lang, duenn, rosa, in einem froehlichen Bogen nach oben
            pfad = {{wurzel, 0.032}, {Vector3D(-0.55, 0.12, 0.05), 0.024}, {Vector3D(-0.78, 0.14, 0.16), 0.018},
                    {Vector3D(-0.90, 0.30, 0.13), 0.012}, {Vector3D(-0.88, 0.44, 0.05), 0.008}};
        else if (katze)
            pfad = {{wurzel, 0.035}, {Vector3D(-0.40, 0.50, 0), 0.03}, {Vector3D(-0.48, 0.64, 0), 0.028}, {Vector3D(-0.50, 0.80, 0), 0.025},
                    {Vector3D(-0.46, 0.90, 0), 0.02}};
        else
            pfad = {{wurzel, 0.035}, {Vector3D(-0.40, 0.62, 0), 0.03}, {Vector3D(-0.44, 0.74, 0), 0.022}};
        auto schwanz = netz(koerper::roehre(pfad, 16, 6, true, true, maus ? nase : fell), "Schwanz");
        object *schwanzZ = schwanz.get();

        tier->kinder.push_back(std::move(kopf));
        for (auto &b : beinGruppen)
            tier->kinder.push_back(std::move(b));
        tier->kinder.push_back(std::move(schwanz));
        for (Bein &b : beine)
        {
            b.unter->grenzenAktualisieren();
            b.bein->grenzenAktualisieren();
        }
        kopfZ->grenzenAktualisieren();
        tier->grenzenAktualisieren();
        object *tz = tier.get();
        std::vector<std::unique_ptr<object>> teile;
        teile.push_back(std::move(tier));
        neueObjekteEinfuegen(std::move(teile), tierName);
        if (bewegung == 0)
            return;

        // ---------- Bewegung: Snapshots und Wiederholungen (wie bei der Figur) ----------
        const bool gehen = bewegung == 1 || bewegung == 3, wedeln = bewegung == 2 || bewegung == 3;
        const double tempo = werte[T_TEMPO].wert, periode = (maus ? 0.45 : 1.0) / tempo; // Maus trippelt
        const double v = gehen ? L * tempo * (maus ? 2.0 : 1.0) : 0.0;
        const Vector3D vorne = R(Vector3D(1, 0, 0)), quer = R(Vector3D(0, 0, 1)), hoch(0, 1, 0);
        double t0, t1;
        if (snapshotAnzahl() >= 2)
        {
            t0 = snapshotZeit(0);
            t1 = snapshotZeit(snapshotAnzahl() - 1);
            for (int i = 0; i < snapshotAnzahl(); ++i)
                snapshotObjektEintragen(i, tz, vorne * (v * (snapshotZeit(i) - t0)));
        }
        else
        {
            t0 = snapshotAnzahl() == 1 ? snapshotZeit(0) : 0.0;
            t1 = t0 + werte[T_DAUER].wert;
            if (snapshotAnzahl() == 1)
                snapshotObjektEintragen(0, tz, Vector3D(0, 0, 0));
            else
                snapshotAufnehmen(t0);
            const Vector3D weg = vorne * (v * (t1 - t0));
            tz->translate(weg);
            snapshotAufnehmen(t1);
            tz->translate(-weg);
        }
        auto pendeln = [&](int abschnitt, object *o, const Vector3D &gelenk, const Vector3D &versatz, double mittel, double ausschlag,
                           double phase, double per, const Vector3D &achse)
        {
            Wiederholung w;
            w.o = o;
            w.art = WIEDERHOLUNG_PENDELN;
            w.achse = 3;
            w.achsVektor = achse;
            w.periode = per;
            w.mittel = mittel;
            w.weite = ausschlag;
            w.phase = phase;
            w.mitGelenk = true;
            w.gelenk = gelenk + versatz;
            wiederholungSetzen(abschnitt, w);
        };
        for (int i = 0; i + 1 < snapshotAnzahl(); ++i)
        {
            const Vector3D vs = vorne * (v * (snapshotZeit(i) - t0));
            if (gehen)
            {
                // Kreuzgang: hinten links, vorne links, hinten rechts, vorne rechts je eine Viertelperiode versetzt
                static const double phasen[4] = {90, 270, 0, 180}; // vorne links, vorne rechts, hinten links, hinten rechts
                for (int b = 0; b < 4; ++b)
                {
                    const Bein &bn = beine[b];
                    pendeln(i, bn.bein, P(bn.huefte.x, bn.huefte.y, bn.huefte.z), vs, 0, 22, phasen[b], periode, quer);
                    pendeln(i, bn.unter, P(bn.knie.x, bn.knie.y, bn.knie.z), vs, -8, 12, phasen[b] - 90, periode, quer);
                }
                pendeln(i, kopfZ, P(halsGelenk.x, halsGelenk.y, 0), vs, 0, 3, 0, periode / 2, quer); // Kopf nickt
                Wiederholung w; // Rumpf wippt leicht
                w.o = tz;
                w.art = WIEDERHOLUNG_WIPPEN;
                w.achse = 1;
                w.periode = periode / 2;
                w.weite = 0.008 * L;
                wiederholungSetzen(i, w);
            }
            if (wedeln) // Schwanz seitlich hin und her (um die senkrechte Achse an der Wurzel)
                pendeln(i, schwanzZ, P(wurzel.x, wurzel.y, 0), vs, 0, gehen ? 18 : (maus ? 20 : 30), 0,
                        gehen ? periode : (maus ? 1.2 : 0.8) / tempo, hoch);
            if (wedeln && maus) // Ohren zucken leicht, gegeneinander versetzt
                for (size_t k = 0; k < mausOhren.size(); ++k)
                    pendeln(i, mausOhren[k], P(mausOhrGelenke[k].x, mausOhrGelenke[k].y, mausOhrGelenke[k].z), vs, 0, 6, k ? 120.0 : 0.0,
                            1.7 / tempo, vorne);
            else if (gehen) // beim Gehen schwingt der Schwanz leicht mit
                pendeln(i, schwanzZ, P(wurzel.x, wurzel.y, 0), vs, 0, 8, 90, periode, hoch);
        }
        animationAktivSetzen(true);
        zeitleisteUmschalten(true);
        zeitWaehlen(t0);
        return;
    }

    case NEU_WOLKE:
    {
        Material m(Farbe(1, 1, 1), Farbe(1, 1, 1), Farbe(0, 0, 0), Farbe(1, 1, 1), 0.3, 0.7, 1.0, 0.0, 0.0, 1.0, 0.0,
                   0.0, Farbe(0, 0, 0)); // wie addWolkenAusKugeln
        static std::vector<EingabeWert> werte;
        if (werte.empty())
            werte = {{L"Radius der ersten Kugel", s * 0.5, false, 1e-6, 1e9,
                      L"Größe der mittleren Kugel; jede weitere Kugel ist etwa 0,75-mal so groß"},
                     {L"Rekursionstiefe", 6, true, 1, 9,
                      L"Wie oft an jede Kugel neue Kugeln angehängt werden (je drei).\n"
                      L"Anzahl Kugeln = (3 hoch Tiefe − 1) / 2: Tiefe 6 = 364, Tiefe 9 = 9841"},
                     {L"Höhe über dem Einfügepunkt", s * 2.0, false, -1e9, 1e9,
                      L"Wie hoch über dem Kamera-Zielpunkt die Mitte der Wolke liegt"}};
        auto wolkeHinweis = [](const std::vector<double> &w)
        {
            int tiefe = int(std::round(w[1]));
            double n = (std::pow(3.0, tiefe) - 1.0) / 2.0;
            // Kugeln haengen am Rand der vorigen, Radius jedes Mal etwa 0,75 x: Ausdehnung grob
            double ausdehnung = 2.0 * w[0] * (1.0 - std::pow(0.75, tiefe)) / 0.25;
            std::wstring t = anzahlText(n) + L" Kugeln (als eine Gruppe), Ausdehnung bis etwa " + zahlText(ausdehnung, 2) + L".";
            if (n > 2000)
                t += L"\nViele Objekte: Liste und Ansichten werden langsam.";
            return t;
        };
        if (!werteAbfragen(L"Neue Wolke aus Kugeln", werte, wolkeHinweis))
            return;
        std::vector<Kugel> kugeln = generateWolkeAusKugeln(p + Vector3D(0, werte[2].wert, 0), werte[0].wert, int(werte[1].wert), m);

        std::vector<std::unique_ptr<object>> teile;
        for (size_t i = 0; i < kugeln.size(); ++i)
        {
            auto k = std::make_unique<Kugel>(kugeln[i]);
            k->name = "Kugel " + std::to_string(i + 1); // in der Gruppe "Wolke N"
            teile.push_back(std::move(k));
        }
        neueObjekteEinfuegen(std::move(teile), "Wolke");
        return;
    }

    // ---------- Koordinatensystem (wie addCoordinateSystem, aber Dicke und Schrift passend zur Laenge) ----------
    case NEU_KOORDINATEN:
    {
        // Vorgaben aus der Szene (bei jedem Oeffnen neu, da die Szene sich aendert)
        Vector3D ursprung = scene.empty() ? Vector3D(0, 0, 0) : sceneMin;
        Vector3D ausdehnung = sceneMax - sceneMin;
        double laengeVorgabe = scene.empty() ? 10.0 : std::max({ausdehnung.x, ausdehnung.y, ausdehnung.z, 1e-6}) * 1.2;

        std::vector<EingabeWert> werte = {
            {L"Ursprung X", ursprung.x, false, -1e12, 1e12, L"Schnittpunkt der Achsen (Vorgabe: kleinste Ecke der Szene)"},
            {L"Ursprung Y", ursprung.y, false, -1e12, 1e12, L"Schnittpunkt der Achsen (Vorgabe: kleinste Ecke der Szene)"},
            {L"Ursprung Z", ursprung.z, false, -1e12, 1e12, L"Schnittpunkt der Achsen (Vorgabe: kleinste Ecke der Szene)"},
            {L"Achsenlänge", laengeVorgabe, false, 1e-9, 1e12,
             L"Länge jeder Achse in positiver Richtung (dazu ein kurzes Stück in negativer Richtung)"},
            {L"Abstand der Teilstriche", schoenerSchritt(laengeVorgabe), false, 1e-9, 1e12,
             L"Abstand der Teilstriche und Zahlen; höchstens 50 Teilstriche je Achse"},
            {L"Dicke der Achsen", laengeVorgabe * 0.004, false, 1e-9, 1e12,
             L"Dicke der Achsen-Pfeile; Teilstriche halb so dick"},
            {L"Beschriftung (1 = ja, 0 = nein)", 1, true, 0, 1,
             L"1: Zahlen (Weltkoordinaten) an die Teilstriche schreiben, 0: nur Striche"}};
        auto achsenHinweis = [](const std::vector<double> &w)
        {
            double laenge = w[3], schritt = w[4];
            if (!(laenge > 0) || !(schritt > 0))
                return std::wstring(L"Länge und Abstand müssen größer als 0 sein.");
            if (laenge / schritt > 50)
                schritt = laenge / 50;
            int striche = int(std::ceil(laenge / schritt * 0.999)) - 1;
            int objekte = 3 * (1 + striche * (w[6] > 0.5 ? 2 : 1));
            return std::to_wstring(striche) + L" Teilstriche je Achse, " + std::to_wstring(objekte) +
                   L" Objekte (als eine Gruppe); Zahlen " + zahlText(schritt * 0.25, 3) + L" hoch.\nX rot, Y grün, Z blau; Zahlen = Weltkoordinaten.";
        };
        if (!werteAbfragen(L"Neues Koordinatensystem", werte, achsenHinweis))
            return;

        const Vector3D o(werte[0].wert, werte[1].wert, werte[2].wert);
        const double laenge = werte[3].wert, dicke = werte[5].wert;
        double schritt = werte[4].wert;
        if (laenge / schritt > 50) // nicht mehr als 50 Teilstriche je Achse
            schritt = laenge / 50;
        const bool beschriften = werte[6].wert > 0.5;
        const double strich = schritt * 0.15;  // Laenge eines Teilstrichs
        const double schrift = schritt * 0.25; // Hoehe der Zahlen

        const Material farben[3] = {makeMaterialSimple(Farbe(1, 0, 0)), makeMaterialSimple(Farbe(0, 1, 0)),
                                    makeMaterialSimple(Farbe(0, 0, 1))};
        const char *achsen[3] = {"Achse X", "Achse Y", "Achse Z"};
        const double ursprungWert[3] = {o.x, o.y, o.z};

        std::vector<std::unique_ptr<object>> teile;
        for (int k = 0; k < 3; ++k)
        {
            Vector3D a = drehAchse(k);
            Vector3D quer = k == 1 ? Vector3D(1, 0, 0) : Vector3D(0, 1, 0); // Teilstriche nach oben, bei Y nach rechts

            auto pfeil = std::make_unique<Pfeil>(o - a * (laenge * 0.1), o + a * laenge, dicke, farben[k]);
            pfeil->name = achsen[k];
            teile.push_back(std::move(pfeil));

            for (double t = schritt; t < laenge * 0.999; t += schritt)
            {
                Vector3D punkt = o + a * t;
                auto st = std::make_unique<Stab>(punkt, punkt + quer * strich, dicke * 0.5, farben[k]);
                st->name = "Teilstrich";
                teile.push_back(std::move(st));

                if (beschriften)
                    teile.push_back(std::make_unique<Text3D>(zahlFuerText(ursprungWert[k] + t),
                                                             punkt + quer * (strich * 1.5), schrift, schrift * 0.2, farben[k]));
            }
        }
        neueObjekteEinfuegen(std::move(teile), "Koordinatensystem");
        return;
    }

    // ---------- weitere Koerper (formen.h): als Mesh, Werte im Dialog, zuletzt benutzte bleiben ----------
    case NEU_RING3D:
    case NEU_RING2D:
    case NEU_PYRAMIDE:
    case NEU_ZYLINDER_NETZ:
    case NEU_TETRAEDER:
    case NEU_OKTAEDER:
    case NEU_KREISFLAECHE:
    case NEU_KEGEL:
    case NEU_TORUS:
    case NEU_KAPSEL:
    case NEU_ROHR:
    {
        static std::map<int, std::vector<EingabeWert>> alleWerte;
        std::vector<EingabeWert> &werte = alleWerte[art];
        const wchar_t *seg = L"Ecken des Kreises: mehr = runder, aber mehr Dreiecke";
        const wchar_t *titel = L"";
        switch (art)
        {
        case NEU_RING3D:
            titel = L"Neuer 3D-Ring";
            if (werte.empty())
                werte = {{L"Außenradius", s * 0.5, false, 1e-9, 1e12, L"Radius des äußeren Rands"},
                         {L"Innenradius", s * 0.35, false, 0, 1e12, L"Radius des Lochs (kleiner als der Außenradius)"},
                         {L"Höhe", s * 0.15, false, 1e-9, 1e12, L"Dicke des Rings (senkrecht)"},
                         {L"Segmente", 64, true, 3, 1024, seg}};
            name = "Ring";
            break;
        case NEU_RING2D:
            titel = L"Neuer 2D-Ring";
            if (werte.empty())
                werte = {{L"Außenradius", s * 0.5, false, 1e-9, 1e12, L"Radius des äußeren Rands"},
                         {L"Innenradius", s * 0.35, false, 0, 1e12, L"Radius des Lochs (0 = volle Kreisfläche)"},
                         {L"Segmente", 64, true, 3, 1024, seg}};
            name = "Ring 2D";
            break;
        case NEU_PYRAMIDE:
            titel = L"Neue Pyramide";
            if (werte.empty())
                werte = {{L"Grundkante", s, false, 1e-9, 1e12, L"Seitenlänge der quadratischen Grundfläche"},
                         {L"Höhe", s, false, 1e-9, 1e12, L"Abstand der Spitze von der Grundfläche"}};
            name = "Pyramide";
            break;
        case NEU_ZYLINDER_NETZ:
            titel = L"Neuer Zylinder";
            if (werte.empty())
                werte = {{L"Radius", s * 0.3, false, 1e-9, 1e12, L"Radius des Zylinders"},
                         {L"Höhe", s, false, 1e-9, 1e12, L"Länge entlang der senkrechten Achse"},
                         {L"Segmente", 48, true, 3, 1024, seg},
                         {L"Deckel (1 = ja, 0 = nein)", 1, true, 0, 1, L"1: oben und unten geschlossen, 0: offenes Rohr ohne Wand"}};
            name = "Zylinder";
            break;
        case NEU_TETRAEDER:
            titel = L"Neuer Tetraeder";
            if (werte.empty())
                werte = {{L"Kantenlänge", s, false, 1e-9, 1e12, L"Länge jeder der sechs gleich langen Kanten"}};
            name = "Tetraeder";
            break;
        case NEU_OKTAEDER:
            titel = L"Neuer Oktaeder";
            if (werte.empty())
                werte = {{L"Radius", s * 0.6, false, 1e-9, 1e12,
                          L"Abstand der sechs Ecken vom Mittelpunkt (Kantenlänge = Radius × √2)"}};
            name = "Oktaeder";
            break;
        case NEU_KREISFLAECHE:
            titel = L"Neue Kreisfläche";
            if (werte.empty())
                werte = {{L"Radius", s * 0.5, false, 1e-9, 1e12, L"Radius der waagrechten Scheibe"},
                         {L"Segmente", 64, true, 3, 1024, seg}};
            name = "Kreisfläche";
            break;
        case NEU_KEGEL:
            titel = L"Neuer Kegel";
            if (werte.empty())
                werte = {{L"Radius", s * 0.4, false, 1e-9, 1e12, L"Radius der Grundfläche"},
                         {L"Höhe", s, false, 1e-9, 1e12, L"Abstand der Spitze von der Grundfläche"},
                         {L"Segmente", 48, true, 3, 1024, seg},
                         {L"Boden (1 = ja, 0 = nein)", 1, true, 0, 1, L"1: Grundfläche geschlossen, 0: offen"}};
            name = "Kegel";
            break;
        case NEU_TORUS:
            titel = L"Neuer Torus";
            if (werte.empty())
                werte = {{L"Ringradius", s * 0.5, false, 1e-9, 1e12, L"Radius des Mittelkreises (Mitte des Rohrs)"},
                         {L"Rohrradius", s * 0.15, false, 1e-9, 1e12, L"Dicke des Rings (Radius des Querschnitts)"},
                         {L"Segmente Ring", 64, true, 3, 1024, L"Unterteilung entlang des Rings"},
                         {L"Segmente Rohr", 24, true, 3, 1024, L"Unterteilung des runden Querschnitts"}};
            name = "Torus";
            break;
        case NEU_KAPSEL:
            titel = L"Neue Kapsel";
            if (werte.empty())
                werte = {{L"Radius", s * 0.25, false, 1e-9, 1e12, L"Radius des Zylinders und der beiden Halbkugeln"},
                         {L"Länge Mittelteil", s * 0.5, false, 0, 1e12,
                          L"Länge des geraden Teils; Gesamtlänge = Mittelteil + 2 × Radius"},
                         {L"Segmente", 32, true, 4, 1024, seg}};
            name = "Kapsel";
            break;
        case NEU_ROHR:
            titel = L"Neues Rohr";
            if (werte.empty())
                werte = {{L"Außenradius", s * 0.2, false, 1e-9, 1e12, L"Radius der Außenwand"},
                         {L"Innenradius", s * 0.15, false, 0, 1e12, L"Radius der Innenwand (kleiner als der Außenradius)"},
                         {L"Länge", s * 2.0, false, 1e-9, 1e12, L"Länge des Rohrs; es liegt waagrecht entlang X"},
                         {L"Segmente", 48, true, 3, 1024, seg}};
            name = "Rohr";
            break;
        }

        // Anzahl Dreiecke wie in formen.h
        auto dreiecke = [art](const std::vector<double> &w) -> double
        {
            auto n = [&](size_t i) { return std::round(w[i]); };
            switch (art)
            {
            case NEU_RING3D: return 8 * n(3);
            case NEU_RING2D: return 2 * n(2);
            case NEU_PYRAMIDE: return 6;
            case NEU_ZYLINDER_NETZ: return n(2) * (2 + (w[3] > 0.5 ? 2 : 0));
            case NEU_TETRAEDER: return 4;
            case NEU_OKTAEDER: return 8;
            case NEU_KREISFLAECHE: return n(1);
            case NEU_KEGEL: return n(2) * (1 + (w[3] > 0.5 ? 1 : 0));
            case NEU_TORUS: return 2 * n(2) * n(3);
            case NEU_KAPSEL: return 2 * n(2) + 4 * n(2) * std::max(2.0, std::floor(n(2) / 4));
            case NEU_ROHR: return 8 * n(3);
            }
            return 0;
        };
        auto formHinweis = [&](const std::vector<double> &w)
        {
            std::wstring t = anzahlText(dreiecke(w)) + L" Dreiecke. Mittelpunkt beim Einfügepunkt (Kamera-Zielpunkt).";
            if ((art == NEU_RING3D || art == NEU_RING2D || art == NEU_ROHR) && w[1] >= w[0])
                t += L"\nDer Innenradius muss kleiner als der Außenradius sein.";
            if (art == NEU_TORUS && w[1] > w[0])
                t += L"\nRohrradius größer als Ringradius: der Torus überschneidet sich in der Mitte.";
            return t;
        };
        for (;;)
        {
            if (!werteAbfragen(titel, werte, formHinweis))
                return;
            bool loch = art == NEU_RING3D || art == NEU_RING2D || art == NEU_ROHR;
            if (!loch || werte[1].wert < werte[0].wert)
                break;
            MessageBoxW(hObjektFenster, L"Der Innenradius muss kleiner als der Außenradius sein.", titel,
                        MB_OK | MB_ICONWARNING);
        }

        auto w = [&](size_t i) { return werte[i].wert; };
        auto ganz = [&](size_t i) { return int(std::round(werte[i].wert)); };
        std::vector<Dreieck> tris;
        switch (art)
        {
        case NEU_RING3D: tris = formen::hohlzylinder(w(1), w(0), w(2), ganz(3), mat); break;
        case NEU_RING2D:
            tris = w(1) > 0 ? formen::ring2d(w(1), w(0), ganz(2), mat) : formen::kreisflaeche(w(0), ganz(2), mat);
            break;
        case NEU_PYRAMIDE: tris = formen::pyramide(w(0), w(1), mat); break;
        case NEU_ZYLINDER_NETZ: tris = formen::zylinder(w(0), w(1), ganz(2), ganz(3) != 0, mat); break;
        case NEU_TETRAEDER: tris = formen::tetraeder(w(0), mat); break;
        case NEU_OKTAEDER: tris = formen::oktaeder(w(0), mat); break;
        case NEU_KREISFLAECHE: tris = formen::kreisflaeche(w(0), ganz(1), mat); break;
        case NEU_KEGEL: tris = formen::kegel(w(0), w(1), ganz(2), ganz(3) != 0, mat); break;
        case NEU_TORUS: tris = formen::torus(w(0), w(1), ganz(2), ganz(3), mat); break;
        case NEU_KAPSEL: tris = formen::kapsel(w(0), w(1), ganz(2), mat); break;
        case NEU_ROHR:
            tris = formen::hohlzylinder(w(1), w(0), w(2), ganz(3), mat);
            formen::liegendInX(tris);
            break;
        }
        formen::verschieben(tris, p);
        auto netz = std::make_unique<Mesh>(tris, Vector3D(0, 0, 0));
        netz->mat = mat; // Material-Reiter zeigt dieses Material
        obj = std::move(netz);
        break;
    }

    case NEU_TEXT: // Buchstaben s hoch, mittig um den Einfuegepunkt
    {
        const std::string text = "Text";
        const double groesse = s, tiefe = s * 0.2;
        auto t = std::make_unique<Text3D>(text, Vector3D(0, 0, 0), groesse, tiefe, mat);
        Vector3D mn, mx;
        t->getWorldAABB(mn, mx); // Breite haengt von den Zeichen ab (Proportionalschrift)
        t->translate(p - (mn + mx) * 0.5);
        obj = std::move(t);
        break; // Name = Text (setzt Text3D selbst)
    }

    default:
        return;
    }

    if (!obj)
        return;

    if (!name.empty())
        obj->name = freierName(name);
    object *neu = obj.get();

    gSzeneAendern([&]
                  { scene.push_back(std::move(obj)); });
    eingefuegtMerken("Neu", {neu});

    auswahlSetzen({neu});
    abgleichen();

    // beim 3D-Text gleich den Text eintippen lassen
    if (art == NEU_TEXT)
    {
        SetFocus(g.info);
        SendMessageW(g.info, EM_SETSEL, 0, -1);
    }
}

// Ausklappmenue unter dem Button "Neu"

//--------------------------------------------------
// Szenen-Vorlagen fuer die Belichtung ("+ Neu" -> "Belichtung / Stimmung")
// Setzen Himmel (Farben, Helligkeit), Belichtung, Nebel und Lichter passend zur Szenengroesse.
// Lichtstaerken nach den Formeln des Renderers (direktes Licht = Farbe * E * cos * kD / pi):
//  - Richtungslicht: E = Staerke (Sonne am Tag etwa 2.5-3.5, Mond etwa 0.3)
//  - Flaechenlicht:  E = Leistung * cos / (pi * d^2)   -> Leistung = E * pi * d^2
//  - Punkt-/Kugellicht: E = Leistung / (4 pi d^2)    -> Leistung = E * 4 pi d^2
//--------------------------------------------------
constexpr int ID_SZENEN_VORLAGE = 9000;

struct SzenenVorlage
{
    const wchar_t *name;
    Farbe horizont, zenit;
    double himmel;       // Himmel-Helligkeit
    double belichtung;   // exposure
    double nebelDichte;  // je Szenengroesse (0 = kein Nebel)
    Farbe nebelFarbe;
    // Sonne / Mond als Richtungslicht (staerke 0 = keines)
    double sonneStaerke;
    Farbe sonneFarbe;
    double hoehe;        // Sonnenhoehe in Grad
    double seite;        // Richtung um die Kamera: 0 = von hinten (Kamera), 90 = von links, 180 = Gegenlicht
    int extra;           // 0 keine, 1 Deckenlampe (Raum), 2 Strassenlaterne (Nacht), 3 Innenleuchte (Fahrzeug), 4 Erdschein (Weltall)
    const wchar_t *tipp;
};

static const SzenenVorlage SZENEN_VORLAGEN[] = {
    {L"Raum (Innenraum)", Farbe(0.8, 0.8, 0.8), Farbe(0.9, 0.9, 0.9), 0.15, 0.0, 0, Farbe(0, 0, 0),
     0.0, Farbe(1, 1, 1), 0, 0, 1, L"Deckenlampe, kaum Himmelslicht"},
    {L"Wald", Farbe(0.45, 0.6, 0.4), Farbe(0.35, 0.55, 0.8), 0.8, 0.2, 0.6, Farbe(0.55, 0.65, 0.55),
     2.2, Farbe(1.0, 0.95, 0.82), 55, 40, 0, L"gedämpftes, grünliches Licht, leichter Dunst"},
    {L"Garten", Farbe(0.6, 0.8, 1.0), Farbe(0.12, 0.35, 0.75), 1.0, 0.0, 0, Farbe(0, 0, 0),
     2.8, Farbe(1.0, 0.97, 0.9), 45, 50, 0, L"sonniger Nachmittag"},
    {L"Mond (Oberfläche)", Farbe(0, 0, 0), Farbe(0, 0, 0), 0.0, 0.0, 0, Farbe(0, 0, 0),
     3.5, Farbe(1, 1, 1), 20, 80, 0, L"harte Sonne, schwarzer Himmel, tiefschwarze Schatten"},
    {L"Unter Wasser", Farbe(0.0, 0.15, 0.25), Farbe(0.2, 0.5, 0.6), 0.8, 0.3, 2.0, Farbe(0.05, 0.25, 0.3),
     1.8, Farbe(0.5, 0.85, 0.9), 75, 20, 0, L"blaugrünes Licht von oben, dichter Wassernebel"},
    {L"Am Wasser", Farbe(0.75, 0.88, 1.0), Farbe(0.2, 0.45, 0.85), 1.2, 0.0, 0.15, Farbe(0.75, 0.85, 0.95),
     2.8, Farbe(1.0, 0.97, 0.92), 40, 60, 0, L"heller Himmel (spiegelt sich im Wasser), leichter Dunst"},
    {L"In den Bergen", Farbe(0.7, 0.8, 0.95), Farbe(0.05, 0.2, 0.55), 1.0, 0.0, 0.3, Farbe(0.6, 0.7, 0.85),
     3.2, Farbe(1.0, 0.98, 0.95), 40, 60, 0, L"klare, kräftige Sonne, blauer Dunst in der Ferne"},
    {L"Abend", Farbe(1.0, 0.55, 0.3), Farbe(0.15, 0.15, 0.35), 0.7, 0.3, 0.1, Farbe(0.8, 0.5, 0.35),
     2.0, Farbe(1.0, 0.55, 0.25), 8, 100, 0, L"tiefe, orange Sonne von der Seite, lange Schatten"},
    {L"Morgen", Farbe(0.95, 0.75, 0.6), Farbe(0.35, 0.5, 0.75), 0.8, 0.2, 0.4, Farbe(0.85, 0.85, 0.9),
     2.2, Farbe(1.0, 0.8, 0.6), 12, -80, 0, L"flache, warme Sonne, Morgendunst"},
    {L"Mittag", Farbe(0.6, 0.8, 1.0), Farbe(0.1, 0.3, 0.65), 1.0, -0.2, 0, Farbe(0, 0, 0),
     3.2, Farbe(1.0, 1.0, 0.97), 75, 30, 0, L"Sonne hoch oben, kurze Schatten"},
    {L"Nacht", Farbe(0.02, 0.03, 0.08), Farbe(0.0, 0.0, 0.02), 1.0, 1.0, 0, Farbe(0, 0, 0),
     0.35, Farbe(0.6, 0.7, 1.0), 45, 60, 2, L"bläuliches Mondlicht und eine warme Laterne"},
    {L"Mars", Farbe(0.85, 0.6, 0.4), Farbe(0.55, 0.35, 0.25), 0.9, 0.1, 0.3, Farbe(0.75, 0.5, 0.35),
     2.0, Farbe(1.0, 0.85, 0.7), 35, 60, 0, L"schwächere Sonne, rötlicher Staubhimmel"},
    {L"Weltall", Farbe(0, 0, 0), Farbe(0, 0, 0), 0.0, 0.0, 0, Farbe(0, 0, 0),
     3.5, Farbe(1, 1, 1), 10, 90, 4, L"Sonne von der Seite, schwacher bläulicher Erdschein von gegenüber"},
    {L"In einem Fahrzeug", Farbe(0.8, 0.9, 1.0), Farbe(0.4, 0.6, 0.9), 1.3, 0.5, 0, Farbe(0, 0, 0),
     2.8, Farbe(1.0, 0.97, 0.92), 45, 30, 3, L"helles Tageslicht durch die Fenster, kleine Innenleuchte"},
};
constexpr int ANZAHL_SZENEN_VORLAGEN = int(sizeof(SZENEN_VORLAGEN) / sizeof(SZENEN_VORLAGEN[0]));

static void szenenVorlageAnwenden(int nr)
{
    if (nr < 0 || nr >= ANZAHL_SZENEN_VORLAGEN || !gSzeneAendern)
        return;
    const SzenenVorlage &v = SZENEN_VORLAGEN[nr];

    // Bisherige Lichter ersetzen oder behalten?
    std::vector<light *> alte;
    for (auto &l : lights)
        alte.push_back(l.get());
    bool ersetzen = false;
    {
        std::wstring frage = std::wstring(L"Belichtung „") + v.name + L"“ (" + v.tipp + L"):\n\n" +
                             L"Himmel, Belichtung und Nebel werden gesetzt, passende Lichter eingefügt.\n";
        int antwort = IDNO;
        if (!alte.empty())
        {
            frage += L"\nDie bisherigen " + std::to_wstring(alte.size()) +
                     L" Lichter ersetzen?\nJa = ersetzen, Nein = behalten und neue dazu, Abbrechen = nichts tun";
            antwort = MessageBoxW(GetForegroundWindow(), frage.c_str(), L"Belichtung", MB_YESNOCANCEL | MB_ICONQUESTION);
            if (antwort == IDCANCEL)
                return;
        }
        ersetzen = antwort == IDYES;
    }
    if (ersetzen)
    {
        auswahlSetzen({});
        selectedLight = nullptr;
        loeschenMitVerlauf("Lichter ersetzen", {}, alte);
    }

    // Groesse und Lage der Szene
    Vector3D mn = sceneMin, mx = sceneMax;
    if (!((mx - mn).length() > 1e-9))
    {
        mn = Vector3D(-5, 0, -5);
        mx = Vector3D(5, 3, 5);
    }
    const double groesse = std::max((mx - mn).length(), 1e-6);
    const Vector3D mitte = (mn + mx) * 0.5;

    // Himmel, Belichtung, Nebel
    himmelHorizont = v.horizont;
    himmelZenit = v.zenit;
    himmelHelligkeit = v.himmel;
    exposure = v.belichtung;
    nebelVorhanden = v.nebelDichte > 0;
    if (nebelVorhanden)
    {
        fogDensity = v.nebelDichte / groesse; // nach "Szenengroesse" Strecke bleibt e^-Dichte uebrig
        fogColor = v.nebelFarbe;
        nebelBeleuchtet = false;
        nebelDaempftLicht = true;
    }

    // Richtung relativ zur Kamera: 0 = Licht kommt von hinter der Kamera, 90 = von links, 180 = Gegenlicht
    Vector3D vorne(0, 0, 1);
    if (!cam.empty())
    {
        Vector3D f = cam[0].lookAt - cam[0].position;
        f.y = 0;
        if (f.length() > 1e-9)
            vorne = f.normalized();
    }
    const Vector3D links = Vector3D(0, 1, 0).cross(vorne).normalized();
    auto zumLicht = [&](double hoeheGrad, double seiteGrad)
    {
        const double h = hoeheGrad * M_PI / 180.0, a = seiteGrad * M_PI / 180.0;
        Vector3D waagrecht = (-vorne) * std::cos(a) + links * std::sin(a);
        return (waagrecht * std::cos(h) + Vector3D(0, 1, 0) * std::sin(h)).normalized();
    };

    std::vector<std::unique_ptr<light>> neue;
    auto richtungslicht = [&](const Vector3D &richtung, const Farbe &f, double staerke)
    {
        auto d = std::make_unique<DirectionalLight>(richtung, f, staerke);
        richtungslichtSymbolSetzen(d.get(), mitte + richtung * (groesse * 0.6));
        neue.push_back(std::move(d));
    };
    if (v.sonneStaerke > 0)
        richtungslicht(zumLicht(v.hoehe, v.seite), v.sonneFarbe, v.sonneStaerke);

    const double hoehe = std::max(mx.y - mn.y, groesse * 0.1);
    switch (v.extra)
    {
    case 1: // Deckenlampe: Flaechenlicht knapp unter der hoechsten Stelle, nach unten leuchtend
    {
        const double k = groesse * 0.15, d = hoehe * 0.95;
        auto a = std::make_unique<AreaLight>(Vector3D(0, 0, 0), Vector3D(k, 0, 0), Vector3D(0, 0, k), Farbe(1.0, 0.92, 0.8),
                                             2.5 * M_PI * d * d); // E ~ 2.5 am Boden
        a->setPosition(Vector3D(mitte.x, mx.y - hoehe * 0.02, mitte.z));
        neue.push_back(std::move(a));
        break;
    }
    case 2: // Laterne: warmes Kugellicht etwas ueber der Mitte, seitlich
    {
        const double d = hoehe * 0.6;
        Vector3D p = mitte + links * (groesse * 0.15) + Vector3D(0, d * 0.5, 0);
        neue.push_back(std::make_unique<SphereLight>(p, groesse * 0.01, Farbe(1.0, 0.7, 0.35), 2.0 * 4 * M_PI * d * d));
        break;
    }
    case 3: // Innenleuchte im Fahrzeug: kleines warmes Licht oben in der Mitte
    {
        const double d = hoehe * 0.5;
        neue.push_back(std::make_unique<SphereLight>(Vector3D(mitte.x, mx.y - hoehe * 0.1, mitte.z), groesse * 0.005,
                                                     Farbe(1.0, 0.85, 0.65), 0.8 * 4 * M_PI * d * d));
        break;
    }
    case 4: // Erdschein: schwaches blaues Licht von der Gegenseite der Sonne
        richtungslicht(zumLicht(-v.hoehe, v.seite + 180), Farbe(0.45, 0.6, 1.0), 0.15);
        break;
    default:
        break;
    }

    std::vector<light *> zeiger;
    for (auto &l : neue)
        zeiger.push_back(l.get());
    gSzeneAendern([&]
                  {
        for (auto &l : neue)
            lights.push_back(std::move(l)); });
    if (!zeiger.empty())
        eingefuegtMerken(std::string("Belichtung"), {}, zeiger);

    szeneGeaendertMarkieren();
    einstellungenAnzeigen();
    abgleichen();
}


//--------------------------------------------------
// Rotationsobjekt: Profil zeichnen (Punkte setzen, verschieben, loeschen), Segmente waehlen, "Erstellen"
// Zeichenflaeche: Achse senkrecht in der Mitte; rechts das Profil (Radius nach rechts, Hoehe nach oben),
// links gespiegelt als Vorschau der Silhouette. Profilwerte 0..1, beim Erstellen mal "Hoehe".
//--------------------------------------------------
#define ID_ROT_BEISPIEL 7101
#define ID_ROT_LEEREN 7102
#define ID_ROT_SEGMENTE 7103
#define ID_ROT_HOEHE 7104
#define ID_ROT_GLATT 7105

static std::vector<rotation::ProfilPunkt> rotProfil = {{0, 0},     {0.3, 0},    {0.35, 0.1}, {0.4, 0.35},
                                                       {0.3, 0.6}, {0.2, 0.75}, {0.22, 0.9}, {0.28, 1.0}};
static HWND rotFlaeche = nullptr, rotSegmente = nullptr, rotHoehe = nullptr, rotGlatt = nullptr, rotHinweis = nullptr;
static int rotZiehen = -1, rotHover = -1;
static bool rotFertig = false, rotOk = false;
static int rotSegmentWert = 32;
static bool rotGlattWert = true;

struct RotAbbildung
{
    double cx, unten, s; // Achse x, Hoehe 0 in Pixeln, Pixel je Einheit
};
static RotAbbildung rotAbbildung()
{
    RECT r;
    GetClientRect(rotFlaeche, &r);
    const double rand = 24;
    RotAbbildung a;
    a.s = std::max(10.0, std::min((r.right - 2 * rand) / 2.0, double(r.bottom) - 2 * rand));
    a.cx = r.right / 2.0;
    a.unten = r.bottom - rand;
    return a;
}
static POINT rotZuBild(const rotation::ProfilPunkt &q, bool gespiegelt = false)
{
    RotAbbildung a = rotAbbildung();
    return {LONG(std::lround(a.cx + (gespiegelt ? -q.r : q.r) * a.s)), LONG(std::lround(a.unten - q.y * a.s))};
}
static rotation::ProfilPunkt rotAusBild(int x, int y)
{
    RotAbbildung a = rotAbbildung();
    rotation::ProfilPunkt q;
    q.r = std::clamp((x - a.cx) / a.s, 0.0, 1.0);
    q.y = std::clamp((a.unten - y) / a.s, 0.0, 1.0);
    if (q.r < 0.015)
        q.r = 0; // nahe der Achse: auf die Achse (geschlossener Boden bzw. Spitze)
    return q;
}
static int rotPunktBei(int x, int y)
{
    int best = -1;
    double bestD = 9.0;
    for (size_t i = 0; i < rotProfil.size(); ++i)
    {
        POINT p = rotZuBild(rotProfil[i]);
        double d = std::hypot(double(p.x - x), double(p.y - y));
        if (d < bestD)
            bestD = d, best = int(i);
    }
    return best;
}
static int rotStreckeBei(int x, int y) // Index k: Strecke zwischen k und k + 1
{
    int best = -1;
    double bestD = 7.0;
    for (size_t k = 0; k + 1 < rotProfil.size(); ++k)
    {
        POINT a = rotZuBild(rotProfil[k]), b = rotZuBild(rotProfil[k + 1]);
        double dx = b.x - a.x, dy = b.y - a.y, l2 = dx * dx + dy * dy;
        double t = l2 > 0 ? std::clamp(((x - a.x) * dx + (y - a.y) * dy) / l2, 0.0, 1.0) : 0.0;
        double d = std::hypot(a.x + t * dx - x, a.y + t * dy - y);
        if (d < bestD)
            bestD = d, best = int(k);
    }
    return best;
}

static void rotHinweisAktualisieren()
{
    if (!rotHinweis)
        return;
    double seg = rotSegmentWert;
    zahlLesen(rotSegmente, seg);
    const int n = int(rotProfil.size());
    std::wstring t = std::to_wstring(n) + L" Punkte";
    if (n >= 2)
        t += L", etwa " + std::to_wstring((n - 1) * 2 * std::max(3, int(std::round(seg)))) + L" Dreiecke";
    t += L"\n\nLinksklick: Punkt anhängen (auf einer Linie: dort einfügen)\nZiehen: Punkt verschieben\n"
         L"Rechtsklick auf einen Punkt: löschen\n\nPunkte nahe der Achse rasten auf ihr ein: dort wird das Objekt "
         L"geschlossen (Boden, Spitze). Links der Achse: Vorschau der Silhouette.";
    SetWindowTextW(rotHinweis, t.c_str());
}

static LRESULT CALLBACK RotFlaecheProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT r;
        GetClientRect(hwnd, &r);
        HDC mem = CreateCompatibleDC(hdc);
        HBITMAP bild = CreateCompatibleBitmap(hdc, std::max(1, int(r.right)), std::max(1, int(r.bottom)));
        HGDIOBJ altBild = SelectObject(mem, bild);
        FillRect(mem, &r, pinselFlaeche);
        RotAbbildung a = rotAbbildung();

        // Raster (0.1) und Achse
        HPEN gitter = CreatePen(PS_SOLID, 1, FARBE_LINIEN);
        HGDIOBJ altStift = SelectObject(mem, gitter);
        for (int i = -10; i <= 10; ++i)
        {
            int x = int(std::lround(a.cx + i * 0.1 * a.s));
            MoveToEx(mem, x, int(a.unten - a.s), nullptr);
            LineTo(mem, x, int(a.unten) + 1);
        }
        for (int i = 0; i <= 10; ++i)
        {
            int y = int(std::lround(a.unten - i * 0.1 * a.s));
            MoveToEx(mem, int(a.cx - a.s), y, nullptr);
            LineTo(mem, int(a.cx + a.s) + 1, y);
        }
        HPEN achse = CreatePen(PS_DASH, 1, RGB(120, 170, 230));
        SelectObject(mem, achse);
        MoveToEx(mem, int(a.cx), 4, nullptr);
        LineTo(mem, int(a.cx), r.bottom - 4);
        SetBkMode(mem, TRANSPARENT);
        HGDIOBJ altSchrift = SelectObject(mem, gFont);
        SetTextColor(mem, RGB(120, 170, 230));
        TextOutW(mem, int(a.cx) + 4, 4, L"Drehachse", 9);

        // gespiegelte Silhouette (Vorschau) und Profil
        HPEN schatten = CreatePen(PS_SOLID, 1, RGB(110, 110, 120));
        HPEN linie = CreatePen(PS_SOLID, 2, RGB(235, 235, 235));
        for (int seite = 0; seite < 2; ++seite)
        {
            SelectObject(mem, seite == 0 ? schatten : linie);
            for (size_t i = 0; i < rotProfil.size(); ++i)
            {
                POINT q = rotZuBild(rotProfil[i], seite == 0);
                if (i == 0)
                    MoveToEx(mem, q.x, q.y, nullptr);
                else
                    LineTo(mem, q.x, q.y);
            }
        }
        // Punkte: erster gruen, gezogener/Hover gelb
        for (size_t i = 0; i < rotProfil.size(); ++i)
        {
            POINT q = rotZuBild(rotProfil[i]);
            COLORREF f = int(i) == rotZiehen || int(i) == rotHover ? RGB(255, 210, 60) : i == 0 ? RGB(90, 200, 120) : RGB(80, 170, 240);
            HBRUSH b = CreateSolidBrush(f);
            RECT pr = {q.x - 4, q.y - 4, q.x + 5, q.y + 5};
            FillRect(mem, &pr, b);
            DeleteObject(b);
        }
        SelectObject(mem, altSchrift);
        SelectObject(mem, altStift);
        DeleteObject(gitter);
        DeleteObject(achse);
        DeleteObject(schatten);
        DeleteObject(linie);
        BitBlt(hdc, 0, 0, r.right, r.bottom, mem, 0, 0, SRCCOPY);
        SelectObject(mem, altBild);
        DeleteObject(bild);
        DeleteDC(mem);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_LBUTTONDOWN:
    {
        const int x = GET_X_LPARAM(lParam), y = GET_Y_LPARAM(lParam);
        int i = rotPunktBei(x, y);
        if (i < 0)
        {
            const int k = rotStreckeBei(x, y);
            if (k >= 0)
            {
                i = k + 1; // auf einer Linie: dort einfuegen
                rotProfil.insert(rotProfil.begin() + i, rotAusBild(x, y));
            }
            else
            {
                rotProfil.push_back(rotAusBild(x, y)); // sonst ans Ende
                i = int(rotProfil.size()) - 1;
            }
        }
        rotZiehen = i;
        SetCapture(hwnd);
        rotHinweisAktualisieren();
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    case WM_MOUSEMOVE:
    {
        const int x = GET_X_LPARAM(lParam), y = GET_Y_LPARAM(lParam);
        if (rotZiehen >= 0 && rotZiehen < int(rotProfil.size()) && GetCapture() == hwnd)
            rotProfil[size_t(rotZiehen)] = rotAusBild(x, y);
        else
            rotHover = rotPunktBei(x, y);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    case WM_LBUTTONUP:
    case WM_CAPTURECHANGED:
        rotZiehen = -1;
        if (msg == WM_LBUTTONUP && GetCapture() == hwnd)
            ReleaseCapture();
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_RBUTTONDOWN:
    {
        int i = rotPunktBei(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        if (i >= 0)
        {
            rotProfil.erase(rotProfil.begin() + i);
            rotHover = -1;
            rotHinweisAktualisieren();
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static LRESULT CALLBACK RotDialogProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_COMMAND:
    {
        const int id = LOWORD(wParam);
        if (id == IDOK || id == IDCANCEL)
        {
            rotOk = id == IDOK;
            rotFertig = true;
            return 0;
        }
        if (id == ID_ROT_BEISPIEL)
        {
            rotProfil = {{0, 0}, {0.3, 0}, {0.35, 0.1}, {0.4, 0.35}, {0.3, 0.6}, {0.2, 0.75}, {0.22, 0.9}, {0.28, 1.0}};
            rotHinweisAktualisieren();
            InvalidateRect(rotFlaeche, nullptr, FALSE);
            return 0;
        }
        if (id == ID_ROT_LEEREN)
        {
            rotProfil.clear();
            rotHinweisAktualisieren();
            InvalidateRect(rotFlaeche, nullptr, FALSE);
            return 0;
        }
        if (HIWORD(wParam) == EN_CHANGE)
        {
            rotHinweisAktualisieren();
            return 0;
        }
        break;
    }
    case WM_CLOSE:
        rotOk = false;
        rotFertig = true;
        return 0;
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
    {
        HDC hdc = (HDC)wParam;
        SetTextColor(hdc, GetWindowLongPtrW((HWND)lParam, GWLP_USERDATA) == 1 ? FARBE_UEBERSCHRIFT : FARBE_TEXT);
        SetBkColor(hdc, FARBE_HINTERGRUND);
        return (LRESULT)pinselHintergrund;
    }
    case WM_CTLCOLOREDIT:
    {
        HDC hdc = (HDC)wParam;
        SetTextColor(hdc, FARBE_TEXT);
        SetBkColor(hdc, FARBE_FLAECHE);
        return (LRESULT)pinselFlaeche;
    }
    case WM_DRAWITEM:
        buttonZeichnen(reinterpret_cast<const DRAWITEMSTRUCT *>(lParam));
        return TRUE;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void rotationsobjektDialog()
{
    if (!gSzeneAendern)
        return;
    HINSTANCE hInst = GetModuleHandleW(nullptr);
    static bool registriert = false;
    if (!registriert)
    {
        WNDCLASSW wc{};
        wc.lpfnWndProc = RotDialogProc;
        wc.hInstance = hInst;
        wc.hbrBackground = pinselHintergrund;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.lpszClassName = L"SZENEN_ROTATION_DIALOG";
        RegisterClassW(&wc);
        WNDCLASSW wf{};
        wf.lpfnWndProc = RotFlaecheProc;
        wf.hInstance = hInst;
        wf.hCursor = LoadCursor(nullptr, IDC_CROSS);
        wf.lpszClassName = L"SZENEN_ROTATION_PROFIL";
        RegisterClassW(&wf);
        registriert = true;
    }

    const int rand = 14, flaecheB = 520, flaecheH = 300, rechtsB = 270, feldH = 26, labelB = 130;
    const int breite = rand + flaecheB + rand + rechtsB + rand;
    const int innenH = rand + std::max(flaecheH, 360) + 10 + BTN_H + rand;
    DWORD stil = WS_POPUP | WS_CAPTION | WS_SYSMENU;
    RECT r = {0, 0, breite, innenH};
    AdjustWindowRectEx(&r, stil, FALSE, WS_EX_DLGMODALFRAME);
    const int w = r.right - r.left, h = r.bottom - r.top;
    RECT bezug;
    GetWindowRect(hHauptfenster ? hHauptfenster : hToolWindow, &bezug);
    int x = (bezug.left + bezug.right) / 2 - w / 2, y = (bezug.top + bezug.bottom) / 2 - h / 2;

    HWND dlg = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_TOPMOST, L"SZENEN_ROTATION_DIALOG", L"Rotationsobjekt", stil,
                               x, y, w, h, hToolWindow, nullptr, hInst, nullptr);
    if (!dlg)
        return;
    auto schrift = [&](HWND hw) { SendMessageW(hw, WM_SETFONT, (WPARAM)gFont, TRUE); return hw; };

    const int innenFlaecheH = innenH - 2 * rand - 10 - BTN_H;
    rotFlaeche = CreateWindowExW(0, L"SZENEN_ROTATION_PROFIL", L"", WS_CHILD | WS_VISIBLE | WS_BORDER, rand, rand, flaecheB,
                                 innenFlaecheH, dlg, nullptr, hInst, nullptr);
    tooltip(rotFlaeche, L"Profil des Rotationsobjekts: rechts der Achse zeichnen (Radius nach rechts, Höhe nach oben).\n"
                        L"Linksklick = Punkt anhängen bzw. auf einer Linie einfügen, Ziehen = verschieben, Rechtsklick = löschen.");

    const int rx = rand + flaecheB + rand;
    int ry = rand;
    schrift(CreateWindowExW(0, L"STATIC", L"Segmente", WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE, rx, ry, labelB, feldH, dlg,
                            nullptr, hInst, nullptr));
    rotSegmente = schrift(CreateWindowExW(0, L"EDIT", std::to_wstring(rotSegmentWert).c_str(),
                                          WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_BORDER | ES_RIGHT | ES_AUTOHSCROLL,
                                          rx + labelB, ry, rechtsB - labelB, feldH, dlg, (HMENU)(INT_PTR)ID_ROT_SEGMENTE, hInst, nullptr));
    tooltip(rotSegmente, L"Anzahl Schritte rundherum (3 bis 512): 3 = dreieckig, 4 = viereckig, 32 und mehr = rund");
    ry += feldH + 6;
    schrift(CreateWindowExW(0, L"STATIC", L"Höhe (Einheiten)", WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE, rx, ry, labelB, feldH,
                            dlg, nullptr, hInst, nullptr));
    rotHoehe = schrift(CreateWindowExW(0, L"EDIT", zahlText(passendeGroesse(), 2).c_str(),
                                       WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_BORDER | ES_RIGHT | ES_AUTOHSCROLL,
                                       rx + labelB, ry, rechtsB - labelB, feldH, dlg, (HMENU)(INT_PTR)ID_ROT_HOEHE, hInst, nullptr));
    tooltip(rotHoehe, L"Höhe des oberen Rasterrands im Profil; der Radius wird im gleichen Maßstab umgerechnet");
    ry += feldH + 6;
    rotGlatt = schrift(CreateWindowExW(0, L"BUTTON", L"Glatt schattieren", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                                       rx, ry, rechtsB, feldH, dlg, (HMENU)(INT_PTR)ID_ROT_GLATT, hInst, nullptr));
    SetWindowTheme(rotGlatt, L"", L"");
    SendMessageW(rotGlatt, BM_SETCHECK, rotGlattWert ? BST_CHECKED : BST_UNCHECKED, 0);
    tooltip(rotGlatt, L"An: weiche Rundungen (Normalen gemittelt), Knicke über 40° bleiben scharf (z.B. Boden). Aus: facettiert.");
    ry += feldH + 10;
    const int halbB = (rechtsB - GAP) / 2;
    HWND b1 = schrift(CreateWindowExW(0, L"BUTTON", L"Beispiel: Vase", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, rx, ry,
                                      halbB, BTN_H, dlg, (HMENU)(INT_PTR)ID_ROT_BEISPIEL, hInst, nullptr));
    HWND b2 = schrift(CreateWindowExW(0, L"BUTTON", L"Alle löschen", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                                      rx + halbB + GAP, ry, rechtsB - halbB - GAP, BTN_H, dlg, (HMENU)(INT_PTR)ID_ROT_LEEREN, hInst, nullptr));
    tooltip(b1, L"Profil einer Vase laden (Startpunkt für eigene Formen)");
    tooltip(b2, L"Alle Punkte entfernen und neu zeichnen");
    ry += BTN_H + 12;
    rotHinweis = schrift(CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE, rx, ry, rechtsB, innenFlaecheH - (ry - rand),
                                         dlg, nullptr, hInst, nullptr));
    SetWindowLongPtrW(rotHinweis, GWLP_USERDATA, 1);
    const int knopfB = 120, ky = innenH - rand - BTN_H;
    HWND ok = schrift(CreateWindowExW(0, L"BUTTON", L"Erstellen", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                                      breite - rand - 2 * knopfB - GAP, ky, knopfB, BTN_H, dlg, (HMENU)(INT_PTR)IDOK, hInst, nullptr));
    schrift(CreateWindowExW(0, L"BUTTON", L"Abbrechen", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, breite - rand - knopfB,
                            ky, knopfB, BTN_H, dlg, (HMENU)(INT_PTR)IDCANCEL, hInst, nullptr));
    tooltip(ok, L"Rotationsobjekt aus dem Profil erzeugen und am Einfügepunkt (Kamera-Ziel) in die Szene setzen");
    rotHinweisAktualisieren();

    EnableWindow(hToolWindow, FALSE);
    EnableWindow(hObjektFenster, FALSE);
    ShowWindow(dlg, SW_SHOW);
    SetForegroundWindow(dlg);

    int segmente = rotSegmentWert;
    double hoehe = passendeGroesse();
    bool ergebnis = false;
    for (;;)
    {
        rotFertig = false;
        rotOk = false;
        MSG m;
        while (!rotFertig && GetMessageW(&m, nullptr, 0, 0) > 0)
            if (!IsDialogMessageW(dlg, &m))
            {
                TranslateMessage(&m);
                DispatchMessageW(&m);
            }
        if (!rotOk)
            break;
        double sv = 0, hv = 0;
        if (rotProfil.size() < 2)
        {
            MessageBoxW(dlg, L"Bitte mindestens zwei Punkte setzen.", L"Rotationsobjekt", MB_OK | MB_ICONINFORMATION);
            continue;
        }
        if (!zahlLesen(rotSegmente, sv) || sv < 3 || sv > 512)
        {
            MessageBeep(MB_ICONWARNING);
            SetFocus(rotSegmente);
            continue;
        }
        if (!zahlLesen(rotHoehe, hv) || !(hv > 0))
        {
            MessageBeep(MB_ICONWARNING);
            SetFocus(rotHoehe);
            continue;
        }
        segmente = int(std::round(sv));
        hoehe = hv;
        ergebnis = true;
        break;
    }
    rotGlattWert = SendMessageW(rotGlatt, BM_GETCHECK, 0, 0) == BST_CHECKED;
    if (ergebnis)
        rotSegmentWert = segmente;
    EnableWindow(hToolWindow, TRUE);
    EnableWindow(hObjektFenster, TRUE);
    DestroyWindow(dlg);
    rotFlaeche = rotSegmente = rotHoehe = rotGlatt = rotHinweis = nullptr;
    if (!ergebnis)
        return;

    // Profil in Einheiten, am Einfuegepunkt (Boden des Profils auf dessen Hoehe)
    std::vector<rotation::ProfilPunkt> profil = rotProfil;
    for (auto &q : profil)
    {
        q.r *= hoehe;
        q.y *= hoehe;
    }
    std::vector<Dreieck> d = rotation::koerper(profil, segmente, rotGlattWert, makeMaterialSimple(Farbe(0.7, 0.7, 0.7)));
    if (d.empty())
    {
        MessageBoxW(hToolWindow, L"Aus diesem Profil entsteht keine Fläche (alle Punkte auf der Achse?).", L"Rotationsobjekt",
                    MB_OK | MB_ICONINFORMATION);
        return;
    }
    const Vector3D p = einfuegePunkt();
    for (auto &t : d)
    {
        t.a += p;
        t.b += p;
        t.c += p;
        t.rebuild();
    }
    std::vector<std::unique_ptr<object>> objekte;
    objekte.push_back(std::make_unique<Mesh>(d, Vector3D(0, 0, 0)));
    neueObjekteEinfuegen(std::move(objekte), "Rotationsobjekt");
}

//--------------------------------------------------
// Roehre entlang Punkten: Pfad in der Seitenansicht zeichnen (X nach rechts, Y nach oben), Radius je Punkt mit dem
// Mausrad; "Erstellen" legt eine geschlossene Roehre (Schwanz, Hals, Schlange, Henkel, Rohr) in die Szene.
// Werte im Raster 0..1 (X -1..1), beim Erstellen mal "Groesse".
//--------------------------------------------------
#define ID_ROE_BEISPIEL_SCHWANZ 7201
#define ID_ROE_BEISPIEL_HENKEL 7202
#define ID_ROE_LEEREN 7203
#define ID_ROE_SEGMENTE 7204
#define ID_ROE_GROESSE 7205
#define ID_ROE_GLATT 7206
#define ID_ROE_KAPPEN 7207
#define ID_ROE_GLAETTUNG 7208
#define ID_ROE_LINEAR 7209 // Radius linear von Anfang bis Ende
#define ID_ROE_RAD_ANFANG 7210
#define ID_ROE_RAD_ENDE 7211

struct RoePunkt
{
    double x, y, r;
};
static std::vector<RoePunkt> roePfad = {{-0.6, 0.30, 0.06}, {-0.25, 0.38, 0.05}, {0.0, 0.55, 0.045}, {0.1, 0.75, 0.04}, {0.05, 0.9, 0.03}};
static HWND roeFlaeche = nullptr, roeSegmente = nullptr, roeGroesse = nullptr, roeGlatt = nullptr, roeKappen = nullptr,
            roeGlaettung = nullptr, roeHinweis = nullptr;
static int roeZiehen = -1, roeHover = -1;
static bool roeFertig = false, roeOk = false;
static int roeSegmentWert = 20, roeGlaettungWert = 6;
static bool roeGlattWert = true, roeKappenWert = true;
// Radius gleichmaessig vom Anfang zum Ende (statt je Punkt); Werte in Einheiten der Szene
static HWND roeLinear = nullptr, roeRadAnfang = nullptr, roeRadEnde = nullptr;
static bool roeLinearWert = false;
static double roeRadAnfangWert = -1, roeRadEndeWert = -1; // < 0: aus der Szenengroesse vorschlagen

// Bei "Radius linear": die Radien der Punkte nach ihrer Lage entlang des Pfads (Bogenlaenge) setzen
static void roeLinearAnwenden()
{
    if (!roeLinear || SendMessageW(roeLinear, BM_GETCHECK, 0, 0) != BST_CHECKED || roePfad.empty())
        return;
    double g = 0, ra = 0, re = 0;
    if (!zahlLesen(roeGroesse, g) || !(g > 0) || !zahlLesen(roeRadAnfang, ra) || !zahlLesen(roeRadEnde, re) || ra < 0 || re < 0)
        return;
    std::vector<double> s(roePfad.size(), 0.0);
    for (size_t i = 1; i < roePfad.size(); ++i)
        s[i] = s[i - 1] + std::hypot(roePfad[i].x - roePfad[i - 1].x, roePfad[i].y - roePfad[i - 1].y);
    const double gesamt = s.back();
    for (size_t i = 0; i < roePfad.size(); ++i)
    {
        const double f = gesamt > 1e-12 ? s[i] / gesamt : (roePfad.size() > 1 ? double(i) / double(roePfad.size() - 1) : 0.0);
        roePfad[i].r = std::max(0.0005, (ra + (re - ra) * f) / g);
    }
}

static RotAbbildung roeAbbildung()
{
    RECT r;
    GetClientRect(roeFlaeche, &r);
    const double rand = 24;
    RotAbbildung a;
    a.s = std::max(10.0, std::min((r.right - 2 * rand) / 2.0, double(r.bottom) - 2 * rand));
    a.cx = r.right / 2.0;
    a.unten = r.bottom - rand;
    return a;
}
static POINT roeZuBild(double x, double y)
{
    RotAbbildung a = roeAbbildung();
    return {LONG(std::lround(a.cx + x * a.s)), LONG(std::lround(a.unten - y * a.s))};
}
static void roeAusBild(int px, int py, double &x, double &y)
{
    RotAbbildung a = roeAbbildung();
    x = std::clamp((px - a.cx) / a.s, -1.0, 1.0);
    y = std::clamp((a.unten - py) / a.s, 0.0, 1.0);
}
static int roePunktBei(int x, int y)
{
    int best = -1;
    double bestD = 9.0;
    for (size_t i = 0; i < roePfad.size(); ++i)
    {
        POINT p = roeZuBild(roePfad[i].x, roePfad[i].y);
        double d = std::hypot(double(p.x - x), double(p.y - y));
        if (d < bestD)
            bestD = d, best = int(i);
    }
    return best;
}
static int roeStreckeBei(int x, int y)
{
    int best = -1;
    double bestD = 7.0;
    for (size_t k = 0; k + 1 < roePfad.size(); ++k)
    {
        POINT a = roeZuBild(roePfad[k].x, roePfad[k].y), b = roeZuBild(roePfad[k + 1].x, roePfad[k + 1].y);
        double dx = b.x - a.x, dy = b.y - a.y, l2 = dx * dx + dy * dy;
        double t = l2 > 0 ? std::clamp(((x - a.x) * dx + (y - a.y) * dy) / l2, 0.0, 1.0) : 0.0;
        double d = std::hypot(a.x + t * dx - x, a.y + t * dy - y);
        if (d < bestD)
            bestD = d, best = int(k);
    }
    return best;
}
// geglaetteter Pfad (wie beim Erstellen) fuer die Vorschau
static std::vector<koerper::PfadPunkt> roeGeglaettet(int schritte)
{
    std::vector<koerper::PfadPunkt> pk;
    for (const RoePunkt &q : roePfad)
        pk.push_back({Vector3D(q.x, q.y, 0), q.r});
    return koerper::glaetten(pk, schritte);
}

static void roeHinweisAktualisieren()
{
    if (!roeHinweis)
        return;
    double seg = roeSegmentWert, gl = roeGlaettungWert;
    zahlLesen(roeSegmente, seg);
    zahlLesen(roeGlaettung, gl);
    const int n = int(roePfad.size());
    std::wstring t = std::to_wstring(n) + L" Punkte";
    if (n >= 2)
        t += L", etwa " + std::to_wstring(((n - 1) * std::max(1, int(gl)) + 6) * 2 * std::max(3, int(std::round(seg)))) + L" Dreiecke";
    t += L"\n\nLinksklick: Punkt anhängen (auf der Linie: einfügen)\nZiehen: verschieben\nRechtsklick: löschen\n"
         L"Mausrad über einem Punkt: dicker / dünner\nShift + Mausrad: alle Punkte\n\nDie Kreise zeigen die Dicke; der grüne "
         L"Punkt ist der Anfang.";
    SetWindowTextW(roeHinweis, t.c_str());
}

static LRESULT CALLBACK RoeFlaecheProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
    {
        roeLinearAnwenden(); // Vorschau zeigt die gleichmaessig zu-/abnehmende Dicke
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT r;
        GetClientRect(hwnd, &r);
        HDC mem = CreateCompatibleDC(hdc);
        HBITMAP bild = CreateCompatibleBitmap(hdc, std::max(1, int(r.right)), std::max(1, int(r.bottom)));
        HGDIOBJ altBild = SelectObject(mem, bild);
        FillRect(mem, &r, pinselFlaeche);
        RotAbbildung a = roeAbbildung();
        HPEN gitter = CreatePen(PS_SOLID, 1, FARBE_LINIEN);
        HGDIOBJ altStift = SelectObject(mem, gitter);
        for (int i = -10; i <= 10; ++i)
        {
            int x = int(std::lround(a.cx + i * 0.1 * a.s));
            MoveToEx(mem, x, int(a.unten - a.s), nullptr);
            LineTo(mem, x, int(a.unten) + 1);
        }
        for (int i = 0; i <= 10; ++i)
        {
            int y = int(std::lround(a.unten - i * 0.1 * a.s));
            MoveToEx(mem, int(a.cx - a.s), y, nullptr);
            LineTo(mem, int(a.cx + a.s) + 1, y);
        }
        // Boden (Einfuegepunkt) und Mitte
        HPEN boden = CreatePen(PS_DASH, 1, RGB(120, 170, 230));
        SelectObject(mem, boden);
        MoveToEx(mem, int(a.cx - a.s), int(a.unten), nullptr);
        LineTo(mem, int(a.cx + a.s), int(a.unten));
        SetBkMode(mem, TRANSPARENT);
        HGDIOBJ altSchrift = SelectObject(mem, gFont);
        SetTextColor(mem, RGB(120, 170, 230));
        TextOutW(mem, int(a.cx - a.s) + 4, int(a.unten) + 3, L"Boden (Einfügepunkt)", 20);

        // Umriss der Roehre (Vorschau): Rand links und rechts des geglaetteten Pfads
        double gl = roeGlaettungWert;
        zahlLesen(roeGlaettung, gl);
        const auto glatt = roeGeglaettet(std::max(1, int(gl)));
        HPEN umriss = CreatePen(PS_SOLID, 1, RGB(150, 155, 165));
        HPEN linie = CreatePen(PS_SOLID, 2, RGB(235, 235, 235));
        if (glatt.size() >= 2)
        {
            SelectObject(mem, umriss);
            for (int seite = -1; seite <= 1; seite += 2)
                for (size_t i = 0; i < glatt.size(); ++i)
                {
                    const size_t i0 = i == 0 ? 0 : i - 1, i1 = std::min(glatt.size() - 1, i + 1);
                    double tx = glatt[i1].p.x - glatt[i0].p.x, ty = glatt[i1].p.y - glatt[i0].p.y, l = std::hypot(tx, ty);
                    if (l < 1e-12)
                        l = 1;
                    POINT q = roeZuBild(glatt[i].p.x - ty / l * glatt[i].r * seite, glatt[i].p.y + tx / l * glatt[i].r * seite);
                    if (i == 0)
                        MoveToEx(mem, q.x, q.y, nullptr);
                    else
                        LineTo(mem, q.x, q.y);
                }
            SelectObject(mem, linie);
            for (size_t i = 0; i < glatt.size(); ++i)
            {
                POINT q = roeZuBild(glatt[i].p.x, glatt[i].p.y);
                if (i == 0)
                    MoveToEx(mem, q.x, q.y, nullptr);
                else
                    LineTo(mem, q.x, q.y);
            }
        }
        // Punkte mit Dicke (Kreis)
        HGDIOBJ altPinsel = SelectObject(mem, GetStockObject(NULL_BRUSH));
        for (size_t i = 0; i < roePfad.size(); ++i)
        {
            POINT q = roeZuBild(roePfad[i].x, roePfad[i].y);
            const bool hervor = int(i) == roeZiehen || int(i) == roeHover;
            HPEN kreis = CreatePen(PS_SOLID, 1, hervor ? RGB(255, 210, 60) : RGB(90, 140, 200));
            SelectObject(mem, kreis);
            const int rp = std::max(2, int(std::lround(roePfad[i].r * a.s)));
            Ellipse(mem, q.x - rp, q.y - rp, q.x + rp + 1, q.y + rp + 1);
            SelectObject(mem, linie);
            DeleteObject(kreis);
            COLORREF f = hervor ? RGB(255, 210, 60) : i == 0 ? RGB(90, 200, 120) : RGB(80, 170, 240);
            HBRUSH b = CreateSolidBrush(f);
            RECT pr = {q.x - 4, q.y - 4, q.x + 5, q.y + 5};
            FillRect(mem, &pr, b);
            DeleteObject(b);
        }
        SelectObject(mem, altPinsel);
        SelectObject(mem, altSchrift);
        SelectObject(mem, altStift);
        DeleteObject(gitter);
        DeleteObject(boden);
        DeleteObject(umriss);
        DeleteObject(linie);
        BitBlt(hdc, 0, 0, r.right, r.bottom, mem, 0, 0, SRCCOPY);
        SelectObject(mem, altBild);
        DeleteObject(bild);
        DeleteDC(mem);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_LBUTTONDOWN:
    {
        SetFocus(hwnd); // fuer das Mausrad
        const int x = GET_X_LPARAM(lParam), y = GET_Y_LPARAM(lParam);
        int i = roePunktBei(x, y);
        if (i < 0)
        {
            RoePunkt q;
            roeAusBild(x, y, q.x, q.y);
            const int k = roeStreckeBei(x, y);
            if (k >= 0)
            {
                i = k + 1;
                q.r = 0.5 * (roePfad[size_t(k)].r + roePfad[size_t(k) + 1].r);
                roePfad.insert(roePfad.begin() + i, q);
            }
            else
            {
                q.r = roePfad.empty() ? 0.05 : roePfad.back().r;
                roePfad.push_back(q);
                i = int(roePfad.size()) - 1;
            }
        }
        roeZiehen = i;
        SetCapture(hwnd);
        roeHinweisAktualisieren();
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    case WM_MOUSEMOVE:
    {
        const int x = GET_X_LPARAM(lParam), y = GET_Y_LPARAM(lParam);
        if (roeZiehen >= 0 && roeZiehen < int(roePfad.size()) && GetCapture() == hwnd)
            roeAusBild(x, y, roePfad[size_t(roeZiehen)].x, roePfad[size_t(roeZiehen)].y);
        else
            roeHover = roePunktBei(x, y);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    case WM_MOUSEWHEEL:
    {
        POINT p = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        ScreenToClient(hwnd, &p);
        const double f = GET_WHEEL_DELTA_WPARAM(wParam) > 0 ? 1.1 : 1.0 / 1.1;
        const bool alle = (GET_KEYSTATE_WPARAM(wParam) & MK_SHIFT) != 0;
        int i = roePunktBei(p.x, p.y);
        if (i < 0)
            i = roeHover;
        for (size_t k = 0; k < roePfad.size(); ++k)
            if (alle || int(k) == i)
                roePfad[k].r = std::clamp(roePfad[k].r * f, 0.002, 0.5);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    case WM_LBUTTONUP:
    case WM_CAPTURECHANGED:
        roeZiehen = -1;
        if (msg == WM_LBUTTONUP && GetCapture() == hwnd)
            ReleaseCapture();
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_RBUTTONDOWN:
    {
        int i = roePunktBei(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        if (i >= 0)
        {
            roePfad.erase(roePfad.begin() + i);
            roeHover = -1;
            roeHinweisAktualisieren();
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static LRESULT CALLBACK RoeDialogProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_COMMAND:
    {
        const int id = LOWORD(wParam);
        if (id == IDOK || id == IDCANCEL)
        {
            roeOk = id == IDOK;
            roeFertig = true;
            return 0;
        }
        if (id == ID_ROE_BEISPIEL_SCHWANZ || id == ID_ROE_BEISPIEL_HENKEL || id == ID_ROE_LEEREN)
        {
            if (id == ID_ROE_BEISPIEL_SCHWANZ)
                roePfad = {{-0.6, 0.30, 0.06}, {-0.25, 0.38, 0.05}, {0.0, 0.55, 0.045}, {0.1, 0.75, 0.04}, {0.05, 0.9, 0.03}};
            else if (id == ID_ROE_BEISPIEL_HENKEL)
                roePfad = {{-0.3, 0.2, 0.05}, {-0.45, 0.4, 0.05}, {-0.45, 0.6, 0.05}, {-0.3, 0.8, 0.05}};
            else
                roePfad.clear();
            roeHinweisAktualisieren();
            InvalidateRect(roeFlaeche, nullptr, FALSE);
            return 0;
        }
        if (id == ID_ROE_LINEAR)
        {
            const bool an = SendMessageW(roeLinear, BM_GETCHECK, 0, 0) == BST_CHECKED;
            EnableWindow(roeRadAnfang, an);
            EnableWindow(roeRadEnde, an);
            InvalidateRect(roeFlaeche, nullptr, FALSE);
            return 0;
        }
        if (HIWORD(wParam) == EN_CHANGE)
        {
            roeHinweisAktualisieren();
            InvalidateRect(roeFlaeche, nullptr, FALSE);
            return 0;
        }
        break;
    }
    case WM_CLOSE:
        roeOk = false;
        roeFertig = true;
        return 0;
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
    {
        HDC hdc = (HDC)wParam;
        SetTextColor(hdc, GetWindowLongPtrW((HWND)lParam, GWLP_USERDATA) == 1 ? FARBE_UEBERSCHRIFT : FARBE_TEXT);
        SetBkColor(hdc, FARBE_HINTERGRUND);
        return (LRESULT)pinselHintergrund;
    }
    case WM_CTLCOLOREDIT:
    {
        HDC hdc = (HDC)wParam;
        SetTextColor(hdc, FARBE_TEXT);
        SetBkColor(hdc, FARBE_FLAECHE);
        return (LRESULT)pinselFlaeche;
    }
    case WM_DRAWITEM:
        buttonZeichnen(reinterpret_cast<const DRAWITEMSTRUCT *>(lParam));
        return TRUE;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void roehreDialog()
{
    if (!gSzeneAendern)
        return;
    HINSTANCE hInst = GetModuleHandleW(nullptr);
    static bool registriert = false;
    if (!registriert)
    {
        WNDCLASSW wc{};
        wc.lpfnWndProc = RoeDialogProc;
        wc.hInstance = hInst;
        wc.hbrBackground = pinselHintergrund;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.lpszClassName = L"SZENEN_ROEHRE_DIALOG";
        RegisterClassW(&wc);
        WNDCLASSW wf{};
        wf.lpfnWndProc = RoeFlaecheProc;
        wf.hInstance = hInst;
        wf.hCursor = LoadCursor(nullptr, IDC_CROSS);
        wf.lpszClassName = L"SZENEN_ROEHRE_PFAD";
        RegisterClassW(&wf);
        registriert = true;
    }
    const int rand = 14, flaecheB = 560, flaecheH = 320, rechtsB = 280, feldH = 26, labelB = 140;
    const int breite = rand + flaecheB + rand + rechtsB + rand;
    const int innenH = rand + std::max(flaecheH, 540) + 10 + BTN_H + rand;
    DWORD stil = WS_POPUP | WS_CAPTION | WS_SYSMENU;
    RECT r = {0, 0, breite, innenH};
    AdjustWindowRectEx(&r, stil, FALSE, WS_EX_DLGMODALFRAME);
    const int w = r.right - r.left, h = r.bottom - r.top;
    RECT bezug;
    GetWindowRect(hHauptfenster ? hHauptfenster : hToolWindow, &bezug);
    int x = (bezug.left + bezug.right) / 2 - w / 2, y = (bezug.top + bezug.bottom) / 2 - h / 2;
    HWND dlg = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_TOPMOST, L"SZENEN_ROEHRE_DIALOG", L"Röhre entlang Punkten", stil, x, y,
                               w, h, hToolWindow, nullptr, hInst, nullptr);
    if (!dlg)
        return;
    auto schrift = [&](HWND hw) { SendMessageW(hw, WM_SETFONT, (WPARAM)gFont, TRUE); return hw; };
    const int innenFlaecheH = innenH - 2 * rand - 10 - BTN_H;
    roeFlaeche = CreateWindowExW(0, L"SZENEN_ROEHRE_PFAD", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP, rand, rand, flaecheB,
                                 innenFlaecheH, dlg, nullptr, hInst, nullptr);
    tooltip(roeFlaeche, L"Pfad der Röhre in der Seitenansicht (X nach rechts, Y nach oben). Linksklick = Punkt, Ziehen = "
                        L"verschieben,\nRechtsklick = löschen, Mausrad über einem Punkt = dicker/dünner (Shift: alle Punkte).");
    const int rx = rand + flaecheB + rand;
    int ry = rand;
    auto feld = [&](const wchar_t *text, const std::wstring &wert, int id, const wchar_t *tipp)
    {
        schrift(CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE, rx, ry, labelB, feldH, dlg, nullptr, hInst, nullptr));
        HWND e = schrift(CreateWindowExW(0, L"EDIT", wert.c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_BORDER | ES_RIGHT | ES_AUTOHSCROLL,
                                         rx + labelB, ry, rechtsB - labelB, feldH, dlg, (HMENU)(INT_PTR)id, hInst, nullptr));
        tooltip(e, tipp);
        ry += feldH + 6;
        return e;
    };
    roeGroesse = feld(L"Größe (Einheiten)", zahlText(passendeGroesse(), 2), ID_ROE_GROESSE,
                      L"Höhe des Rasters (0 bis 1) in Einheiten der Szene; X und Dicke im gleichen Maßstab");
    roeSegmente = feld(L"Segmente rundherum", std::to_wstring(roeSegmentWert), ID_ROE_SEGMENTE,
                       L"Ecken des Querschnitts (3 bis 128): 4 = kantig, 16 bis 32 = rund");
    roeGlaettung = feld(L"Glättung des Pfads", std::to_wstring(roeGlaettungWert), ID_ROE_GLAETTUNG,
                        L"Zwischenschritte je Strecke (1 bis 32): 1 = gerade Stücke mit Knicken, 6 bis 10 = weiche Kurve");
    roeGlatt = schrift(CreateWindowExW(0, L"BUTTON", L"Glatt schattieren", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, rx, ry,
                                       rechtsB, feldH, dlg, (HMENU)(INT_PTR)ID_ROE_GLATT, hInst, nullptr));
    SetWindowTheme(roeGlatt, L"", L"");
    SendMessageW(roeGlatt, BM_SETCHECK, roeGlattWert ? BST_CHECKED : BST_UNCHECKED, 0);
    ry += feldH + 2;
    roeKappen = schrift(CreateWindowExW(0, L"BUTTON", L"Enden rund schließen", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, rx,
                                        ry, rechtsB, feldH, dlg, (HMENU)(INT_PTR)ID_ROE_KAPPEN, hInst, nullptr));
    SetWindowTheme(roeKappen, L"", L"");
    SendMessageW(roeKappen, BM_SETCHECK, roeKappenWert ? BST_CHECKED : BST_UNCHECKED, 0);
    tooltip(roeKappen, L"An: runde Kuppen an beiden Enden (geschlossener Körper, z.B. für Objekte addieren). Aus: offenes Rohr.");
    ry += feldH + 2;
    // Radius gleichmaessig vom Anfang zum Ende
    roeLinear = schrift(CreateWindowExW(0, L"BUTTON", L"Radius linear: Anfang → Ende", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                                        rx, ry, rechtsB, feldH, dlg, (HMENU)(INT_PTR)ID_ROE_LINEAR, hInst, nullptr));
    SetWindowTheme(roeLinear, L"", L"");
    SendMessageW(roeLinear, BM_SETCHECK, roeLinearWert ? BST_CHECKED : BST_UNCHECKED, 0);
    tooltip(roeLinear, L"An: die Dicke nimmt entlang des Pfads gleichmäßig vom Anfangs- zum Endradius zu bzw. ab\n"
                       L"(grüner Punkt = Anfang). Das Mausrad ändert die Dicke dann nicht mehr.\n"
                       L"Aus: jeder Punkt hat seine eigene Dicke (Mausrad über dem Punkt).");
    ry += feldH + 4;
    {
        const double pg = passendeGroesse();
        if (roeRadAnfangWert < 0)
            roeRadAnfangWert = 0.06 * pg;
        if (roeRadEndeWert < 0)
            roeRadEndeWert = 0.02 * pg;
    }
    roeRadAnfang = feld(L"Radius Anfang", zahlText(roeRadAnfangWert, 4), ID_ROE_RAD_ANFANG,
                        L"Radius am Anfang der Röhre (grüner Punkt) in Einheiten der Szene");
    roeRadEnde = feld(L"Radius Ende", zahlText(roeRadEndeWert, 4), ID_ROE_RAD_ENDE,
                      L"Radius am Ende der Röhre in Einheiten der Szene; dazwischen gleichmäßig");
    EnableWindow(roeRadAnfang, roeLinearWert);
    EnableWindow(roeRadEnde, roeLinearWert);
    ry += 4;
    const int drittel = (rechtsB - 2 * GAP) / 3;
    HWND b1 = schrift(CreateWindowExW(0, L"BUTTON", L"Schwanz", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, rx, ry, drittel,
                                      BTN_H, dlg, (HMENU)(INT_PTR)ID_ROE_BEISPIEL_SCHWANZ, hInst, nullptr));
    HWND b2 = schrift(CreateWindowExW(0, L"BUTTON", L"Henkel", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, rx + drittel + GAP, ry,
                                      drittel, BTN_H, dlg, (HMENU)(INT_PTR)ID_ROE_BEISPIEL_HENKEL, hInst, nullptr));
    HWND b3 = schrift(CreateWindowExW(0, L"BUTTON", L"Leeren", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, rx + 2 * (drittel + GAP),
                                      ry, rechtsB - 2 * (drittel + GAP), BTN_H, dlg, (HMENU)(INT_PTR)ID_ROE_LEEREN, hInst, nullptr));
    tooltip(b1, L"Beispiel: Katzenschwanz (nach oben gebogen, zur Spitze dünner)");
    tooltip(b2, L"Beispiel: Henkel einer Tasse (Bogen)");
    tooltip(b3, L"Alle Punkte entfernen");
    ry += BTN_H + 12;
    roeHinweis = schrift(CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE, rx, ry, rechtsB, innenFlaecheH - (ry - rand), dlg,
                                         nullptr, hInst, nullptr));
    SetWindowLongPtrW(roeHinweis, GWLP_USERDATA, 1);
    const int knopfB = 120, ky = innenH - rand - BTN_H;
    HWND ok = schrift(CreateWindowExW(0, L"BUTTON", L"Erstellen", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                                      breite - rand - 2 * knopfB - GAP, ky, knopfB, BTN_H, dlg, (HMENU)(INT_PTR)IDOK, hInst, nullptr));
    schrift(CreateWindowExW(0, L"BUTTON", L"Abbrechen", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, breite - rand - knopfB, ky,
                            knopfB, BTN_H, dlg, (HMENU)(INT_PTR)IDCANCEL, hInst, nullptr));
    tooltip(ok, L"Röhre erzeugen und am Einfügepunkt (Kamera-Ziel) in die Szene setzen");
    roeHinweisAktualisieren();

    EnableWindow(hToolWindow, FALSE);
    EnableWindow(hObjektFenster, FALSE);
    ShowWindow(dlg, SW_SHOW);
    SetForegroundWindow(dlg);
    double groesse = passendeGroesse();
    bool ergebnis = false;
    for (;;)
    {
        roeFertig = false;
        roeOk = false;
        MSG m;
        while (!roeFertig && GetMessageW(&m, nullptr, 0, 0) > 0)
            if (!IsDialogMessageW(dlg, &m))
            {
                TranslateMessage(&m);
                DispatchMessageW(&m);
            }
        if (!roeOk)
            break;
        double sv = 0, gv = 0, lv = 0;
        if (roePfad.size() < 2)
        {
            MessageBoxW(dlg, L"Bitte mindestens zwei Punkte setzen.", L"Röhre", MB_OK | MB_ICONINFORMATION);
            continue;
        }
        if (!zahlLesen(roeSegmente, sv) || sv < 3 || sv > 128)
        {
            MessageBeep(MB_ICONWARNING);
            SetFocus(roeSegmente);
            continue;
        }
        if (!zahlLesen(roeGlaettung, lv) || lv < 1 || lv > 32)
        {
            MessageBeep(MB_ICONWARNING);
            SetFocus(roeGlaettung);
            continue;
        }
        if (!zahlLesen(roeGroesse, gv) || !(gv > 0))
        {
            MessageBeep(MB_ICONWARNING);
            SetFocus(roeGroesse);
            continue;
        }
        if (SendMessageW(roeLinear, BM_GETCHECK, 0, 0) == BST_CHECKED)
        {
            double ra = 0, re = 0;
            if (!zahlLesen(roeRadAnfang, ra) || ra <= 0)
            {
                MessageBeep(MB_ICONWARNING);
                SetFocus(roeRadAnfang);
                continue;
            }
            if (!zahlLesen(roeRadEnde, re) || re <= 0)
            {
                MessageBeep(MB_ICONWARNING);
                SetFocus(roeRadEnde);
                continue;
            }
            roeRadAnfangWert = ra;
            roeRadEndeWert = re;
            roeLinearAnwenden();
        }
        roeSegmentWert = int(std::round(sv));
        roeGlaettungWert = int(std::round(lv));
        groesse = gv;
        ergebnis = true;
        break;
    }
    roeGlattWert = SendMessageW(roeGlatt, BM_GETCHECK, 0, 0) == BST_CHECKED;
    roeKappenWert = SendMessageW(roeKappen, BM_GETCHECK, 0, 0) == BST_CHECKED;
    roeLinearWert = SendMessageW(roeLinear, BM_GETCHECK, 0, 0) == BST_CHECKED;
    EnableWindow(hToolWindow, TRUE);
    EnableWindow(hObjektFenster, TRUE);
    DestroyWindow(dlg);
    roeFlaeche = roeSegmente = roeGroesse = roeGlatt = roeKappen = roeGlaettung = roeHinweis = nullptr;
    roeLinear = roeRadAnfang = roeRadEnde = nullptr;
    if (!ergebnis)
        return;
    const Vector3D p = einfuegePunkt();
    std::vector<koerper::PfadPunkt> pfad;
    for (const RoePunkt &q : roePfad)
        pfad.push_back({p + Vector3D(q.x * groesse, q.y * groesse, 0), q.r * groesse});
    std::vector<Dreieck> d = koerper::roehre(pfad, roeSegmentWert, roeGlaettungWert, roeGlattWert, roeKappenWert,
                                             makeMaterialSimple(Farbe(0.7, 0.7, 0.7)));
    if (d.empty())
        return;
    std::vector<std::unique_ptr<object>> objekte;
    objekte.push_back(std::make_unique<Mesh>(d, Vector3D(0, 0, 0)));
    neueObjekteEinfuegen(std::move(objekte), "Röhre");
}

static void neuMenue()
{
    HMENU menue = CreatePopupMenu();
    AppendMenuW(menue, MF_STRING, NEU_WUERFEL, L"Würfel");
    AppendMenuW(menue, MF_STRING, NEU_KUGEL, L"Kugel");
    AppendMenuW(menue, MF_STRING, NEU_EBENE, L"Ebene (Boden)");
    AppendMenuW(menue, MF_STRING, NEU_WAND, L"Wand");
    AppendMenuW(menue, MF_STRING, NEU_ZYLINDER, L"Stab (Zylinder, schnell)");
    AppendMenuW(menue, MF_STRING, NEU_PFEIL, L"Pfeil");
    AppendMenuW(menue, MF_STRING, NEU_TEXT, L"3D-Text");

    HMENU material = CreatePopupMenu();
    AppendMenuW(material, MF_STRING, NEU_KUGEL_SPIEGEL, L"Spiegel");
    AppendMenuW(material, MF_STRING, NEU_KUGEL_GOLD, L"Gold");
    AppendMenuW(material, MF_STRING, NEU_KUGEL_GLAS, L"Glas");
    AppendMenuW(material, MF_STRING, NEU_KUGEL_GRUENGLAS, L"Grünes Glas");
    AppendMenuW(material, MF_STRING, NEU_KUGEL_PLASTIK, L"Blaues Plastik");
    AppendMenuW(menue, MF_POPUP, reinterpret_cast<UINT_PTR>(material), L"Kugel mit Material");

    HMENU koerper = CreatePopupMenu();
    AppendMenuW(koerper, MF_STRING, NEU_ZYLINDER_NETZ, L"Zylinder …");
    AppendMenuW(koerper, MF_STRING, NEU_KEGEL, L"Kegel …");
    AppendMenuW(koerper, MF_STRING, NEU_PYRAMIDE, L"Pyramide …");
    AppendMenuW(koerper, MF_STRING, NEU_TETRAEDER, L"Tetraeder …");
    AppendMenuW(koerper, MF_STRING, NEU_OKTAEDER, L"Oktaeder …");
    AppendMenuW(koerper, MF_STRING, NEU_KAPSEL, L"Kapsel …");
    AppendMenuW(koerper, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(koerper, MF_STRING, NEU_TORUS, L"Torus …");
    AppendMenuW(koerper, MF_STRING, NEU_RING3D, L"3D-Ring …");
    AppendMenuW(koerper, MF_STRING, NEU_ROHR, L"Rohr …");
    AppendMenuW(koerper, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(koerper, MF_STRING, NEU_KREISFLAECHE, L"Kreisfläche …");
    AppendMenuW(koerper, MF_STRING, NEU_RING2D, L"2D-Ring …");
    AppendMenuW(menue, MF_POPUP, reinterpret_cast<UINT_PTR>(koerper), L"Weitere Körper");

    AppendMenuW(menue, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menue, MF_STRING, NEU_BAUM, L"Baum …");
    AppendMenuW(menue, MF_STRING, NEU_TERRAIN, L"Terrain …");
    AppendMenuW(menue, MF_STRING, NEU_WASSER, L"Wasser …");
    AppendMenuW(menue, MF_STRING, NEU_WOLKE, L"Wolke aus Kugeln …");
    AppendMenuW(menue, MF_STRING, NEU_ROTATION, L"Rotationsobjekt …");
    AppendMenuW(menue, MF_STRING, NEU_ELLIPSOID, L"Ellipsoid …");
    AppendMenuW(menue, MF_STRING, NEU_ROEHRE, L"Röhre entlang Punkten …");
    AppendMenuW(menue, MF_STRING, NEU_STEIN, L"Stein …");
    AppendMenuW(menue, MF_STRING, NEU_FIGUR, L"Figur … (rennt, geht, winkt)");
    AppendMenuW(menue, MF_STRING, NEU_TIER, L"Tier … (Katze, Hund, Maus)");
    AppendMenuW(menue, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menue, MF_STRING, NEU_KOORDINATEN, L"Koordinatensystem …");
    AppendMenuW(menue, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menue, MF_STRING, NEU_PUNKTLICHT, L"Punktlicht");
    AppendMenuW(menue, MF_STRING, NEU_KUGELLICHT, L"Kugellicht");
    AppendMenuW(menue, MF_STRING, NEU_SPOTLICHT, L"Spotlicht (Lichtkegel)");
    AppendMenuW(menue, MF_STRING, NEU_RICHTUNGSLICHT, L"Richtungslicht");
    AppendMenuW(menue, MF_STRING, NEU_FLAECHENLICHT, L"Flächenlicht (Deckenlampe)");
    AppendMenuW(menue, MF_STRING, NEU_MESHLICHT, L"Mesh-Licht (leuchtender Würfel)");

    // Belichtungs-Vorlagen: Himmel, Belichtung, Nebel und Lichter fuer eine Stimmung
    HMENU stimmung = CreatePopupMenu();
    for (int i = 0; i < ANZAHL_SZENEN_VORLAGEN; ++i)
    {
        std::wstring t = std::wstring(SZENEN_VORLAGEN[i].name) + L"\t" + SZENEN_VORLAGEN[i].tipp;
        AppendMenuW(stimmung, MF_STRING, ID_SZENEN_VORLAGE + i, t.c_str());
        if (i == 0 || i == 6 || i == 10)
            AppendMenuW(stimmung, MF_SEPARATOR, 0, nullptr);
    }
    AppendMenuW(menue, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menue, MF_POPUP, reinterpret_cast<UINT_PTR>(stimmung), L"Belichtung / Stimmung (Szenen-Vorlage)");

    RECT r;
    GetWindowRect(g.btnNeu, &r);
    int auswahl = int(TrackPopupMenu(menue, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN | TPM_NONOTIFY,
                                     r.left, r.bottom, 0, hObjektFenster, nullptr));
    DestroyMenu(menue);

    if (auswahl == NEU_ROTATION)
        rotationsobjektDialog();
    else if (auswahl == NEU_ROEHRE)
        roehreDialog();
    else if (auswahl >= ID_SZENEN_VORLAGE && auswahl < ID_SZENEN_VORLAGE + ANZAHL_SZENEN_VORLAGEN)
        szenenVorlageAnwenden(auswahl - ID_SZENEN_VORLAGE);
    else if (auswahl)
        einfuegen(auswahl);
}

//--------------------------------------------------
// Ausgewaehltes Objekt oder Licht loeschen (Button, Entf in Liste und Ansichten)
//--------------------------------------------------

void AusgewaehltesObjektLoeschen()
{
    std::vector<object *> alle = ausgewaehlteObjekte();

    // Rauchboxen/Wolkenbereich: die Box in den Render-Einstellungen loeschen bzw. Wolken ausschalten
    {
        std::vector<int> rauch;
        bool wolke = false;
        std::vector<object *> rest;
        for (object *o : alle)
            if (auto *vb = dynamic_cast<VolumenBox *>(o))
                vb->istWolke() ? (void)(wolke = true) : rauch.push_back(vb->rauchIndex);
            else
                rest.push_back(o);
        if ((!rauch.empty() || wolke) && gSzeneAendern)
        {
            std::wstring frage = !rauch.empty() && wolke ? L"Rauchbox(en) löschen und Wolkenvolumen ausschalten?"
                                 : wolke               ? L"Wolkenvolumen ausschalten?"
                                                       : L"Rauchbox löschen?";
            if (MessageBoxW(GetForegroundWindow(), frage.c_str(), L"Löschen", MB_YESNO | MB_ICONQUESTION) != IDYES)
                return;
            auswahlSetzen({});
            std::sort(rauch.rbegin(), rauch.rend());
            gSzeneAendern([&]
                          {
                for (int i : rauch)
                    if (i >= 0 && i < int(smokeBoxes.size()))
                        smokeBoxes.erase(smokeBoxes.begin() + i);
                if (wolke)
                    wolkeVolumeVorhanden = false;
                volumenBoxenAbgleichen(); });
            szeneGeaendertMarkieren();
            einstellungenAnzeigen();
            if (rest.empty())
            {
                abgleichen();
                return;
            }
            auswahlSetzen(rest);
            alle = rest;
        }
    }
    int objekt = selectedType == SelectedType::OBJECT ? objektIndex(selectedObject) : -1;
    int licht = selectedType == SelectedType::LIGHT ? lichtIndex(selectedLight) : -1;

    if ((alle.empty() && licht < 0) || !gSzeneAendern)
    {
        MessageBeep(MB_ICONWARNING);
        return;
    }
    // Teile einer Gruppe lassen sich einzeln loeschen (die Gruppe bleibt); ist ihre Gruppe ebenfalls gewaehlt,
    // geht das Teil ohnehin mit ihr
    {
        std::vector<object *> rest;
        for (object *o : alle)
        {
            bool mitGruppe = false;
            for (Gruppe *g = gruppeVon(o); g; g = gruppeVon(g))
                mitGruppe = mitGruppe || std::find(alle.begin(), alle.end(), g) != alle.end();
            if (!mitGruppe)
                rest.push_back(o);
        }
        alle = rest;
    }
    const bool teil = alle.size() == 1 && gruppeVon(alle[0]);
    std::wstring name = alle.size() > 1 ? std::to_wstring(alle.size()) + L" Objekte"
                        : teil          ? L"„" + objektGrundname(alle[0], 0) + L"“ (aus der Gruppe „" +
                                     objektGrundname(gruppeVon(alle[0]), 0) + L"“)"
                        : objekt >= 0   ? L"„" + objektGrundname(selectedObject, size_t(objekt)) + L"“"
                                        : L"„" + lichtName(selectedLight, size_t(licht)) + L"“";
    std::wstring frage = name + L" aus der Szene löschen?\n(Rückgängig mit Strg+Z)";

    if (MessageBoxW(GetForegroundWindow(), frage.c_str(), licht >= 0 ? L"Licht löschen" : L"Objekt löschen",
                    MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES)
        return;

    // Objekte bleiben im Verlauf erhalten (Rueckgaengig)
    if (licht >= 0)
        loeschenMitVerlauf("Licht löschen", {}, {selectedLight});
    else
        loeschenMitVerlauf("Löschen", alle);
    abgleichen();
}

//--------------------------------------------------
// Kopieren, Einfuegen, Duplizieren, alles auswaehlen, Rueckgaengig
//--------------------------------------------------

static std::vector<std::unique_ptr<object>> ablageObjekte; // Zwischenablage (Kopien)
static std::vector<std::unique_ptr<light>> ablageLichter;
static int einfuegeZaehler = 0; // jedes Einfuegen ein Stueck weiter versetzt

// "Würfel 3" -> naechster freier Name "Würfel N"; Text bleibt (Name = Text)
static void kopieBenennen(object *k)
{
    if (dynamic_cast<Text3D *>(k) || k->name.empty())
        return;
    std::string basis = k->name;
    size_t ende = basis.find_last_not_of("0123456789");
    if (ende != std::string::npos && ende + 1 < basis.size() && basis[ende] == ' ')
        basis = basis.substr(0, ende);
    k->name = freierName(basis);
}

// Kopien in die Szene legen, versetzt; werden ausgewaehlt und sind rueckgaengig machbar
static void kopienEinfuegen(std::vector<std::unique_ptr<object>> objekte,
                            std::vector<std::unique_ptr<light>> lichter, const Vector3D &versatz,
                            const std::string &name)
{
    if ((objekte.empty() && lichter.empty()) || !gSzeneAendern)
    {
        MessageBeep(MB_ICONWARNING);
        return;
    }

    std::vector<object *> neueObjekte;
    std::vector<light *> neueLichter;
    for (auto &o : objekte)
    {
        o->translate(versatz);
        kopieBenennen(o.get());
        neueObjekte.push_back(o.get());
    }
    for (auto &l : lichter)
    {
        l->translate(-versatz); // translate() der Lichter verschiebt um -delta
        neueLichter.push_back(l.get());
    }

    gSzeneAendern([&]
                  {
        for (auto &o : objekte)
            scene.push_back(std::move(o));
        for (auto &l : lichter)
            lights.push_back(std::move(l)); });
    eingefuegtMerken(name, neueObjekte, neueLichter);

    if (!neueObjekte.empty())
        auswahlSetzen(neueObjekte);
    else
    {
        auswahlSetzen({});
        selectedLight = neueLichter.front();
        selectedType = SelectedType::LIGHT;
    }
    abgleichen();
}

static Vector3D kopieVersatz(int n)
{
    double s = schritt(0);
    return Vector3D(s, 0, s) * double(n);
}

// Kopien der Auswahl (Objekte bzw. das gewaehlte Licht); nicht kopierbare Typen fehlen
static void auswahlKopien(std::vector<std::unique_ptr<object>> &objekte, std::vector<std::unique_ptr<light>> &lichter)
{
    for (object *o : ausgewaehlteObjekte())
        if (auto k = objektKopieren(o))
            objekte.push_back(std::move(k));
    if (selectedType == SelectedType::LIGHT && selectedLight && lichtIndex(selectedLight) >= 0)
        if (auto k = lichtKopieren(selectedLight))
            lichter.push_back(std::move(k));
}

void AuswahlDuplizieren()
{
    std::vector<std::unique_ptr<object>> objekte;
    std::vector<std::unique_ptr<light>> lichter;
    auswahlKopien(objekte, lichter);
    kopienEinfuegen(std::move(objekte), std::move(lichter), kopieVersatz(1), "Duplizieren");
}

// Auswahl in Instanzen umwandeln (eine Kopie wird jeweils die Vorlage; Lage bleibt) - ein Schritt im Verlauf.
// Liefert alle ausgewaehlten Instanzen (schon vorhandene und neue); sie sind danach ausgewaehlt.
static std::vector<object *> auswahlAlsInstanzen()
{
    std::vector<object *> instanzen;
    std::vector<std::pair<object *, std::unique_ptr<object>>> paare;
    for (object *o : ausgewaehlteObjekte())
    {
        if (nurInstanzen(o)) // Instanz oder Gruppe, deren Teile schon Instanzen sind
            instanzen.push_back(o);
        else if (auto in = alsInstanz(o))
            paare.push_back({o, std::move(in)});
    }
    if (!paare.empty())
    {
        std::vector<object *> neu = ersetzenMehrereMitVerlauf("In Instanz umwandeln", std::move(paare));
        instanzen.insert(instanzen.end(), neu.begin(), neu.end());
    }
    if (!instanzen.empty())
        auswahlSetzen(instanzen);
    return instanzen;
}

// Strg+Umschalt+D: Instanz erstellen (die Auswahl wird zur Instanz, daneben eine weitere Instanz)
void AuswahlInstanzErstellen()
{
    if (!gSzeneAendern || ausgewaehlteObjekte().empty())
    {
        MessageBeep(MB_ICONWARNING);
        return;
    }
    std::vector<object *> instanzen = auswahlAlsInstanzen();
    std::vector<std::unique_ptr<object>> objekte;
    for (object *o : instanzen)
        if (auto k = objektKopieren(o))
            objekte.push_back(std::move(k));
    kopienEinfuegen(std::move(objekte), {}, kopieVersatz(1), "Instanz erstellen");
}

// Instanz loesen: eigenstaendige Kopie der Vorlage an ihrer Stelle (danach einzeln verformbar)
void AuswahlInstanzLoesen()
{
    std::vector<std::pair<object *, std::unique_ptr<object>>> paare;
    for (object *o : ausgewaehlteObjekte())
        if (auto k = instanzAufloesen(o))
            paare.push_back({o, std::move(k)});
    if (paare.empty() || !gSzeneAendern)
    {
        MessageBeep(MB_ICONWARNING);
        return;
    }
    ersetzenMehrereMitVerlauf("Instanz lösen", std::move(paare));
    abgleichen();
}

void AuswahlKopieren()
{
    ablageObjekte.clear();
    ablageLichter.clear();
    auswahlKopien(ablageObjekte, ablageLichter);
    einfuegeZaehler = 0;
    if (ablageObjekte.empty() && ablageLichter.empty())
        MessageBeep(MB_ICONWARNING);
}

void ZwischenablageEinfuegen()
{
    // jedes Mal neue Kopien der Ablage, damit man mehrfach einfuegen kann
    std::vector<std::unique_ptr<object>> objekte;
    std::vector<std::unique_ptr<light>> lichter;
    for (auto &o : ablageObjekte)
        if (auto k = objektKopieren(o.get()))
            objekte.push_back(std::move(k));
    for (auto &l : ablageLichter)
        if (auto k = lichtKopieren(l.get()))
            lichter.push_back(std::move(k));

    ++einfuegeZaehler;
    kopienEinfuegen(std::move(objekte), std::move(lichter), kopieVersatz(einfuegeZaehler), "Einfügen");
}

void AuswahlGruppieren()
{
    std::vector<object *> alle;
    for (object *o : ausgewaehlteObjekte())
        if (!gruppeVon(o) && !dynamic_cast<VolumenBox *>(o)) // Rauchbox/Wolken bleiben eigenstaendig
            alle.push_back(o);
    if (alle.size() < 2)
    {
        MessageBeep(MB_ICONWARNING); // mindestens zwei Objekte (Strg+Klick oder Rahmen)
        return;
    }
    Gruppe *g = gruppieren(alle, freierName("Gruppe"));
    abgleichen();
    // neue Gruppe zugeklappt zeigen (beim Neuaufbau der Liste war noch ein Teil gewaehlt - das klappte sie auf)
    if (HTREEITEM e = g ? eintragVon(g) : nullptr)
    {
        auswahlWirdGesetzt = true;
        TreeView_Expand(hTreeObjects, e, TVE_COLLAPSE);
        TreeView_SelectItem(hTreeObjects, e);
        auswahlWirdGesetzt = false;
    }
}

void GruppeAufheben()
{
    bool getan = false;
    for (object *o : ausgewaehlteObjekte())
        if (auto *g = dynamic_cast<Gruppe *>(o))
            getan = gruppeAufheben(g) || getan;
    if (!getan)
        MessageBeep(MB_ICONWARNING);
    abgleichen();
}

void BaumAufteilen()
{
    object *o = selectedType == SelectedType::OBJECT ? selectedObject : nullptr;
    if (!o || !dynamic_cast<Tree *>(o) || gruppeVon(o))
    {
        MessageBeep(MB_ICONWARNING);
        return;
    }
    std::unique_ptr<object> teile = baumZerlegen(o);
    if (!teile)
        return;
    teile->name = o->name.empty() ? freierName("Baum") : o->name;
    ersetzenMitVerlauf("Baum aufteilen", o, std::move(teile));
    abgleichen();
}

void AllesAuswaehlen()
{
    std::vector<object *> alle;
    for (auto &o : scene)
        alle.push_back(o.get());
    auswahlSetzen(alle);
    abgleichen();
}

void Rueckgaengig()
{
    if (!rueckgaengig())
        MessageBeep(MB_ICONWARNING);
    abgleichen();
}

void Wiederholen()
{
    if (!wiederholen())
        MessageBeep(MB_ICONWARNING);
    abgleichen();
}
