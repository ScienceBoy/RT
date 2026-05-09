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

// global
std::atomic<bool> stopRendering(false);
std::atomic<bool> rendernRunningNow(false);

ThreadPool pool(std::max(1u, std::thread::hardware_concurrency()-3));

void startRendering(Fenster& f)
{
    using clock = std::chrono::steady_clock;
    auto startTime = clock::now();
    rendernRunning = true;
    stopRendering = false;
    rendernRunningNow = true;

    // Fenster löschen
    for (int y = 0; y < f.hoehe(); y++)
        for (int x = 0; x < f.breite(); x++)
            f.pixel(x, y, Farbe(0, 0, 0));

    f.update();

    struct Tile {
        int xStart, xEnd;
        int yStart, yEnd;
    };

    int width  = f.breite();
    int height = f.hoehe();

    const int tileSize = 1;

    std::vector<Tile> tiles;

    for (int y = 0; y < height; y += tileSize)
    {
        for (int x = 0; x < width; x += tileSize)
        {
            tiles.push_back({
                x, std::min(x + tileSize, width),
                y, std::min(y + tileSize, height)
            });
        }
    }

    // Zufällig mischen (für schöneres Rendering)
    std::shuffle(tiles.begin(), tiles.end(),
                 std::mt19937{std::random_device{}()});

    std::atomic<int> linesDone(0);
    std::atomic<int> tasksRemaining(tiles.size());

    std::mutex doneMutex;
    std::condition_variable doneCV;

    const int totalTasks = static_cast<int>(tiles.size());
    std::atomic<int> tasksDone{0};

    // Tasks in ThreadPool
    std::atomic<int> lastPrintedPercent{-1};

    for (const Tile& t : tiles)
    {
        pool.enqueue([&, t]()
        {
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
            int rounded = (percent / 100.0) * 100.0;   // 0,1,2,...

            int last = lastPrintedPercent.load();
            if (rounded != last &&
                lastPrintedPercent.compare_exchange_strong(last, rounded))
            {
                // Zeitberechnung
                auto now = clock::now();
                double elapsed =
                    std::chrono::duration<double>(now - startTime).count();

                double etaSeconds = 0.0;
                if (done > 0)
                {
                    etaSeconds = elapsed * (totalTasks - done) / done;
                }

                int etaMin = int(etaSeconds / 60);
                int etaSec = int(etaSeconds) % 60;

                std::cout
                    << "\rRendering: " << rounded << "% | ETA: "
                    << etaMin << "m " << etaSec << "s       "
                    << std::flush;
            }


            // Fertig zählen
            if (--tasksRemaining == 0)
            {
                std::lock_guard<std::mutex> lock(doneMutex);
                doneCV.notify_one();
            }
        });
    }

    // Warten bis alles fertig ist
    {
        std::unique_lock<std::mutex> lock(doneMutex);
        doneCV.wait(lock, [&]()
        {
            rendernRunning = false;
            return tasksRemaining == 0;
        });
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
    rendernRunning = false;

    /*auto name = askFilename(f.getHWND());
    if (!name.empty()) {
        saveWindowBMPWithTimestamp(f.getHWND(), name.c_str());
        std::wstring filename = name + L".xml";
        SceneSaver::saveSceneToXML(std::string(filename.begin(), filename.end()));
    }*/
    saveWindowBMPWithTimestamp(f.getHWND(), L"screenshot");
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