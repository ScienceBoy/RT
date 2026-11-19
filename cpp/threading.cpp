#include "threading.h"
#include "render.h"
#include "wireframe.h"
#include <atomic>
#include <thread>
#include <vector>
#include <iostream>
#include <algorithm>
#include <random>
#include "threadPool.h"
#include "SceneSaver.h"
#include "stats.h"
#include "fenster.h"
#include <sstream>
#include "render_einstellungen.h"

std::atomic<long long> triangleTests = 0;
extern bool FensterDirty; // main.cpp: Hauptschleife erneuert die Anzeige
std::atomic<int> gRenderProzent{-1}; // Fortschritt fuer das Taskleisten-Symbol (toolwindow.cpp)

// global
std::atomic<bool> stopRendering(false);
std::atomic<bool> rendernRunningNow(false);

// Anzahl Render-Threads: hardware_concurrency() zaehlt LOGISCHE Prozessoren (mit Hyperthreading
// zwei je Kern, z.B. 20 bei 14 Kernen). Frei bleiben RENDER_FREI logische Prozessoren, d.h. bei
// Hyperthreading RENDER_FREI / 2 Kerne. Vorher "hardware_concurrency() - 2": bei 0 oder 1
// (Wert unbekannt) lief die Rechnung ueber (unsigned) und ergab Milliarden Threads.
static constexpr unsigned RENDER_FREI = 4; // 4 logische = 2 Kerne mit Hyperthreading
static unsigned renderThreadAnzahl()
{
    unsigned n = std::thread::hardware_concurrency();
    if (n == 0)
        n = 4; // unbekannt
    return n > RENDER_FREI ? n - RENDER_FREI : 1u;
}
ThreadPool pool(renderThreadAnzahl());

// Render-Threads mit niedriger Prioritaet: freie Kerne allein genuegen nicht, damit der PC
// fluessig bleibt (Windows verteilt Threads gleicher Prioritaet auf alle Kerne, auch auf die
// "freien"; Speicher und Cache teilen sich ohnehin alle). Mit niedrigerer Prioritaet bekommen
// Maus, Fenster und andere Programme sofort Rechenzeit; ist der PC sonst untaetig, rendert der
// Renderer trotzdem mit voller Geschwindigkeit.
static void renderThreadPrioritaetSenken()
{
    static thread_local bool gesenkt = false; // je Thread nur einmal
    if (!gesenkt)
    {
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
        gesenkt = true;
    }
}

void vorschauRendern(Fenster &f, std::atomic<bool> &stop); // render.cpp
void startRenderingBereich(Fenster &f, int bx0, int by0, int bx1, int by1, bool ganzesBild);

void startRendering(Fenster &f)
{
    startRenderingBereich(f, 0, 0, f.breite(), f.hoehe(), true);
}

