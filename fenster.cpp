#ifndef UNICODE
    #define UNICODE
#endif
#ifndef _UNICODE
    #define _UNICODE
#endif

#include <windows.h>
#include "fenster.h"
#include <cstdlib>
#include <iostream>
#include <ctime>
#include <cwchar>
#include "camera.h"
#include "farbe.h"
#include "threading.h"
#include <fstream>
#include <functional>
#include <thread>
#include <chrono>
#include <windowsx.h>  // für GET_X_LPARAM, GET_Y_LPARAM
#include <string>
#include <sstream>
#include <iomanip>
#include <cstdio>
#include <cstdint>
#include <vector>
#include <commdlg.h>

extern Fenster winRay; 

///////////////////////////////////////////////////////////////////////////
//  Fenster – MULTI-WINDOW-FÄHIGE IMPLEMENTIERUNG
///////////////////////////////////////////////////////////////////////////

// ========== Statische WindowProc ==========
// Diese Funktion ruft die instanzbezogene handleMessage auf
LRESULT CALLBACK Fenster::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    // Instanzzeiger aus UserData holen
    Fenster* self = (Fenster*)GetWindowLongPtr(hwnd, GWLP_USERDATA);

    if (self)
        return self->handleMessage(msg, wp, lp);

    // Vor der Konstruktion (CREATE) ist self noch nullptr
    return DefWindowProc(hwnd, msg, wp, lp);
}


// ========== Konstruktor ==========

Fenster::Fenster(int breite, int hoehe)
{
    w = breite;
    h = hoehe;
    buffer = new uint32_t[w * h];

    HINSTANCE hInstance = GetModuleHandle(nullptr);

    static int classCounter = 0;
    classCounter++;

    wchar_t className[64];
    swprintf(className, 64, L"RenderingClass_%d", classCounter);

    WNDCLASSW wc = {};
    wc.lpfnWndProc   = Fenster::WndProc;
    wc.hInstance     = hInstance;
    wc.lpszClassName = className;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);  // <- Normale Pfeilhand
    RegisterClassW(&wc);

    hwnd = CreateWindowExW(
        0, className, L"Rendering",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        w, h,
        nullptr, nullptr,
        hInstance, nullptr
    );

    SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)this);

    ShowWindow(hwnd, SW_SHOW);

    // Fenster in den Vordergrund holen
    /*SetWindowPos(
        hwnd,
        HWND_TOP,
        0,0,0,0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW
    );*/

    ZeroMemory(&bmi, sizeof(bmi));
    bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth       = w;
    bmi.bmiHeader.biHeight      = -h;
    bmi.bmiHeader.biPlanes      = 1;
    bmi.bmiHeader.biBitCount    = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    ShowCursor(TRUE);
}


Fenster::~Fenster()
{
    delete[] buffer;
    DestroyWindow(hwnd);
}


// ========== Instanzbezogene WindowProc ==========

LRESULT Fenster::handleMessage(UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch(msg)
    {
        case WM_KEYDOWN:
            if (onKey)
                onKey(wParam);
            return 0;
            
        case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);

            StretchDIBits(
                hdc,
                0, 0, w, h,
                0, 0, w, h,
                buffer,
                &bmi,
                DIB_RGB_COLORS,
                SRCCOPY
            );

            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_SIZE:
        {
            int newW = LOWORD(lParam);
            int newH = HIWORD(lParam);

            if (newW > 0 && newH > 0)
            {
                w = newW;
                h = newH;

                delete[] buffer;
                buffer = new uint32_t[w * h];

                ZeroMemory(&bmi, sizeof(bmi));
                bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
                bmi.bmiHeader.biWidth       = w;
                bmi.bmiHeader.biHeight      = -h;
                bmi.bmiHeader.biPlanes      = 1;
                bmi.bmiHeader.biBitCount    = 32;
                bmi.bmiHeader.biCompression = BI_RGB;
            }
            return 0;
        }

        case WM_MOUSEMOVE:
        {
            if (onMouseMove)
            {
                int x = GET_X_LPARAM(lParam);
                int y = GET_Y_LPARAM(lParam);
                onMouseMove(x, y, *this);
            }
            return 0;
        }

        case WM_SETFOCUS:
        {
            // Perspektiv-Wireframe zurücksetzen, um Sprünge beim ersten Maus-Move zu verhindern
            if (onMouseMove)
            {
                onMouseMove(0, 0, *this); 
            }
            return 0;
        }

        case WM_SETCURSOR:
        {
            SetCursor(LoadCursor(nullptr, IDC_ARROW));
            return TRUE;
        }

        case WM_MOUSEWHEEL:
        {
            int delta = GET_WHEEL_DELTA_WPARAM(wParam);

            if (onMouseWheel)
                onMouseWheel(delta, *this);

            return 0;
        }

        case WM_LBUTTONDOWN:
        {
            int x = GET_X_LPARAM(lParam);
            int y = GET_Y_LPARAM(lParam);

            if (onMouseDown)
                onMouseDown(x, y, *this);

            return 0;
        }

        case WM_LBUTTONUP:
        {
            if (onMouseUp)
                onMouseUp();

            return 0;
        }

        case WM_DESTROY:
            running = false;
            PostQuitMessage(0);
            return 0;

        case WM_CLOSE:
            running = false;
            DestroyWindow(hwnd);
            return 0;

    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}


