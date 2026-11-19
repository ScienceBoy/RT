#include "fell.h"
#include <windows.h>
#include "fenster.h"
#include "wireframe_top.h"
#include "wireframe_front.h"
#include "wireframe_side.h"
#include "wireframe_perspective.h"
#include "wireframe.h"
#include "raster_placement.h"
#include "render.h"
#include "scene.h"
#include "preprocessing.h"
#include "cube.h"
#include "kugel.h"
#include "wand.h"
#include "threading.h"
// #include "wireframe_util.h"
#include "obj_import_dialog.h"
#include <thread>
#include <fstream>
#include <cstdio>
#include <vector>
#include <memory>
#include <iostream>
#include <mutex>
#include <algorithm>
#include <cmath>
#include "SceneSaver.h"
#include "SceneLoader.h"
#include "obj_export.h"
#include <commdlg.h>
#include "object.h"
#include <iostream>
#include "mesh.h"
#include "fenster.h"
#include "toolwindow.h"
#include "drehung.h"
#include "bearbeitung.h"
#include "animation.h"
#include "kamera_bahn.h"
#include "render_einstellungen.h"
#include <climits>
#include "gpu_ansichten.h"

// true: die vier Ansichten werden auf der GPU gezeichnet (GpuAnsichten)
// false: bisheriger CPU-Rasterizer in Wireframe::run()
constexpr bool GPU_ANSICHTEN = true;

extern "C"
{
    __declspec(dllexport)
    DWORD NvOptimusEnablement = 0x00000001;
}

object *selectedObject = nullptr;
light *selectedLight = nullptr;
SelectedType selectedType = SelectedType::NONE;
SelectedAction selectedAction = SelectedAction::MOVE;

std::wstring askFilename(HWND hwnd);

// Datei-Dialog zum Speichern oder Oeffnen; Ergebnis im ANSI-Zeichensatz
// wie von std::ifstream/ofstream erwartet. Leer bei Abbruch.
static std::string dateiWaehlen(HWND owner, bool speichern, const wchar_t *filter, const wchar_t *endung)
{
    wchar_t datei[MAX_PATH] = L"";

    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = datei;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = endung;
    // NOCHANGEDIR: relative Pfade (Texturen, OBJ-Dateien) beziehen sich weiter aufs Programmverzeichnis
    ofn.Flags = OFN_EXPLORER | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR |
                (speichern ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);

    if (!(speichern ? GetSaveFileNameW(&ofn) : GetOpenFileNameW(&ofn)))
        return "";

    int laenge = WideCharToMultiByte(CP_ACP, 0, datei, -1, nullptr, 0, nullptr, nullptr);
    if (laenge <= 1)
        return "";
    std::string ergebnis(size_t(laenge - 1), '\0');
    WideCharToMultiByte(CP_ACP, 0, datei, -1, &ergebnis[0], laenge, nullptr, nullptr);
    return ergebnis;
}

static std::string xmlDateiWaehlen(HWND owner, bool speichern)
{
    return dateiWaehlen(owner, speichern, L"Szenen (*.xml)\0*.xml\0Alle Dateien (*.*)\0*.*\0", L"xml");
}
// void saveWindowBMPWithTimestamp(HWND hwnd, const wchar_t* filename);
void moveWinRayToSecondMonitor(Fenster &winRay);
// static bool wasPressed = false;
int selectionIndex = -1;

// std::vector<std::unique_ptr<object>> scene;
// std::vector<Camera> cam;

bool FensterDirty = false;
extern std::atomic<bool> stopRendering;                                                   // threading.cpp
void startRenderingBereich(Fenster &f, int bx0, int by0, int bx1, int by1, bool ganzesBild); // threading.cpp
// Autom. Rendern: ein ganzes Bild (Taste Y) ist begonnen bzw. beendet (fertig = nicht angehalten)
static std::atomic<bool> gGanzesBildUnfertig{true};
static void ganzesBildBegonnen() { gGanzesBildUnfertig = true; }
static void ganzesBildBeendet(bool fertig) { gGanzesBildUnfertig = !fertig; }
std::atomic<int> gRenderLaeufe{0}; // siehe toolwindow.h
static std::atomic<bool> animationAbbrechen{false}; // Stop (X), Szene geaendert/geladen

// ffmpeg fuer das Video der Animation; fehlt die Datei, wird ffmpeg.exe im PATH gesucht
static const wchar_t *FFMPEG_PFAD = L"C:\\Users\\JumpStart\\Projects\\ffmpeg-essentials_build\\bin\\ffmpeg.exe";

