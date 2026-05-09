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
//#include "wireframe_util.h"
#include "obj_import_dialog.h"
#include <thread>
#include <vector>
#include <memory>
#include <iostream>
#include <mutex>
#include <algorithm>
#include <filesystem>
#include "SceneSaver.h"
#include <iostream>
#include "mesh.h"


void moveWinRayToSecondMonitor(Fenster& winRay);
static bool wasPressed = false;

//std::vector<std::unique_ptr<object>> scene;
//std::vector<Camera> cam;

int main()
{
    int debug_break_here = 0; // Breakpoint
    //Fenster f(600,400);
    
    // Vier Fenster erzeugen
    Fenster winTop(600,340);
    Fenster winFront(600,340);
    Fenster winSide(600,340);
    Fenster winPersp(600,340);
    Fenster winRay(1600,900);

    // Fenster platzieren
    place2x2(winTop,   0, 1, L"TOP VIEW");
    place2x2(winFront, 1, 1, L"FRONT VIEW");
    place2x2(winSide,  1, 0, L"SIDE VIEW");
    place2x2(winPersp,   0, 0, L"PERPEKTIVE");
    //place2x2(winRay,   0, 0, L"RAYTRACING");

    // Fenster in den Vordergrund holen
    auto bringToFront = [&](Fenster& f) {HWND h = f.getHWND(); SetWindowPos(h, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);};
    
    // ANWENDEN:
    bringToFront(winRay);
    bringToFront(winTop);
    bringToFront(winFront);
    bringToFront(winSide);
    bringToFront(winPersp);
    //SetForegroundWindow(winRay.getHWND());

    initScene(winRay);

    OBJImportDialog objDialog(scene);
    std::vector<object*> objectPointers;
    for (auto& obj : scene)
        objectPointers.push_back(obj.get());

    // Wireframes
    WireframeTop   wfTop(winTop);
    WireframeFront wfFront(winFront);
    WireframeSide  wfSide(winSide);
    WireframePerspective wfPersp(winPersp, &cam[0], objectPointers);

    wfTop.setScene(scene);
    wfFront.setScene(scene);
    wfSide.setScene(scene);
    wfPersp.setScene(scene);

    // Threads starten
    std::thread t1([&]{ wfTop.run();   });
    std::thread t2([&]{ wfFront.run(); });
    std::thread t3([&]{ wfSide.run();  });
    std::thread t4([&]{ wfPersp.run();  });
    //std::thread t4([&]{ preprocessing(winRay); threadsRufenRenderAuf(winRay);});
    
    t1.detach();
    t2.detach();
    t3.detach();
    t4.detach();

    moveWinRayToSecondMonitor(winRay);


    /*
    // --- Animation der springenden Kugel ---
    bringToFront(winRay);

    std::vector<double> animationSteps;
    Vector3D startPos = jumpingSphere->position; // aktuelle Position merken
    double amplitude = 200.0; // Sprunghöhe
    int numSteps = 40;
    for(int i=0;i<=numSteps;i++)
        animationSteps.push_back(i / double(numSteps)); // t von 0 bis 1

    size_t currentStep = 0;

    // Taste "N" für nächsten Frame
    auto handleAnimationStep = [&](WPARAM key) {
        if(key == 'N' && currentStep < animationSteps.size()) {
            double t = animationSteps[currentStep];
            jumpingSphere->position.y = startPos.y + amplitude * sin(t * 3.14159265); // Kugelposition aktualisieren
            currentStep++;               // nächster Frame
        }
    };*/

    auto handleKeys = [&](WPARAM key)
    {

        if (key == 'S')
        {
            if (!wasPressed) {
                wasPressed = true;
                stopRenderingThreads();
            }
            else
                wasPressed = false;
        }

        else if (key == 'R')
        {
            if (!wasPressed) {
                wasPressed = true;
                stopRenderingThreads();
                bringToFront(winRay);
                preprocessing(winRay);
                std::thread([&winRay]() {startRendering(winRay);}).detach();
            }
            else
                wasPressed = false;            
        }
        else if (key == 'I')
        {
            if (!wasPressed) {
                wasPressed = true;

                stopRenderingThreads();

                std::string filename = objDialog.openFileDialog();
                if (filename.empty()) return;

                std::thread([&, filename]() 
                {
                    rendernRunning = true;

                    objDialog.import(filename); // nur Import, kein Dialog

                    rendernRunning = false;

                    wfTop.setScene(scene);
                    wfFront.setScene(scene);
                    wfSide.setScene(scene);
                    wfPersp.setScene(scene);
                }).detach();
            }
            else
                wasPressed = false;            
        }
        else if (key == 'O') 
        {
            if (!wasPressed) {
                wasPressed = true;
                //fenster.saveBMPWithTimestamp(L"screenshot");  // Ergebnis: "screenshot-06-04-2026-14-45-10.bmp"
                ///saveWindowBMPWithTimestamp(winRay.getHWND(), L"screenshot");
                auto name = askFilename(winRay.getHWND());
                if (!name.empty()) {
                    saveWindowBMPWithTimestamp(winRay.getHWND(), name.c_str());
                    std::filesystem::path p(name);
                    p.replace_extension(L".xml");
                    std::wstring filename = p.wstring();
                    SceneSaver::saveSceneToXML(std::string(filename.begin(), filename.end()));
                }

            }
            else
                wasPressed = false;            
        }
        else if (key == 'Q')
        {
            if (!wasPressed) {
                wasPressed = true;
                DestroyWindow(winRay.getHWND());
            }
            else
                wasPressed = false;            
        }
        else if (key == 'C')
        {
            if (!wasPressed) {
                wasPressed = true;
                stopRenderingThreads();
                while (rendernRunning)
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));

                scene.clear();

                wfTop.clearScene();
                wfFront.clearScene();
                wfSide.clearScene();
                wfPersp.clearScene();

                //scene.push_back(std::make_unique<Wand>(Vector3D(0, 0, 0), Vector3D(1000, 0, 0), Vector3D(1000, 10, 1000), Vector3D(0, 10, 1000), makeMaterialSimple(Farbe(0.0,0.0,0.7)))); // blauer Boden

                wfTop.setScene(scene);
                wfFront.setScene(scene);
                wfSide.setScene(scene);
                wfPersp.setScene(scene);
                //computeSceneBounds();
            }
            else
                wasPressed = false;            
        }
        else if (key == 'Z')
        {
            if (!wasPressed) {
                wasPressed = true;
                if (!cam.empty())
                {
                    Vector3D sceneCenter = (sceneMax - sceneMin) * 0.5 + sceneMin;
                    std::cout << sceneCenter.x << " | " << sceneCenter.y << " | " << sceneCenter.z << '\n';
                    Vector3D sceneSize = sceneMax - sceneMin;
                    double radius = std::max({sceneSize.x, sceneSize.y, sceneSize.z}) * 0.5;
                    Vector3D camPos = sceneCenter + Vector3D(0, 0, -2.5 * radius);
                    cam[0] = Camera(camPos, sceneCenter, 120.0, 16.0/9.0);
                }
            }
            else
                wasPressed = false;            
        }
        else 
            wfPersp.onKey(key); 
    };

    winTop.setKeyHandler(handleKeys);
    winFront.setKeyHandler(handleKeys);
    winSide.setKeyHandler(handleKeys);
    winPersp.setKeyHandler(handleKeys);
    //winRay.setKeyHandler(handleAnimationStep);

    std::thread globalKeys([&]{
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
    globalKeys.detach();

    winRay.loop();

    //std::cin.get();
}

// Hilfsfunktion: HWND des Fensters holen
HWND getHWND(Fenster& f) {
    return f.getHWND();
}

// winRay auf zweiten Monitor verschieben und maximieren
void moveWinRayToSecondMonitor(Fenster& winRay) {
    struct Data { 
        int idx; 
        Fenster* f; 
    } data;
    data.idx = 0;
    data.f = &winRay;

    EnumDisplayMonitors(NULL, NULL,
        [](HMONITOR hMon, HDC, LPRECT, LPARAM lParam) -> BOOL {
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
                return FALSE; // Abbruch nach zweitem Monitor
            }
            data->idx++;
            return TRUE;
        },
        (LPARAM)&data
    );
}