// ========== Pixel zeichnen ==========

void Fenster::pixel(int x, int y, COLORREF farbe)
{
    if (x < 0 || y < 0 || x >= w || y >= h) 
        return;

    unsigned char r = GetRValue(farbe);
    unsigned char g = GetGValue(farbe);
    unsigned char b = GetBValue(farbe);

    buffer[y*w + x] = (r << 16) | (g << 8) | b;
}

void Fenster::pixel(int x, int y, const Farbe& f)
{ 
    pixel(x, y, f.toCOLORREF());
}


void Fenster::update()
{
    InvalidateRect(hwnd, nullptr, FALSE);
    UpdateWindow(hwnd);
}

void Fenster::processEvents()
{
    MSG msg;
    while(PeekMessage(&msg, nullptr, 0,0, PM_REMOVE))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
}

/*void Fenster::loop()
{
    MSG msg;
    while (true)
    {
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT) return;
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        InvalidateRect(hwnd, NULL, FALSE);
        UpdateWindow(hwnd);
    }
}*/

bool Fenster::isOpen() const
{
    return running; 
}

void Fenster::loop()
{
    while (isOpen())
    {
        processEvents();
        update();
        std::this_thread::sleep_for(std::chrono::milliseconds(33)); // ~30 FPS, damit CPU nicht 100% arbeiten muss
    }
}

void Fenster::setTitle(const wchar_t* title)
{
    SetWindowTextW(hwnd, title);
}

std::wstring askFilename(HWND owner) {
    wchar_t filename[MAX_PATH] = L"";

    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = L"BMP Dateien (*.bmp)\0*.bmp\0Alle Dateien (*.*)\0*.*\0";
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_EXPLORER | OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT;
    ofn.lpstrDefExt = L"bmp";

    if (GetSaveFileNameW(&ofn)) {
        return filename;
    }

    return L""; // Abbruch
}

bool saveWindowBMPWithTimestamp(HWND hwnd, const std::wstring& baseName)
{
    std::wcout << "Speichere: " << baseName << std::endl;
    if (!hwnd) return false;

    // Fenstergröße bestimmen
    RECT rc;
    GetClientRect(hwnd, &rc);
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;

    HDC hdcWindow = GetDC(hwnd);
    HDC hdcMem = CreateCompatibleDC(hdcWindow);
    HBITMAP hBitmap = CreateCompatibleBitmap(hdcWindow, w, h);
    SelectObject(hdcMem, hBitmap);

    // Fensterinhalt kopieren
    BitBlt(hdcMem, 0, 0, w, h, hdcWindow, 0, 0, SRCCOPY);

    // Bitmapinfo vorbereiten
    BITMAPINFOHEADER bi = {};
    bi.biSize = sizeof(BITMAPINFOHEADER);
    bi.biWidth = w;
    bi.biHeight = h;
    bi.biPlanes = 1;
    bi.biBitCount = 32; // 32 Bit für einfaches Auslesen
    bi.biCompression = BI_RGB;

    std::vector<uint32_t> buffer(w * h);
    GetDIBits(hdcWindow, hBitmap, 0, h, buffer.data(), (BITMAPINFO*)&bi, DIB_RGB_COLORS);

    // Timestamp erzeugen
    std::wostringstream woss;
    std::time_t t = std::time(nullptr);
    std::tm tm;
    localtime_s(&tm, &t);
    woss << std::setfill(L'0')
         << std::setw(2) << tm.tm_mday << L"-"
         << std::setw(2) << (tm.tm_mon + 1) << L"-"
         << std::setw(4) << (tm.tm_year + 1900) << "-"
         << std::setw(2) << tm.tm_hour << "-"
         << std::setw(2) << tm.tm_min << "-"
         << std::setw(2) << tm.tm_sec;

    std::wstring filename = baseName + L"-" + woss.str() + L".bmp";

    // Datei öffnen
    FILE* file = _wfopen(filename.c_str(), L"wb");
    if (!file) return false;

    // BMP Header schreiben
    BITMAPFILEHEADER bmfHeader = {};
    bmfHeader.bfType = 0x4D42;
    int rowSize = (w * 3 + 3) & (~3);
    DWORD dwBmpSize = rowSize * h;
    bmfHeader.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    bmfHeader.bfSize = bmfHeader.bfOffBits + dwBmpSize;

    fwrite(&bmfHeader, sizeof(bmfHeader), 1, file);

    BITMAPINFOHEADER biHeader = {};
    biHeader.biSize = sizeof(BITMAPINFOHEADER);
    biHeader.biWidth = w;
    biHeader.biHeight = h;
    biHeader.biPlanes = 1;
    biHeader.biBitCount = 24;
    biHeader.biCompression = BI_RGB;

    fwrite(&biHeader, sizeof(biHeader), 1, file);

    unsigned char pad[3] = {0,0,0};
    int padding = (4 - (w * 3) % 4) % 4;

    // Pixel schreiben (unten nach oben)
    for (int y = h - 1; y >= 0; y--)
    {
        for (int x = 0; x < w; x++)
        {
            uint32_t pixel = buffer[y * w + x];
            unsigned char b = pixel & 0xFF;
            unsigned char g = (pixel >> 8) & 0xFF;
            unsigned char r = (pixel >> 16) & 0xFF;
            unsigned char color[3] = {b, g, r};
            fwrite(color, 1, 3, file);
        }
        fwrite(pad, 1, padding, file);
    }

    fclose(file);

    // Ressourcen freigeben
    DeleteObject(hBitmap);
    DeleteDC(hdcMem);
    ReleaseDC(hwnd, hdcWindow);

    return true;
}