// Bilder basis_0001.bmp ... mit ffmpeg zu basis.mp4 (H.264); liefert eine Meldung fuer den Benutzer.
// Ausgaben von ffmpeg landen in basis_ffmpeg.txt.
static std::wstring videoErstellen(const std::wstring &basis, int fps, bool &ok)
{
    ok = false;
    std::wstring programm = FFMPEG_PFAD;
    if (GetFileAttributesW(programm.c_str()) == INVALID_FILE_ATTRIBUTES)
        programm = L"ffmpeg.exe"; // vielleicht im PATH

    // % ist im Muster von ffmpeg ein Platzhalter
    std::wstring muster;
    for (wchar_t c : basis)
        muster += (c == L'%') ? std::wstring(L"%%") : std::wstring(1, c);
    muster += L"_%04d.bmp";
    const std::wstring video = basis + L".mp4";
    const std::wstring protokoll = basis + L"_ffmpeg.txt";

    // pad: H.264/yuv420p braucht gerade Breite und Hoehe (z.B. 1009 Zeilen bei maximiertem Fenster)
    std::wstring befehl = L"\"" + programm + L"\" -y -hide_banner -loglevel warning -framerate " + std::to_wstring(fps) +
                          L" -i \"" + muster + L"\" -vf \"pad=ceil(iw/2)*2:ceil(ih/2)*2\" -c:v libx264 -pix_fmt yuv420p -crf 18 \"" +
                          video + L"\"";

    SECURITY_ATTRIBUTES sa = {sizeof(sa), nullptr, TRUE};
    HANDLE log = CreateFileW(protokoll.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);

    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    if (log != INVALID_HANDLE_VALUE)
    {
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdOutput = log;
        si.hStdError = log;
        si.hStdInput = nullptr;
    }
    PROCESS_INFORMATION pi = {};
    std::vector<wchar_t> zeile(befehl.begin(), befehl.end());
    zeile.push_back(0);
    BOOL gestartet = CreateProcessW(nullptr, zeile.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    if (!gestartet)
    {
        if (log != INVALID_HANDLE_VALUE)
            CloseHandle(log);
        return L"ffmpeg konnte nicht gestartet werden:\n" + programm;
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    if (log != INVALID_HANDLE_VALUE)
        CloseHandle(log);

    if (code != 0)
        return L"ffmpeg meldet einen Fehler (Code " + std::to_wstring(code) + L"), siehe\n" + protokoll;
    ok = true;
    DeleteFileW(protokoll.c_str()); // nur Warnungen: nicht noetig
    return L"Video: " + video;
}
extern HWND hToolWindow;
extern std::mutex sceneMutex;

int main()
{
    int debug_break_here = 0; // Breakpoint
    // Rechnet eine Funktion laenger (z.B. Laden einer grossen Szene), ersetzt Windows ein Fenster, das einige Sekunden
    // keine Nachrichten abholt, durch ein "Geisterfenster" ("Keine Rueckmeldung"). Bei Bildschirm-Skalierung (z.B. 150 %)
    // zeichnet Windows dieses Ersatzbild ohne Skalierung - das Programm schrumpfte auf etwa 2/3 und wirkte eingefroren.
    // Ohne Geisterfenster bleibt das Fenster in seiner Groesse stehen; der Fortschritt erscheint im kleinen Fenster.
    DisableProcessWindowsGhosting();
    SetConsoleTitleW(L"3D-Editor und RayTracer");
    // Fenster f(600,400);

    // Vier Fenster erzeugen
    const FensterModus ansichtModus = GPU_ANSICHTEN ? FensterModus::GPU : FensterModus::CPU;
    Fenster winTop(600, 340, ansichtModus);
    Fenster winFront(600, 340, ansichtModus);
    Fenster winSide(600, 340, ansichtModus);
    Fenster winPersp(600, 340, ansichtModus);
    Fenster winRay(1600, 900);

    // Fenster platzieren
    place2x2(winTop, 0, 1, L"TOP VIEW");
    place2x2(winFront, 1, 1, L"FRONT VIEW");
    place2x2(winSide, 1, 0, L"SIDE VIEW");
    place2x2(winPersp, 0, 0, L"PERPEKTIVE");
    CreateToolWindow(GetModuleHandle(nullptr));
    WerkzeugleisteAndocken({winTop.getHWND(), winFront.getHWND(), winSide.getHWND(), winPersp.getHWND()});
    RenderfensterSetzen(winRay.getHWND());
    KamerasichtAnmelden(winPersp.getHWND(), &winRay); // Render-Bild in der Kamerasicht, externer Bildschirm
    winRay.zoomKnopfAnlegen(); // "Eingepasst / 1:1 Originalgroesse"
    AnsichtBeschriftungAnlegen(winTop.getHWND(), L"Top");
    AnsichtBeschriftungAnlegen(winFront.getHWND(), L"Front");
    AnsichtBeschriftungAnlegen(winSide.getHWND(), L"Side");
    AnsichtBeschriftungAnlegen(winPersp.getHWND(), L"Kamerasicht");
    // place2x2(winRay,   0, 0, L"RAYTRACING");

    // Fenster in den Vordergrund holen
    auto bringToFront = [&](Fenster &f)
    {
        // im Hauptfenster: das Render-Bild ist ein Reiter
        if (hHauptfenster && f.getHWND() == winRay.getHWND())
        {
            RenderReiterZeigen(true);
            return;
        }
        HWND h = f.getHWND();
        SetWindowPos(h, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    };

    // ANWENDEN:
    bringToFront(winRay);
    bringToFront(winTop);
    bringToFront(winFront);
    bringToFront(winSide);
    bringToFront(winPersp);
    // SetForegroundWindow(winRay.getHWND());

    initScene(winRay);
    UpdateObjectList();

    OBJImportDialog objDialog(scene);
    std::vector<object *> objectPointers;
    for (auto &obj : scene)
        objectPointers.push_back(obj.get());

    // Wireframes
    WireframeTop wfTop(winTop);
    WireframeFront wfFront(winFront);
    WireframeSide wfSide(winSide);
    WireframePerspective wfPersp(winPersp, &cam[0], objectPointers);

    wfTop.setScene(scene);
    wfFront.setScene(scene);
    wfSide.setScene(scene);
    wfPersp.setScene(scene);

    // Ansichten zeichnen: GPU (ein Renderthread fuer alle vier) oder bisherige CPU-Threads.
    // gpuAnsichten wird vor den Fenstern zerstoert und haelt dabei den Renderthread an.
    std::unique_ptr<GpuAnsichten> gpuAnsichten;

    if (GPU_ANSICHTEN)
    {
        gpuAnsichten = std::make_unique<GpuAnsichten>(std::vector<Wireframe *>{&wfTop, &wfFront, &wfSide, &wfPersp});
        gpuAnsichten->start();
    }
    else
    {
        std::thread t1([&]
                       { wfTop.run(); });
        std::thread t2([&]
                       { wfFront.run(); });
        std::thread t3([&]
                       { wfSide.run(); });
        std::thread t4([&]
                       { wfPersp.run(); });
        // std::thread t4([&]{ preprocessing(winRay); threadsRufenRenderAuf(winRay);});

        t1.detach();
        t2.detach();
        t3.detach();
        t4.detach();
    }

    if (!hHauptfenster) // ohne Hauptfenster wie bisher: Render-Fenster auf den zweiten Monitor
    {
        moveWinRayToSecondMonitor(winRay);
        // Maximieren holt das Rendering-Fenster nach vorne: ganz nach hinten, Ansichten nach vorne
        SetWindowPos(winRay.getHWND(), HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
    // Ansichten liegen im Hauptfenster: dieses nach vorne, Tastatur in die Perspektive
    if (hHauptfenster)
    {
        SetForegroundWindow(hHauptfenster);
        SetFocus(winPersp.getHWND());
    }

    // In die aktuelle Datei speichern (ohne Datei bzw. 'neuerName': mit Dialog); false bei Abbruch/Fehler
    auto speichern = [&](bool neuerName) -> bool
    {
        std::string datei = aktuelleDatei();
        if (neuerName || datei.empty())
        {
            datei = xmlDateiWaehlen(hToolWindow ? hToolWindow : winRay.getHWND(), true);
            if (datei.empty())
                return false;
        }
        try
        {
            SceneSaver::saveSceneToXML(datei);
        }
        catch (const std::exception &e)
        {
            std::string t = e.what();
            std::wstring text = L"Speichern fehlgeschlagen:\n" + std::wstring(t.begin(), t.end());
            MessageBoxW(hToolWindow, text.c_str(), L"Speichern", MB_OK | MB_ICONWARNING);
            return false;
        }
        aktuelleDateiSetzen(datei);
        szeneGespeichertMarkieren();
        return true;
    };

    // Vor Leeren/Laden/Beenden: ungespeicherte Aenderungen sichern? false = abbrechen
    auto aenderungenSichern = [&](const wchar_t *frage) -> bool
    {
        if (!szeneGeaendert())
            return true;
        std::wstring text = std::wstring(L"Die Szene hat ungespeicherte Änderungen.\n") + frage;
        int antwort = MessageBoxW(hToolWindow, text.c_str(), L"Ungespeicherte Änderungen",
                                  MB_YESNOCANCEL | MB_ICONQUESTION);
        if (antwort == IDCANCEL)
            return false;
        if (antwort == IDYES)
            return speichern(false);
        return true;
    };

    // Ortho-Ansichten und Kamera auf den Bereich mn..mx ausrichten.
    // schraeg: Kamera schraeg von oben (vorne rechts), sonst behaelt sie ihre Blickrichtung.
    auto ansichtAusrichten = [&](const Vector3D &mn, const Vector3D &mx, bool schraeg, bool kameraMit = true,
                                 bool orthoMit = true)
    {
        Vector3D mitte = (mn + mx) * 0.5;
        Vector3D ausdehnung = mx - mn;
        double groesste = std::max({ausdehnung.x, ausdehnung.y, ausdehnung.z, 1e-6});

        // Ortho: Bereich fuellt etwa 75 % der kleinsten Ansicht
        if (orthoMit)
        {
            int pixel = INT_MAX;
            for (Wireframe *wf : {(Wireframe *)&wfTop, (Wireframe *)&wfFront, (Wireframe *)&wfSide})
                pixel = std::min(pixel, std::min(wf->w, wf->h));
            scale = std::max(0.001, 0.75 * std::max(pixel, 50) / groesste);
            ansichtsMitteSetzen(mitte);
        }

        if (cam.empty() || !kameraMit)
            return;

        // Perspektive: Abstand so, dass die umschliessende Kugel ganz ins Bild passt.
        // Oeffnungswinkel wie WireframePerspective::ansicht() = Raytracing-Kamera; die schmalere
        // Richtung (hoch oder quer) zaehlt
        double tanHalbY = std::tan(cam[0].fov * M_PI / 360.0);
        double seiten = winPersp.getHWND() && wfPersp.h > 0 ? double(wfPersp.w) / double(wfPersp.h) : 1.0;
        double halbWinkel = std::atan(std::max(tanHalbY * std::min(1.0, seiten), 1e-9));
        double radius = std::max(ausdehnung.length() * 0.5, 1e-6);

        if (!schraeg)
            kameraBahnAbgleichen(cam[0]); // Blickrichtung behalten
        cam[0].target = mitte;
        cam[0].lookAt = mitte;
        cam[0].distance = 1.15 * radius / std::sin(halbWinkel);
        if (schraeg)
        {
            // Kamera auf der -Z-Seite wie die Raytracing-Kamera (yaw 0 waere +Z: Bild links-rechts
            // vertauscht), 35 Grad seitlich und 30 Grad von oben
            cam[0].yaw = (180.0 + 35.0) * M_PI / 180.0;
            cam[0].pitch = 30.0 * M_PI / 180.0;
        }
        cam[0].updateOrbit();
    };

    // Knopf unten rechts in jeder Ansicht: alles ins Bild. Die drei Ortho-Ansichten teilen den Zoom,
    // die Perspektive hat ihren eigenen (Abstand der Kamera; Blickrichtung bleibt)
    auto szenenGrenzen = [&](Vector3D &mn, Vector3D &mx)
    {
        if (scene.empty())
            mn = Vector3D(-1, -1, -1), mx = Vector3D(1, 1, 1);
        else
            mn = sceneMin, mx = sceneMax;
    };
    for (Fenster *fe : {&winTop, &winFront, &winSide})
        EinpassKnopfAnlegen(fe->getHWND(), [&]
                            {
                                Vector3D mn, mx;
                                szenenGrenzen(mn, mx);
                                ansichtAusrichten(mn, mx, false, false); },
                            L"Alle Objekte ins Bild (Draufsicht, Vorderansicht und Seitenansicht zoomen gemeinsam)");
    EinpassKnopfAnlegen(winPersp.getHWND(), [&]
                        {
                            if (cam.empty())
                                return;
                            Vector3D mn, mx;
                            szenenGrenzen(mn, mx);
                            ansichtAusrichten(mn, mx, false, true, false); // nur die Kamera
                        },
                        L"Alle Objekte ins Bild: Kamera rückt auf die Szenenmitte und den passenden Abstand\n"
                        L"(Blickrichtung bleibt; das ist auch das gerenderte Bild)");

    // Massstab links neben dem Einpass-Knopf: Ortho-Ansichten teilen den Zoom (scale = Pixel je Einheit),
    // Kamerasicht: Massstab in der Entfernung des Zielpunkts (naeher = groesser, weiter weg = kleiner)
    for (Fenster *fe : {&winTop, &winFront, &winSide})
        MassstabAnlegen(fe->getHWND(), [&]
                        { return scale > 0 ? 1.0 / scale : 0.0; },
                        L"Maßstab: so lang ist der Balken in Einheiten der Szene (ändert sich mit dem Zoom)");
    MassstabAnlegen(winPersp.getHWND(), [&]
                    {
                        if (cam.empty() || wfPersp.h <= 0)
                            return 0.0;
                        const double abstand = (cam[0].position - cam[0].lookAt).length();
                        return 2.0 * abstand * std::tan(cam[0].fov * M_PI / 360.0) / double(wfPersp.h);
                    },
                    L"Maßstab in der Entfernung des Kamera-Ziels (Mitte des Bilds): davor erscheint alles größer,\n"
                    L"dahinter kleiner – in der Perspektive gilt ein Maßstab nur für eine Entfernung");

    // Szene aus einer XML-Datei laden (Taste L, Fortsetzen einer abgebrochenen Animation); true bei Erfolg
    auto szeneLaden = [&](const std::string &xmlName) -> bool
    {
        std::string fehler;
        gSzeneAendern([&]
                      {
            // Auswahl zeigt sonst auf geloeschte Objekte/Lichter/Kamera
            selectedObject = nullptr;
            selectedLight = nullptr;
            selectedType = SelectedType::NONE;
            selectionIndex = -1;
            for (Wireframe *wf : {(Wireframe *)&wfTop, (Wireframe *)&wfFront, (Wireframe *)&wfSide, (Wireframe *)&wfPersp})
                wf->selectedCamera = nullptr; // Feld je Ansicht (das globale selectedCamera ist nirgends definiert)

            try
            {
                SceneLoader::loadSceneFromXML(xmlName, winRay);
            }
            catch (const std::exception &e)
            {
                fehler = e.what();
            }

            // cam wird neu befuellt: Zeiger der Perspektive auffrischen
            if (!cam.empty())
                wfPersp.camera = &cam[0]; });

        ansichtenNeuZentrieren();

        // die alten Objekte sind zerstoert: Verlauf passt nicht mehr
        // (Snapshots leert und laedt SceneLoader selbst)
        verlaufLeeren();
        szeneGespeichertMarkieren();
        aktuelleDateiSetzen(fehler.empty() ? xmlName : std::string());

        if (!fehler.empty())
        {
            std::wstring text = L"Die Szene konnte nicht geladen werden:\n" +
                                std::wstring(fehler.begin(), fehler.end());
            MessageBoxW(hToolWindow, text.c_str(), L"Load XML", MB_OK | MB_ICONWARNING);
        }
        return fehler.empty();
    };

    // vor dem Rendern (kein Rendering laeuft): Bildgroesse aus den Render-Einstellungen, Kamera mit
    // passendem Seitenverhaeltnis (sonst waere ein Bild mit anderem Format verzerrt)
    auto renderBildVorbereiten = [&]
    {
        winRay.bildgroesseSetzen(renderBreite, renderHoehe);
        if (!cam.empty() && winRay.hoehe() > 0)
        {
            cam[0].aspectRatio = double(winRay.breite()) / double(winRay.hoehe());
            cam[0].update();
        }
    };

    auto handleKeys = [&](WPARAM key)
    {
        FensterDirty = true;
        if (PinselTaste(key)) // Terrain-Pinsel: Esc beendet
            return;
        if (KantenTaste(key)) // Kanten-Modus: Entf, K, U, Esc
            return;
        if (key == VK_ESCAPE && AuswahlEbeneHoch()) // Teil einer Gruppe gewaehlt: zurueck zur Gruppe
            return;
        if (key == 0x10001) // Menue "Datei" -> "Szene speichern" (wie Strg+S, ohne gedrueckte Strg-Taste)
        {
            speichern(false);
            return;
        }
        HWND foreground = GetForegroundWindow();

        // Prüfen, ob eines deiner Fenster aktiv ist
        if (foreground != winRay.getHWND() &&
            foreground != winTop.getHWND() &&
            foreground != winFront.getHWND() &&
            foreground != winSide.getHWND() &&
            foreground != winPersp.getHWND() &&
            foreground != hToolWindow &&
            foreground != hHauptfenster &&
            foreground != hObjektFenster)
        {
            return;
        }

        // Strg-Kombinationen (Strg+Pfeiltasten bleiben fuer die Kamera)
        if (GetKeyState(VK_CONTROL) & 0x8000)
        {
            const bool umschalt = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            switch (key)
            {
            case 'Z':
                umschalt ? Wiederholen() : Rueckgaengig();
                return;
            case 'Y':
                Wiederholen();
                return;
            case 'S': // Strg+Umschalt+S: speichern unter
                speichern(umschalt);
                return;
            case 'D': // Strg+Umschalt+D: Instanz (verknuepfte Kopie)
                umschalt ? AuswahlInstanzErstellen() : AuswahlDuplizieren();
                return;
            case 'C':
                AuswahlKopieren();
                return;
            case 'V':
                ZwischenablageEinfuegen();
                return;
            case 'A':
                AllesAuswaehlen();
                return;
            case 'G': // Strg+Umschalt+G: Gruppe aufheben
                umschalt ? GruppeAufheben() : AuswahlGruppieren();
                return;
            default:
                break;
            }
        }

        if (key == 'X') // Stop rendering
        {
            animationAbbrechen = true; // auch eine laufende Animation
            // if (!wasPressed) {
            // wasPressed = true;
            stopRenderingThreads();
            //}
            // else
            // wasPressed = false;
        }
        else if (key == 'C') // Clear Scene (bevor  neue Szene geladen werden soll)
        {
            // if (!wasPressed) {
            // wasPressed = true;
            if (!aenderungenSichern(L"Vor dem Leeren der Szene speichern?"))
                return;
            auswahlSetzen({});
            selectedLight = nullptr;
            selectedType = SelectedType::NONE;
            stopRenderingThreads();
            while (rendernRunning)
                std::this_thread::sleep_for(std::chrono::milliseconds(10));

            // Erst die Ansichten leeren (wartet, bis der Renderthread fertig gelesen hat),
            // dann die Objekte loeschen
            wfTop.clearScene();
            wfFront.clearScene();
            wfSide.clearScene();
            wfPersp.clearScene();

            scene.clear();
            UpdateObjectList();

            // scene.push_back(std::make_unique<Wand>(Vector3D(0, 0, 0), Vector3D(1000, 0, 0), Vector3D(1000, 10, 1000), Vector3D(0, 10, 1000), makeMaterialSimple(Farbe(0.0,0.0,0.7)))); // blauer Boden

            wfTop.setScene(scene);
            wfFront.setScene(scene);
            wfSide.setScene(scene);
            wfPersp.setScene(scene);
            // computeSceneBounds();

            // Objekte sind zerstoert: Verlauf und Snapshots passen nicht mehr; leere Szene gilt als neu
            verlaufLeeren();
            animationLeeren();
            szeneGespeichertMarkieren();
            aktuelleDateiSetzen("");
            //}
            // else
            // wasPressed = false;
        }
        else if (key == 'F') // Flat / Wireframe
        {
            // if (!wasPressed) {
            // wasPressed = true;
            useFlat = !useFlat;
            //}
            // else
            // wasPressed = false;
        }
        else if (key == 'I') // Import Scene
        {
            // if (!wasPressed) {
            // wasPressed = true;

            stopRenderingThreads();

            std::string filename = objDialog.openFileDialog();
            if (filename.empty())
            {
                // wasPressed = false;
                return;
            }

            std::thread([&, filename]()
                        {
                    rendernRunning = true;

                    objDialog.import(filename); 
                    szeneGeaendertMarkieren();

                    UpdateObjectList();
                    computeSceneBounds(); // Ortho-Ansichten zentrieren auf die neue Szene

                    rendernRunning = false;

                    wfTop.setScene(scene);
                    wfFront.setScene(scene);
                    wfSide.setScene(scene);
                    wfPersp.setScene(scene); })
                .detach();
            //}
            // else
            // wasPressed = false;
        }
        else if (key == 'M') // Select "Move"-Action
        {
            // if (!wasPressed) {
            // wasPressed = true;
            selectedAction = SelectedAction::MOVE;
            //}
            // else
            // wasPressed = false;
        }
        else if (key == 'N') // Select next object or light
        {
            // if (!wasPressed)
            //{
            // wasPressed = true;

            int total = wfFront.sceneobjects.size() + lights.size();

            if (total == 0)
                return;

            // nächstes Element
            selectionIndex++;
            selectionIndex %= total;

            // Alte Auswahl löschen
            selectedObject = nullptr;
            selectedLight = nullptr;
            selectedType = SelectedType::NONE;

            // ---------- OBJECTS ----------
            if (selectionIndex < wfFront.sceneobjects.size())
            {
                auswahlSetzen({wfFront.sceneobjects[selectionIndex]}); // beendet eine Mehrfachauswahl
            }
            // ---------- LIGHTS ----------
            else
            {
                int lightIndex = selectionIndex - wfFront.sceneobjects.size();

                selectedLight = lights[lightIndex].get();
                selectedType = SelectedType::LIGHT;
            }
            //}
            // else
            // wasPressed = false;
        }
        else if (key == 'O') // Output image and xml
        {
            // if (!wasPressed) {
            // wasPressed = true;
            // fenster.saveBMPWithTimestamp(L"screenshot");  // Ergebnis: "screenshot-06-04-2026-14-45-10.bmp"
            /// saveWindowBMPWithTimestamp(winRay.getHWND(), L"screenshot");
            // vorher askFilename(): Dialog fuer BMP-Dateien, die Endung wurde danach durch .xml ersetzt
            speichern(true); // immer mit Dialog ("Speichern unter"); Strg+S speichert ohne Dialog

            //}
            // else
            // wasPressed = false;
        }
        else if (key == 'E') // Export OBJ: Auswahl, ohne Auswahl die ganze Szene
        {
            std::string objName = dateiWaehlen(hToolWindow ? hToolWindow : winRay.getHWND(), true,
                                               L"OBJ-Dateien (*.obj)\0*.obj\0Alle Dateien (*.*)\0*.*\0", L"obj");
            if (objName.empty())
                return;

            std::string meldung;
            bool ok;
            const std::vector<object *> auswahl = ausgewaehlteObjekte(); // leer: ganze Szene
            {
                std::lock_guard<std::mutex> lock(sceneMutex); // liest die Szene
                objExportNur = auswahl.empty() ? nullptr : &auswahl;
                ok = szeneAlsOBJExportieren(objName, meldung);
                objExportNur = nullptr;
            }

            std::wstring text(meldung.begin(), meldung.end());
            MessageBoxW(hToolWindow, text.c_str(), auswahl.empty() ? L"Export OBJ (ganze Szene)" : L"Export OBJ (Auswahl)",
                        MB_OK | (ok ? MB_ICONINFORMATION : MB_ICONWARNING));
        }
        else if (key == 'W') // Rendering-Fenster ein-/ausblenden
        {
            HWND h = winRay.getHWND();
            if (hHauptfenster)
                RenderReiterZeigen(!RenderReiterAktiv()); // Reiter Ansichten <-> Rendering
            else if (IsWindowVisible(h))
                ShowWindow(h, SW_HIDE);
            else
                bringToFront(winRay);
        }
        else if (key == 'L') // Load XML: Szene ersetzen
        {
            if (!aenderungenSichern(L"Vor dem Laden speichern?"))
                return;
            std::string xmlName = xmlDateiWaehlen(hToolWindow ? hToolWindow : winRay.getHWND(), false);
            if (xmlName.empty() || !gSzeneAendern)
                return;

            szeneLaden(xmlName);
        }
        else if (key == 'Q') // Quit
        {
            // if (!wasPressed) {
            // wasPressed = true;
            if (!aenderungenSichern(L"Vor dem Beenden speichern?"))
                return;
            DestroyWindow(winRay.getHWND());
            //}
            // else
            // wasPressed = false;
        }
        else if (key == 'B') // Kamerasicht: Render-Bild / Editor
            KamerasichtBildUmschalten();
        else if (key == 'R') // Select "Rotate"-Action
        {
            // if (!wasPressed) {
            // wasPressed = true;
            selectedAction = SelectedAction::ROTATE;
            //}
            // else
            // wasPressed = false;
        }
        else if (key == 'S') // Select "Scale"-Action
        {
            // if (!wasPressed) {
            // wasPressed = true;
            selectedAction = SelectedAction::SCALE;
            //}
            // else
            // wasPressed = false;
        }
        else if (key == 'Y') // Start rendering
        {
            if (gAnimationBilder > 0) // waehrend eine Animation gerendert wird: erst Stop
                return;
            // if (!wasPressed) {
            // wasPressed = true;
            stopRenderingThreads();
            // Ansichten vorne: Bild live in der Kamerasicht; sonst Reiter "Rendering" (das Fenster hat danach seine
            // endgueltige Groesse)
            if (!RenderStartInKamerasicht())
                bringToFront(winRay);
            renderBildVorbereiten();
            preprocessing(winRay);
            ++gRenderLaeufe; // "Stop" zeigen, bis startRendering zurueckkehrt (fertig oder angehalten)
            ganzesBildBegonnen();
            std::thread([&winRay]()
                        {
                            startRendering(winRay);
                            ganzesBildBeendet(!stopRendering);
                            --gRenderLaeufe; })
                .detach();
            //}
            // else
            // wasPressed = false;
        }
        else if (key == 'Z') // Set LookAt to center of scene
        {
            // if (!wasPressed) {
            // wasPressed = true;
            // ganze Szene: Kamera schraeg von oben, Abstand und Zoom passend zur Szenengroesse
            // (vorher: Kamera waagrecht von vorne mit festem fov 120 und Abstand 2,5 x Radius)
            if (scene.empty())
                ansichtAusrichten(Vector3D(-1, -1, -1), Vector3D(1, 1, 1), true);
            else
                ansichtAusrichten(sceneMin, sceneMax, true);
            //}
            // else
            // wasPressed = false;
        }
        else if (
            key == '6' || key == VK_NUMPAD6 ||
            key == '4' || key == VK_NUMPAD4 ||
            key == '8' || key == VK_NUMPAD8 ||
            key == '2' || key == VK_NUMPAD2 ||
            key == '9' || key == VK_NUMPAD9 ||
            key == '1' || key == VK_NUMPAD1)
        {
            Vector3D delta(0, 0, 0);

            // Richtung bestimmen
            if (key == '6' || key == VK_NUMPAD6)
                delta = Vector3D(1, 0, 0);

            else if (key == '4' || key == VK_NUMPAD4)
                delta = Vector3D(-1, 0, 0);

            else if (key == '8' || key == VK_NUMPAD8)
                delta = Vector3D(0, -1, 0);

            else if (key == '2' || key == VK_NUMPAD2)
                delta = Vector3D(0, 1, 0);

            else if (key == '9' || key == VK_NUMPAD9)
                delta = Vector3D(0, 0, 1);

            else if (key == '1' || key == VK_NUMPAD1)
                delta = Vector3D(0, 0, -1);

            // mit der Schrittweite und dem Modus aus dem Objektfenster (wie die Buttons -X..+Z);
            // vorher fest 1 Einheit / 15 Grad / 10 %, und ueber selectionIndex statt der Auswahl
            int achse = delta.x != 0 ? 0 : delta.y != 0 ? 1 : 2;
            int vorzeichen = (delta.x + delta.y + delta.z) > 0 ? 1 : -1;
            SchrittAusfuehren(achse, vorzeichen);
        }
        else if (key == VK_ADD || (key == VK_OEM_PLUS && (GetKeyState(VK_SHIFT) & 0x8000)))
        {
            scale *= 1.1;
            if (scale < 0.001)
                scale = 0.001;
            std::cout << "Zoom In, scale = " << scale << std::endl;
        }
        else if (key == VK_SUBTRACT || key == VK_OEM_MINUS)
        {
            scale /= 1.1;
            if (scale < 0.001)
                scale = 0.001;
            std::cout << "Zoom Out, scale = " << scale << std::endl;
        }
        else if (key == VK_DECIMAL || key == VK_OEM_PERIOD) // Fokus: Ansichten auf die Auswahl
        {
            Vector3D mn, mx;
            bool ok = auswahlGrenzen(ausgewaehlteObjekte(), mn, mx);
            if (!ok)
            {
                // Licht, Kamera, LookAt: Punkt mit etwa 5 % der Szenengroesse drumherum
                bool punkt = true;
                Vector3D p;
                if (selectedType == SelectedType::LIGHT && selectedLight)
                    p = selectedLight->getPosition();
                else if (selectedType == SelectedType::CAMERA && !cam.empty())
                    p = cam[0].position;
                else if (selectedType == SelectedType::LOOKAT && !cam.empty())
                    p = cam[0].lookAt;
                else
                    punkt = false;
                if (punkt)
                {
                    double r = std::max((sceneMax - sceneMin).length() * 0.05, 1e-3);
                    mn = p - Vector3D(r, r, r);
                    mx = p + Vector3D(r, r, r);
                    ok = true;
                }
            }
            if (!ok)
            {
                MessageBeep(MB_ICONWARNING);
                return;
            }

            // Fokus auf die Kamera selbst: nur die Ortho-Ansichten, die Kamera bleibt stehen
            ansichtAusrichten(mn, mx, false, selectedType != SelectedType::CAMERA);
        }
        else if (key == VK_ESCAPE)
        {
            auswahlSetzen({});
            selectedType = SelectedType::NONE;
            selectedObject = nullptr;
            selectedLight = nullptr;
        }
        else if (key == VK_DELETE) // ausgewaehltes Objekt loeschen (mit Rueckfrage)
        {
            AusgewaehltesObjektLoeschen();
        }
        else
            wfPersp.onKey(key);
    };

    // Szene aendern (Objekte/Lichter einfuegen oder loeschen, aus dem Tool-Fenster):
    // Raytracing anhalten, Aenderung unter sceneMutex ausfuehren, Ansichten im selben Schritt
    // aktualisieren - so sieht der GPU-Renderthread nie einen Zeiger auf ein geloeschtes Objekt.
    // Animation rendern: fuer jedes Bild den Zeitpunkt aus den Snapshots berechnen, raytracen, als BMP speichern.
    // Die Szene wird wie bei Y auf dem Hauptthread vorbereitet (AufHauptthread), gerendert wird im eigenen Thread.
    // ---------- Animation rendern, mit Fortsetzen nach einem Absturz ----------
    // Fortschrittsdatei neben dem Programm: Szene, Bildname, Anzahl, naechstes Bild. Sie bleibt nach einem
    // Absturz stehen; beim naechsten Start wird angeboten, die Szene zu laden und weiterzurechnen.
    // Fertige Bilder (vollstaendige BMP-Dateien) werden dabei nicht noch einmal gerechnet.
    static const char *FORTSCHRITT_DATEI = "animation_fortschritt.txt";
    auto utf8 = [](const std::wstring &w)
    {
        int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
        std::string s(size_t(std::max(1, n) - 1), '\0');
        WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, &s[0], n, nullptr, nullptr);
        return s;
    };
    auto ausUtf8W = [](const std::string &s)
    {
        int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
        std::wstring w(size_t(std::max(1, n) - 1), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n);
        return w;
    };
    // Bild i (0-basiert) schon vollstaendig gespeichert? (Groesse wie ein BMP in Fenstergroesse)
    auto bildVorhanden = [&winRay](const std::wstring &basis, int i)
    {
        wchar_t nummer[32];
        swprintf(nummer, 32, L"_%04d.bmp", i + 1);
        WIN32_FILE_ATTRIBUTE_DATA info;
        if (!GetFileAttributesExW((basis + nummer).c_str(), GetFileExInfoStandard, &info))
            return false;
        const uint64_t groesse = (uint64_t(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
        const uint64_t soll = 54ull + uint64_t((winRay.breite() * 3 + 3) & ~3) * uint64_t(winRay.hoehe());
        return groesse == soll; // halb geschriebene Datei (Absturz beim Speichern) zaehlt nicht
    };

    auto animationStarten = [&, utf8, bildVorhanden](const std::wstring &basis, bool fragen)
    {
        const int n = anzahlBilder();
        if (n < 2 || gAnimationBilder > 0)
            return;
        const double t0 = animationsBeginn();
        const int fps = bilderProSekunde();
        const double zeitVorher = aktuelleZeit();

        // Bildgroesse jetzt festlegen (bestimmt auch, ob vorhandene Bilder passen)
        stopRenderingThreads();
        while (rendernRunning)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        bringToFront(winRay); // zuerst zeigen: das Fenster hat danach seine endgueltige Groesse
        renderBildVorbereiten();

        // schon vorhandene Bilder am Anfang: fortsetzen?
        int ab = 0;
        while (ab < n && bildVorhanden(basis, ab))
            ++ab;
        if (ab > 0 && fragen)
        {
            std::wstring frage = std::to_wstring(ab) + L" von " + std::to_wstring(n) +
                                 L" Bildern sind schon vorhanden.\n\nJa: ab Bild " + std::to_wstring(std::min(ab + 1, n)) +
                                 L" weiterrechnen\nNein: alle Bilder neu rechnen";
            int antwort = MessageBoxW(hToolWindow, frage.c_str(), L"Animation rendern", MB_YESNOCANCEL | MB_ICONQUESTION);
            if (antwort == IDCANCEL)
                return;
            if (antwort == IDNO)
                ab = 0;
        }

        // Fortschrittsdatei (Szene nur, wenn sie gespeichert ist: sonst laesst sie sich nicht neu laden)
        const std::string xml = szeneGeaendert() ? std::string() : aktuelleDatei();
        auto fortschrittSchreiben = [=](int naechstes)
        {
            std::ofstream f(FORTSCHRITT_DATEI);
            f << "xml=" << xml << "\nbasis=" << utf8(basis) << "\nbilder=" << n << "\nfps=" << fps
              << "\nnaechstes=" << naechstes << "\n";
        };
        fortschrittSchreiben(ab);

        stopRenderingThreads();
        while (rendernRunning)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        animationAbbrechen = false;
        gAnimationBild = ab + 1;
        gAnimationBilder = n;
        gAnimationAbBild = ab + 1;
        gAnimationStartMs = GetTickCount64();
        bringToFront(winRay);
        ++gRenderLaeufe; // "Stop" zeigen

        std::thread([&winRay, basis, n, t0, fps, zeitVorher, ab, fortschrittSchreiben]()
                    {
            int fertig = ab; // vorhandene Bilder zaehlen mit
            for (int i = ab; i < n && !animationAbbrechen; ++i)
            {
                gAnimationBild = i + 1;
                fortschrittSchreiben(i); // stuerzt dieses Bild ab, wird es beim Fortsetzen neu gerechnet
                const double t = t0 + double(i) / fps;
                // Bewegungsunschaerfe: mehrere Teilbilder ueber die Verschlusszeit (um t herum) rendern und
                // mitteln - was sich bewegt, verwischt entlang seiner Bahn (auch Drehungen und die Kamera)
                const int teile = std::max(1, std::min(bewegungTeilbilder, 64));
                const double offen = std::clamp(verschluss, 0.0, 1.0) / double(std::max(1, fps));
                std::vector<double> summeR, summeG, summeB;
                std::vector<uint32_t> teilbild;
                int bw = 0, bh = 0;
                for (int k = 0; k < teile && !animationAbbrechen; ++k)
                {
                    const double tk = teile > 1 ? t + offen * ((k + 0.5) / teile - 0.5) : t;
                    AufHauptthread([&]
                                   {
                                       zeitpunktAnwenden(tk, false);
                                       preprocessing(winRay); });
                    if (animationAbbrechen)
                        break;
                    startRendering(winRay);
                    while (rendernRunning)
                        std::this_thread::sleep_for(std::chrono::milliseconds(20));
                    if (teile > 1 && !animationAbbrechen && winRay.bildKopie(teilbild, bw, bh))
                    {
                        if (summeR.size() != teilbild.size())
                        {
                            summeR.assign(teilbild.size(), 0.0);
                            summeG.assign(teilbild.size(), 0.0);
                            summeB.assign(teilbild.size(), 0.0);
                        }
                        for (size_t p = 0; p < teilbild.size(); ++p)
                        {
                            summeR[p] += double((teilbild[p] >> 16) & 0xFF);
                            summeG[p] += double((teilbild[p] >> 8) & 0xFF);
                            summeB[p] += double(teilbild[p] & 0xFF);
                        }
                    }
                }
                if (animationAbbrechen) // angehaltenes Bild nicht speichern
                    break;
                if (teile > 1 && !summeR.empty() && size_t(bw) * size_t(bh) == summeR.size())
                {
                    for (int y = 0; y < bh; ++y)
                        for (int x = 0; x < bw; ++x)
                        {
                            const size_t p = size_t(y) * size_t(bw) + size_t(x);
                            auto kanal = [&](double v) { return BYTE(std::clamp(int(std::lround(v / teile)), 0, 255)); };
                            winRay.pixel(x, y, RGB(kanal(summeR[p]), kanal(summeG[p]), kanal(summeB[p])));
                        }
                    FensterDirty = true;
                }
                wchar_t nummer[32];
                swprintf(nummer, 32, L"_%04d.bmp", i + 1);
                winRay.saveBMP((basis + nummer).c_str());
                ++fertig;
            }
            // Szene wieder auf den Zeitpunkt vor dem Rendern
            AufHauptthread([&]
                           { zeitpunktAnwenden(zeitVorher, false); });

            std::wstring text = std::to_wstring(fertig) + L" von " + std::to_wstring(n) + L" Bildern gespeichert:\n" +
                                basis + L"_0001.bmp ...";

            // alle Bilder da: Video mit ffmpeg (Statusleiste zeigt "Video wird erstellt")
            if (fertig == n && gAnimationVideo)
            {
                gAnimationBild = -1;
                bool ok = false;
                text += L"\n\n" + videoErstellen(basis, fps, ok);
            }
            if (fertig == n)
                std::remove(FORTSCHRITT_DATEI); // fertig: nichts mehr fortzusetzen
            else
            {
                fortschrittSchreiben(fertig);
                text += L"\n\nFortsetzen: \"Animation rendern …\" mit demselben Namen (fertige Bilder bleiben).";
            }
            gAnimationBilder = 0;
            gAnimationBild = 0;
            gAnimationStartMs = 0;
            --gRenderLaeufe;

            MessageBoxW(nullptr, text.c_str(), fertig == n ? L"Animation fertig" : L"Animation abgebrochen",
                        MB_OK | MB_ICONINFORMATION | MB_TOPMOST); })
            .detach();
    };
    gAnimationRendern = [&, animationStarten](const std::wstring &basis)
    { animationStarten(basis, true); };

    // Beim Start: Ist das letzte Rendern einer Animation nicht fertig geworden (Absturz)? Dann anbieten,
    // die Szene zu laden und ab dem abgebrochenen Bild weiterzurechnen (nach dem Einrichten der Fenster).
    auto abgebrocheneAnimationPruefen = [&, ausUtf8W, animationStarten]
    {
        std::ifstream f(FORTSCHRITT_DATEI);
        if (!f)
            return;
        std::string zeile, xml, basis;
        int n = 0, fps = 24, naechstes = 0;
        while (std::getline(f, zeile))
        {
            size_t gl = zeile.find('=');
            if (gl == std::string::npos)
                continue;
            std::string k = zeile.substr(0, gl), v = zeile.substr(gl + 1);
            if (k == "xml") xml = v;
            else if (k == "basis") basis = v;
            else if (k == "bilder") n = std::atoi(v.c_str());
            else if (k == "fps") fps = std::atoi(v.c_str());
            else if (k == "naechstes") naechstes = std::atoi(v.c_str());
        }
        f.close();
        if (basis.empty() || n <= 0)
        {
            std::remove(FORTSCHRITT_DATEI);
            return;
        }
        const std::wstring wbasis = ausUtf8W(basis);
        std::wstring text = L"Das Rendern einer Animation wurde nicht fertig (Absturz?):\n" + wbasis + L"_….bmp, Bild " +
                            std::to_wstring(naechstes + 1) + L" von " + std::to_wstring(n) + L".\n\n";
        if (xml.empty() || GetFileAttributesA(xml.c_str()) == INVALID_FILE_ATTRIBUTES)
        {
            text += L"Die Szene war nicht gespeichert, sie lässt sich nicht automatisch laden. Zum Fortsetzen die Szene "
                    L"laden und \"Animation rendern …\" mit demselben Namen wählen (fertige Bilder bleiben).\n\n"
                    L"Diesen Hinweis künftig nicht mehr zeigen?";
            if (MessageBoxW(hToolWindow, text.c_str(), L"Animation fortsetzen", MB_YESNO | MB_ICONINFORMATION) == IDYES)
                std::remove(FORTSCHRITT_DATEI);
            return;
        }
        text += L"Szene \"" + ausUtf8W(xml) + L"\" laden und ab dort weiterrechnen?\n\nNein: nicht fortsetzen (Hinweis entfällt)";
        int antwort = MessageBoxW(hToolWindow, text.c_str(), L"Animation fortsetzen", MB_YESNOCANCEL | MB_ICONQUESTION);
        if (antwort == IDNO)
        {
            std::remove(FORTSCHRITT_DATEI);
            return;
        }
        if (antwort != IDYES)
            return; // Abbrechen: beim naechsten Start wieder fragen
        if (!szeneLaden(xml))
            return;
        bilderProSekundeSetzen(fps);
        animationStarten(wbasis, false); // fertige Bilder werden uebersprungen
    };

    gSzeneAendern = [&](const std::function<void()> &aenderung)
    {
        if (gAnimationBilder > 0)
            animationAbbrechen = true; // Szene geaendert: die Animation passt nicht mehr (und das Bild wird angehalten)
        stopRenderingThreads();
        while (rendernRunning)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));

        Wireframe *ansichten[] = {&wfTop, &wfFront, &wfSide, &wfPersp};
        {
            std::lock_guard<std::mutex> lock(sceneMutex);
            aenderung();
            fell::nachfuehren(scene); // Fell-Imitation folgt ihren Koerperteilen
            ++gGeometrieStand; // Ansichten: grosse Netze vollstaendig auf Aenderungen pruefen

            for (Wireframe *wf : ansichten)
            {
                // wie setScene(), das hier nicht geht, weil es selbst sceneMutex sperrt
                wf->sceneobjects.clear();
                for (const auto &obj : scene)
                    wf->sceneobjects.push_back(obj.get());

                // koennen auf Geloeschtes zeigen
                wf->hoverObject = nullptr;
                wf->hoverLight = nullptr;
                wf->hoverType = SelectedType::NONE;
                wf->lastHits.clear();
            }
        }

        computeSceneBounds();
        UpdateObjectList();
        autoRenderAktivitaet(); // Autom. Rendern: Wartezeit beginnt neu
    };

    // ---------- Autom. Rendern (Schalter neben "Render", toolwindow.cpp) ----------
    // Bereich x0..x1, y0..y1 (Bildpunkte) im Hintergrund rendern; ganz: ganzes Bild (mit Vorschau). Vorher wird das
    // Bild gesichert: wird abgebrochen (neue Aenderung in der Wartezeit), kommt das vorige Bild zurueck.
    static std::vector<uint32_t> autoSicherung;
    static int autoSicherungB = 0, autoSicherungH = 0;
    static std::atomic<bool> autoUnfertig{false}; // Bereich wurde begonnen, aber nicht fertig
    gAutoRendernStarten = [&](double nx0, double ny0, double nx1, double ny1, bool ganz) -> bool
    {
        if (gAnimationBilder > 0 || gRenderLaeufe > 0 || rendernRunning)
            return false;
        stopRenderingThreads();
        const int alteB = winRay.breite(), alteH = winRay.hoehe();
        renderBildVorbereiten();
        if (winRay.breite() != alteB || winRay.hoehe() != alteH || gGanzesBildUnfertig)
            ganz = true; // andere Bildgroesse oder noch kein ganzes Bild
        int x0 = int(std::floor(std::clamp(nx0, 0.0, 1.0) * winRay.breite()));
        int y0 = int(std::floor(std::clamp(ny0, 0.0, 1.0) * winRay.hoehe()));
        int x1 = int(std::ceil(std::clamp(nx1, 0.0, 1.0) * winRay.breite()));
        int y1 = int(std::ceil(std::clamp(ny1, 0.0, 1.0) * winRay.hoehe()));
        if (ganz)
            x0 = y0 = 0, x1 = winRay.breite(), y1 = winRay.hoehe();
        if (x1 <= x0 || y1 <= y0)
            return true; // nichts Sichtbares geaendert
        winRay.bildKopie(autoSicherung, autoSicherungB, autoSicherungH);
        preprocessing(winRay);
        autoUnfertig = true;
        ++gRenderLaeufe;
        std::thread([&winRay, x0, y0, x1, y1, ganz]()
                    {
                        startRenderingBereich(winRay, x0, y0, x1, y1, ganz);
                        if (!stopRendering)
                        {
                            autoUnfertig = false; // fertig: neues Bild gilt
                            if (ganz)
                                gGanzesBildUnfertig = false;
                        }
                        FensterDirty = true;
                        --gRenderLaeufe; })
            .detach();
        return true;
    };
    gAutoRendernAbbrechen = [&]() -> bool
    {
        if (!autoUnfertig)
            return false;
        stopRenderingThreads();
        while (gRenderLaeufe > 0)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        if (autoUnfertig && !autoSicherung.empty())
            winRay.bildSetzen(autoSicherung, autoSicherungB, autoSicherungH); // voriges Bild wieder zeigen
        autoUnfertig = false;
        FensterDirty = true;
        return true;
    };
    gSzeneOeffnen = [&](const std::string &datei)
    {
        if (datei.empty() || !gSzeneAendern)
            return;
        if (GetFileAttributesA(datei.c_str()) == INVALID_FILE_ATTRIBUTES)
        {
            MessageBoxW(hToolWindow, (L"Die Datei gibt es nicht mehr:\n" + ausUtf8W(datei)).c_str(), L"Zuletzt geöffnet",
                        MB_OK | MB_ICONWARNING);
            return;
        }
        if (!aenderungenSichern(L"Vor dem Laden speichern?"))
            return;
        szeneLaden(datei);
    };

    gHandleKeys = handleKeys;

    // Schliessen mit X: ungespeicherte Aenderungen sichern?
    winRay.setCloseHandler([&]
                           { return aenderungenSichern(L"Vor dem Beenden speichern?"); });

    winTop.setKeyHandler(handleKeys);
    winFront.setKeyHandler(handleKeys);
    winSide.setKeyHandler(handleKeys);
    winPersp.setKeyHandler(handleKeys);
    // Rechtsklick: Kanten-Menue (im Kanten-Modus), sonst Objekt-Menue wie in der Liste (u.a. Material-Pinsel)
    std::pair<Fenster *, Wireframe *> ansichtsPaare[] = {{&winTop, &wfTop}, {&winFront, &wfFront}, {&winSide, &wfSide}, {&winPersp, &wfPersp}};
    for (auto &[f, wf] : ansichtsPaare)
    {
        HWND h = f->getHWND();
        Wireframe *w = wf;
        f->onRightClick = [h, w](int x, int y) { AnsichtMenue(h, x, y, w->objektKetteUnter(x, y)); };
    }
    // winRay.setKeyHandler(handleAnimationStep);

    /*std::thread globalKeys([&]{
        while(true){
            if(GetAsyncKeyState('S') & 0x8000) handleKeys('S');
            if(GetAsyncKeyState('R') & 0x8000) handleKeys('R');
            if(GetAsyncKeyState('I') & 0x8000) handleKeys('I');
            if(GetAsyncKeyState('O') & 0x8000) handleKeys('O');
            if(GetAsyncKeyState('Q') & 0x8000) handleKeys('Q');
            if(GetAsyncKeyState('C') & 0x8000) handleKeys('C');
            if(GetAsyncKeyState('Z') & 0x8000) handleKeys('Z');
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    });
    globalKeys.detach();*/

    abgebrocheneAnimationPruefen();

    // Beim Start: zuletzt geoeffnete Szene laden oder mit einer leeren Szene beginnen
    // (nicht, wenn eben eine abgebrochene Animation samt Szene fortgesetzt wurde)
    if (aktuelleDatei().empty())
    {
        szeneGespeichertMarkieren(); // die Startszene: beim Leeren nicht nach dem Speichern fragen
        const std::string letzte = LetzteSzene();
        if (!letzte.empty() && GetFileAttributesA(letzte.c_str()) != INVALID_FILE_ATTRIBUTES)
        {
            std::wstring text = L"Zuletzt geöffnete Szene laden?\n\n" + ausUtf8W(letzte) +
                                L"\n\nJa: diese Szene laden\nNein: mit einer leeren Szene beginnen";
            if (MessageBoxW(hHauptfenster ? hHauptfenster : hToolWindow, text.c_str(), L"Programmstart",
                            MB_YESNO | MB_ICONQUESTION) == IDYES)
                szeneLaden(letzte);
            else
                handleKeys('C'); // leere Szene (ungespeicherte Aenderungen gibt es beim Start nicht)
        }
        else
            handleKeys('C'); // keine zuletzt geoeffnete Szene: leer beginnen
    }

    try
    {
        winRay.loop();
    }
    catch (const std::exception &e)
    {
        std::cout << "Exception: "
                  << e.what()
                  << std::endl;
    }

    // std::cin.get();
}

// Hilfsfunktion: HWND des Fensters holen
HWND getHWND(Fenster &f)
{
    return f.getHWND();
}

// winRay auf zweiten Monitor verschieben und maximieren
void moveWinRayToSecondMonitor(Fenster &winRay)
{
    struct Data
    {
        int idx;
        Fenster *f;
    } data;
    data.idx = 0;
    data.f = &winRay;

    EnumDisplayMonitors(NULL, NULL, [](HMONITOR hMon, HDC, LPRECT, LPARAM lParam) -> BOOL
                        {
            Data* data = (Data*)lParam;

            if (data->idx == 1) { // zweiter Monitor
                MONITORINFO mi;
                mi.cbSize = sizeof(mi);
                if (GetMonitorInfo(hMon, &mi)) {
                    HWND h = data->f->getHWND();
                    // Fenster verschieben und auf Monitorgröße setzen
                    SetWindowPos(h, HWND_TOP,
                                 mi.rcMonitor.left,
                                 mi.rcMonitor.top,
                                 mi.rcMonitor.right - mi.rcMonitor.left,
                                 mi.rcMonitor.bottom - mi.rcMonitor.top,
                                 SWP_SHOWWINDOW);
                    // Fenster maximieren
                    ShowWindow(h, SW_MAXIMIZE);
                }
                data->idx = -1; // erledigt
                return FALSE; // Abbruch nach zweitem Monitor
            }
            data->idx++;
            return TRUE; }, (LPARAM)&data);

    // nur ein Monitor: dort so gross wie moeglich (vorher blieb es 1600 x 900)
    if (data.idx != -1)
        ShowWindow(winRay.getHWND(), SW_MAXIMIZE);
}