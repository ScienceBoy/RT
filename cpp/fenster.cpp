#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include "fenster.h"

extern bool rendernRunning; // threading.cpp/scene: laufendes Rendering
static const int ID_ZOOMKNOPF = 9001; // Knopf "Eingepasst / Originalgroesse" im Render-Fenster
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include <cstdlib>
#include <string>
#include <algorithm>
#include <cmath>
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
#include <windowsx.h> // für GET_X_LPARAM, GET_Y_LPARAM
#include <string>
#include <sstream>
#include <iomanip>
#include <cstdio>
#include <cstdint>
#include <vector>
#include <commdlg.h>

extern Fenster winRay;
extern Fenster winTop;
extern Fenster winFront;
extern Fenster winSide;
extern Fenster winPersp;
extern bool FensterDirty;

///////////////////////////////////////////////////////////////////////////
//  Fenster – MULTI-WINDOW-FÄHIGE IMPLEMENTIERUNG
///////////////////////////////////////////////////////////////////////////

// ========== Statische WindowProc ==========
// Diese Funktion ruft die instanzbezogene handleMessage auf
LRESULT CALLBACK Fenster::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    // Instanzzeiger aus UserData holen
    Fenster *self = (Fenster *)GetWindowLongPtr(hwnd, GWLP_USERDATA);

    if (self)
        return self->handleMessage(msg, wp, lp);

    // Vor der Konstruktion (CREATE) ist self noch nullptr
    return DefWindowProc(hwnd, msg, wp, lp);
}

void Fenster::initRenderDevice()
{
    DXGI_SWAP_CHAIN_DESC swapDesc = {};
    swapDesc.BufferCount = 2;
    swapDesc.BufferDesc.Width = w;
    swapDesc.BufferDesc.Height = h;
    swapDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapDesc.BufferDesc.RefreshRate.Numerator = 60;
    swapDesc.BufferDesc.RefreshRate.Denominator = 1;
    swapDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapDesc.OutputWindow = hwnd;
    swapDesc.SampleDesc.Count = 1;
    swapDesc.Windowed = TRUE;

    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        0,
        nullptr,
        0,
        D3D11_SDK_VERSION,
        &swapDesc,
        &renderSwapChain,
        &renderDevice,
        nullptr,
        &renderContext);

    if (FAILED(hr))
    {
        std::cerr << "D3D11CreateDeviceAndSwapChain failed\n";
        return;
    }

    IDXGIDevice *dxgiDevice = nullptr;
    renderDevice->QueryInterface(__uuidof(IDXGIDevice),
                                 (void **)&dxgiDevice);

    IDXGIAdapter *adapter = nullptr;
    dxgiDevice->GetAdapter(&adapter);

    DXGI_ADAPTER_DESC desc;
    adapter->GetDesc(&desc);

    std::wcout << L"GPU: "
               << desc.Description
               << std::endl;

    adapter->Release();
    dxgiDevice->Release();

    ID3D11Texture2D *backBufferTexture = nullptr;
    renderSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void **)&backBufferTexture);
    renderDevice->CreateRenderTargetView(backBufferTexture, nullptr, &renderBackBufferView);
    backBufferTexture->Release();

    D3D11_TEXTURE2D_DESC texDesc = {};
    texDesc.Width = w;
    texDesc.Height = h;
    texDesc.MipLevels = 1;
    texDesc.ArraySize = 1;
    texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    texDesc.SampleDesc.Count = 1;
    texDesc.Usage = D3D11_USAGE_DYNAMIC;
    texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    texDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    texDesc.MiscFlags = 0;

    hr = renderDevice->CreateTexture2D(&texDesc, nullptr, &frameUploadTexture);
    if (FAILED(hr))
    {
        std::cerr << "CreateTexture2D failed\n";
        return;
    }

    renderDevice->CreateShaderResourceView(frameUploadTexture, nullptr, &frameTextureView);

    initFullscreenQuad();
}

