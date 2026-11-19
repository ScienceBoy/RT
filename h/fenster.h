#pragma once
#include <windows.h>
#include <stdint.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include "farbe.h"
#include <functional>
#include <atomic>
#include <string>
#include <vector>
#include <mutex>

// CPU: Pixelpuffer wird hochgeladen und angezeigt (bisheriges Verhalten, z.B. winRay)
// GPU: das Fenster bekommt kein eigenes Device, GpuAnsichten zeichnet direkt hinein
enum class FensterModus
{
    CPU,
    GPU
};

class Fenster
{
public:
    Fenster(int breite, int hoehe, FensterModus modus = FensterModus::CPU);
    ~Fenster();

    bool istGpuModus() const { return modus == FensterModus::GPU; }

    int breite() const { return w; }
    int hoehe() const { return h; }
    void loop();

    void pixel(int x, int y, COLORREF farbe);
    void pixel(int x, int y, const Farbe &f);

    void update();
    void processEvents();
    bool running = true;

    std::mutex bufferMutex;
    std::recursive_mutex d3dMutex; // D3D11-Kontext: nie von zwei Threads gleichzeitig

    HWND getHWND() const { return hwnd; }

    std::function<void(int, int, Fenster &)> onMouseMove = nullptr;
    std::function<void(int, Fenster &)> onMouseWheel;
    std::function<void(int, int, Fenster &)> onMouseDown;
    std::function<void()> onMouseUp;
    std::function<void(int, int, Fenster &)> onMiddleMouseDown;
    std::function<void()> onMiddleMouseUp;
    std::function<void(int, int)> onRightClick; // Rechtsklick (Fensterkoordinaten), z.B. Kanten-Menue

    void setKeyHandler(std::function<void(WPARAM)> handler) { onKey = handler; }
    void setMouseWheelHandler(std::function<void(int, Fenster &)> handler) { onMouseWheel = handler; }
    void setMouseMoveHandler(std::function<void(int, int, Fenster &)> handler) { onMouseMove = handler; }
    void setMouseDownHandler(std::function<void(int, int, Fenster &)> handler) { onMouseDown = handler; }
    void setMouseUpHandler(std::function<void()> handler) { onMouseUp = handler; }
    void setMiddleMouseDownHandler(std::function<void(int, int, Fenster &)> cb) { onMiddleMouseDown = cb; }
    void setMiddleMouseUpHandler(std::function<void()> cb) { onMiddleMouseUp = cb; }

    // Vor dem Schliessen (X): false behaelt das Fenster offen (z.B. "Aenderungen speichern?" abgebrochen)
    std::function<bool()> onClose;
    void setCloseHandler(std::function<bool()> cb) { onClose = cb; }

    friend LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

    void setTitle(const wchar_t *title);
    int getWidth() const;
    int getHeight() const;
    bool saveBMP(const wchar_t *filename);
    bool saveBMPWithTimestamp(const std::wstring &baseName);
    bool isOpen() const;
    void handleMouseWheel(WPARAM wParam);

    void initRenderDevice();
    void initFullscreenQuad();
    void uploadFrameToGpu();
    void presentFrame();

    void copyBuffer(const Farbe *src);

    // Kopie des Bildes (fuer die Anzeige in der Kamerasicht); false, wenn noch kein Puffer da ist.
    // bildStand zaehlt jede Aktualisierung (update): aendert er sich, gibt es etwas Neues zu zeigen.
    bool bildKopie(std::vector<uint32_t> &ziel, int &breite, int &hoehe);
    // Gegenstueck zu bildKopie (Autom. Rendern: abgebrochenen Bereich wieder wie vorher); false bei anderer Groesse
    bool bildSetzen(const std::vector<uint32_t> &quelle, int breite, int hoehe);
    std::atomic<unsigned> bildStand{0};

    // Bildgroesse unabhaengig vom Fenster (Render-Einstellungen "Bildbreite/-hoehe"); 0 = wie das Fenster.
    // Nur aufrufen, wenn nicht gerendert wird (der Puffer wird neu angelegt). breite()/hoehe() = Bildgroesse.
    void bildgroesseSetzen(int breite, int hoehe);
    // Knopf oben rechts: Bild auf Fenstergroesse einpassen bzw. 1:1 (dann mit der Maus verschiebbar)
    void zoomKnopfAnlegen();

private:
    HWND hwnd = nullptr;
    FensterModus modus = FensterModus::CPU;
    int w = 0;
    int h = 0;
    uint32_t *buffer = nullptr;
    BITMAPINFO bmi;

    ID3D11Device *renderDevice = nullptr;
    ID3D11DeviceContext *renderContext = nullptr;
    IDXGISwapChain *renderSwapChain = nullptr;
    ID3D11RenderTargetView *renderBackBufferView = nullptr;

    ID3D11Texture2D *frameUploadTexture = nullptr;
    ID3D11ShaderResourceView *frameTextureView = nullptr;

    ID3D11VertexShader *quadVertexShader = nullptr;
    ID3D11PixelShader *quadPixelShader = nullptr;
    ID3D11InputLayout *quadInputLayout = nullptr;
    ID3D11Buffer *quadVertexBuffer = nullptr;
    ID3D11SamplerState *quadSampler = nullptr;

    std::vector<uint8_t> gpuFrameData;

    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT handleMessage(UINT, WPARAM, LPARAM);

    std::function<void(WPARAM)> onKey;

    void resizeRenderDevice(int newW, int newH);
    void bildTexturAnlegen();
    void zoomKnopfSetzen();

    int fw = 0, fh = 0;            // Fenster (Innenflaeche) = Swapchain; w/h = Bild
    bool festeBildgroesse = false; // Bildgroesse eingestellt statt Fenstergroesse
    bool eingepasst = true;        // Darstellung: ganzes Bild im Fenster (sonst 1:1)
    double zoom = 1.0;             // nicht eingepasst: Bildschirmpixel je Bildpixel (Mausrad, 1:1 = 1)
    double offX = 0, offY = 0;     // Lage der linken oberen Bildecke im Fenster
    bool ansichtNeu = true;        // beim naechsten Zeichnen mittig ausrichten
    bool ziehen = false;
    bool uebersichtZiehen = false; // Ziehen in der Uebersicht oben links
    int ziehX = 0, ziehY = 0;

    // Uebersicht und Farbflaechen
    ID3D11PixelShader *farbShader = nullptr;
    ID3D11Buffer *farbPuffer = nullptr;
    ID3D11SamplerState *punktSampler = nullptr; // starker Zoom: Pixel scharf
    void ansichtBegrenzen();
    bool uebersicht(double &x, double &y, double &b, double &h) const; // Lage der Uebersicht (false: nicht noetig)
    void rechteckFuellen(float x, float y, float b, float h, float r, float g, float bl, float a);
    void zoomUm(double faktor, double mx, double my);
    void uebersichtKlick(int x, int y);
    HWND zoomKnopf = nullptr;
};