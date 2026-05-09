#include "fenster.h"
#include <windows.h>

void place2x2(Fenster& f, int row, int col, const wchar_t* title)
{
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);

    //if (screenW > 1300) screenW = 1300;
    //if (screenH > 900)  screenH = 900;

    int winW = screenW / 2;
    int winH = screenH / 2;

    int x = col * winW;
    int y = row * winH;

    // Fenster verschieben + Größe anpassen
    SetWindowPos(
        f.getHWND(),
        NULL,
        x, y,
        winW, winH,
        SWP_SHOWWINDOW
    );

    // Titel setzen
    f.setTitle(title);
}