void Fenster::initFullscreenQuad()
{
    struct QuadVertex
    {
        float x, y, z;
        float u, v;
    };

    static const char *vertexShaderCode =
        "struct VSIn { float3 pos : POSITION; float2 uv : TEXCOORD0; };"
        "struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };"
        "VSOut main(VSIn input)"
        "{"
        "    VSOut output;"
        "    output.pos = float4(input.pos, 1.0f);"
        "    output.uv = input.uv;"
        "    return output;"
        "}";

    static const char *pixelShaderCode =
        "Texture2D gTexture : register(t0);"
        "SamplerState gSampler : register(s0);"
        "struct PSIn { float4 pos : SV_Position; float2 uv : TEXCOORD0; };"
        "float4 main(PSIn input) : SV_Target"
        "{"
        "    return gTexture.Sample(gSampler, input.uv);"
        "}";

    ID3DBlob *vsBlob = nullptr;
    ID3DBlob *psBlob = nullptr;

    HRESULT hr = D3DCompile(
        vertexShaderCode,
        strlen(vertexShaderCode),
        nullptr,
        nullptr,
        nullptr,
        "main",
        "vs_5_0",
        0,
        0,
        &vsBlob,
        nullptr);

    if (FAILED(hr))
    {
        std::cerr << "VS compile failed\n";
        return;
    }

    hr = D3DCompile(
        pixelShaderCode,
        strlen(pixelShaderCode),
        nullptr,
        nullptr,
        nullptr,
        "main",
        "ps_5_0",
        0,
        0,
        &psBlob,
        nullptr);

    if (FAILED(hr))
    {
        std::cerr << "PS compile failed\n";
        vsBlob->Release();
        return;
    }

    renderDevice->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &quadVertexShader);
    renderDevice->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &quadPixelShader);

    D3D11_INPUT_ELEMENT_DESC inputLayoutDesc[] =
        {
            {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0}};

    renderDevice->CreateInputLayout(
        inputLayoutDesc,
        2,
        vsBlob->GetBufferPointer(),
        vsBlob->GetBufferSize(),
        &quadInputLayout);

    QuadVertex quadVertices[] =
        {
            {-1.0f, 1.0f, 0.0f, 0.0f, 0.0f},
            {1.0f, 1.0f, 0.0f, 1.0f, 0.0f},
            {-1.0f, -1.0f, 0.0f, 0.0f, 1.0f},
            {-1.0f, -1.0f, 0.0f, 0.0f, 1.0f},
            {1.0f, 1.0f, 0.0f, 1.0f, 0.0f},
            {1.0f, -1.0f, 0.0f, 1.0f, 1.0f},
        };

    D3D11_BUFFER_DESC vertexBufferDesc = {};
    vertexBufferDesc.Usage = D3D11_USAGE_DEFAULT;
    vertexBufferDesc.ByteWidth = sizeof(quadVertices);
    vertexBufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;

    D3D11_SUBRESOURCE_DATA vertexSubresource = {quadVertices};
    renderDevice->CreateBuffer(&vertexBufferDesc, &vertexSubresource, &quadVertexBuffer);

    D3D11_SAMPLER_DESC samplerDesc = {};
    samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    renderDevice->CreateSamplerState(&samplerDesc, &quadSampler);

    // Punkt-Sampler fuer starken Zoom (einzelne Pixel scharf statt verwischt)
    samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    renderDevice->CreateSamplerState(&samplerDesc, &punktSampler);

    // einfarbige Flaechen (Uebersicht: Hintergrund und Rahmen des Ausschnitts)
    {
        static const char *farbCode =
            "cbuffer F : register(b0) { float4 farbe; };"
            "float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target { return farbe; }";
        ID3DBlob *blob = nullptr;
        if (SUCCEEDED(D3DCompile(farbCode, strlen(farbCode), nullptr, nullptr, nullptr, "main", "ps_5_0", 0, 0, &blob, nullptr)))
        {
            renderDevice->CreatePixelShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &farbShader);
            blob->Release();
        }
        D3D11_BUFFER_DESC bd = {};
        bd.ByteWidth = 16;
        bd.Usage = D3D11_USAGE_DEFAULT;
        bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        renderDevice->CreateBuffer(&bd, nullptr, &farbPuffer);
    }

    vsBlob->Release();
    psBlob->Release();
}

void Fenster::uploadFrameToGpu()
{
    // std::lock_guard<std::mutex> lock(bufferMutex);

    if (!renderDevice || !renderContext || !frameUploadTexture)
        return;

    D3D11_MAPPED_SUBRESOURCE mapped = {};
    HRESULT hr = renderContext->Map(frameUploadTexture, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);

    if (FAILED(hr))
        return;

    /*const size_t bytesPerPixel = 4;
    const size_t requiredBytes = static_cast<size_t>(w) * static_cast<size_t>(h) * bytesPerPixel;
    if (gpuFrameData.size() != requiredBytes)
        gpuFrameData.resize(requiredBytes);*/

    unsigned char *dst = static_cast<unsigned char *>(mapped.pData);

    for (int y = 0; y < h; ++y)
    {
        unsigned char *row =
            (unsigned char *)mapped.pData + y * mapped.RowPitch;

        for (int x = 0; x < w; ++x)
        {
            uint32_t pixel = buffer[y * w + x];

            unsigned char r = (pixel >> 16) & 0xFF;
            unsigned char g = (pixel >> 8) & 0xFF;
            unsigned char b = pixel & 0xFF;

            row[x * 4 + 0] = r;
            row[x * 4 + 1] = g;
            row[x * 4 + 2] = b;
            row[x * 4 + 3] = 255;
        }
    }

    renderContext->Unmap(frameUploadTexture, 0);
}

// ========== Konstruktor ==========