int Fenster::getWidth() const { return w; }
int Fenster::getHeight() const { return h; }

bool Fenster::saveBMP(const wchar_t* filename)
{
    BITMAPFILEHEADER bmfHeader;
    BITMAPINFOHEADER biHeader = bmi.bmiHeader;

    DWORD dwBmpSize = ((w * biHeader.biBitCount + 31) / 32) * 4 * h;

    // Dateiheader vorbereiten
    bmfHeader.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    bmfHeader.bfSize = dwBmpSize + bmfHeader.bfOffBits;
    bmfHeader.bfType = 0x4D42; // 'BM'

    FILE* file = _wfopen(filename, L"wb");
    if (!file) return false;

    // Header schreiben
    fwrite(&bmfHeader, sizeof(bmfHeader), 1, file);
    fwrite(&biHeader, sizeof(biHeader), 1, file);

    // Pixel schreiben
    // BMP speichert Pixel in BGR statt RGB, wir müssen also umwandeln
    for (int y = 0; y < h; y++)
    {
        for (int x = 0; x < w; x++)
        {
            uint32_t pixel = buffer[y * w + x];
            unsigned char b = pixel & 0xFF;
            unsigned char g = (pixel >> 8) & 0xFF;
            unsigned char r = (pixel >> 16) & 0xFF;
            unsigned char color[3] = { b, g, r };
            fwrite(color, 1, 3, file);
        }

        // BMP-Zeilen müssen auf 4-Byte-Grenze ausgerichtet sein
        int padding = (4 - (w * 3) % 4) % 4;
        unsigned char pad[3] = {0,0,0};
        fwrite(pad, 1, padding, file);
    }

    fclose(file);
    return true;
}

bool Fenster::saveBMPWithTimestamp(const std::wstring& baseName)
{
    // Timestamp bauen
    std::wostringstream woss;
    std::time_t t = std::time(nullptr);
    std::tm tm;
    localtime_s(&tm, &t);
    woss << std::setfill(L'0')
         << std::setw(2) << tm.tm_mday << L"-"
         << std::setw(2) << (tm.tm_mon + 1) << L"-"
         << std::setw(4) << (tm.tm_year + 1900) << "-"
         << std::setw(2) << tm.tm_hour << "-"
         << std::setw(2) << tm.tm_min << "-"
         << std::setw(2) << tm.tm_sec;

    std::wstring filename = baseName + L"-" + woss.str() + L".bmp";

    // Datei öffnen
    FILE* file = _wfopen(filename.c_str(), L"wb");
    if (!file) return false;

    // BMP Header vorbereiten
    BITMAPFILEHEADER bmfHeader = {};
    BITMAPINFOHEADER biHeader = bmi.bmiHeader; // vorhandener BITMAPINFOHEADER
    biHeader.biBitCount = 24;                  // 24-Bit RGB
    biHeader.biCompression = BI_RGB;           // unkomprimiert

    int rowSize = (w * 3 + 3) & (~3);          // Zeilenbreite auf Vielfaches von 4 runden
    DWORD dwBmpSize = rowSize * h;

    bmfHeader.bfType = 0x4D42; // 'BM'
    bmfHeader.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    bmfHeader.bfSize = bmfHeader.bfOffBits + dwBmpSize;

    // Header schreiben
    fwrite(&bmfHeader, sizeof(bmfHeader), 1, file);
    fwrite(&biHeader, sizeof(biHeader), 1, file);

    // Pixel schreiben (unten nach oben)
    unsigned char pad[3] = {0,0,0};
    int padding = (4 - (w * 3) % 4) % 4;

    for (int y = h - 1; y >= 0; y--)
    {
        for (int x = 0; x < w; x++)
        {
            uint32_t pixel = buffer[y * w + x];
            unsigned char b = pixel & 0xFF;
            unsigned char g = (pixel >> 8) & 0xFF;
            unsigned char r = (pixel >> 16) & 0xFF;
            unsigned char color[3] = {b, g, r};
            fwrite(color, 1, 3, file);
        }
        fwrite(pad, 1, padding, file);
    }

    fclose(file);
    return true;
}