// Rendern; nur Kacheln im Bereich x0..x1, y0..y1 (Bildpunkte, x1/y1 ausschliesslich). ganzesBild: vorher die
// Vorschau (Szene wie in der perspektivischen Ansicht, statt schwarz). Bereich (Autom. Rendern): das bisherige Bild
// bleibt stehen, bis die Kacheln es ueberschreiben.
void startRenderingBereich(Fenster &f, int bx0, int by0, int bx1, int by1, bool ganzesBild)
{
    using clock = std::chrono::steady_clock;
    auto startTime = clock::now();
    rendernRunning = true;
    stopRendering = false;
    rendernRunningNow = true;

    // Beschleunigungsstruktur ueber die ganze Szene (die Render-Threads und die Vorschau lesen sie nur)
    szenenBVHBauen();
    if (ganzesBild)
    {
        vorschauRendern(f, stopRendering);
        FensterDirty = true;
    }

    // Anzeige nicht von hier aus erneuern: dieser Thread ist nicht der Hauptthread, und der
    // D3D11-Kontext des Fensters darf nicht von zwei Threads gleichzeitig benutzt werden
    // (sonst "Present failed: 887a0005" = Grafikgeraet entfernt). Die Hauptschleife zeichnet neu.
    FensterDirty = true;

    struct Tile
    {
        int xStart, xEnd;
        int yStart, yEnd;
    };

    int width = f.breite();
    int height = f.hoehe();

    // Kachelgroesse aus den Render-Einstellungen: 1 = Pixel einzeln in zufaelliger Reihenfolge,
    // groesser = Bloecke mit rotem Rahmen um die gerade berechneten Kacheln (wie frueher).
    // Beim Schnell-Rendering mindestens ein Block und ein Vielfaches davon (Kacheln = ganze Bloecke)
    int tileSize = std::max(1, kachelGroesse);
    const int block = std::max(1, std::min(schnellBlock, 64));
    if (block > 1)
        tileSize = std::max(block, (tileSize + block - 1) / block * block);

    // Schnell-Rendering: Tabelle der berechneten Bloecke fuer dieses Rendern
    schnellRenderVorbereiten(width, height);

    // Bereich auf das Kachelraster (Vielfache der Kachel- bzw. Blockgroesse) erweitern
    bx0 = std::clamp(bx0, 0, width);
    by0 = std::clamp(by0, 0, height);
    bx1 = std::clamp(bx1, 0, width);
    by1 = std::clamp(by1, 0, height);
    bx0 = bx0 / tileSize * tileSize;
    by0 = by0 / tileSize * tileSize;

    std::vector<Tile> tiles;

    for (int y = by0; y < by1; y += tileSize)
    {
        for (int x = bx0; x < bx1; x += tileSize)
        {
            tiles.push_back({x, std::min(x + tileSize, width),
                             y, std::min(y + tileSize, height)});
        }
    }
    if (tiles.empty())
    {
        rendernRunningNow = false;
        gRenderProzent = -1;
        rendernRunning = false;
        return;
    }

    // Zufällig mischen (für schöneres Rendering)
    std::shuffle(tiles.begin(), tiles.end(),
                 std::mt19937{std::random_device{}()});

    gRenderProzent = 0;
    std::atomic<int> linesDone(0);
    std::atomic<int> tasksRemaining(tiles.size());

    std::mutex doneMutex;
    std::condition_variable doneCV;

    const int totalTasks = static_cast<int>(tiles.size());
    std::atomic<int> tasksDone{0};

    // Tasks in ThreadPool
    std::atomic<int> lastPrintedPercent{-1};

    for (const Tile &t : tiles)
    {
        pool.enqueue([&, t]()
                     {
            renderThreadPrioritaetSenken();
            if (!stopRendering)
            {
                render(f,
                       t.xStart, t.xEnd,
                       t.yStart, t.yEnd,
                       linesDone,
                       stopRendering);
            }

            int done = tasksDone.fetch_add(1, std::memory_order_relaxed) + 1;

            int percent = done * 100.0 / totalTasks;
            gRenderProzent = percent;
            int rounded = (percent / 100.0) * 100.0;   // 0,1,2,...

            int last = lastPrintedPercent.load();
            if (rounded != last &&
                lastPrintedPercent.compare_exchange_strong(last, rounded))
            {
                // Zeitberechnung
                auto now = clock::now();

                double elapsed =
                    std::chrono::duration<double>(now - startTime).count();

                double eta =
                    done > 0
                    ? elapsed * (totalTasks - done) / done
                    : 0.0;

                std::stringstream ss;

                ss << "Rendering: " << rounded
                << "%   |   Elapsed: "
                << int(elapsed / 60) << "m "
                << int(elapsed) % 60 << "s"
                << "   |   ETA: "
                << int(eta / 60) << "m "
                << int(eta) % 60 << "s";

                std::cout << "\r" << ss.str() << "      " << std::flush;

                //std::wstring wText(ss.str().begin(), ss.str().end());
                //SetWindowTextW(f.getHWND(), wText.c_str());
                std::string text = ss.str();

                // Titel des Render-Fensters. Nicht mit SetWindowTextA: das wartet, bis der Hauptthread
                // die Nachricht bearbeitet. Wartet der Hauptthread gerade selbst auf das Ende des
                // Renderns (Stop -> stopRenderingThreads, Szene aendern), warten beide aufeinander:
                // das Programm fror ein. Jetzt hoechstens 50 ms warten und nach Stop gar nicht mehr.
                if (!stopRendering)
                {
                    DWORD_PTR ergebnis = 0;
                    SendMessageTimeoutA(f.getHWND(), WM_SETTEXT, 0, (LPARAM)text.c_str(),
                                        SMTO_ABORTIFHUNG | SMTO_NORMAL, 50, &ergebnis);
                }
            }

            // Fertig zählen
            if (--tasksRemaining == 0)
            {
                std::lock_guard<std::mutex> lock(doneMutex);
                doneCV.notify_one();
            } });
    }

    // Warten bis alles fertig ist
    {
        std::unique_lock<std::mutex> lock(doneMutex);
        // rendernRunning bleibt gesetzt, bis alle Kacheln fertig sind. Vorher wurde es hier schon beim
        // ersten Pruefen auf false gesetzt: stopRenderingThreads() und Aenderungen an der Szene warteten
        // dann nicht auf die noch laufenden Kacheln.
        doneCV.wait(lock, [&]()
                    { return tasksRemaining == 0; });
    }
    auto endTime = clock::now();
    double totalSeconds =
        std::chrono::duration<double>(endTime - startTime).count();

    int totalMin = int(totalSeconds / 60);
    int totalSec = int(totalSeconds) % 60;

    if (totalMin > 0)
        std::cout << "\nGesamtdauer: " << totalMin << "m " << totalSec << "s\n";
    else
        std::cout << "\nGesamtdauer: " << totalSec << "s\n";

    rendernRunningNow = false;
    gRenderProzent = -1;
    rendernRunning = false;

    /*auto name = askFilename(f.getHWND());
    if (!name.empty()) {
        saveWindowBMPWithTimestamp(f.getHWND(), name.c_str());
        std::wstring filename = name + L".xml";
        SceneSaver::saveSceneToXML(std::string(filename.begin(), filename.end()));
    }*/
    // saveWindowBMPWithTimestamp(f.getHWND(), L"screenshot");
    std::cout << "Triangle Tests: "
              << triangleTests
              << std::endl;

    triangleTests = 0;
}

void stopRenderingNow()
{
    stopRendering = true;
}

void stopRenderingThreads()
{
    stopRendering = true;

    // Warten bis Rendering fertig ist
    while (rendernRunning)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    rendernRunning = false;
}

// --- Wartet, bis das aktuelle Rendering fertig ist ---
void waitForRenderingDone()
{
    /*for (auto& t : renderThreads)
    {
        if (t.joinable())
            t.join();  // Blockiert, bis der Thread fertig ist
    }
    if (renderThreads.size() > 0)
        renderThreads.clear();  // Liste leeren, damit beim nächsten startRendering neue Threads starten
        */
}