Fenster::Fenster(int breite, int hoehe, FensterModus modus)
    : modus(modus)
{
    w = breite;
    h = hoehe;
    fw = breite;
    fh = hoehe;
    buffer = new uint32_t[w * h];

    HINSTANCE hInstance = GetModuleHandle(nullptr);

    static int classCounter = 0;
    classCounter++;

    wchar_t className[64];
    swprintf(className, 64, L"RenderingClass_%d", classCounter);

    // Programm-Icon wie beim Hauptfenster (Ressource 1, sonst raytracer_editor.ico neben der .exe): auch
    // abgedockte Ansichten und das Rendering-Fenster zeigen es in der Taskleiste (vorher das leere Standard-Icon)
    auto programmIcon = [&](int groesse) -> HICON
    {
        HICON ic = (HICON)LoadImageW(hInstance, MAKEINTRESOURCEW(1), IMAGE_ICON, groesse, groesse, LR_DEFAULTCOLOR);
        if (!ic)
        {
            wchar_t pfad[MAX_PATH] = {};
            GetModuleFileNameW(nullptr, pfad, MAX_PATH);
            std::wstring datei = pfad;
            size_t schnitt = datei.find_last_of(L"/\\");
            datei = (schnitt == std::wstring::npos ? std::wstring() : datei.substr(0, schnitt + 1)) + L"raytracer_editor.ico";
            ic = (HICON)LoadImageW(nullptr, datei.c_str(), IMAGE_ICON, groesse, groesse, LR_LOADFROMFILE);
        }
        return ic;
    };
    static HICON iconGross = programmIcon(GetSystemMetrics(SM_CXICON));
    static HICON iconKlein = programmIcon(GetSystemMetrics(SM_CXSMICON));

    WNDCLASSW wc = {};
    wc.lpfnWndProc = Fenster::WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = className;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW); // <- Normale Pfeilhand
    wc.hIcon = iconGross;
    RegisterClassW(&wc);

    hwnd = CreateWindowExW(
        0, className, L"Rendering",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, // Kindfenster (Knoepfe) nicht uebermalen
        CW_USEDEFAULT, CW_USEDEFAULT,
        w, h,
        nullptr, nullptr,
        hInstance, nullptr);

    SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)this);
    if (iconGross)
        SendMessageW(hwnd, WM_SETICON, ICON_BIG, (LPARAM)iconGross);
    if (iconKlein)
        SendMessageW(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)iconKlein);

    ShowWindow(hwnd, SW_SHOW);

    // Im GPU-Modus legt GpuAnsichten die Swapchain an (gemeinsames Device fuer alle Ansichten)
    if (modus == FensterModus::CPU)
        initRenderDevice();

    // Fenster in den Vordergrund holen
    /*SetWindowPos(
        hwnd,
        HWND_TOP,
        0,0,0,0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW
    );*/

    ZeroMemory(&bmi, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    ShowCursor(TRUE);
}

Fenster::~Fenster()
{
    if (renderContext)
        renderContext->ClearState();

    if (quadSampler)
        quadSampler->Release();
    if (quadInputLayout)
        quadInputLayout->Release();
    if (quadPixelShader)
        quadPixelShader->Release();
    if (quadVertexShader)
        quadVertexShader->Release();
    if (quadVertexBuffer)
        quadVertexBuffer->Release();
    if (frameTextureView)
        frameTextureView->Release();
    if (frameUploadTexture)
        frameUploadTexture->Release();
    if (renderBackBufferView)
        renderBackBufferView->Release();
    if (renderSwapChain)
        renderSwapChain->Release();
    if (renderContext)
        renderContext->Release();
    if (renderDevice)
        renderDevice->Release();

    delete[] buffer;
    DestroyWindow(hwnd);
}

// ========== Instanzbezogene WindowProc ==========

