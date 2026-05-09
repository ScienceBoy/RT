#pragma once
#include <windows.h>
#include <stdint.h>
#include "farbe.h"
#include <functional>
#include <atomic>
#include <string>

class Fenster
{
public:
    Fenster(int breite, int hoehe);
    ~Fenster();

    int breite() const { return w; }
    int hoehe() const { return h; }
    void loop();

    void pixel(int x, int y, COLORREF farbe);
    void pixel(int x, int y, const Farbe& f);

    void update();
    void processEvents();
    bool running = true;

    HWND getHWND() const { return hwnd; }

    std::function<void(int, int, Fenster&)> onMouseMove = nullptr;
    std::function<void(int, Fenster&)> onMouseWheel;
    std::function<void(int,int, Fenster&)> onMouseDown;
    std::function<void()> onMouseUp;

    void setKeyHandler(std::function<void(WPARAM)> handler) {onKey = handler;}
    void setMouseWheelHandler(std::function<void(int, Fenster&)> handler) {onMouseWheel = handler;}
    void setMouseMoveHandler(std::function<void(int, int, Fenster&)> handler) {onMouseMove = handler;}
    void setMouseDownHandler(std::function<void(int,int, Fenster&)> handler){onMouseDown = handler;}
    void setMouseUpHandler(std::function<void()> handler){onMouseUp = handler;}

    friend LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

    void setTitle(const wchar_t* title);

    int getWidth() const;

    int getHeight() const;

    bool saveBMP(const wchar_t *filename);

    bool saveBMPWithTimestamp(const std::wstring &baseName);

    bool isOpen() const;

    void handleMouseWheel(WPARAM wParam);

private:
    HWND hwnd = nullptr;
    int w, h;
    uint32_t* buffer;
    BITMAPINFO bmi;

    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT handleMessage(UINT, WPARAM, LPARAM);
    
    std::function<void(WPARAM)> onKey;

};

std::wstring askFilename(HWND owner);

bool saveWindowBMPWithTimestamp(HWND hwnd, const std::wstring &baseName);