LRESULT Fenster::handleMessage(UINT msg, WPARAM wParam, LPARAM lParam)
{
    FensterDirty = true;
    switch (msg)
    {
    case WM_KEYDOWN:
    {
        if (onKey)
            onKey(wParam);

        return 0;
    }

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        /*StretchDIBits(
            hdc,
            0, 0, w, h,
            0, 0, w, h,
            buffer,
            &bmi,
            DIB_RGB_COLORS,
            SRCCOPY);*/

        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_SIZE:
    {
        int newW = LOWORD(lParam);
        int newH = HIWORD(lParam);

        if (newW <= 0 || newH <= 0)
            return 0;

        resizeRenderDevice(newW, newH);

        return 0;
    }

    case WM_COMMAND:
        if (LOWORD(wParam) == ID_ZOOMKNOPF && zoomKnopf)
        {
            // Eingepasst -> 1:1 (Bildmitte bleibt in der Fenstermitte), sonst -> eingepasst
            if (eingepasst)
            {
                ansichtBegrenzen();
                double mx = (fw > 0 ? fw : w) * 0.5, my = (fh > 0 ? fh : h) * 0.5;
                double z0 = std::min(double(fw > 0 ? fw : w) / std::max(w, 1), double(fh > 0 ? fh : h) / std::max(h, 1));
                double ix = (mx - offX) / z0, iy = (my - offY) / z0;
                eingepasst = false;
                zoom = 1.0;
                offX = mx - ix;
                offY = my - iy;
            }
            else
                eingepasst = true;
            zoomKnopfSetzen();
            SetFocus(hwnd);
            update();
            return 0;
        }
        break;

    case WM_MOUSEMOVE:
    {
        if (ziehen) // 1:1-Darstellung: Bild mit der Maus verschieben
        {
            int x = GET_X_LPARAM(lParam), y = GET_Y_LPARAM(lParam);
            if (uebersichtZiehen)
                uebersichtKlick(x, y);
            else
            {
                offX += x - ziehX;
                offY += y - ziehY;
            }
            ziehX = x;
            ziehY = y;
            update();
            return 0;
        }
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
        // Fenster erhielt Fokus - nicht onMouseMove aufrufen, da das einen
        // großen Sprung verursachen kann. Das wird in WireframePerspective::onMouseMove
        // durch den Safety-Check für große Distanzen behandelt.
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

        if (zoomKnopf) // Render-Bild: Zoom um die Mausposition (Shift: feiner)
        {
            POINT p = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)}; // Bildschirmkoordinaten
            ScreenToClient(hwnd, &p);
            double schritt = (GetKeyState(VK_SHIFT) & 0x8000) ? 1.05 : 1.25;
            zoomUm(std::pow(schritt, double(delta) / WHEEL_DELTA), p.x, p.y);
            return 0;
        }

        if (onMouseWheel)
            onMouseWheel(delta, *this);

        return 0;
    }

    case WM_LBUTTONDOWN:
    {
        // als Kindfenster (im Hauptfenster) bekommt die Ansicht die Tastatur erst durch einen Klick
        if (GetWindowLongPtrW(hwnd, GWL_STYLE) & WS_CHILD)
            SetFocus(hwnd);
        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);
        if (zoomKnopf) // Render-Bild: Klick in die Uebersicht springt dorthin; sonst ziehen verschiebt (gezoomt)
        {
            double ux, uy, ub, uh;
            uebersichtZiehen = uebersicht(ux, uy, ub, uh) && x >= ux && x <= ux + ub && y >= uy && y <= uy + uh;
            if (uebersichtZiehen || !eingepasst)
            {
                ziehen = true;
                ziehX = x;
                ziehY = y;
                SetCapture(hwnd);
                if (uebersichtZiehen)
                {
                    uebersichtKlick(x, y);
                    update();
                }
                return 0;
            }
        }

        if (onMouseDown)
            onMouseDown(x, y, *this);

        return 0;
    }

    case WM_LBUTTONUP:
    {
        if (ziehen)
        {
            ziehen = false;
            ReleaseCapture();
            return 0;
        }
        if (onMouseUp)
            onMouseUp();
        return 0;
    }

    case WM_RBUTTONUP:
    {
        if (onRightClick)
        {
            onRightClick(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            return 0;
        }
        break;
    }

    case WM_MBUTTONDOWN:
    {
        if (GetWindowLongPtrW(hwnd, GWL_STYLE) & WS_CHILD)
            SetFocus(hwnd);
        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);

        if (onMiddleMouseDown)
            onMiddleMouseDown(x, y, *this);

        return 0;
    }

    case WM_MBUTTONUP:
    {
        if (onMiddleMouseUp)
            onMiddleMouseUp();

        return 0;
    }

    case WM_DESTROY:
        running = false;
        PostQuitMessage(0);
        return 0;

    case WM_CLOSE:
        if (onClose && !onClose())
            return 0;
        running = false;
        DestroyWindow(hwnd);
        return 0;
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}

// ========== Pixel zeichnen ==========

void Fenster::pixel(int x, int y, COLORREF farbe)
{
    // std::lock_guard<std::mutex> lock(bufferMutex);

    if (x < 0 || y < 0 || x >= w || y >= h)
        return;

    unsigned char r = GetRValue(farbe);
    unsigned char g = GetGValue(farbe);
    unsigned char b = GetBValue(farbe);

    buffer[y * w + x] = (r << 16) | (g << 8) | b;
}

void Fenster::pixel(int x, int y, const Farbe &f)
{
    pixel(breite() - x, y, f.toCOLORREF());
}

bool Fenster::bildKopie(std::vector<uint32_t> &ziel, int &breite, int &hoehe)
{
    std::lock_guard<std::recursive_mutex> sperre(d3dMutex); // bildgroesseSetzen legt den Puffer unter dieser Sperre neu an
    if (!buffer || w <= 0 || h <= 0)
        return false;
    ziel.assign(buffer, buffer + size_t(w) * size_t(h));
    breite = w;
    hoehe = h;
    return true;
}

bool Fenster::bildSetzen(const std::vector<uint32_t> &quelle, int breite, int hoehe)
{
    std::lock_guard<std::recursive_mutex> sperre(d3dMutex);
    if (!buffer || breite != w || hoehe != h || quelle.size() != size_t(w) * size_t(h))
        return false;
    std::copy(quelle.begin(), quelle.end(), buffer);
    return true;
}

void Fenster::update()
{
    std::lock_guard<std::recursive_mutex> sperre(d3dMutex);
    ++bildStand;
    InvalidateRect(hwnd, nullptr, FALSE);
    UpdateWindow(hwnd);

    // GPU-Upload nach dem Renderer-Frame
    uploadFrameToGpu();
    presentFrame();
}

void Fenster::copyBuffer(const Farbe *src)
{
    // std::lock_guard<std::mutex> lock(bufferMutex);

    for (int i = 0; i < w * h; i++)
        buffer[i] = src[i].toCOLORREF();
}

void Fenster::presentFrame()
{
    if (!renderContext || !renderSwapChain || !renderBackBufferView || !quadVertexShader || !quadPixelShader || !frameTextureView || !quadSampler)
        return;

    ID3D11RenderTargetView *renderTargets[] = {renderBackBufferView};
    renderContext->OMSetRenderTargets(1, renderTargets, nullptr);

    // Bild (w x h) im Fenster (fw x fh): eingepasst (Seitenverhaeltnis bleibt, mittig) oder mit
    // Zoom (Mausrad, 1:1 = Originalgroesse) an der Stelle offX/offY, mit der Maus verschiebbar
    ansichtBegrenzen();
    const double z = eingepasst ? std::min(double(fw > 0 ? fw : w) / std::max(w, 1), double(fh > 0 ? fh : h) / std::max(h, 1))
                                : zoom;
    D3D11_VIEWPORT viewport = {};
    viewport.Width = float(w * z);
    viewport.Height = float(h * z);
    viewport.TopLeftX = float(offX);
    viewport.TopLeftY = float(offY);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    renderContext->RSSetViewports(1, &viewport);

    UINT stride = sizeof(float) * 5;
    UINT offset = 0;
    renderContext->IASetVertexBuffers(0, 1, &quadVertexBuffer, &stride, &offset);
    renderContext->IASetInputLayout(quadInputLayout);
    renderContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    renderContext->VSSetShader(quadVertexShader, nullptr, 0);
    renderContext->PSSetShader(quadPixelShader, nullptr, 0);
    renderContext->PSSetShaderResources(0, 1, &frameTextureView);
    ID3D11SamplerState *sampler = (!eingepasst && zoom >= 2.0 && punktSampler) ? punktSampler : quadSampler;
    renderContext->PSSetSamplers(0, 1, &sampler);

    float clearColor[4] = {0.12f, 0.12f, 0.12f, 1}; // Rand um das Bild (vorher rot)

    renderContext->ClearRenderTargetView(
        renderBackBufferView,
        clearColor);

    renderContext->Draw(6, 0);

    // Uebersicht oben links: ganzes Bild klein, sichtbarer Ausschnitt als Rahmen (nur wenn nicht alles sichtbar ist)
    double ux, uy, ub, uh;
    if (uebersicht(ux, uy, ub, uh) && farbShader && farbPuffer)
    {
        rechteckFuellen(float(ux - 3), float(uy - 3), float(ub + 6), float(uh + 6), 0.05f, 0.05f, 0.05f, 1.0f);
        D3D11_VIEWPORT klein = {float(ux), float(uy), float(ub), float(uh), 0.0f, 1.0f};
        renderContext->RSSetViewports(1, &klein);
        renderContext->PSSetShader(quadPixelShader, nullptr, 0);
        renderContext->PSSetShaderResources(0, 1, &frameTextureView);
        renderContext->PSSetSamplers(0, 1, &quadSampler);
        renderContext->Draw(6, 0);

        // sichtbarer Ausschnitt im Bild (in Bildpixeln), auf die Uebersicht umgerechnet
        const double fb = fw > 0 ? fw : w, fhh = fh > 0 ? fh : h;
        double x0 = std::max(0.0, -offX / z), y0 = std::max(0.0, -offY / z);
        double x1 = std::min(double(w), (fb - offX) / z), y1 = std::min(double(h), (fhh - offY) / z);
        double sk = ub / std::max(w, 1);
        float rx = float(ux + x0 * sk), ry = float(uy + y0 * sk), rb = float((x1 - x0) * sk), rh = float((y1 - y0) * sk);
        const float d = 2.0f; // Rahmenbreite
        rechteckFuellen(rx, ry, rb, d, 1.0f, 0.3f, 0.2f, 1.0f);
        rechteckFuellen(rx, ry + rh - d, rb, d, 1.0f, 0.3f, 0.2f, 1.0f);
        rechteckFuellen(rx, ry, d, rh, 1.0f, 0.3f, 0.2f, 1.0f);
        rechteckFuellen(rx + rb - d, ry, d, rh, 1.0f, 0.3f, 0.2f, 1.0f);
    }

    HRESULT hr = renderSwapChain->Present(1, 0);

    if (FAILED(hr))
    {
        // 887a0005 = DXGI_ERROR_DEVICE_REMOVED: den Grund dazu ausgeben (nur einmal)
        static bool gemeldet = false;
        if (!gemeldet)
        {
            gemeldet = true;
            HRESULT grund = renderDevice ? renderDevice->GetDeviceRemovedReason() : S_OK;
            const char *text = grund == DXGI_ERROR_DEVICE_HUNG              ? "DEVICE_HUNG (Befehl hing / Treiber-Timeout)"
                               : grund == DXGI_ERROR_DEVICE_RESET           ? "DEVICE_RESET (fehlerhafter Befehl, Geraet zurueckgesetzt)"
                               : grund == DXGI_ERROR_DRIVER_INTERNAL_ERROR  ? "DRIVER_INTERNAL_ERROR (Treiberfehler)"
                               : grund == DXGI_ERROR_INVALID_CALL           ? "INVALID_CALL (ungueltiger Aufruf)"
                               : grund == S_OK                              ? "kein Geraetefehler"
                                                                            : "unbekannt";
            std::cout << "Present failed: " << std::hex << hr << ", Grund: " << grund << " = " << text << std::dec
                      << std::endl;
        }
    }
}

void Fenster::processEvents()
{
    MSG msg;
    while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
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
        if (FensterDirty)
        {
            update();
            FensterDirty = false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(33)); // ~30 FPS, damit CPU nicht 100% arbeiten muss
    }
}

void Fenster::setTitle(const wchar_t *title)
{
    SetWindowTextW(hwnd, title);
}

std::wstring askFilename(HWND owner)
{
    wchar_t filename[MAX_PATH] = L"";

    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = L"BMP Dateien (*.bmp)\0*.bmp\0Alle Dateien (*.*)\0*.*\0";
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_EXPLORER | OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT;
    ofn.lpstrDefExt = L"bmp";

    if (GetSaveFileNameW(&ofn))
    {
        return filename;
    }

    return L""; // Abbruch
}

bool saveWindowBMPWithTimestamp(HWND hwnd, const std::wstring &baseName)
{
    std::wcout << "Speichere: " << baseName << std::endl;
    if (!hwnd)
        return false;

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
    GetDIBits(hdcWindow, hBitmap, 0, h, buffer.data(), (BITMAPINFO *)&bi, DIB_RGB_COLORS);

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
    FILE *file = _wfopen(filename.c_str(), L"wb");
    if (!file)
        return false;

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

    unsigned char pad[3] = {0, 0, 0};
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

bool Fenster::saveBMP(const wchar_t *filename)
{
    // 24-Bit-BMP, Zeilen von unten nach oben (Standard). Vorher wurde der Header des Fensterpuffers
    // uebernommen (32 Bit, negative Hoehe), die Pixel aber mit 24 Bit geschrieben: Header und Daten
    // passten nicht zusammen, kein Programm konnte die Datei lesen.
    const int zeile = (w * 3 + 3) & ~3; // auf 4 Byte aufgerundet
    const DWORD bildGroesse = DWORD(zeile) * DWORD(h);

    BITMAPFILEHEADER bmfHeader = {};
    bmfHeader.bfType = 0x4D42; // 'BM'
    bmfHeader.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    bmfHeader.bfSize = bmfHeader.bfOffBits + bildGroesse;

    BITMAPINFOHEADER biHeader = {};
    biHeader.biSize = sizeof(BITMAPINFOHEADER);
    biHeader.biWidth = w;
    biHeader.biHeight = h; // positiv: unterste Zeile zuerst
    biHeader.biPlanes = 1;
    biHeader.biBitCount = 24;
    biHeader.biCompression = BI_RGB;
    biHeader.biSizeImage = bildGroesse;
    biHeader.biXPelsPerMeter = 2835; // 72 dpi
    biHeader.biYPelsPerMeter = 2835;

    FILE *file = _wfopen(filename, L"wb");
    if (!file)
        return false;

    bool ok = fwrite(&bmfHeader, sizeof(bmfHeader), 1, file) == 1 &&
              fwrite(&biHeader, sizeof(biHeader), 1, file) == 1;

    // buffer ist von oben nach unten (0x00RRGGBB); BMP will BGR, unterste Zeile zuerst
    std::vector<unsigned char> daten(size_t(zeile), 0);
    for (int y = h - 1; y >= 0 && ok; y--)
    {
        for (int x = 0; x < w; x++)
        {
            uint32_t pixel = buffer[size_t(y) * w + x];
            daten[size_t(x) * 3 + 0] = pixel & 0xFF;
            daten[size_t(x) * 3 + 1] = (pixel >> 8) & 0xFF;
            daten[size_t(x) * 3 + 2] = (pixel >> 16) & 0xFF;
        }
        ok = fwrite(daten.data(), 1, daten.size(), file) == daten.size();
    }

    fclose(file);
    return ok;
}

bool Fenster::saveBMPWithTimestamp(const std::wstring &baseName)
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
    FILE *file = _wfopen(filename.c_str(), L"wb");
    if (!file)
        return false;

    // BMP Header vorbereiten
    BITMAPFILEHEADER bmfHeader = {};
    BITMAPINFOHEADER biHeader = bmi.bmiHeader; // vorhandener BITMAPINFOHEADER
    biHeader.biBitCount = 24;                  // 24-Bit RGB
    biHeader.biCompression = BI_RGB;           // unkomprimiert

    int rowSize = (w * 3 + 3) & (~3); // Zeilenbreite auf Vielfaches von 4 runden
    DWORD dwBmpSize = rowSize * h;

    bmfHeader.bfType = 0x4D42; // 'BM'
    bmfHeader.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    bmfHeader.bfSize = bmfHeader.bfOffBits + dwBmpSize;

    // Header schreiben
    fwrite(&bmfHeader, sizeof(bmfHeader), 1, file);
    fwrite(&biHeader, sizeof(biHeader), 1, file);

    // Pixel schreiben (unten nach oben)
    unsigned char pad[3] = {0, 0, 0};
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

void Fenster::resizeRenderDevice(int newW, int newH)
{
    std::lock_guard<std::recursive_mutex> sperre(d3dMutex);
    fw = newW;
    fh = newH;
    zoomKnopfSetzen();

    // Bildpuffer nur neu, wenn sich die Bildgroesse aendert (vorher bei jeder Fenstergroesse: das
    // gerenderte Bild ging verloren, und ein laufendes Rendering schrieb in freigegebenen Speicher)
    // waehrend des Renderns bleibt das Bild, wie es ist (die Render-Threads schreiben hinein)
    const bool bildNeu = !festeBildgroesse && !rendernRunning && (w != newW || h != newH);
    if (bildNeu)
    {
        w = newW;
        h = newH;
    }

    if (bildNeu)
    {
        std::lock_guard<std::mutex> lock(bufferMutex);

        delete[] buffer;
        buffer = new uint32_t[w * h];

        ZeroMemory(&bmi, sizeof(bmi));
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = w;
        bmi.bmiHeader.biHeight = -h;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;
    }

    if (!renderSwapChain)
        return;

    renderContext->OMSetRenderTargets(0, nullptr, nullptr);

    if (renderBackBufferView)
    {
        renderBackBufferView->Release();
        renderBackBufferView = nullptr;
    }

    if (frameTextureView)
    {
        frameTextureView->Release();
        frameTextureView = nullptr;
    }

    if (frameUploadTexture)
    {
        frameUploadTexture->Release();
        frameUploadTexture = nullptr;
    }

    HRESULT hr = renderSwapChain->ResizeBuffers(
        0,
        fw,
        fh,
        DXGI_FORMAT_UNKNOWN,
        0);

    if (FAILED(hr))
    {
        std::cout << "ResizeBuffers failed\n";
        return;
    }

    ID3D11Texture2D *backBufferTexture = nullptr;

    hr = renderSwapChain->GetBuffer(
        0,
        __uuidof(ID3D11Texture2D),
        (void **)&backBufferTexture);

    if (FAILED(hr))
        return;

    hr = renderDevice->CreateRenderTargetView(
        backBufferTexture,
        nullptr,
        &renderBackBufferView);

    backBufferTexture->Release();

    if (FAILED(hr))
        return;

    D3D11_TEXTURE2D_DESC texDesc = {};
    texDesc.Width = w;
    texDesc.Height = h;
    texDesc.MipLevels = 1;
    texDesc.ArraySize = 1;
    texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    texDesc.SampleDesc.Count = 1;
    texDesc.Usage = D3D11_USAGE_DYNAMIC;
    texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    texDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    hr = renderDevice->CreateTexture2D(
        &texDesc,
        nullptr,
        &frameUploadTexture);

    if (FAILED(hr))
        return;

    hr = renderDevice->CreateShaderResourceView(
        frameUploadTexture,
        nullptr,
        &frameTextureView);

    if (FAILED(hr))
        return;

    std::cout << "Resize: "
              << w << " x "
              << h << std::endl;
}

// ========== Bildgroesse unabhaengig vom Fenster ==========

void Fenster::bildTexturAnlegen()
{
    if (!renderDevice)
        return;
    if (frameTextureView)
    {
        frameTextureView->Release();
        frameTextureView = nullptr;
    }
    if (frameUploadTexture)
    {
        frameUploadTexture->Release();
        frameUploadTexture = nullptr;
    }
    D3D11_TEXTURE2D_DESC texDesc = {};
    texDesc.Width = w;
    texDesc.Height = h;
    texDesc.MipLevels = 1;
    texDesc.ArraySize = 1;
    texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    texDesc.SampleDesc.Count = 1;
    texDesc.Usage = D3D11_USAGE_DYNAMIC;
    texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    texDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (SUCCEEDED(renderDevice->CreateTexture2D(&texDesc, nullptr, &frameUploadTexture)))
        renderDevice->CreateShaderResourceView(frameUploadTexture, nullptr, &frameTextureView);
}

void Fenster::bildgroesseSetzen(int breite, int hoehe)
{
    std::lock_guard<std::recursive_mutex> sperre(d3dMutex);
    int nb = breite, nh = hoehe;
    festeBildgroesse = breite > 0 && hoehe > 0;
    if (!festeBildgroesse)
    {
        nb = fw > 0 ? fw : w;
        nh = fh > 0 ? fh : h;
    }
    nb = std::max(1, std::min(nb, 16384)); // Grenze der GPU-Textur
    nh = std::max(1, std::min(nh, 16384));
    if (nb == w && nh == h)
        return;
    {
        std::lock_guard<std::mutex> lock(bufferMutex);
        delete[] buffer;
        w = nb;
        h = nh;
        buffer = new uint32_t[size_t(w) * size_t(h)]();
        ZeroMemory(&bmi, sizeof(bmi));
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = w;
        bmi.bmiHeader.biHeight = -h;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;
    }
    bildTexturAnlegen();
    ansichtNeu = true;
    std::cout << "Bildgroesse: " << w << " x " << h << std::endl;
}

// ========== Knopf "Eingepasst / Originalgroesse" ==========


void Fenster::zoomKnopfAnlegen()
{
    if (zoomKnopf)
        return;
    zoomKnopf = CreateWindowExW(0, L"BUTTON", L"", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 10, 10, hwnd,
                                (HMENU)(INT_PTR)ID_ZOOMKNOPF, GetModuleHandle(nullptr), nullptr);
    SendMessageW(zoomKnopf, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
    zoomKnopfSetzen();
}

void Fenster::zoomKnopfSetzen()
{
    if (!zoomKnopf)
        return;
    std::wstring text;
    if (eingepasst)
        text = L"Eingepasst → 1:1 Originalgröße";
    else
        text = std::to_wstring(int(zoom * 100.0 + 0.5)) + L" % (Mausrad zoomt, ziehen verschiebt) → Eingepasst";
    SetWindowTextW(zoomKnopf, text.c_str());
    const int b = eingepasst ? 230 : 330, hh = 26;
    SetWindowPos(zoomKnopf, HWND_TOP, std::max(0, (fw > 0 ? fw : w) - b - 8), 8, b, hh, SWP_NOACTIVATE);
}

// ========== Zoom, Verschieben und Uebersicht im Render-Bild ==========

// Lage begrenzen: kleiner als das Fenster -> mittig, groesser -> kein leerer Rand
void Fenster::ansichtBegrenzen()
{
    const double fb = fw > 0 ? fw : w, fhh = fh > 0 ? fh : h;
    const double z = eingepasst ? std::min(fb / std::max(w, 1), fhh / std::max(h, 1)) : zoom;
    const double bb = w * z, bh = h * z;
    if (eingepasst || ansichtNeu)
    {
        offX = (fb - bb) * 0.5;
        offY = (fhh - bh) * 0.5;
        ansichtNeu = false;
    }
    offX = bb <= fb ? (fb - bb) * 0.5 : std::min(0.0, std::max(fb - bb, offX));
    offY = bh <= fhh ? (fhh - bh) * 0.5 : std::min(0.0, std::max(fhh - bh, offY));
}

void Fenster::zoomUm(double faktor, double mx, double my)
{
    const double fb = fw > 0 ? fw : w, fhh = fh > 0 ? fh : h;
    const double z = eingepasst ? std::min(fb / std::max(w, 1), fhh / std::max(h, 1)) : zoom;
    ansichtBegrenzen();
    const double ix = (mx - offX) / z, iy = (my - offY) / z; // Bildpunkt unter der Maus bleibt stehen
    const double zMax = std::min(32.0, 30000.0 / std::max(1, std::max(w, h))); // Grenze des Viewports
    const double zMin = std::min(0.05, std::min(fb / std::max(w, 1), fhh / std::max(h, 1)));
    eingepasst = false;
    zoom = std::max(zMin, std::min(zMax, z * faktor));
    offX = mx - ix * zoom;
    offY = my - iy * zoom;
    zoomKnopfSetzen();
    update();
}

// Uebersicht oben links (hoechstens 180 x 120), nur wenn nicht das ganze Bild sichtbar ist
bool Fenster::uebersicht(double &x, double &y, double &b, double &h2) const
{
    if (!zoomKnopf || eingepasst)
        return false;
    const double fb = fw > 0 ? fw : w, fhh = fh > 0 ? fh : h;
    if (w * zoom <= fb + 0.5 && h * zoom <= fhh + 0.5)
        return false;
    const double s = std::min(180.0 / std::max(w, 1), 120.0 / std::max(h, 1));
    x = 12;
    y = 12;
    b = w * s;
    h2 = h * s;
    return true;
}

// Klick/Ziehen in der Uebersicht: diesen Bildpunkt in die Fenstermitte
void Fenster::uebersichtKlick(int x, int y)
{
    double ux, uy, ub, uh;
    if (!uebersicht(ux, uy, ub, uh))
        return;
    const double s = ub / std::max(w, 1);
    const double ix = (x - ux) / s, iy = (y - uy) / s;
    offX = (fw > 0 ? fw : w) * 0.5 - ix * zoom;
    offY = (fh > 0 ? fh : h) * 0.5 - iy * zoom;
}

void Fenster::rechteckFuellen(float x, float y, float b, float h2, float r, float g, float bl, float a)
{
    if (!farbShader || !farbPuffer || b <= 0 || h2 <= 0)
        return;
    const float farbe[4] = {r, g, bl, a};
    renderContext->UpdateSubresource(farbPuffer, 0, nullptr, farbe, 0, 0);
    D3D11_VIEWPORT vp = {x, y, b, h2, 0.0f, 1.0f};
    renderContext->RSSetViewports(1, &vp);
    renderContext->PSSetShader(farbShader, nullptr, 0);
    renderContext->PSSetConstantBuffers(0, 1, &farbPuffer);
    renderContext->Draw(6, 0);
}
