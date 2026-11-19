#include "gpu_ansichten.h"
#include "wireframe.h"
#include "camera.h"
#include "scene.h"
#include "object.h"
#include "light.h"
#include "sphereLight.h"
#include "mesh.h"
#include "tree.h"
#include "kugel.h"
#include "pfeil.h"
#include "text3d.h"
#include "bearbeitung.h"
#include "gruppe.h"
#include "instanz.h"
#include <functional>
#include <unordered_set>
#include "volumenbox.h"
#include "drehpunkt.h"
#include "kanten.h"
#include "stab.h"
#include "textur_abbildung.h"
#include "texture.h"
#include <d3dcompiler.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iostream>
#include <mutex>

extern std::mutex sceneMutex;
extern bool overObject;

// true: Gitterlinien und Achsen liegen wie bisher immer ueber allen Objekten
// false: Gitterlinien werden von Objekten verdeckt
static constexpr bool GITTER_IMMER_VORNE = false;

// true: eine Ansicht wird nur neu gezeichnet, wenn sich etwas geaendert hat (spart CPU/GPU/Akku)
static constexpr bool NUR_BEI_AENDERUNG = true;

// Auswahl/Hover: Objekt behaelt seine Farbe und wird leicht getoent (Alpha = Staerke, 0x40 = 25 %).
// Format 0xAABBGGRR. Vorher wurden die Farben invertiert (aus Gruen wurde Pink).
static constexpr uint32_t TOENUNG_AUSWAHL = 0x4000FFFF; // gelb
static constexpr uint32_t TOENUNG_HOVER = 0x40FFFF00;   // cyan

static constexpr int ZIEL_FPS = 60;
static constexpr int MAX_LICHTER = 32;

// ======================================================================
// Shader
// ======================================================================

static const char *SHADER_GEOMETRIE_VS = R"(
cbuffer Geometrie : register(b0)
{
    row_major float4x4 transform;
    float biasMul;
    float biasAdd;
    float modus;     // 0 = normal, 1 = Auswahl/Hover (getoent)
    float frei;
    float4 versatz;
    float4 zeileX;   // Instanzen: lineare Abbildung (Zeilen), sonst Einheit
    float4 zeileY;
    float4 zeileZ;
    float4 normX;    // dazu fuer Normalen (inverse Transponierte)
    float4 normY;
    float4 normZ;
};

cbuffer Farben : register(b1)
{
    float4 toenung;  // rgb = Toenungsfarbe, a = Staerke
    float4 farbeB;
};

struct VSIn  { float3 pos : POSITION; float4 farbe : COLOR; };
struct VSOut { float4 pos : SV_Position; float4 farbe : COLOR; };

VSOut main(VSIn i)
{
    VSOut o;
    float3 q = float3(dot(zeileX.xyz, i.pos), dot(zeileY.xyz, i.pos), dot(zeileZ.xyz, i.pos));
    o.pos = mul(float4(q + versatz.xyz, 1.0f), transform);
    // Linien etwas naeher zum Betrachter, damit sie auf Flaechen sichtbar bleiben
    o.pos.z = o.pos.z * biasMul + biasAdd * o.pos.w;
    o.farbe = i.farbe;
    if (modus > 0.5f)
        o.farbe.rgb = lerp(o.farbe.rgb, toenung.rgb, toenung.a);
    return o;
}
)";

// Beleuchtung pro Flaeche wie in Dreieck/Pfeil/Kugel/Stab::drawFlat():
// Helligkeit = max(0, N * L), L = normierte Summe der Lichtrichtungen an der Flaechenmitte
static const char *SHADER_BELEUCHTET_VS = R"(
cbuffer Geometrie : register(b0)
{
    row_major float4x4 transform;
    float biasMul;
    float biasAdd;
    float modus;     // 0 = normal, 1 = Auswahl/Hover (getoent)
    float frei;
    float4 versatz;  // Objekt-Ursprung relativ zum Frame-Ursprung
    float4 zeileX;   // Instanzen: lineare Abbildung (Zeilen), sonst Einheit
    float4 zeileY;
    float4 zeileZ;
    float4 normX;    // dazu fuer Normalen (inverse Transponierte)
    float4 normY;
    float4 normZ;
};

cbuffer Farben : register(b1)
{
    float4 toenung;  // rgb = Toenungsfarbe, a = Staerke
    float4 farbeB;
};

cbuffer Beleuchtung : register(b2)
{
    float4 kamera;              // cam[0].position relativ zum Frame-Ursprung
    float4 lichter[32];         // xyz: Position (w = 0) oder Richtung (w = 1)
    int4 anzahlLichter;
};

// Zeile 0..2: Farbe(x,0,0), Farbe(0,x,0), Farbe(0,0,x) nach toCOLORREF; Zeile 3: Farbe(0,0,0)
Texture2D<float4> farbTabelle : register(t0);

struct VSIn
{
    float3 pos : POSITION;
    float3 normale : NORMAL;
    float3 mitte : TEXCOORD0;
    float4 basis : COLOR;   // rgb = mat.diffuse, a = Flags (4 = mit Textur)
    float2 uv : TEXCOORD1;  // Bildkoordinaten (wie im Raytracer, textur_abbildung.h)
};

struct VSOut
{
    float4 pos : SV_Position;
    float4 farbe : COLOR;
    float2 uv : TEXCOORD0;
    float licht : TEXCOORD1;      // Helligkeit 0..1 (fuer die Texturfarbe im Pixel-Shader)
    float texturiert : TEXCOORD2; // 1 = Farbe aus dem Bild
};

float3 wieCpu(float3 c)
{
    int3 i = int3(saturate(c) * 1023.0f + 0.5f);
    float3 r = farbTabelle.Load(int3(i.r, 0, 0)).rgb;
    float3 g = farbTabelle.Load(int3(i.g, 1, 0)).rgb;
    float3 b = farbTabelle.Load(int3(i.b, 2, 0)).rgb;
    float3 n = farbTabelle.Load(int3(0, 3, 0)).rgb;
    return r + g + b - 2.0f * n;
}

VSOut main(VSIn i)
{
    VSOut o;
    float3 p = float3(dot(zeileX.xyz, i.pos), dot(zeileY.xyz, i.pos), dot(zeileZ.xyz, i.pos)) + versatz.xyz;
    o.pos = mul(float4(p, 1.0f), transform);
    o.pos.z = o.pos.z * biasMul + biasAdd * o.pos.w;

    uint flags = (uint)(i.basis.a + 0.5f);
    float3 m = float3(dot(zeileX.xyz, i.mitte), dot(zeileY.xyz, i.mitte), dot(zeileZ.xyz, i.mitte)) + versatz.xyz;
    float3 n = float3(dot(normX.xyz, i.normale), dot(normY.xyz, i.normale), dot(normZ.xyz, i.normale));
    float nl = length(n);
    n = nl > 1e-20f ? n / nl : i.normale;

    if (flags & 1)
    {
        float3 blick = normalize(m - kamera.xyz);
        n = dot(n, blick) < 0.0f ? n : -n;
    }

    float3 L = float3(0.0f, 0.0f, 0.0f);
    for (int k = 0; k < anzahlLichter.x; ++k)
        L += lichter[k].w > 0.5f ? lichter[k].xyz : normalize(lichter[k].xyz - m);

    float laenge = length(L);
    float intensitaet = laenge > 1e-12f ? max(0.0f, dot(n, L / laenge)) : 0.0f;

    o.farbe = float4(wieCpu(i.basis.rgb * intensitaet), 1.0f);
    o.uv = i.uv;
    o.licht = intensitaet;
    o.texturiert = (flags & 4) ? 1.0f : 0.0f;

    // Auswahl/Hover: normale Farbe, leicht in Richtung Gelb/Cyan getoent (statt invertiert)
    if (modus > 0.5f)
        o.farbe.rgb = lerp(o.farbe.rgb, toenung.rgb, toenung.a);
    return o;
}
)";

static const char *SHADER_GEOMETRIE_PS = R"(
float4 main(float4 pos : SV_Position, float4 farbe : COLOR) : SV_Target
{
    return farbe;
}
)";

// Flaechen mit Textur: Bild abtasten (Mipmaps, wiederholt), mit der Helligkeit des Vertex-Shaders mischen und wie
// die anderen Farben ueber die Farbtabelle umrechnen; Alpha < 0,5 = ausgeschnitten
static const char *SHADER_TEXTUR_PS = R"(
cbuffer Geometrie : register(b0)
{
    row_major float4x4 transform;
    float biasMul;
    float biasAdd;
    float modus;
    float frei;
};

cbuffer Farben : register(b1)
{
    float4 toenung;
    float4 farbeB;
};

Texture2D<float4> farbTabelle : register(t0);
Texture2D<float4> bild : register(t1);
SamplerState abtaster : register(s0);

struct PSIn
{
    float4 pos : SV_Position;
    float4 farbe : COLOR;
    float2 uv : TEXCOORD0;
    float licht : TEXCOORD1;
    float texturiert : TEXCOORD2;
};

float3 wieCpu(float3 c)
{
    int3 i = int3(saturate(c) * 1023.0f + 0.5f);
    float3 r = farbTabelle.Load(int3(i.r, 0, 0)).rgb;
    float3 g = farbTabelle.Load(int3(i.g, 1, 0)).rgb;
    float3 b = farbTabelle.Load(int3(i.b, 2, 0)).rgb;
    float3 n = farbTabelle.Load(int3(0, 3, 0)).rgb;
    return r + g + b - 2.0f * n;
}

float4 main(PSIn i) : SV_Target
{
    if (i.texturiert < 0.5f)
        return i.farbe;
    float4 t = bild.Sample(abtaster, i.uv);
    if (t.a < 0.5f)
        discard;
    float3 c = wieCpu(t.rgb * i.licht);
    if (modus > 0.5f)
        c = lerp(c, toenung.rgb, toenung.a);
    return float4(c, 1.0f);
}
)";

static const char *SHADER_MASKE_PS = R"(
cbuffer Farben : register(b1)
{
    float4 farbeA;
    float4 farbeB;
};

float4 main(float4 pos : SV_Position, float4 farbe : COLOR) : SV_Target
{
    return farbeA;
}
)";

static const char *SHADER_VOLLBILD_VS = R"(
float4 main(uint id : SV_VertexID) : SV_Position
{
    float2 uv = float2((id << 1) & 2, id & 2);
    return float4(uv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
}
)";

// Maske: r = ausgewaehltes Objekt, g = Hover-Objekt. Gezeichnet wird ein Rand ausserhalb der Maske.
static const char *SHADER_UMRISS_PS = R"(
Texture2D<float2> maske : register(t0);

cbuffer Farben : register(b1)
{
    float4 farbeAuswahl;
    float4 farbeHover;
};

float4 main(float4 pos : SV_Position) : SV_Target
{
    int2 p = int2(pos.xy);
    float2 mitte = maske.Load(int3(p, 0));
    float2 umgebung = float2(0.0f, 0.0f);

    [unroll] for (int dy = -2; dy <= 2; ++dy)
        [unroll] for (int dx = -2; dx <= 2; ++dx)
            umgebung = max(umgebung, maske.Load(int3(p + int2(dx, dy), 0)));

    if (mitte.x < 0.5f && umgebung.x > 0.5f)
        return farbeAuswahl;

    if (mitte.x < 0.5f && mitte.y < 0.5f && umgebung.y > 0.5f)
        return farbeHover;

    discard;
    return float4(0.0f, 0.0f, 0.0f, 0.0f);
}
)";

static bool kompilieren(const char *quelle, const char *profil, GpuRef<ID3DBlob> &ergebnis)
{
    GpuRef<ID3DBlob> fehler;
    HRESULT hr = D3DCompile(quelle, strlen(quelle), nullptr, nullptr, nullptr, "main", profil,
                            D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, ergebnis.put(), fehler.put());

    if (FAILED(hr))
    {
        std::cerr << "GpuAnsichten: Shader (" << profil << ") nicht kompiliert\n";
        if (fehler)
            std::cerr << (const char *)fehler->GetBufferPointer() << "\n";
        return false;
    }
    return true;
}

// ======================================================================
// Hilfsfunktionen
// ======================================================================

// FNV-1a: Pruefsumme ueber alles, was das Bild beeinflusst
struct Pruefsumme
{
    uint64_t h = 1469598103934665603ull;

    void bytes(const void *p, size_t n)
    {
        const unsigned char *c = static_cast<const unsigned char *>(p);
        for (size_t i = 0; i < n; ++i)
        {
            h ^= c[i];
            h *= 1099511628211ull;
        }
    }
    void zahl(uint64_t x) { bytes(&x, sizeof(x)); }
    void wert(double x) { bytes(&x, sizeof(x)); }
    void zeiger(const void *p) { zahl(uint64_t(reinterpret_cast<uintptr_t>(p))); }
    void vektor(const Vector3D &v)
    {
        wert(v.x);
        wert(v.y);
        wert(v.z);
    }
    void farbe(const Farbe &f)
    {
        wert(f.r);
        wert(f.g);
        wert(f.b);
    }
    void dreieck(const Dreieck &t)
    {
        vektor(t.a);
        vektor(t.b);
        vektor(t.c);
        vektor(t.position);
        vektor(t.normale);
        farbe(t.mat.diffuse);
    }
    // Groesse, Speicherort und einige Dreiecke: aendert sich bei translate/rotate/scale/Neuaufbau
    void dreiecke(const std::vector<Dreieck> &v)
    {
        zahl(v.size());
        zeiger(v.data());
        if (v.empty())
            return;
        dreieck(v.front());
        dreieck(v[v.size() / 3]);
        dreieck(v[v.size() / 2]);
        dreieck(v[2 * v.size() / 3]);
        dreieck(v.back());
    }
};

static void spalteSetzen(double m[16], int spalte, const Vector3D &v, double t)
{
    m[0 * 4 + spalte] = v.x;
    m[1 * 4 + spalte] = v.y;
    m[2 * 4 + spalte] = v.z;
    m[3 * 4 + spalte] = t;
}

// Gleiche Abbildung wie Wireframe::project(), nur fuer Punkte relativ zu 'ursprung'
// (Zeilenvektor * Matrix, wie mul(float4(pos,1), transform) im Shader).
// Tiefe ist umgekehrt (nah = 1, fern = 0), das gibt mit float-Tiefe die beste Genauigkeit.
static void weltMatrix(const AnsichtParameter &a, const Vector3D &ursprung,
                       int breite, int hoehe, double tiefenRadius, double m[16])
{
    Vector3D d = ursprung - a.auge;
    double tR = d * a.rechts;
    double tO = d * a.oben;
    double tV = d * a.vorne;

    if (!a.perspektivisch)
    {
        double kx = 2.0 * a.pixelProEinheit / breite;
        double ky = 2.0 * a.pixelProEinheit / hoehe;
        double kz = -1.0 / (2.0 * tiefenRadius);

        spalteSetzen(m, 0, a.rechts * kx, tR * kx);
        spalteSetzen(m, 1, a.oben * ky, tO * ky);
        spalteSetzen(m, 2, a.vorne * kz, 0.5 + tV * kz);
        spalteSetzen(m, 3, Vector3D(0, 0, 0), 1.0);
        return;
    }

    const double nah = 0.05; // Near-Plane; Fern-Ebene ist unendlich weit weg

    spalteSetzen(m, 0, a.rechts * (1.0 / a.tanHalbX), tR / a.tanHalbX);
    spalteSetzen(m, 1, a.oben * (1.0 / a.tanHalbY), tO / a.tanHalbY);
    spalteSetzen(m, 2, Vector3D(0, 0, 0), nah);
    spalteSetzen(m, 3, a.vorne, tV);
}

// Pixelkoordinaten (0,0 = oben links) -> Bildschirm
static void pixelMatrix(int breite, int hoehe, double m[16])
{
    spalteSetzen(m, 0, Vector3D(2.0 / breite, 0, 0), -1.0);
    spalteSetzen(m, 1, Vector3D(0, -2.0 / hoehe, 0), 1.0);
    spalteSetzen(m, 2, Vector3D(0, 0, 0), 0.0);
    spalteSetzen(m, 3, Vector3D(0, 0, 0), 1.0);
}

static void rechteck(std::vector<GpuVertex> &v, float x0, float y0, float x1, float y1, uint32_t farbe)
{
    v.push_back({x0, y0, 0, farbe});
    v.push_back({x1, y0, 0, farbe});
    v.push_back({x0, y1, 0, farbe});
    v.push_back({x0, y1, 0, farbe});
    v.push_back({x1, y0, 0, farbe});
    v.push_back({x1, y1, 0, farbe});
}

static void farbeZuFloat(uint32_t rgba, float f[4])
{
    for (int i = 0; i < 4; ++i)
        f[i] = float((rgba >> (8 * i)) & 0xFF) / 255.0f;
}

static DrawMode zeichenModus(int modus)
{
    return modus == 1 ? DrawMode::HIGHLIGHT : modus == 2 ? DrawMode::HOVER
                                                         : DrawMode::NORMAL;
}

// Setzt Wireframe::aufzeichnung fuer die Dauer eines Blocks, auch bei Exceptions
struct AufzeichnungAktiv
{
    explicit AufzeichnungAktiv(GpuAufzeichnung *a) { Wireframe::aufzeichnung = a; }
    ~AufzeichnungAktiv() { Wireframe::aufzeichnung = nullptr; }
};

// ======================================================================
// Start / Stop
// ======================================================================

GpuAnsichten::GpuAnsichten(const std::vector<Wireframe *> &ansichten)
{
    ziele.resize(ansichten.size());
    for (size_t i = 0; i < ansichten.size(); ++i)
        ziele[i].wf = ansichten[i];
}

GpuAnsichten::~GpuAnsichten()
{
    stop();
}

void GpuAnsichten::start()
{
    if (laufen || ziele.empty())
        return;

    laufen = true;
    thread = std::thread([this]
                         { schleife(); });
}

void GpuAnsichten::stop()
{
    laufen = false;
    if (thread.joinable())
        thread.join();
}

void GpuAnsichten::schleife()
{
    if (!initGeraet() || !initShader() || !initZustaende() || !initFarbTabelle())
    {
        std::cerr << "GpuAnsichten: Initialisierung fehlgeschlagen, Ansichten bleiben leer\n";
        allesFreigeben();
        laufen = false;
        return;
    }

    const auto frameZeit = std::chrono::milliseconds(1000 / ZIEL_FPS);

    while (laufen)
    {
        auto beginn = std::chrono::steady_clock::now();

        try
        {
            szeneAufzeichnen();
            hochladen(objektPuffer, objektVertices);

            for (auto &z : ziele)
                ansichtZeichnen(z);
        }
        catch (const std::exception &e)
        {
            std::cerr << "GpuAnsichten: " << e.what() << "\n";
        }

        auto dauer = std::chrono::steady_clock::now() - beginn;
        if (dauer < frameZeit)
            std::this_thread::sleep_for(frameZeit - dauer);
    }

    allesFreigeben();
}

// ======================================================================
// Initialisierung
// ======================================================================

bool GpuAnsichten::initGeraet()
{
    D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;

    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, &level, 1,
                                   D3D11_SDK_VERSION, device.put(), nullptr, context.put());
    if (FAILED(hr))
    {
        std::cerr << "GpuAnsichten: keine Hardware-GPU, verwende WARP\n";
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, &level, 1,
                               D3D11_SDK_VERSION, device.put(), nullptr, context.put());
    }
    if (FAILED(hr))
        return false;

    GpuRef<IDXGIDevice> dxgiDevice;
    GpuRef<IDXGIAdapter> adapter;

    if (FAILED(device->QueryInterface(__uuidof(IDXGIDevice), (void **)dxgiDevice.put())) ||
        FAILED(dxgiDevice->GetAdapter(adapter.put())) ||
        FAILED(adapter->GetParent(__uuidof(IDXGIFactory), (void **)factory.put())))
        return false;

    DXGI_ADAPTER_DESC desc;
    if (SUCCEEDED(adapter->GetDesc(&desc)))
        std::wcout << L"GPU (Ansichten): " << desc.Description << std::endl;

    return true;
}

bool GpuAnsichten::initShader()
{
    GpuRef<ID3DBlob> vs, vsB, ps, psM, vsV, psU;

    if (!kompilieren(SHADER_GEOMETRIE_VS, "vs_5_0", vs) ||
        !kompilieren(SHADER_BELEUCHTET_VS, "vs_5_0", vsB) ||
        !kompilieren(SHADER_GEOMETRIE_PS, "ps_5_0", ps) ||
        !kompilieren(SHADER_MASKE_PS, "ps_5_0", psM) ||
        !kompilieren(SHADER_VOLLBILD_VS, "vs_5_0", vsV) ||
        !kompilieren(SHADER_UMRISS_PS, "ps_5_0", psU))
        return false;
    GpuRef<ID3DBlob> psT;
    if (!kompilieren(SHADER_TEXTUR_PS, "ps_5_0", psT) ||
        FAILED(device->CreatePixelShader(psT->GetBufferPointer(), psT->GetBufferSize(), nullptr, psTextur.put())))
        return false;

    if (FAILED(device->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, vsGeometrie.put())) ||
        FAILED(device->CreateVertexShader(vsB->GetBufferPointer(), vsB->GetBufferSize(), nullptr, vsBeleuchtet.put())) ||
        FAILED(device->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, psGeometrie.put())) ||
        FAILED(device->CreatePixelShader(psM->GetBufferPointer(), psM->GetBufferSize(), nullptr, psMaske.put())) ||
        FAILED(device->CreateVertexShader(vsV->GetBufferPointer(), vsV->GetBufferSize(), nullptr, vsVollbild.put())) ||
        FAILED(device->CreatePixelShader(psU->GetBufferPointer(), psU->GetBufferSize(), nullptr, psUmriss.put())))
        return false;

    D3D11_INPUT_ELEMENT_DESC elemente[] =
        {
            {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0}};

    if (FAILED(device->CreateInputLayout(elemente, 2, vs->GetBufferPointer(), vs->GetBufferSize(), layout.put())))
        return false;

    D3D11_INPUT_ELEMENT_DESC elementeBeleuchtet[] =
        {
            {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 36, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT, 0, 52, D3D11_INPUT_PER_VERTEX_DATA, 0}};

    if (FAILED(device->CreateInputLayout(elementeBeleuchtet, 5, vsB->GetBufferPointer(), vsB->GetBufferSize(), layoutBeleuchtet.put())))
        return false;

    D3D11_BUFFER_DESC cb = {};
    cb.Usage = D3D11_USAGE_DYNAMIC;
    cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cb.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    cb.ByteWidth = 192; // float4x4 + 4 floats + float4 + 6 x float4 (Instanz-Abbildung)
    if (FAILED(device->CreateBuffer(&cb, nullptr, cbGeometrie.put())))
        return false;

    cb.ByteWidth = 32; // 2 x float4
    if (FAILED(device->CreateBuffer(&cb, nullptr, cbFarben.put())))
        return false;

    cb.ByteWidth = 16 + 16 * MAX_LICHTER + 16; // kamera + lichter + anzahl
    if (FAILED(device->CreateBuffer(&cb, nullptr, cbBeleuchtung.put())))
        return false;

    return true;
}

bool GpuAnsichten::initZustaende()
{
    D3D11_RASTERIZER_DESC rs = {};
    rs.FillMode = D3D11_FILL_SOLID;
    rs.CullMode = D3D11_CULL_NONE; // Dreiecke sind nicht einheitlich orientiert
    rs.DepthClipEnable = TRUE;
    if (FAILED(device->CreateRasterizerState(&rs, rasterizer.put())))
        return false;

    D3D11_DEPTH_STENCIL_DESC ds = {};
    ds.DepthEnable = TRUE;
    ds.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    ds.DepthFunc = D3D11_COMPARISON_GREATER_EQUAL; // umgekehrte Tiefe: groesser = naeher
    if (FAILED(device->CreateDepthStencilState(&ds, tiefeSchreiben.put())))
        return false;

    ds.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    if (FAILED(device->CreateDepthStencilState(&ds, tiefeNurTesten.put())))
        return false;

    ds.DepthEnable = FALSE;
    if (FAILED(device->CreateDepthStencilState(&ds, tiefeAus.put())))
        return false;

    D3D11_BLEND_DESC bd = {};
    bd.RenderTarget[0].BlendEnable = TRUE;
    bd.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    bd.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    if (FAILED(device->CreateBlendState(&bd, blendAlpha.put())))
        return false;

    bd.RenderTarget[0].BlendEnable = FALSE;
    if (FAILED(device->CreateBlendState(&bd, blendAus.put())))
        return false;

    // Texturen: wiederholt, mit Mipmaps, anisotrop (schraeg gesehene Boeden bleiben scharf)
    D3D11_SAMPLER_DESC sd = {};
    sd.Filter = D3D11_FILTER_ANISOTROPIC;
    sd.MaxAnisotropy = 8;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    sd.ComparisonFunc = D3D11_COMPARISON_NEVER;
    sd.MinLOD = 0;
    sd.MaxLOD = D3D11_FLOAT32_MAX;
    if (FAILED(device->CreateSamplerState(&sd, abtaster.put())))
        return false;

    return true;
}

// Tabelle, ueber die der Shader Farben genau wie toCOLORREF()/farbeZuRgba() umrechnet
// (inklusive Rot/Blau-Einstellung). Funktioniert fuer jede kanalweise Umrechnung.
bool GpuAnsichten::initFarbTabelle()
{
    const int N = 1024;
    std::vector<uint32_t> daten(N * 4);

    for (int i = 0; i < N; ++i)
    {
        double x = double(i) / (N - 1);
        daten[0 * N + i] = farbeZuRgba(Farbe(x, 0, 0));
        daten[1 * N + i] = farbeZuRgba(Farbe(0, x, 0));
        daten[2 * N + i] = farbeZuRgba(Farbe(0, 0, x));
        daten[3 * N + i] = farbeZuRgba(Farbe(0, 0, 0));
    }

    D3D11_TEXTURE2D_DESC td = {};
    td.Width = N;
    td.Height = 4;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_IMMUTABLE;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA sd = {};
    sd.pSysMem = daten.data();
    sd.SysMemPitch = N * sizeof(uint32_t);

    return SUCCEEDED(device->CreateTexture2D(&td, &sd, farbTabelle.put())) &&
           SUCCEEDED(device->CreateShaderResourceView(farbTabelle.get(), nullptr, farbTabelleSrv.put()));
}

bool GpuAnsichten::zielErstellen(Ziel &z, int breite, int hoehe)
{
    HWND hwnd = z.wf->getFenster().getHWND();

    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 2;
    sd.BufferDesc.Width = breite;
    sd.BufferDesc.Height = hoehe;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    if (FAILED(factory->CreateSwapChain(device.get(), &sd, z.swapChain.put())))
    {
        std::cerr << "GpuAnsichten: CreateSwapChain fehlgeschlagen\n";
        return false;
    }

    factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);

    return zielPuffer(z, breite, hoehe);
}

void GpuAnsichten::zielFreigeben(Ziel &z)
{
    z.rtv.reset();
    z.dsv.reset();
    z.tiefe.reset();
    z.maskeSrv.reset();
    z.maskeRtv.reset();
    z.maske.reset();
}

// Backbuffer-View, Tiefenpuffer und Masken-Textur in der aktuellen Fenstergroesse
bool GpuAnsichten::zielPuffer(Ziel &z, int breite, int hoehe)
{
    context->OMSetRenderTargets(0, nullptr, nullptr);
    zielFreigeben(z);
    z.breite = 0;
    z.hoehe = 0;

    if (FAILED(z.swapChain->ResizeBuffers(0, breite, hoehe, DXGI_FORMAT_UNKNOWN, 0)))
        return false;

    GpuRef<ID3D11Texture2D> backBuffer;
    if (FAILED(z.swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void **)backBuffer.put())) ||
        FAILED(device->CreateRenderTargetView(backBuffer.get(), nullptr, z.rtv.put())))
        return false;

    D3D11_TEXTURE2D_DESC td = {};
    td.Width = breite;
    td.Height = hoehe;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;

    td.Format = DXGI_FORMAT_D32_FLOAT;
    td.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    if (FAILED(device->CreateTexture2D(&td, nullptr, z.tiefe.put())) ||
        FAILED(device->CreateDepthStencilView(z.tiefe.get(), nullptr, z.dsv.put())))
        return false;

    td.Format = DXGI_FORMAT_R8G8_UNORM;
    td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(device->CreateTexture2D(&td, nullptr, z.maske.put())) ||
        FAILED(device->CreateRenderTargetView(z.maske.get(), nullptr, z.maskeRtv.put())) ||
        FAILED(device->CreateShaderResourceView(z.maske.get(), nullptr, z.maskeSrv.put())))
        return false;

    z.breite = breite;
    z.hoehe = hoehe;
    z.signatur = 0; // neu zeichnen
    return true;
}

void GpuAnsichten::allesFreigeben()
{
    if (context)
        context->ClearState();

    for (auto &z : ziele)
    {
        zielFreigeben(z);
        z.swapChain.reset();
    }

    cache.clear();
    objektPuffer.buffer.reset();
    ansichtPuffer.buffer.reset();
    blendAus.reset();
    blendAlpha.reset();
    tiefeAus.reset();
    tiefeNurTesten.reset();
    tiefeSchreiben.reset();
    rasterizer.reset();
    farbTabelleSrv.reset();
    farbTabelle.reset();
    cbBeleuchtung.reset();
    cbFarben.reset();
    cbGeometrie.reset();
    layoutBeleuchtet.reset();
    layout.reset();
    bilder.clear();
    abtaster.reset();
    psTextur.reset();
    psUmriss.reset();
    vsVollbild.reset();
    psMaske.reset();
    psGeometrie.reset();
    vsBeleuchtet.reset();
    vsGeometrie.reset();
    factory.reset();
    context.reset();
    device.reset();
}

// ======================================================================
// Puffer und Zeichenaufrufe
// ======================================================================

bool GpuAnsichten::hochladen(Puffer &p, const std::vector<GpuVertex> &daten)
{
    if (daten.empty())
        return true;

    if (!p.buffer || p.kapazitaet < daten.size())
    {
        size_t kapazitaet = std::max<size_t>(daten.size() + daten.size() / 2, 1024);

        D3D11_BUFFER_DESC bd = {};
        bd.Usage = D3D11_USAGE_DYNAMIC;
        bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        bd.ByteWidth = UINT(kapazitaet * sizeof(GpuVertex));

        if (FAILED(device->CreateBuffer(&bd, nullptr, p.buffer.put())))
        {
            p.kapazitaet = 0;
            return false;
        }
        p.kapazitaet = kapazitaet;
    }

    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (FAILED(context->Map(p.buffer.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
        return false;

    memcpy(mapped.pData, daten.data(), daten.size() * sizeof(GpuVertex));
    context->Unmap(p.buffer.get(), 0);
    return true;
}

bool GpuAnsichten::unveraenderlicherPuffer(GpuRef<ID3D11Buffer> &buffer, const void *daten, size_t bytes)
{
    buffer.reset();
    if (bytes == 0)
        return true;

    D3D11_BUFFER_DESC bd = {};
    bd.Usage = D3D11_USAGE_IMMUTABLE;
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    bd.ByteWidth = UINT(bytes);

    D3D11_SUBRESOURCE_DATA sd = {};
    sd.pSysMem = daten;

    return SUCCEEDED(device->CreateBuffer(&bd, &sd, buffer.put()));
}

void GpuAnsichten::geometrieKonstanten(const double m[16], double biasMul, double biasAdd, double modus, const Vector3D &versatz,
                                       const Affin *abb)
{
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (FAILED(context->Map(cbGeometrie.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
        return;

    float *f = static_cast<float *>(mapped.pData);
    for (int i = 0; i < 16; ++i)
        f[i] = float(m[i]);
    f[16] = float(biasMul);
    f[17] = float(biasAdd);
    f[18] = float(modus);
    f[19] = 0.0f;
    f[20] = float(versatz.x);
    f[21] = float(versatz.y);
    f[22] = float(versatz.z);
    f[23] = 0.0f;
    // lineare Abbildung (Instanz) und die fuer Normalen: Kofaktoren, Vorzeichen wie Affin::normale
    double k[3][3];
    const Affin eins;
    const Affin &a = abb ? *abb : eins;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
        {
            const int i1 = (i + 1) % 3, i2 = (i + 2) % 3, j1 = (j + 1) % 3, j2 = (j + 2) % 3;
            k[i][j] = a.m[i1][j1] * a.m[i2][j2] - a.m[i1][j2] * a.m[i2][j1];
        }
    const double vz = a.determinante() < 0 ? -1.0 : 1.0;
    for (int i = 0; i < 3; ++i)
    {
        for (int j = 0; j < 3; ++j)
        {
            f[24 + 4 * i + j] = float(a.m[i][j]);
            f[36 + 4 * i + j] = float(k[i][j] * vz);
        }
        f[24 + 4 * i + 3] = 0.0f;
        f[36 + 4 * i + 3] = 0.0f;
    }

    context->Unmap(cbGeometrie.get(), 0);
}

void GpuAnsichten::farbKonstanten(uint32_t a, uint32_t b)
{
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (FAILED(context->Map(cbFarben.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
        return;

    float *f = static_cast<float *>(mapped.pData);
    farbeZuFloat(a, f);
    farbeZuFloat(b, f + 4);

    context->Unmap(cbFarben.get(), 0);
}

void GpuAnsichten::geometriePipeline()
{
    context->IASetInputLayout(layout.get());
    context->VSSetShader(vsGeometrie.get(), nullptr, 0);
}

void GpuAnsichten::beleuchtetPipeline()
{
    context->IASetInputLayout(layoutBeleuchtet.get());
    context->VSSetShader(vsBeleuchtet.get(), nullptr, 0);
}

void GpuAnsichten::zeichnen(Puffer &p, D3D11_PRIMITIVE_TOPOLOGY topologie, const Bereich &b)
{
    if (b.anzahl == 0 || !p.buffer)
        return;

    ID3D11Buffer *buffer = p.buffer.get();
    UINT stride = sizeof(GpuVertex);
    UINT offset = 0;
    context->IASetVertexBuffers(0, 1, &buffer, &stride, &offset);
    context->IASetPrimitiveTopology(topologie);
    context->Draw(b.anzahl, b.start);
}

void GpuAnsichten::zeichnen(Puffer &p, const ObjektBereiche &b, const double m[16], double linienMul, double linienAdd, double modus)
{
    if (b.dreiecke.anzahl == 0 && b.linien.anzahl == 0)
        return;

    geometriePipeline();

    geometrieKonstanten(m, 1.0, 0.0, modus, Vector3D(0, 0, 0));
    zeichnen(p, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST, b.dreiecke);

    geometrieKonstanten(m, linienMul, linienAdd, modus, Vector3D(0, 0, 0));
    zeichnen(p, D3D11_PRIMITIVE_TOPOLOGY_LINELIST, b.linien);
}

// Ein bekanntes Objekt: flach beleuchtet oder als Drahtgitter (modus 0 normal, sonst getoent)
void GpuAnsichten::objektZeichnen(ObjektEintrag &e, int modus, const double m[16], double linienMul, double linienAdd,
                                  const Affin *abb)
{
    // Instanz: Ecken relativ zu e.ursprung, also Welt = A * ecke + A(e.ursprung)
    Vector3D versatz = (abb ? abb->punkt(e.ursprung) : e.ursprung) - ursprung;
    UINT offset = 0;

    if (useFlat)
    {
        if (!e.beleuchtet)
            return;

        beleuchtetPipeline();
        ID3D11Buffer *buffer = e.beleuchtet.get();
        UINT stride = sizeof(BeleuchtetVertex);
        context->IASetVertexBuffers(0, 1, &buffer, &stride, &offset);

        double hervorheben = modus != 0 ? 1.0 : 0.0;

        // mit Textur: eigener Pixel-Shader, danach wieder der normale (nicht beim Zeichnen der Umriss-Maske)
        const bool mitBild = e.bild && psTextur && !maskeAktiv;
        struct BildZurueck
        {
            GpuAnsichten *g;
            bool aktiv;
            ~BildZurueck()
            {
                if (!aktiv)
                    return;
                ID3D11ShaderResourceView *keine[2] = {nullptr, nullptr};
                g->context->PSSetShaderResources(0, 2, keine);
                g->context->PSSetShader(g->psGeometrie.get(), nullptr, 0);
            }
        } zurueck{this, mitBild};
        if (mitBild)
        {
            context->PSSetShader(psTextur.get(), nullptr, 0);
            ID3D11ShaderResourceView *srv[2] = {farbTabelleSrv.get(), e.bild.get()};
            context->PSSetShaderResources(0, 2, srv);
            ID3D11SamplerState *s = abtaster.get();
            context->PSSetSamplers(0, 1, &s);
        }

        if (e.dreiecke > 0)
        {
            geometrieKonstanten(m, 1.0, 0.0, hervorheben, versatz, abb);
            context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            context->Draw(e.dreiecke, 0);
        }
        if (e.linien > 0)
        {
            geometrieKonstanten(m, linienMul, linienAdd, hervorheben, versatz, abb);
            context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
            context->Draw(e.linien, e.dreiecke);
        }
        return;
    }

    if (!e.drahtOk[0] || !e.draht[0])
        return;

    geometriePipeline();
    ID3D11Buffer *buffer = e.draht[0].get();
    UINT stride = sizeof(GpuVertex);
    context->IASetVertexBuffers(0, 1, &buffer, &stride, &offset);

    double hervorheben = modus != 0 ? 1.0 : 0.0;

    if (e.drahtDreiecke[0] > 0)
    {
        geometrieKonstanten(m, 1.0, 0.0, hervorheben, versatz, abb);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->Draw(e.drahtDreiecke[0], 0);
    }
    if (e.drahtLinien[0] > 0)
    {
        geometrieKonstanten(m, linienMul, linienAdd, hervorheben, versatz, abb);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
        context->Draw(e.drahtLinien[0], e.drahtDreiecke[0]);
    }
}

// ======================================================================
// Bekannte Objekte: Geometrie direkt aus den Klassen, Beleuchtung im Shader
// ======================================================================

// Pruefsumme und Bezugspunkt fuer Objekte, deren Geometrie wir direkt lesen koennen.
// false: unbekannter Typ, wird in jedem Frame ueber drawFlat() aufgezeichnet.
bool GpuAnsichten::bekanntesObjekt(object *obj, uint64_t &fingerabdruck, Vector3D &objektUrsprung) const
{
    // Grosse Netze (Mesh, Baum): die volle Pruefsumme ueber alle Dreiecke kostete bei grossen Szenen in jedem Bild
    // viel Zeit unter der Szenensperre. Jetzt: eine kurze Pruefsumme (Anzahl, Grenzen, 32 Stichproben) in jedem
    // Bild; die volle nur, wenn sich die kurze oder der globale Aenderungszaehler (gGeometrieStand: jede
    // Bearbeitung, Rueckgaengig, Animation) geaendert hat.
    {
        const std::vector<Dreieck> *l1 = nullptr, *l2 = nullptr;
        int art = 0;
        if (auto *m = dynamic_cast<const Mesh *>(obj))
            l1 = &m->triangles, art = 1;
        else if (auto *t = dynamic_cast<const Tree *>(obj))
            l1 = &t->woodTriangles, l2 = &t->leaves, art = 2;
        const size_t gesamt = (l1 ? l1->size() : 0) + (l2 ? l2->size() : 0);
        if (art && gesamt > 2000)
        {
            Pruefsumme k;
            k.zahl(uint64_t(art));
            k.zeiger(obj->texture.get());
            k.zahl(uint64_t(obj->textureMode));
            k.zeiger(obj->texPar.get());
            k.zahl(uint64_t((obj->texturSpiegelU ? 1 : 0) | (obj->texturSpiegelV ? 2 : 0) | ((obj->texturDrehung & 3) << 2)));
            k.vektor(obj->minBound);
            k.vektor(obj->maxBound);
            k.farbe(obj->mat.diffuse);
            for (const auto *l : {l1, l2})
            {
                if (!l)
                    continue;
                k.zahl(l->size());
                const size_t schritt = std::max<size_t>(1, l->size() / 32);
                for (size_t i = 0; i < l->size(); i += schritt)
                {
                    const Dreieck &d = (*l)[i];
                    k.vektor(d.a + d.position);
                    k.vektor(d.b + d.position);
                    k.vektor(d.c + d.position);
                    k.vektor(d.nA);
                    k.farbe(d.mat.diffuse);
                }
            }
            const unsigned stand = gGeometrieStand.load();
            auto it = fingerSpeicher.find(obj);
            if (it != fingerSpeicher.end() && it->second.stand == stand && it->second.kurz == k.h)
            {
                fingerabdruck = it->second.voll;
                objektUrsprung = it->second.ursprung;
                it->second.benutzt = true;
                return true;
            }
            uint64_t voll = 0;
            Vector3D u;
            if (!bekanntesObjektVoll(obj, voll, u))
                return false;
            fingerSpeicher[obj] = {stand, k.h, voll, u, true};
            fingerabdruck = voll;
            objektUrsprung = u;
            return true;
        }
    }
    return bekanntesObjektVoll(obj, fingerabdruck, objektUrsprung);
}

bool GpuAnsichten::bekanntesObjektVoll(object *obj, uint64_t &fingerabdruck, Vector3D &objektUrsprung) const
{
    Pruefsumme p;
    p.zahl(obj->geometrieZaehler); // Terrain-Pinsel u.a.: einzelne Ecken geaendert, die Stichproben merken das nicht
    // Textur: Bild, Art, Einstellungen (neue Einstellungen = neuer Zeiger), Spiegeln/Drehen
    p.zeiger(obj->texture.get());
    p.zahl(uint64_t(obj->textureMode));
    p.zeiger(obj->texPar.get());
    p.zahl(uint64_t((obj->texturSpiegelU ? 1 : 0) | (obj->texturSpiegelV ? 2 : 0) | ((obj->texturDrehung & 3) << 2)));

    if (auto *m = dynamic_cast<const Mesh *>(obj))
    {
        p.zahl(1);
        p.dreiecke(m->triangles);
        objektUrsprung = m->triangles.empty() ? Vector3D(0, 0, 0) : m->triangles.front().a + m->triangles.front().position;
    }
    else if (auto *t = dynamic_cast<const Tree *>(obj))
    {
        p.zahl(2);
        p.dreiecke(t->woodTriangles);
        p.dreiecke(t->leaves);
        const Dreieck *erstes = !t->woodTriangles.empty() ? &t->woodTriangles.front() : !t->leaves.empty() ? &t->leaves.front()
                                                                                                          : nullptr;
        objektUrsprung = erstes ? erstes->a + erstes->position : Vector3D(0, 0, 0);
    }
    else if (auto *k = dynamic_cast<const Kugel *>(obj))
    {
        p.zahl(3);
        p.vektor(k->position);
        p.wert(k->radius);
        p.farbe(k->mat.diffuse);
        objektUrsprung = k->position;
    }
    else if (auto *pf = dynamic_cast<const Pfeil *>(obj))
    {
        p.zahl(4);
        p.vektor(pf->position);
        p.dreiecke(pf->tris); // aendert sich auch beim Drehen/Skalieren (Neuaufbau)
        p.farbe(pf->mat.diffuse);
        objektUrsprung = pf->position;
    }
    else if (auto *tx = dynamic_cast<const Text3D *>(obj))
    {
        p.zahl(6);
        p.vektor(tx->position);
        p.dreiecke(tx->tris); // aendert sich auch bei neuem Text, Drehen, Skalieren
        p.farbe(tx->mat.diffuse);
        objektUrsprung = tx->position;
    }
    else if (auto *s = dynamic_cast<const Stab *>(obj))
    {
        p.zahl(5);
        p.vektor(s->position);
        p.vektor(s->p1);
        p.vektor(s->p2);
        p.wert(s->radius);
        p.farbe(s->mat.diffuse);
        objektUrsprung = s->position;
    }
    else
        return false;

    fingerabdruck = p.h;
    return true;
}

// fuer die Szenen-BVH des Raytracers (render.cpp), ohne Direct3D-Header dort
bool szenenObjektGrenzen(const object *o, Vector3D &mn, Vector3D &mx) { return GpuAnsichten::objektGrenzen(o, mn, mx); }

bool GpuAnsichten::objektGrenzen(const object *obj, Vector3D &minimum, Vector3D &maximum)
{
    // Rauchbox/Wolkenbereich: die Box (hat keine eigene Geometrie)
    if (auto *vb = dynamic_cast<const VolumenBox *>(obj))
    {
        vb->box(minimum, maximum);
        return true;
    }

    // Gruppe: Grenzen aller Objekte darin
    if (auto *g = dynamic_cast<const Gruppe *>(obj))
    {
        bool erstes = true;
        for (auto &k : g->kinder)
        {
            Vector3D mn, mx;
            if (!objektGrenzen(k.get(), mn, mx))
                continue;
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

    bool leer = true;
    auto punkt = [&](const Vector3D &p)
    {
        if (leer)
        {
            minimum = maximum = p;
            leer = false;
            return;
        }
        minimum = Vector3D(std::min(minimum.x, p.x), std::min(minimum.y, p.y), std::min(minimum.z, p.z));
        maximum = Vector3D(std::max(maximum.x, p.x), std::max(maximum.y, p.y), std::max(maximum.z, p.z));
    };

    auto dreiecke = [&](const std::vector<Dreieck> &liste, const Vector3D &versatz)
    {
        for (const auto &t : liste)
        {
            punkt(t.a + t.position + versatz);
            punkt(t.b + t.position + versatz);
            punkt(t.c + t.position + versatz);
        }
    };

    // grosse Netze: Ergebnis merken, solange sich nichts geaendert hat (Aenderungszaehler und drei Stichproben:
    // Verschieben/Drehen beim Ziehen aendert die Ecken). Die Grenzen werden mehrmals je Sekunde gebraucht
    // (Eigenschaften der Auswahl, Szenen-BVH, Absetzen) - bei Millionen Dreiecken spuerbar.
    auto grossesNetz = [&](const std::vector<Dreieck> *l1, const std::vector<Dreieck> *l2) -> bool
    {
        const size_t n1 = l1 ? l1->size() : 0, n2 = l2 ? l2->size() : 0;
        if (n1 + n2 < 20000)
            return false;
        struct Merk
        {
            unsigned stand;
            size_t n1, n2;
            Vector3D s[6];
            Vector3D mn, mx;
        };
        static std::mutex merkSperre;
        static std::unordered_map<const object *, Merk> merk;
        Vector3D stich[6];
        int k = 0;
        for (const auto *l : {l1, l2})
        {
            if (!l || l->empty())
            {
                stich[k++] = Vector3D(0, 0, 0);
                stich[k++] = Vector3D(0, 0, 0);
                stich[k++] = Vector3D(0, 0, 0);
                continue;
            }
            const Dreieck &a0 = l->front(), &a1 = (*l)[l->size() / 2], &a2 = l->back();
            stich[k++] = a0.a + a0.position;
            stich[k++] = a1.b + a1.position;
            stich[k++] = a2.c + a2.position;
        }
        const unsigned stand = gGeometrieStand.load();
        {
            std::lock_guard<std::mutex> sperre(merkSperre);
            auto it = merk.find(obj);
            if (it != merk.end() && it->second.stand == stand && it->second.n1 == n1 && it->second.n2 == n2)
            {
                bool gleich = true;
                for (int i = 0; i < 6 && gleich; ++i)
                    gleich = (it->second.s[i] - stich[i]).length() == 0.0;
                if (gleich)
                {
                    punkt(it->second.mn);
                    punkt(it->second.mx);
                    return true;
                }
            }
        }
        if (l1)
            dreiecke(*l1, Vector3D(0, 0, 0));
        if (l2)
            dreiecke(*l2, Vector3D(0, 0, 0));
        if (leer)
            return true;
        std::lock_guard<std::mutex> sperre(merkSperre);
        if (merk.size() > 4096)
            merk.clear();
        Merk m{stand, n1, n2, {}, minimum, maximum};
        for (int i = 0; i < 6; ++i)
            m.s[i] = stich[i];
        merk[obj] = m;
        return true;
    };

    if (auto *m = dynamic_cast<const Mesh *>(obj))
    {
        if (!grossesNetz(&m->triangles, nullptr))
            dreiecke(m->triangles, Vector3D(0, 0, 0));
    }
    else if (auto *t = dynamic_cast<const Tree *>(obj))
    {
        if (!grossesNetz(&t->woodTriangles, &t->leaves))
        {
            dreiecke(t->woodTriangles, Vector3D(0, 0, 0));
            dreiecke(t->leaves, Vector3D(0, 0, 0));
        }
    }
    else if (auto *k = dynamic_cast<const Kugel *>(obj))
    {
        Vector3D r(k->radius, k->radius, k->radius);
        punkt(k->position - r);
        punkt(k->position + r);
    }
    else if (auto *pf = dynamic_cast<const Pfeil *>(obj))
        dreiecke(pf->tris, pf->position);
    else if (auto *tx = dynamic_cast<const Text3D *>(obj))
        dreiecke(tx->tris, tx->position);
    else if (auto *s = dynamic_cast<const Stab *>(obj))
    {
        Vector3D r(s->radius, s->radius, s->radius);
        punkt(s->p1 + s->position - r);
        punkt(s->p1 + s->position + r);
        punkt(s->p2 + s->position - r);
        punkt(s->p2 + s->position + r);
    }
    else if (obj)
    {
        punkt(obj->minBound);
        punkt(obj->maxBound);
    }

    return !leer;
}

void GpuAnsichten::beleuchtetErzeugen(object *obj, ObjektEintrag &e)
{
    std::vector<BeleuchtetVertex> dreiecke, linien;
    double radius2 = 0.0;

    auto vertex = [&](std::vector<BeleuchtetVertex> &ziel, const Vector3D &p, const Vector3D &n,
                      const Vector3D &m, const Farbe &f, int flags, float u = 0.0f, float v = 0.0f)
    {
        Vector3D r = p - e.ursprung;
        Vector3D rm = m - e.ursprung;
        radius2 = std::max(radius2, r * r);
        ziel.push_back({float(r.x), float(r.y), float(r.z),
                        float(n.x), float(n.y), float(n.z),
                        float(rm.x), float(rm.y), float(rm.z),
                        float(f.r), float(f.g), float(f.b), float(flags), u, v});
    };

    // Textur: Bildkoordinaten der drei Ecken wie im Raytracer (textur_abbildung.h; "Wuerfel weich" als Wuerfel).
    // Kugel/Zylinder: liegt die Naht im Dreieck, wird u um eine Periode verschoben (das Bild wiederholt sich).
    const object *texturQuelle = nullptr; // Objekt, dessen Textur die Ansicht zeigt
    auto texturiert = [&](const object &o) { return o.texture && o.texture->width() > 0 && o.textureMode != TextureMode::None; };
    auto dreieckUV = [&](const object &o, const Vector3D &a, const Vector3D &b, const Vector3D &c, const Vector3D &n,
                         float uv[6])
    {
        const TexturParameter &par = o.texturPar();
        const Vector3D P[3] = {a, b, c};
        double u[3], v[3], periode = 0.0;
        for (int k = 0; k < 3; ++k)
        {
            const TexturProbe pr = texturRoh(o.textureMode, par, texturRaum(o, P[k], n), true);
            u[k] = pr.n ? pr.u[0] : 0.0;
            v[k] = pr.n ? pr.v[0] : 0.0;
            periode = std::max(periode, pr.periodeU);
        }
        if (periode > 0)
        {
            const double hoch = std::max({u[0], u[1], u[2]});
            for (int k = 0; k < 3; ++k)
                if (hoch - u[k] > 0.5 * periode)
                    u[k] += periode;
        }
        for (int k = 0; k < 3; ++k)
        {
            texturNach(o, par, u[k], v[k]);
            uv[2 * k] = float(u[k]);
            uv[2 * k + 1] = float(v[k]);
        }
    };

    auto dreieck = [&](const Vector3D &a, const Vector3D &b, const Vector3D &c,
                       const Vector3D &n, const Vector3D &m, const Farbe &f, int flags, const object *tex = nullptr)
    {
        if (tex && texturiert(*tex))
        {
            float uv[6];
            dreieckUV(*tex, a, b, c, n, uv);
            if (!texturQuelle)
                texturQuelle = tex;
            vertex(dreiecke, a, n, m, f, flags | 4, uv[0], uv[1]);
            vertex(dreiecke, b, n, m, f, flags | 4, uv[2], uv[3]);
            vertex(dreiecke, c, n, m, f, flags | 4, uv[4], uv[5]);
            return;
        }
        vertex(dreiecke, a, n, m, f, flags);
        vertex(dreiecke, b, n, m, f, flags);
        vertex(dreiecke, c, n, m, f, flags);
    };

    auto linie = [&](const Vector3D &a, const Vector3D &b,
                     const Vector3D &n, const Vector3D &m, const Farbe &f, int flags)
    {
        vertex(linien, a, n, m, f, flags);
        vertex(linien, b, n, m, f, flags);
    };

    // wie Dreieck::drawFlat(): Normale 'normale', zur Kamera gedreht, Auswahl ohne Schattierung
    auto dreieckListe = [&](const std::vector<Dreieck> &liste)
    {
        for (const auto &t : liste)
        {
            Vector3D A = t.a + t.position;
            Vector3D B = t.b + t.position;
            Vector3D C = t.c + t.position;
            Vector3D M = (A + B + C) * (1.0 / 3.0);
            dreieck(A, B, C, t.normale, M, t.mat.diffuse, 1 | 2, &t);
        }
    };

    if (auto *m = dynamic_cast<const Mesh *>(obj))
    {
        dreiecke.reserve(m->triangles.size() * 3);
        dreieckListe(m->triangles);
    }
    else if (auto *t = dynamic_cast<const Tree *>(obj))
    {
        dreiecke.reserve((t->woodTriangles.size() + t->leaves.size()) * 3);
        dreieckListe(t->woodTriangles);
        dreieckListe(t->leaves);
    }
    else if (auto *k = dynamic_cast<const Kugel *>(obj))
    {
        // wie Kugel::drawFlat(): 32 x 16 Vierecke, Normale nicht zur Kamera gedreht
        const int slices = 32;
        const int stacks = 16;
        const double radius = k->radius;

        for (int i = 0; i < stacks; i++)
        {
            double phi0 = M_PI * i / stacks;
            double phi1 = M_PI * (i + 1) / stacks;

            for (int j = 0; j < slices; j++)
            {
                double theta0 = 2 * M_PI * j / slices;
                double theta1 = 2 * M_PI * (j + 1) / slices;

                Vector3D p0(radius * sin(phi0) * cos(theta0), radius * cos(phi0), radius * sin(phi0) * sin(theta0));
                Vector3D p1(radius * sin(phi1) * cos(theta0), radius * cos(phi1), radius * sin(phi1) * sin(theta0));
                Vector3D p2(radius * sin(phi1) * cos(theta1), radius * cos(phi1), radius * sin(phi1) * sin(theta1));
                Vector3D p3(radius * sin(phi0) * cos(theta1), radius * cos(phi0), radius * sin(phi0) * sin(theta1));

                p0 += k->position;
                p1 += k->position;
                p2 += k->position;
                p3 += k->position;

                Vector3D M = (p0 + p1 + p2 + p3) * (1.0 / 4.0);
                Vector3D N = (p2 - p0).cross(p1 - p0).normalized();

                dreieck(p0, p1, p2, N, M, k->mat.diffuse, 0, k);
                dreieck(p0, p2, p3, N, M, k->mat.diffuse, 0, k);
            }
        }
    }
    else if (auto *pf = dynamic_cast<const Pfeil *>(obj))
    {
        // wie Pfeil::drawFlat(): Flaeche plus Kanten in derselben Farbe, Normale zur Kamera gedreht
        for (const auto &t : pf->tris)
        {
            Vector3D a = t.a + pf->position;
            Vector3D b = t.b + pf->position;
            Vector3D c = t.c + pf->position;
            Vector3D M = (a + b + c) * (1.0 / 3.0);
            Vector3D N = (c - a).cross(b - a).normalized();

            dreieck(a, b, c, N, M, pf->mat.diffuse, 1);
            linie(a, b, N, M, pf->mat.diffuse, 1);
            linie(b, c, N, M, pf->mat.diffuse, 1);
            linie(c, a, N, M, pf->mat.diffuse, 1);
        }
    }
    else if (auto *tx = dynamic_cast<const Text3D *>(obj))
    {
        // Flaechen, Normale zur Kamera gedreht. Kanten nur aussen an jedem Viereck: die Dreiecke
        // kommen paarweise (p,q,r)(p,r,s); die gemeinsame Diagonale p-r wird nicht gezeichnet
        for (size_t i = 0; i < tx->tris.size(); ++i)
        {
            const auto &t = tx->tris[i];
            Vector3D a = t.a + tx->position;
            Vector3D b = t.b + tx->position;
            Vector3D c = t.c + tx->position;
            Vector3D M = (a + b + c) * (1.0 / 3.0);
            Vector3D N = (b - a).cross(c - a).normalized();

            dreieck(a, b, c, N, M, tx->mat.diffuse, 1);
            if (i % 2 == 0)
            {
                linie(a, b, N, M, tx->mat.diffuse, 1);
                linie(b, c, N, M, tx->mat.diffuse, 1);
            }
            else
            {
                linie(b, c, N, M, tx->mat.diffuse, 1);
                linie(c, a, N, M, tx->mat.diffuse, 1);
            }
        }
    }
    else if (auto *s = dynamic_cast<const Stab *>(obj))
    {
        // wie Stab::drawFlat(): 24 Vierecke um die Achse, Normale nicht zur Kamera gedreht
        const int steps = 24;
        Vector3D axis = (s->p2 - s->p1).normalized();
        Vector3D tmp = fabs(axis.y) < 0.99 ? Vector3D(0, 1, 0) : Vector3D(1, 0, 0);
        Vector3D right = axis.cross(tmp).normalized();
        Vector3D forward = axis.cross(right).normalized();

        for (int i = 0; i < steps; i++)
        {
            double a0 = 2 * M_PI * i / steps;
            double a1 = 2 * M_PI * (i + 1) / steps;

            Vector3D r0 = right * cos(a0) + forward * sin(a0);
            Vector3D r1 = right * cos(a1) + forward * sin(a1);

            Vector3D P1 = s->p1 + s->position;
            Vector3D P2 = s->p2 + s->position;

            Vector3D p0 = P1 + r0 * s->radius;
            Vector3D p1_ = P2 + r0 * s->radius;
            Vector3D p2_ = P2 + r1 * s->radius;
            Vector3D p3 = P1 + r1 * s->radius;

            Vector3D N = (p1_ - p0).cross(p3 - p0).normalized();
            Vector3D M = (p0 + p1_ + p2_ + p3) * (1.0 / 4.0);

            dreieck(p0, p1_, p2_, N, M, s->mat.diffuse, 0, s);
            dreieck(p0, p2_, p3, N, M, s->mat.diffuse, 0, s);
        }
    }

    e.dreiecke = UINT(dreiecke.size());
    e.linien = UINT(linien.size());
    dreiecke.insert(dreiecke.end(), linien.begin(), linien.end());

    e.radius = std::max(e.radius, std::sqrt(radius2));
    e.beleuchtetOk = unveraenderlicherPuffer(e.beleuchtet, dreiecke.data(), dreiecke.size() * sizeof(BeleuchtetVertex));
    e.bild.reset();
    if (texturQuelle)
        if (ID3D11ShaderResourceView *srv = bildHochladen(texturQuelle->texture, texturQuelle->texturPar().bild[BILD_ALPHA]))
        {
            srv->AddRef();
            e.bild.reset();
            *e.bild.put() = srv;
        }
}

// Bild als Textur mit allen Mipmap-Stufen hochladen (sRGB: der Shader bekommt lineare Farben wie der Raytracer).
// Mit Alpha-Bild: dessen Helligkeit wird zum Alpha-Kanal (ausgeschnittene Stellen fehlen auch in den Ansichten).
ID3D11ShaderResourceView *GpuAnsichten::bildHochladen(const std::shared_ptr<Texture> &bild, const std::shared_ptr<Texture> &alpha)
{
    if (!bild || bild->stufen() <= 0 || !device)
        return nullptr;
    const auto schluessel = std::make_pair((const Texture *)bild.get(), (const Texture *)alpha.get());
    auto it = bilder.find(schluessel);
    if (it != bilder.end())
    {
        it->second.benutzt = true;
        return it->second.srv.get();
    }

    std::vector<std::vector<uint32_t>> stufen;
    std::vector<std::pair<int, int>> groesse;
    if (!alpha || alpha->stufen() <= 0)
        for (int i = 0; i < bild->stufen(); ++i)
        {
            stufen.push_back(bild->stufePixel(i));
            groesse.push_back({bild->stufeBreite(i), bild->stufeHoehe(i)});
        }
    else
    {
        // Stufe 0 mit Alpha aus dem Alpha-Bild, dann selbst verkleinern (Mittelwert in den Bytes)
        int b = bild->stufeBreite(0), h = bild->stufeHoehe(0);
        std::vector<uint32_t> px = bild->stufePixel(0);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < b; ++x)
            {
                const double a = std::clamp(alpha->hellAbtasten((x + 0.5) / b, 1.0 - (y + 0.5) / h, 0.0) *
                                                (alpha->hatAlpha() ? alpha->alphaAbtasten((x + 0.5) / b, 1.0 - (y + 0.5) / h, 0.0) : 1.0),
                                            0.0, 1.0);
                uint32_t &p = px[size_t(y) * b + x];
                p = (p & 0x00FFFFFFu) | (uint32_t(std::lround(a * 255.0)) << 24);
            }
        stufen.push_back(px);
        groesse.push_back({b, h});
        while (b > 1 || h > 1)
        {
            const int nb = std::max(1, b / 2), nh = std::max(1, h / 2);
            const std::vector<uint32_t> &a = stufen.back();
            std::vector<uint32_t> n(size_t(nb) * nh);
            for (int y = 0; y < nh; ++y)
                for (int x = 0; x < nb; ++x)
                {
                    unsigned s[4] = {0, 0, 0, 0};
                    for (int k = 0; k < 4; ++k)
                    {
                        const uint32_t q = a[size_t(std::min(h - 1, 2 * y + (k >> 1))) * b + std::min(b - 1, 2 * x + (k & 1))];
                        for (int c = 0; c < 4; ++c)
                            s[c] += (q >> (8 * c)) & 0xFF;
                    }
                    n[size_t(y) * nb + x] = ((s[0] + 2) / 4) | (((s[1] + 2) / 4) << 8) | (((s[2] + 2) / 4) << 16) | (((s[3] + 2) / 4) << 24);
                }
            stufen.push_back(std::move(n));
            groesse.push_back({nb, nh});
            b = nb, h = nh;
        }
    }

    D3D11_TEXTURE2D_DESC td = {};
    td.Width = UINT(groesse[0].first);
    td.Height = UINT(groesse[0].second);
    td.MipLevels = UINT(stufen.size());
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_B8G8R8A8_UNORM_SRGB; // ARGB als uint32 = B, G, R, A im Speicher
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_IMMUTABLE;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    std::vector<D3D11_SUBRESOURCE_DATA> daten(stufen.size());
    for (size_t i = 0; i < stufen.size(); ++i)
    {
        daten[i].pSysMem = stufen[i].data();
        daten[i].SysMemPitch = UINT(groesse[i].first * 4);
    }
    GpuRef<ID3D11Texture2D> tex;
    BildEintrag eintrag;
    if (FAILED(device->CreateTexture2D(&td, daten.data(), tex.put())) ||
        FAILED(device->CreateShaderResourceView(tex.get(), nullptr, eintrag.srv.put())))
        return nullptr;
    eintrag.bild = bild;
    eintrag.alpha = alpha;
    eintrag.benutzt = true;
    return (bilder[schluessel] = std::move(eintrag)).srv.get();
}

// Drahtgitter haengt nicht von Kamera oder Licht ab: einmal aufzeichnen und behalten
void GpuAnsichten::drahtErzeugen(object *obj, ObjektEintrag &e, int modus, Wireframe &wf)
{
    einzel.ursprung = e.ursprung;
    einzel.zWert = -1.0;
    einzel.leeren();
    {
        AufzeichnungAktiv aktiv(&einzel);
        obj->drawWireframePixels(wf, zeichenModus(modus));
    }

    std::vector<GpuVertex> v(einzel.dreiecke);
    v.insert(v.end(), einzel.linien.begin(), einzel.linien.end());

    e.drahtDreiecke[modus] = UINT(einzel.dreiecke.size());
    e.drahtLinien[modus] = UINT(einzel.linien.size());
    e.radius = std::max(e.radius, std::sqrt(einzel.maxAbstand2));
    e.drahtOk[modus] = unveraenderlicherPuffer(e.draht[modus], v.data(), v.size() * sizeof(GpuVertex));
}

// Kamera und Lichter fuer den Shader (relativ zum Frame-Ursprung)
void GpuAnsichten::beleuchtungAufzeichnen(uint64_t &signatur)
{
    Pruefsumme p;
    p.zahl(signatur);

    float daten[4 + 4 * MAX_LICHTER + 4] = {};

    if (!cam.empty())
    {
        Vector3D k = cam[0].position - ursprung;
        daten[0] = float(k.x);
        daten[1] = float(k.y);
        daten[2] = float(k.z);

        p.vektor(cam[0].position);
        p.vektor(cam[0].lookAt);
        p.vektor(cam[0].u);
        p.vektor(cam[0].v);
        p.vektor(cam[0].w);
        p.wert(cam[0].fov);
        p.wert(cam[0].halfWidth);
        p.wert(cam[0].halfHeight);
    }

    int anzahl = 0;
    for (const auto &l : lights)
    {
        if (anzahl >= MAX_LICHTER)
            break;

        Vector3D wert;
        float art = 0.0f; // 0 = Punkt, 1 = Richtung

        if (auto *s = dynamic_cast<const SphereLight *>(l.get()))
            wert = s->center - ursprung; // vom Mittelpunkt aus (statt lastSamplePoint)
        else
        {
            // Richtungslicht erkennt man daran, dass directionFrom() ueberall gleich ist
            Vector3D d1 = l->directionFrom(Vector3D(1e4, 0, 0));
            Vector3D d2 = l->directionFrom(Vector3D(-1e4, 3e3, 7e3));

            if ((d1 - d2).length() < 1e-9)
            {
                wert = d1;
                art = 1.0f;
            }
            else
                wert = l->getPosition() - ursprung;
        }

        float *f = daten + 4 + 4 * anzahl;
        f[0] = float(wert.x);
        f[1] = float(wert.y);
        f[2] = float(wert.z);
        f[3] = art;

        p.vektor(wert);
        p.wert(art);
        p.vektor(l->getPosition());
        p.farbe(l->farbe);
        ++anzahl;
    }

    int32_t anzahlFeld = anzahl;
    memcpy(&daten[4 + 4 * MAX_LICHTER], &anzahlFeld, sizeof(anzahlFeld));
    p.zahl(uint64_t(anzahl));

    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (SUCCEEDED(context->Map(cbBeleuchtung.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
    {
        memcpy(mapped.pData, daten, sizeof(daten));
        context->Unmap(cbBeleuchtung.get(), 0);
    }

    signatur = p.h;
}

// ======================================================================
// Szene einmal pro Frame vorbereiten (fuer alle Ansichten gemeinsam)
// ======================================================================

void GpuAnsichten::szeneAufzeichnen()
{
    // Waehrend der Aufzeichnung gesperrt, damit 'C' (Szene leeren) keine Objekte loescht,
    // die gerade gelesen werden (siehe main.cpp)
    std::lock_guard<std::mutex> lock(sceneMutex);

    // Gruppen in ihre Objekte zerlegen: jedes wird einzeln (mit Zwischenspeicher) gezeichnet
    // ausgeblendete Objekte (Haken in der Objektliste) und Gruppen bleiben weg
    std::vector<object *> objekte;
    std::vector<Instanz *> instanzen;
    std::function<void(object *)> sichtbare = [&](object *o)
    {
        if (!o || o->versteckt)
            return;
        if (auto *in = dynamic_cast<Instanz *>(o))
        {
            if (in->vorlage)
                instanzen.push_back(in);
            return;
        }
        if (auto *gr = dynamic_cast<Gruppe *>(o))
        {
            for (auto &k : gr->kinder)
                sichtbare(k.get());
            return;
        }
        objekte.push_back(o);
    };
    for (object *o : ziele[0].wf->sceneobjects)
        sichtbare(o);
    Wireframe &wf = *ziele[0].wf;

    // Nachschlagen in Mengen statt Listen: bei tausenden Objekten und grosser Auswahl war das quadratisch
    // (Millionen Vergleiche je Bild, waehrend die Szene gesperrt ist - die Oberflaeche wartete)
    const std::unordered_set<object *> objektMenge(objekte.begin(), objekte.end());
    const std::unordered_set<object *> instanzMenge(instanzen.begin(), instanzen.end());
    auto enthalten = [&](object *o) { return o && objektMenge.count(o) != 0; };
    const std::vector<object *> auswahlJetzt = ausgewaehlteObjekte();

    ursprung = (sceneMin + sceneMax) * 0.5;

    Pruefsumme sig;
    sig.zahl(useFlat);
    sig.zahl(uint64_t(gVersteckAenderungen.load()));
    sig.wert(scale);
    sig.vektor(sceneMin);
    sig.vektor(sceneMax);
    sig.zahl(uint64_t(selectedType));
    sig.zeiger(selectedObject);
    sig.zeiger(selectedLight);

    // Render-Einstellungen, die in den Ansichten als Rahmen erscheinen
    sig.zahl(rauchVorhanden);
    sig.zahl(wolkeVolumeVorhanden);
    for (const auto &b : smokeBoxes)
    {
        sig.vektor(b.min);
        sig.vektor(b.max);
    }
    sig.vektor(SkyMin);
    sig.vektor(SkyMax);
    sig.vektor(WolkeboxMin);
    sig.vektor(WolkeboxMax);
    // Rahmen der Rauchboxen/Wolken werden bei Auswahl und Hover gelb bzw. cyan
    sig.zeiger(wf.hoverObject);
    // Drehpunkt (Rotate/Scale)
    sig.zahl(drehpunktSichtbar());
    sig.zahl(drehpunktZiehen);
    sig.zahl(uint64_t(selectedAction));
    if (drehpunktSichtbar())
        sig.vektor(drehpunkt());
    // Griffmarken am Wolkenbereich haben feste Pixelgroesse: haengen an Zoom und Kamera
    if (!cam.empty())
    {
        sig.vektor(cam[0].position);
        sig.vektor(cam[0].lookAt);
        sig.wert(cam[0].fov);
    }
    for (object *a : ausgewaehlteObjekte())
        if (dynamic_cast<VolumenBox *>(a))
            sig.zeiger(a);

    ausgewaehlt.clear();
    instanzenAusgewaehlt.clear();
    instanzenHover.clear();
    ausgewaehltMenge.clear();
    instanzAuswahlMenge.clear();
    instanzHoverMenge.clear();
    {
        std::vector<object *> teile;
        for (object *a : auswahlJetzt)
            gruppenAufloesen(a, teile); // ausgewaehlte Gruppe: alle Objekte darin
        for (object *o : teile)
        {
            if (enthalten(o) && ausgewaehltMenge.insert(o).second)
            {
                ausgewaehlt.push_back(o);
                sig.zeiger(o);
            }
            // Instanzen: ausgewaehlt (auch als Teil einer Gruppe)
            if (instanzMenge.count(o) && instanzAuswahlMenge.insert(static_cast<Instanz *>(o)).second)
            {
                instanzenAusgewaehlt.push_back(static_cast<Instanz *>(o));
                sig.zeiger(o);
            }
        }
    }
    auto istAusgewaehltHier = [&](object *o) { return ausgewaehltMenge.count(o) != 0; };

    std::vector<object *> hoverObjekte;
    std::unordered_set<object *> hoverMenge;
    for (auto &z : ziele)
    {
        if (z.wf->hoverType != SelectedType::OBJECT)
            continue;
        std::vector<object *> teile;
        gruppenAufloesen(z.wf->hoverObject, teile); // Gruppe unter der Maus: alle Objekte darin
        for (object *h : teile)
        {
            if (!istAusgewaehltHier(h) && enthalten(h) && hoverMenge.insert(h).second)
                hoverObjekte.push_back(h);
            if (instanzMenge.count(h) && !instanzAuswahlMenge.count(static_cast<Instanz *>(h)) &&
                instanzHoverMenge.insert(static_cast<Instanz *>(h)).second)
                instanzenHover.push_back(static_cast<Instanz *>(h));
        }
    }

    // ---------- bekannte Objekte: nur nach Aenderungen neu hochladen ----------
    for (auto &kv : cache)
        kv.second.benutzt = false;

    bekannte.clear();
    instanzTeile.clear();
    std::vector<object *> unbekannt;

    // Eintrag im Cache sicherstellen (neu hochladen nach Aenderungen); false: unbekannter Typ
    auto eintragSichern = [&](object *obj, uint64_t &fingerabdruck) -> bool
    {
        Vector3D objektUrsprung;
        if (!bekanntesObjekt(obj, fingerabdruck, objektUrsprung))
            return false;
        ObjektEintrag &e = cache[obj];
        e.benutzt = true;

        if (e.fingerabdruck != fingerabdruck || (!e.beleuchtetOk && !e.drahtOk[0] && !e.drahtOk[1] && !e.drahtOk[2]))
        {
            e.fingerabdruck = fingerabdruck;
            e.ursprung = objektUrsprung;
            e.radius = 0.0;
            e.beleuchtetOk = false;
            e.beleuchtet.reset();
            e.bild.reset();
            for (int m = 0; m < 3; ++m)
            {
                e.drahtOk[m] = false;
                e.draht[m].reset();
            }
        }

        if (useFlat)
        {
            if (!e.beleuchtetOk)
                beleuchtetErzeugen(obj, e);
        }
        else
        {
            if (!e.drahtOk[0])
                drahtErzeugen(obj, e, 0, wf);
        }
        return true;
    };

    for (object *obj : objekte)
    {
        if (!obj)
            continue;
        uint64_t fingerabdruck = 0;
        if (!eintragSichern(obj, fingerabdruck))
        {
            unbekannt.push_back(obj);
            continue;
        }
        bekannte.push_back(obj);
        sig.zeiger(obj);
        sig.zahl(fingerabdruck);
    }

    // Instanzen: die Teile der Vorlage einmal hochladen, je Instanz mit ihrer Abbildung zeichnen
    // (Teile unbekannter Typen werden bei Instanzen nicht gezeichnet)
    for (Instanz *in : instanzen)
    {
        std::vector<object *> teile;
        std::function<void(object *)> sammeln = [&](object *o)
        {
            if (!o || o->versteckt)
                return;
            if (auto *gr = dynamic_cast<Gruppe *>(o))
            {
                for (auto &k : gr->kinder)
                    sammeln(k.get());
                return;
            }
            if (!dynamic_cast<Instanz *>(o))
                teile.push_back(o);
        };
        sammeln(in->vorlage.get());
        sig.zeiger(in);
        sig.bytes(&in->abb, sizeof(Affin));
        for (object *t : teile)
        {
            uint64_t fingerabdruck = 0;
            if (!eintragSichern(t, fingerabdruck))
                continue;
            instanzTeile.push_back({t, in});
            sig.zeiger(t);
            sig.zahl(fingerabdruck);
        }
    }

    for (auto it = cache.begin(); it != cache.end();)
    {
        if (it->second.benutzt)
            ++it;
        else
            it = cache.erase(it);
    }
    for (auto it = fingerSpeicher.begin(); it != fingerSpeicher.end();)
    {
        if (it->second.benutzt)
        {
            it->second.benutzt = false;
            ++it;
        }
        else
            it = fingerSpeicher.erase(it);
    }

    // ---------- unbekannte Objekte: wie bisher in jedem Frame aufzeichnen ----------
    szene.ursprung = ursprung;
    szene.zWert = -1.0;
    szene.leeren();
    hoverBereiche.clear();
    ausgewaehltBereiche.clear();

    {
        AufzeichnungAktiv aktiv(&szene);

        auto erfassen = [&](object *obj, DrawMode mode)
        {
            ObjektBereiche b;
            b.dreiecke.start = UINT(szene.dreiecke.size());
            b.linien.start = UINT(szene.linien.size());

            szene.neuesObjekt();
            if (useFlat)
                obj->drawFlat(wf, mode);
            else
                obj->drawWireframePixels(wf, mode);

            b.dreiecke.anzahl = UINT(szene.dreiecke.size()) - b.dreiecke.start;
            b.linien.anzahl = UINT(szene.linien.size()) - b.linien.start;
            return b;
        };

        for (object *obj : unbekannt)
            erfassen(obj, DrawMode::NORMAL);

        alleObjekte.dreiecke = {0, UINT(szene.dreiecke.size())};
        alleObjekte.linien = {0, UINT(szene.linien.size())};

        const std::unordered_set<object *> unbekanntMenge(unbekannt.begin(), unbekannt.end());
        for (object *a : ausgewaehlt)
            if (unbekanntMenge.count(a))
                ausgewaehltBereiche.push_back({a, erfassen(a, DrawMode::NORMAL)});

        for (object *h : hoverObjekte)
            if (unbekanntMenge.count(h))
                hoverBereiche.push_back({h, erfassen(h, DrawMode::NORMAL)});
    }

    // Ein Puffer: erst alle Dreiecke, dann alle Linien
    UINT linienOffset = UINT(szene.dreiecke.size());

    objektVertices.clear();
    objektVertices.reserve(szene.dreiecke.size() + szene.linien.size());
    objektVertices.insert(objektVertices.end(), szene.dreiecke.begin(), szene.dreiecke.end());
    objektVertices.insert(objektVertices.end(), szene.linien.begin(), szene.linien.end());

    alleObjekte.linien.start += linienOffset;
    for (auto &a : ausgewaehltBereiche)
        a.second.linien.start += linienOffset;
    for (auto &h : hoverBereiche)
        h.second.linien.start += linienOffset;

    // Aenderungen an unbekannten Objekten erkennt man nur am Ergebnis
    sig.bytes(objektVertices.data(), objektVertices.size() * sizeof(GpuVertex));
    sig.zahl(linienOffset);

    // ---------- Kamera und Lichter ----------
    uint64_t signatur = sig.h;
    beleuchtungAufzeichnen(signatur);
    szenenSignatur = signatur;
}

// ======================================================================
// Gitter, Achsen und Boden – entspricht Wireframe::drawGrid()
// ======================================================================

void GpuAnsichten::gitterErzeugen(bool perspektivisch, int breite, int hoehe, const AnsichtParameter &a, Bereich &boden,
                                  Bereich &gitter, Bereich &achsen)
{
    std::vector<GpuVertex> &v = ansichtVertices;

    double viewWidth = breite / scale;
    double viewHeight = hoehe / scale;
    double extent = std::max(viewWidth, viewHeight) * 1.0;

    double spacing = pow(10.0, floor(log10(150.0 / scale)));

    double centerX = 0.0;
    double centerY = 0.0;
    double centerZ = 0.0;

    double startX = floor(centerX / spacing) * spacing;
    double startY = floor(centerY / spacing) * spacing;
    double startZ = floor(centerZ / spacing) * spacing;

    int count = static_cast<int>(std::ceil(extent / spacing));

    constexpr double eps = 0.01;

    Farbe farColor = invertColor(Farbe(0.0, 0.0, 0.2));
    Farbe nearColor = invertColor(Farbe(0.8, 0.9, 1.0));

    auto punkt = [&](const Vector3D &p, uint32_t farbe)
    {
        Vector3D r = p - ursprung;
        v.push_back({float(r.x), float(r.y), float(r.z), farbe});
    };

    auto linie = [&](const Vector3D &a, const Vector3D &b, const Farbe &f)
    {
        uint32_t farbe = farbeZuRgba(f);
        punkt(a, farbe);
        punkt(b, farbe);
    };

    // Boden (nur Perspektive), halbtransparent wie setPixelAlpha(..., 0.25)
    boden.start = UINT(v.size());
    if (perspektivisch)
    {
        constexpr double groundY = -0.1;
        const int tiles = 100;
        const uint8_t alpha = 64;

        for (int ix = 0; ix < tiles; ++ix)
        {
            for (int iz = 0; iz < tiles; ++iz)
            {
                double x0 = -extent + ix * (2.0 * extent) / tiles;
                double x1 = -extent + (ix + 1) * (2.0 * extent) / tiles;
                double z0 = -extent + iz * (2.0 * extent) / tiles;
                double z1 = -extent + (iz + 1) * (2.0 * extent) / tiles;

                Vector3D A(x0, groundY, z0);
                Vector3D B(x1, groundY, z0);
                Vector3D C(x1, groundY, z1);
                Vector3D D(x0, groundY, z1);

                double t0 = double(iz) / tiles;
                double t1 = double(iz + 1) / tiles;

                uint32_t color0 = farbeZuRgba(Farbe(
                                                  farColor.r * (1.0 - t0) + nearColor.r * t0,
                                                  farColor.g * (1.0 - t0) + nearColor.g * t0,
                                                  farColor.b * (1.0 - t0) + nearColor.b * t0),
                                              alpha);

                uint32_t color1 = farbeZuRgba(Farbe(
                                                  farColor.r * (1.0 - t1) + nearColor.r * t1,
                                                  farColor.g * (1.0 - t1) + nearColor.g * t1,
                                                  farColor.b * (1.0 - t1) + nearColor.b * t1),
                                              alpha);

                punkt(A, color0);
                punkt(B, color0);
                punkt(C, color1);

                punkt(A, color0);
                punkt(C, color1);
                punkt(D, color1);
            }
        }
    }
    boden.anzahl = UINT(v.size()) - boden.start;

    // Nulllinien (Achsen): eigener Bereich, so lang, dass sie auch in einer verschobenen oder stark
    // verkleinerten Ansicht bis an den Rand reichen
    achsen.start = UINT(v.size());
    {
        const double weit = 4.0 * extent + 2.0 * a.auge.length() + 1.0;
        linie(Vector3D(-weit, 0, 0), Vector3D(weit, 0, 0), Farbe(0.35, 0.35, 1.0)); // X
        if (!perspektivisch)
            linie(Vector3D(0, -weit, 0), Vector3D(0, weit, 0), Farbe(0.3, 0.9, 0.3)); // Y
        linie(Vector3D(0, 0, -weit), Vector3D(0, 0, weit), Farbe(1.0, 0.35, 0.35)); // Z
    }
    achsen.anzahl = UINT(v.size()) - achsen.start;

    gitter.start = UINT(v.size());

    // Kamera fuer die Tiefenfaerbung der Linien
    Vector3D camPos(0, 0, -1);
    Vector3D viewDir(0, 0, 1);
    if (!cam.empty())
    {
        camPos = cam[0].position;
        viewDir = (cam[0].lookAt - camPos).normalized();
    }

    for (int i = -count; i <= count; ++i)
    {
        double pX = startX + i * spacing;
        double pY = startY + i * spacing;
        double pZ = startZ + i * spacing;

        if (pX < centerX - extent || pX > centerX + extent)
            continue;

        if (pZ < centerZ - extent || pZ > centerZ + extent)
            continue;

        if (std::abs(pX) < eps || std::abs(pY) < eps || std::abs(pZ) < eps)
            continue;

        Vector3D point(0.0, 0.0, pZ);
        double depth = extent - (point - camPos) * viewDir;
        double t = std::clamp(depth / extent, 0.0, 1.0);

        Farbe c(
            nearColor.r + t * (farColor.r - nearColor.r),
            nearColor.g + t * (farColor.g - nearColor.g),
            nearColor.b + t * (farColor.b - nearColor.b));

        c = invertColor(c);

        // Oben-Ansicht (XZ)
        linie(Vector3D(centerX - extent, 0, pZ), Vector3D(centerX + extent, 0, pZ), c);
        linie(Vector3D(pX, 0, centerZ - extent), Vector3D(pX, 0, centerZ + extent), c);

        if (!perspektivisch)
        {
            // Seiten-Ansicht (YZ)
            linie(Vector3D(0, pY, centerZ - extent), Vector3D(0, pY, centerZ + extent), c);
            linie(Vector3D(0, centerY - extent, pZ), Vector3D(0, centerY + extent, pZ), c);

            // Front-Ansicht (XY)
            linie(Vector3D(centerX - extent, pY, 0), Vector3D(centerX + extent, pY, 0), c);
            linie(Vector3D(pX, centerY - extent, 0), Vector3D(pX, centerY + extent, 0), c);
        }
    }

    gitter.anzahl = UINT(v.size()) - gitter.start;
}

// ======================================================================
// Eine Ansicht zeichnen
// ======================================================================

void GpuAnsichten::ansichtZeichnen(Ziel &z)
{
    Wireframe &wf = *z.wf;
    Fenster &fenster = wf.getFenster();

    if (!fenster.isOpen())
        return;

    HWND hwnd = fenster.getHWND();
    RECT rc;
    if (!GetClientRect(hwnd, &rc))
        return;

    int breite = rc.right - rc.left;
    int hoehe = rc.bottom - rc.top;
    if (breite <= 0 || hoehe <= 0)
        return; // minimiert

    if (!z.swapChain)
    {
        if (!zielErstellen(z, breite, hoehe))
            return;
    }
    else if (breite != z.breite || hoehe != z.hoehe)
    {
        if (!zielPuffer(z, breite, hoehe))
            return;
    }

    // wie bisher in initBuffers(): Groesse fuer project()/createRay() aktualisieren
    wf.w = breite;
    wf.h = hoehe;

    // ---------- Nur zeichnen, wenn sich etwas geaendert hat ----------
    POINT cursor;
    bool mausHier = GetCursorPos(&cursor) && WindowFromPoint(cursor) == hwnd;

    Pruefsumme sig;
    sig.zahl(szenenSignatur);
    sig.zahl(uint64_t(breite));
    sig.zahl(uint64_t(hoehe));
    sig.zahl(uint64_t(wf.hoverType));
    sig.zeiger(wf.hoverObject);
    sig.zeiger(wf.hoverLight);
    {
        AnsichtParameter blick = wf.ansicht(); // Ansicht verschoben (mittlere Maustaste, Fokus)
        sig.vektor(blick.auge);
        sig.vektor(blick.vorne);
        sig.wert(blick.tanHalbY);
    }
    sig.zahl(uint64_t(wf.griffArt));
    if (wf.griffArt != 0)
    {
        sig.zahl(uint64_t(int64_t(wf.griffX)));
        sig.zahl(uint64_t(int64_t(wf.griffY)));
        sig.zahl(uint64_t(int64_t(wf.mouseX)));
        sig.zahl(uint64_t(int64_t(wf.mouseY)));
    }
    sig.zahl(wf.rahmenAktiv);
    if (wf.rahmenAktiv)
    {
        sig.zahl(uint64_t(int64_t(wf.rahmenX0)));
        sig.zahl(uint64_t(int64_t(wf.rahmenY0)));
        sig.zahl(uint64_t(int64_t(wf.rahmenX1)));
        sig.zahl(uint64_t(int64_t(wf.rahmenY1)));
    }
    sig.zahl(uint64_t(kanten::gModus));
    sig.zahl(uint64_t(kanten::gStand));
    sig.zahl(mausHier);
    if (mausHier)
    {
        sig.zahl(uint64_t(int64_t(wf.mouseX)));
        sig.zahl(uint64_t(int64_t(wf.mouseY)));
        sig.zahl(overObject);
    }

    auto jetzt = std::chrono::steady_clock::now();
    if (NUR_BEI_AENDERUNG && sig.h == z.signatur &&
        jetzt - z.gezeichnet < std::chrono::seconds(1))
        return;

    z.signatur = sig.h;
    z.gezeichnet = jetzt;

    AnsichtParameter a = wf.ansicht();
    bool perspektivisch = a.perspektivisch;

    // ---------- Lichter und Kamera (Farbe haengt von Auswahl/Hover dieser Ansicht ab) ----------
    ansichtsObjekte.ursprung = ursprung;
    ansichtsObjekte.zWert = perspektivisch ? 1.0 : -1.0; // Kamera-Frustum nur in Ortho-Ansichten fuellen
    ansichtsObjekte.leeren();
    size_t drehpunktAb = 0, drehpunktBis = 0; // Linien des Drehpunkts (werden ganz vorne gezeichnet)
    {
        // Lichter koennen aus dem Tool-Fenster geloescht werden (siehe gSzeneAendern in main.cpp)
        std::lock_guard<std::mutex> lock(sceneMutex);
        AufzeichnungAktiv aktiv(&ansichtsObjekte);

        for (auto &l : lights)
        {
            DrawMode mode = DrawMode::NORMAL;
            if (selectedType == SelectedType::LIGHT && l.get() == selectedLight)
                mode = DrawMode::HIGHLIGHT;
            else if (wf.hoverType == SelectedType::LIGHT && l.get() == wf.hoverLight)
                mode = DrawMode::HOVER;

            ansichtsObjekte.neuesObjekt();
            if (useFlat)
                l->drawFlat(wf, mode);
            else
                l->drawWireframePixels(wf, mode);
            wf.lichtMarkierungZeichnen(*l, mode); // immer sichtbar gross
        }

        // Render-Einstellungen: Rauchboxen und Wolkenbereich als Rahmen
        auto rahmen = [&](const Vector3D &mn, const Vector3D &mx, const Farbe &f)
        {
            ansichtsObjekte.neuesObjekt();
            Material m = makeMaterialSimple(f);
            auto ecke = [&](int i)
            { return Vector3D((i & 1) ? mx.x : mn.x, (i & 2) ? mx.y : mn.y, (i & 4) ? mx.z : mn.z); };
            const int kanten[12][2] = {{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3}, {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
            for (auto &k : kanten)
            {
                int x0, y0, x1, y1;
                double z0, z1;
                wf.project(ecke(k[0]), x0, y0, z0);
                wf.project(ecke(k[1]), x1, y1, z1);
                wf.drawLine(x0, y0, z0, x1, y1, z1, m);
            }
        };
        // Farbe je nach Auswahl (gelb) und Maus darueber (cyan): die Boxen sind als VolumenBox anklickbar
        auto boxFarbe = [&](int index, const Farbe &normal)
        {
            VolumenBox *vb = volumenBoxVon(index);
            if (vb && istAusgewaehlt(vb))
                return Farbe(1.0, 1.0, 0.0);
            if (vb && wf.hoverType == SelectedType::OBJECT && wf.hoverObject == vb)
                return Farbe(0.0, 1.0, 1.0);
            return normal;
        };
        if (rauchVorhanden)
            for (size_t i = 0; i < smokeBoxes.size(); ++i)
                rahmen(smokeBoxes[i].min, smokeBoxes[i].max, boxFarbe(int(i), Farbe(0.75, 0.75, 0.75)));
        if (wolkeVolumeVorhanden)
        {
            Vector3D mn, mx;
            wolkenBereich(mn, mx);
            const Farbe wolkenFarbe = boxFarbe(-1, Farbe(0.85, 0.9, 1.0));
            rahmen(mn, mx, wolkenFarbe); // hier entstehen die Wolken

            // Griffmarken an Ecken und Kantenmitten: dort laesst sich der Wolkenbereich anfassen
            // (im Innern nicht, sonst saesse er ueberall im Weg). Feste Groesse in Pixeln.
            {
                ansichtsObjekte.neuesObjekt();
                Material m = makeMaterialSimple(wolkenFarbe);
                AnsichtParameter av = wf.ansicht();
                auto weltJePixel = [&](const Vector3D &p) -> double
                {
                    if (!av.perspektivisch)
                        return av.pixelProEinheit > 0 ? 1.0 / av.pixelProEinheit : 0.0;
                    double tiefe = (p - av.auge) * av.vorne;
                    return tiefe > 1e-9 ? tiefe * 2.0 * av.tanHalbY / std::max(wf.h, 1) : 0.0;
                };
                auto strich = [&](const Vector3D &a, const Vector3D &b)
                {
                    int x0, y0, x1, y1;
                    double z0, z1;
                    wf.project(a, x0, y0, z0);
                    wf.project(b, x1, y1, z1);
                    wf.drawLine(x0, y0, z0, x1, y1, z1, m);
                };
                auto marke = [&](const Vector3D &p, double pixel)
                {
                    double g = weltJePixel(p) * pixel;
                    if (!(g > 0) || !std::isfinite(g))
                        return;
                    // kleiner Wuerfel aus Kanten: in jeder Ansicht als Quadrat sichtbar
                    Vector3D a = p - Vector3D(g, g, g), b = p + Vector3D(g, g, g);
                    auto e = [&](int i)
                    { return Vector3D((i & 1) ? b.x : a.x, (i & 2) ? b.y : a.y, (i & 4) ? b.z : a.z); };
                    const int kanten[12][2] = {{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3}, {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
                    for (auto &k : kanten)
                        strich(e(k[0]), e(k[1]));
                };
                auto ecke = [&](int i)
                { return Vector3D((i & 1) ? mx.x : mn.x, (i & 2) ? mx.y : mn.y, (i & 4) ? mx.z : mn.z); };
                for (int i = 0; i < 8; ++i)
                    marke(ecke(i), 5.0); // Ecken
                const int kanten[12][2] = {{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3}, {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
                for (auto &k : kanten)
                    marke((ecke(k[0]) + ecke(k[1])) * 0.5, 3.0); // Kantenmitten (kleiner)
            }
            // Himmelsbereich nur zusaetzlich, wenn er hoeher/tiefer reicht als die Wolkenschicht
            if (std::fabs(std::min(SkyMin.y, SkyMax.y) - mn.y) > 1e-9 || std::fabs(std::max(SkyMin.y, SkyMax.y) - mx.y) > 1e-9)
                rahmen(SkyMin, SkyMax, Farbe(0.4, 0.6, 1.0));
        }

        // Drehpunkt: Achsenkreuz mit kleinem Wuerfel, feste Groesse in Pixeln (orange, beim Ziehen gelb)
        drehpunktAb = drehpunktBis = ansichtsObjekte.linien.size();
        if (drehpunktSichtbar())
        {
            ansichtsObjekte.neuesObjekt();
            const Vector3D p = drehpunkt();
            Material m = makeMaterialSimple(drehpunktZiehen ? Farbe(1.0, 1.0, 0.2) : Farbe(1.0, 0.55, 0.1));
            AnsichtParameter av = wf.ansicht();
            double jePixel = 0.0;
            if (!av.perspektivisch)
                jePixel = av.pixelProEinheit > 0 ? 1.0 / av.pixelProEinheit : 0.0;
            else
            {
                double tiefe = (p - av.auge) * av.vorne;
                jePixel = tiefe > 1e-9 ? tiefe * 2.0 * av.tanHalbY / std::max(wf.h, 1) : 0.0;
            }
            if (jePixel > 0 && std::isfinite(jePixel))
            {
                auto strich = [&](const Vector3D &a, const Vector3D &b)
                {
                    int x0, y0, x1, y1;
                    double z0, z1;
                    wf.project(a, x0, y0, z0);
                    wf.project(b, x1, y1, z1);
                    wf.drawLine(x0, y0, z0, x1, y1, z1, m);
                };
                const double l = 12.0 * jePixel, g = 3.5 * jePixel;
                strich(p - Vector3D(l, 0, 0), p + Vector3D(l, 0, 0));
                strich(p - Vector3D(0, l, 0), p + Vector3D(0, l, 0));
                strich(p - Vector3D(0, 0, l), p + Vector3D(0, 0, l));
                Vector3D a = p - Vector3D(g, g, g), b = p + Vector3D(g, g, g);
                auto e = [&](int i)
                { return Vector3D((i & 1) ? b.x : a.x, (i & 2) ? b.y : a.y, (i & 4) ? b.z : a.z); };
                const int kanten[12][2] = {{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3}, {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
                for (auto &k : kanten)
                    strich(e(k[0]), e(k[1]));
            }
        }
        // Kanten-Modus: gewaehlte Kanten kraeftig (vor allem anderen, 3 Pixel breit)
        if (kanten::gModus)
        {
            std::vector<kanten::Kante> gewaehlt;
            {
                std::lock_guard<std::mutex> lock(kanten::gMutex);
                gewaehlt = kanten::gGewaehlt;
            }
            if (!gewaehlt.empty())
            {
                ansichtsObjekte.neuesObjekt();
                Material m = makeMaterialSimple(Farbe(1.0, 0.35, 0.15));
                for (const kanten::Kante &k : gewaehlt)
                {
                    int x0, y0, x1, y1;
                    double z0, z1;
                    wf.project(k.a, x0, y0, z0);
                    wf.project(k.b, x1, y1, z1);
                    wf.drawLine(x0, y0, z0, x1, y1, z1, m);
                }
            }
        }
        drehpunktBis = ansichtsObjekte.linien.size();

        // Kanten-Modus: alle Kanten des Netzes duenn (wie Drahtgitter), damit man sie sieht und anklicken kann
        std::vector<object *> kantenAuswahl = kanten::gModus ? ausgewaehlteObjekte() : std::vector<object *>();
        if (kanten::gModus && kanten::gObjekt &&
            std::find(kantenAuswahl.begin(), kantenAuswahl.end(), kanten::gObjekt) != kantenAuswahl.end()) // noch vorhanden
        {
            static std::vector<kanten::Kante> alle;
            static object *von = nullptr;
            static unsigned stand = ~0u;
            static size_t dreiecke = 0;
            auto listen = objektDreiecke(kanten::gObjekt);
            size_t n = 0;
            for (auto *l : listen)
                n += l->size();
            if (von != kanten::gObjekt || stand != kanten::gStand || dreiecke != n)
            {
                alle = kanten::alleKanten(listen, 60000);
                von = kanten::gObjekt;
                stand = kanten::gStand;
                dreiecke = n;
            }
            ansichtsObjekte.neuesObjekt();
            Material m = makeMaterialSimple(Farbe(0.95, 0.85, 0.3));
            for (const kanten::Kante &k : alle)
            {
                int x0, y0, x1, y1;
                double z0, z1;
                wf.project(k.a, x0, y0, z0);
                wf.project(k.b, x1, y1, z1);
                wf.drawLine(x0, y0, z0, x1, y1, z1, m);
            }
        }

        if (!cam.empty())
        {
            DrawMode mode = DrawMode::NORMAL;
            if (selectedType == SelectedType::CAMERA)
                mode = DrawMode::HIGHLIGHT;
            else if (wf.hoverType == SelectedType::CAMERA || wf.hoverType == SelectedType::LOOKAT)
                mode = DrawMode::HOVER;

            ansichtsObjekte.neuesObjekt();
            if (useFlat)
                cam[0].drawFlat(wf, mode);
            else
                cam[0].drawWireframePixels(wf, mode);
        }
    }

    // ---------- Vertices dieser Ansicht ----------
    std::vector<GpuVertex> &v = ansichtVertices;
    v.clear();

    Bereich hintergrund, lkDreiecke, lkLinien, boden, gitter, achsen, kreuz;

    // Hintergrund in Pixeln, wie initBuffers()
    hintergrund.start = UINT(v.size());
    if (perspektivisch)
    {
        for (int y = 0; y < hoehe; y++)
        {
            double t = hoehe > 1 ? double(y) / double(hoehe - 1) : 0.0;
            double c = 0.18 * (1.0 - t * t) + 0.35 * t * t;
            rechteck(v, 0.0f, float(y), float(breite), float(y + 1), farbeZuRgba(Farbe(c, c, c)));
        }
    }
    else
    {
        rechteck(v, 0.0f, 0.0f, float(breite), float(hoehe), farbeZuRgba(Farbe(0.22, 0.22, 0.22)));
    }
    hintergrund.anzahl = UINT(v.size()) - hintergrund.start;

    lkDreiecke.start = UINT(v.size());
    v.insert(v.end(), ansichtsObjekte.dreiecke.begin(), ansichtsObjekte.dreiecke.end());
    lkDreiecke.anzahl = UINT(v.size()) - lkDreiecke.start;

    // Linien von Lichtern, Kamera, Rahmen (ohne Drehpunkt), danach die des Drehpunkts fuer sich
    Bereich drehpunktLinien;
    {
        auto &li = ansichtsObjekte.linien;
        drehpunktAb = std::min(drehpunktAb, li.size());
        drehpunktBis = std::min(std::max(drehpunktBis, drehpunktAb), li.size());
        lkLinien.start = UINT(v.size());
        v.insert(v.end(), li.begin(), li.begin() + drehpunktAb);
        v.insert(v.end(), li.begin() + drehpunktBis, li.end());
        lkLinien.anzahl = UINT(v.size()) - lkLinien.start;
        drehpunktLinien.start = UINT(v.size());
        v.insert(v.end(), li.begin() + drehpunktAb, li.begin() + drehpunktBis);
        drehpunktLinien.anzahl = UINT(v.size()) - drehpunktLinien.start;
    }

    gitterErzeugen(perspektivisch, breite, hoehe, a, boden, gitter, achsen);

    // Rauchboxen und Wolkenbereich leicht neblig: halbtransparente Seiten und drei waagrechte Schichten
    Bereich nebel;
    nebel.start = UINT(v.size());
    {
        auto dreieck = [&](const Vector3D &a, const Vector3D &b, const Vector3D &c, uint32_t f)
        {
            for (const Vector3D *p : {&a, &b, &c})
            {
                Vector3D r = *p - ursprung;
                v.push_back({float(r.x), float(r.y), float(r.z), f});
            }
        };
        auto viereck = [&](const Vector3D &a, const Vector3D &b, const Vector3D &c, const Vector3D &d, uint32_t f)
        {
            dreieck(a, b, c, f);
            dreieck(a, c, d, f);
        };
        auto nebelBox = [&](const Vector3D &mn, const Vector3D &mx, const Farbe &farbe, uint8_t alpha)
        {
            const uint32_t f = farbeZuRgba(farbe, alpha);
            auto e = [&](int i)
            { return Vector3D((i & 1) ? mx.x : mn.x, (i & 2) ? mx.y : mn.y, (i & 4) ? mx.z : mn.z); };
            viereck(e(0), e(1), e(3), e(2), f); // vorne (z min)
            viereck(e(4), e(5), e(7), e(6), f); // hinten
            viereck(e(0), e(2), e(6), e(4), f); // links
            viereck(e(1), e(3), e(7), e(5), f); // rechts
            viereck(e(0), e(1), e(5), e(4), f); // unten
            viereck(e(2), e(3), e(7), e(6), f); // oben
            for (double t : {0.25, 0.5, 0.75}) // innere Schichten: wirkt wie ein Volumen
            {
                double y = mn.y + (mx.y - mn.y) * t;
                viereck(Vector3D(mn.x, y, mn.z), Vector3D(mx.x, y, mn.z), Vector3D(mx.x, y, mx.z), Vector3D(mn.x, y, mx.z), f);
            }
        };
        if (rauchVorhanden)
            for (const auto &b : smokeBoxes)
            {
                Farbe c(std::min(1.0, b.color.r), std::min(1.0, b.color.g), std::min(1.0, b.color.b));
                nebelBox(Vector3D(std::min(b.min.x, b.max.x), std::min(b.min.y, b.max.y), std::min(b.min.z, b.max.z)),
                         Vector3D(std::max(b.min.x, b.max.x), std::max(b.min.y, b.max.y), std::max(b.min.z, b.max.z)), c, 26);
            }
        if (wolkeVolumeVorhanden)
        {
            Vector3D mn, mx;
            wolkenBereich(mn, mx);
            nebelBox(mn, mx, Farbe(0.9, 0.93, 1.0), 12);
        }
    }
    nebel.anzahl = UINT(v.size()) - nebel.start;

    // Fadenkreuz an der Maus (nur wenn die Maus ueber diesem Fenster ist)
    kreuz.start = UINT(v.size());
    if (mausHier)
    {
        uint32_t farbe = farbeZuRgba(overObject ? Farbe(0, 1, 1) : Farbe(1, 0, 0));
        float mx = float(wf.mouseX) + 0.5f;
        float my = float(wf.mouseY) + 0.5f;

        v.push_back({mx - 10.0f, my, 0, farbe});
        v.push_back({mx + 11.0f, my, 0, farbe});
        v.push_back({mx, my - 10.0f, 0, farbe});
        v.push_back({mx, my + 11.0f, 0, farbe});
    }

    // Auswahlrahmen: blau = Objekte ganz im Rahmen, gruen = Objekte, die ihn beruehren
    if (wf.rahmenAktiv)
    {
        uint32_t farbe = farbeZuRgba(wf.rahmenX1 >= wf.rahmenX0 ? Farbe(0.3, 0.6, 1.0) : Farbe(0.3, 1.0, 0.5));
        float x0 = float(std::min(wf.rahmenX0, wf.rahmenX1)) + 0.5f, x1 = float(std::max(wf.rahmenX0, wf.rahmenX1)) + 0.5f;
        float y0 = float(std::min(wf.rahmenY0, wf.rahmenY1)) + 0.5f, y1 = float(std::max(wf.rahmenY0, wf.rahmenY1)) + 0.5f;
        float ecken[5][2] = {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}, {x0, y0}};
        for (int i = 0; i < 4; ++i)
        {
            v.push_back({ecken[i][0], ecken[i][1], 0, farbe});
            v.push_back({ecken[i + 1][0], ecken[i + 1][1], 0, farbe});
        }
    }
    // Drehkreis bzw. Skalierlinie, nur in der Ansicht, in der gezogen wird
    if (wf.griffArt != 0)
    {
        const uint32_t gelb = farbeZuRgba(Farbe(1.0, 0.85, 0.2));
        const uint32_t grau = farbeZuRgba(Farbe(0.55, 0.55, 0.55));
        auto linie = [&](float x0, float y0, float x1, float y1, uint32_t f)
        {
            v.push_back({x0, y0, 0, f});
            v.push_back({x1, y1, 0, f});
        };
        const float cx = float(wf.griffX) + 0.5f, cy = float(wf.griffY) + 0.5f;
        const float mx = float(wf.mouseX) + 0.5f, my = float(wf.mouseY) + 0.5f;
        const float sx = float(wf.griffStartX) + 0.5f, sy = float(wf.griffStartY) + 0.5f;

        // Mitte als kleines Kreuz
        linie(cx - 5, cy, cx + 6, cy, gelb);
        linie(cx, cy - 5, cx, cy + 6, gelb);

        if (wf.griffArt == 1)
        {
            // Kreis durch die Maus, Strahl zur Startposition (grau) und zur Maus (gelb)
            float r = std::max(8.0f, std::hypot(mx - cx, my - cy));
            const int n = 64;
            for (int i = 0; i < n; ++i)
            {
                float w0 = float(2 * M_PI * i / n), w1 = float(2 * M_PI * (i + 1) / n);
                linie(cx + r * std::cos(w0), cy + r * std::sin(w0), cx + r * std::cos(w1), cy + r * std::sin(w1), gelb);
            }
            float rs = std::hypot(sx - cx, sy - cy);
            if (rs > 1.0f)
                linie(cx, cy, cx + (sx - cx) / rs * r, cy + (sy - cy) / rs * r, grau);
            linie(cx, cy, mx, my, gelb);
        }
        else
        {
            // Linie von der Mitte zur Maus mit Griff; Startabstand grau
            linie(cx, cy, mx, my, gelb);
            linie(mx - 4, my - 4, mx + 4, my - 4, gelb);
            linie(mx + 4, my - 4, mx + 4, my + 4, gelb);
            linie(mx + 4, my + 4, mx - 4, my + 4, gelb);
            linie(mx - 4, my + 4, mx - 4, my - 4, gelb);
            linie(cx, cy, sx, sy, grau);
            if (wf.griffJeRichtung)
            {
                // waagrechter und senkrechter Anteil (je Richtung getrennt skaliert)
                linie(cx, cy, mx, cy, grau);
                linie(mx, cy, mx, my, grau);
            }
        }
    }

    // Achsenkreuz unten links: X rot, Y gruen, Z blau, mit Buchstaben an den Spitzen.
    // Zeigt eine Achse aus dem Bild: Kreis mit Punkt (zum Betrachter) bzw. Kreuz (vom Betrachter weg)
    {
        const float g = 2.0f; // doppelt so gross wie frueher
        const float ox = 40.0f * g, oy = float(hoehe) - 40.0f * g, laenge = 26.0f * g;
        // doppelt so dick: jede Linie zweimal, um ein Pixel quer versetzt (Linien sind immer 1 Pixel breit)
        auto linie = [&](float x0, float y0, float x1, float y1, uint32_t f)
        {
            float dx = x1 - x0, dy = y1 - y0;
            float l = std::sqrt(dx * dx + dy * dy);
            float qx = l > 1e-6f ? -dy / l : 0.0f, qy = l > 1e-6f ? dx / l : 1.0f;
            if (std::fabs(qx) > std::fabs(qy)) // auf ganze Pixel: waagrecht bzw. senkrecht versetzen
                qx = qx > 0 ? 1.0f : -1.0f, qy = 0.0f;
            else
                qy = qy > 0 ? 1.0f : -1.0f, qx = 0.0f;
            for (int k = 0; k < 2; ++k)
            {
                v.push_back({x0 + qx * k, y0 + qy * k, 0, f});
                v.push_back({x1 + qx * k, y1 + qy * k, 0, f});
            }
        };
        auto buchstabe = [&](int k, float cx, float cy, uint32_t f)
        {
            if (k == 0) // X
            {
                linie(cx - 3 * g, cy - 4 * g, cx + 3 * g, cy + 4 * g, f);
                linie(cx + 3 * g, cy - 4 * g, cx - 3 * g, cy + 4 * g, f);
            }
            else if (k == 1) // Y
            {
                linie(cx - 3 * g, cy - 4 * g, cx, cy, f);
                linie(cx + 3 * g, cy - 4 * g, cx, cy, f);
                linie(cx, cy, cx, cy + 4 * g, f);
            }
            else // Z
            {
                linie(cx - 3 * g, cy - 4 * g, cx + 3 * g, cy - 4 * g, f);
                linie(cx + 3 * g, cy - 4 * g, cx - 3 * g, cy + 4 * g, f);
                linie(cx - 3 * g, cy + 4 * g, cx + 3 * g, cy + 4 * g, f);
            }
        };

        const Farbe farben[3] = {Farbe(1.0, 0.35, 0.35), Farbe(0.35, 1.0, 0.35), Farbe(0.4, 0.6, 1.0)};
        for (int k = 0; k < 3; ++k)
        {
            Vector3D d(k == 0, k == 1, k == 2);
            uint32_t f = farbeZuRgba(farben[k]);
            double sx = d * a.rechts, sy = -(d * a.oben); // Bildschirm: y nach unten
            double l = std::sqrt(sx * sx + sy * sy);

            if (l < 0.25)
            {
                // Achse zeigt aus dem Bild heraus bzw. hinein
                const int n = 12;
                for (int i = 0; i < n; ++i)
                {
                    float w0 = float(2 * M_PI * i / n), w1 = float(2 * M_PI * (i + 1) / n);
                    linie(ox + 5 * g * std::cos(w0), oy + 5 * g * std::sin(w0), ox + 5 * g * std::cos(w1), oy + 5 * g * std::sin(w1), f);
                }
                if (d * a.vorne < 0) // zum Betrachter: Punkt
                    {
                    linie(ox - g, oy, ox + g, oy, f);
                    linie(ox, oy - g, ox, oy + g, f);
                }
                else // vom Betrachter weg: Kreuz
                {
                    linie(ox - 3 * g, oy - 3 * g, ox + 3 * g, oy + 3 * g, f);
                    linie(ox + 3 * g, oy - 3 * g, ox - 3 * g, oy + 3 * g, f);
                }
                buchstabe(k, ox + 11 * g, oy + 9 * g, f);
                continue;
            }

            float ux = float(sx / l), uy = float(sy / l);
            float s = laenge * float(std::min(1.0, l)); // in der Perspektive verkuerzt
            float ex = ox + ux * s, ey = oy + uy * s;
            linie(ox, oy, ex, ey, f);
            // Pfeilspitze
            const float c = 0.906f, sn = 0.423f; // cos/sin 25 Grad
            linie(ex, ey, ex - 7 * g * (ux * c - uy * sn), ey - 7 * g * (uy * c + ux * sn), f);
            linie(ex, ey, ex - 7 * g * (ux * c + uy * sn), ey - 7 * g * (uy * c - ux * sn), f);
            buchstabe(k, ex + ux * 9 * g, ey + uy * 9 * g, f);
        }
    }

    kreuz.anzahl = UINT(v.size()) - kreuz.start;

    hochladen(ansichtPuffer, v);

    // ---------- Matrizen ----------
    double extent = std::max(breite, hoehe) / scale;
    double abstandUrsprung = (ursprung - a.auge).length();
    double tiefenRadius;
    {
        std::lock_guard<std::mutex> lock(sceneMutex); // liest Lichter und Kamera
        tiefenRadius = wf.szenenRadius(a.auge);
    }
    tiefenRadius = std::max(tiefenRadius, abstandUrsprung + std::sqrt(szene.maxAbstand2));
    tiefenRadius = std::max(tiefenRadius, abstandUrsprung + std::sqrt(ansichtsObjekte.maxAbstand2));
    tiefenRadius = std::max(tiefenRadius, a.auge.length() + extent * 1.5);
    for (object *obj : bekannte)
    {
        const ObjektEintrag &e = cache[obj];
        tiefenRadius = std::max(tiefenRadius, (e.ursprung - a.auge).length() + e.radius);
    }
    for (const auto &it : instanzTeile)
        tiefenRadius = std::max(tiefenRadius, (it.second->boundingCenter - a.auge).length() + it.second->boundingRadius);
    tiefenRadius += 10.0;

    double welt[16], pixel[16];
    weltMatrix(a, ursprung, breite, hoehe, tiefenRadius, welt);
    pixelMatrix(breite, hoehe, pixel);

    // Linien minimal naeher zum Betrachter (wie der Bias in drawLine())
    double linienMul = perspektivisch ? 1.0 + 1e-5 : 1.0;
    double linienAdd = perspektivisch ? 0.0 : 1e-6;

    // ---------- Zeichnen ----------
    ID3D11RenderTargetView *rtv = z.rtv.get();
    context->OMSetRenderTargets(1, &rtv, z.dsv.get());
    context->ClearDepthStencilView(z.dsv.get(), D3D11_CLEAR_DEPTH, 0.0f, 0);

    D3D11_VIEWPORT vp = {};
    vp.Width = float(breite);
    vp.Height = float(hoehe);
    vp.MaxDepth = 1.0f;
    context->RSSetViewports(1, &vp);
    context->RSSetState(rasterizer.get());

    context->PSSetShader(psGeometrie.get(), nullptr, 0);
    ID3D11Buffer *cbs[3] = {cbGeometrie.get(), cbFarben.get(), cbBeleuchtung.get()};
    context->VSSetConstantBuffers(0, 3, cbs);
    context->PSSetConstantBuffers(0, 2, cbs);
    ID3D11ShaderResourceView *tabelle = farbTabelleSrv.get();
    context->VSSetShaderResources(0, 1, &tabelle);
    context->OMSetBlendState(blendAlpha.get(), nullptr, 0xFFFFFFFF);

    // Hintergrund
    geometriePipeline();
    context->OMSetDepthStencilState(tiefeAus.get(), 0);
    geometrieKonstanten(pixel, 1.0, 0.0, 0.0, Vector3D(0, 0, 0));
    zeichnen(ansichtPuffer, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST, hintergrund);

    // Objekte, danach Auswahl/Hover in ihrer Farbe mit gleicher Tiefe darueber
    context->OMSetDepthStencilState(tiefeSchreiben.get(), 0);

    for (object *obj : bekannte)
        objektZeichnen(cache[obj], 0, welt, linienMul, linienAdd);
    for (const auto &it : instanzTeile)
        objektZeichnen(cache[it.first], 0, welt, linienMul, linienAdd, &it.second->abb);
    zeichnen(objektPuffer, alleObjekte, welt, linienMul, linienAdd);

    // Objekt unter der Maus (eine Gruppe: alle Objekte darin), ausser es ist ausgewaehlt
    std::vector<object *> hoverTeile;
    if (wf.hoverType == SelectedType::OBJECT)
    {
        std::vector<object *> teile;
        gruppenAufloesen(wf.hoverObject, teile);
        for (object *h : teile)
            if (std::find(ausgewaehlt.begin(), ausgewaehlt.end(), h) == ausgewaehlt.end())
                hoverTeile.push_back(h);
    }
    const bool hover = !hoverTeile.empty() || !instanzenHover.empty();

    auto auswahlZeichnen = [&](double mul, double add)
    {
        for (object *a : ausgewaehlt)
        {
            auto it = cache.find(a);
            if (it != cache.end())
            {
                objektZeichnen(it->second, 1, welt, mul, add);
                continue;
            }
            for (auto &b : ausgewaehltBereiche)
                if (b.first == a)
                    zeichnen(objektPuffer, b.second, welt, mul, add, 1.0);
        }
        if (!instanzAuswahlMenge.empty())
            for (const auto &it : instanzTeile)
                if (instanzAuswahlMenge.count(it.second))
                    objektZeichnen(cache[it.first], 1, welt, mul, add, &it.second->abb);
    };

    auto hoverZeichnen = [&](double mul, double add)
    {
        for (object *h : hoverTeile)
        {
            auto it = cache.find(h);
            if (it != cache.end())
            {
                objektZeichnen(it->second, 2, welt, mul, add);
                continue;
            }
            for (auto &b : hoverBereiche)
                if (b.first == h)
                    zeichnen(objektPuffer, b.second, welt, mul, add, 1.0);
        }
        if (!instanzHoverMenge.empty())
            for (const auto &it : instanzTeile)
                if (instanzHoverMenge.count(it.second))
                    objektZeichnen(cache[it.first], 2, welt, mul, add, &it.second->abb);
    };

    farbKonstanten(TOENUNG_AUSWAHL, 0);
    auswahlZeichnen(linienMul, linienAdd);
    farbKonstanten(TOENUNG_HOVER, 0);
    hoverZeichnen(linienMul, linienAdd);

    // Lichter und Kamera
    zeichnen(ansichtPuffer, ObjektBereiche{lkDreiecke, lkLinien}, welt, linienMul, linienAdd);

    // Boden (halbtransparent, schreibt Tiefe wie setPixelAlpha)
    geometriePipeline();
    geometrieKonstanten(welt, 1.0, 0.0, 0.0, Vector3D(0, 0, 0));
    zeichnen(ansichtPuffer, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST, boden);

    // Nebel der Rauchboxen/Wolken: halbtransparent, verdeckt nichts (Tiefe nur pruefen)
    context->OMSetDepthStencilState(tiefeNurTesten.get(), 0);
    zeichnen(ansichtPuffer, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST, nebel);

    // Gitter und Achsen
    context->OMSetDepthStencilState(GITTER_IMMER_VORNE ? tiefeAus.get() : tiefeNurTesten.get(), 0);
    geometrieKonstanten(welt, linienMul, linienAdd, 0.0, Vector3D(0, 0, 0));
    zeichnen(ansichtPuffer, D3D11_PRIMITIVE_TOPOLOGY_LINELIST, gitter);
    // Nulllinien: wie das Gitter von Objekten verdeckt, aber immer bis an den Rand der Ansicht
    zeichnen(ansichtPuffer, D3D11_PRIMITIVE_TOPOLOGY_LINELIST, achsen);

    // Umrandung fuer Auswahl (gelb) und Hover (cyan)
    const bool auswahlDa = !ausgewaehlt.empty() || !instanzenAusgewaehlt.empty();
    if (auswahlDa || hover)
    {
        ID3D11RenderTargetView *maskeRtv = z.maskeRtv.get();
        context->OMSetRenderTargets(1, &maskeRtv, nullptr);
        const float leer[4] = {0, 0, 0, 0};
        context->ClearRenderTargetView(maskeRtv, leer);

        context->OMSetDepthStencilState(tiefeAus.get(), 0);
        context->OMSetBlendState(blendAus.get(), nullptr, 0xFFFFFFFF);
        context->PSSetShader(psMaske.get(), nullptr, 0);

        maskeAktiv = true;
        if (auswahlDa)
        {
            farbKonstanten(0x000000FF, 0); // r = 1
            auswahlZeichnen(1.0, 0.0);
        }
        if (hover)
        {
            farbKonstanten(0x0000FF00, 0); // g = 1
            hoverZeichnen(1.0, 0.0);
        }
        maskeAktiv = false;

        context->OMSetRenderTargets(1, &rtv, z.dsv.get());
        context->OMSetBlendState(blendAlpha.get(), nullptr, 0xFFFFFFFF);

        context->IASetInputLayout(nullptr);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(vsVollbild.get(), nullptr, 0);
        context->PSSetShader(psUmriss.get(), nullptr, 0);

        ID3D11ShaderResourceView *srv = z.maskeSrv.get();
        context->PSSetShaderResources(0, 1, &srv);
        farbKonstanten(0xFF00FFFF, 0xFFFFFF00); // gelb (Auswahl), cyan (Hover) - direkt, ohne toCOLORREF
        context->Draw(3, 0);

        ID3D11ShaderResourceView *keine = nullptr;
        context->PSSetShaderResources(0, 1, &keine);
        context->PSSetShader(psGeometrie.get(), nullptr, 0);
    }

    // Drehpunkt ganz vorne: nicht von Objekten verdeckt (z.B. im Inneren eines Baums)
    context->OMSetDepthStencilState(tiefeAus.get(), 0);
    zeichnen(ansichtPuffer, ObjektBereiche{Bereich{}, drehpunktLinien}, welt, 1.0, 0.0);

    // Fadenkreuz ganz oben
    geometriePipeline();
    context->OMSetDepthStencilState(tiefeAus.get(), 0);
    geometrieKonstanten(pixel, 1.0, 0.0, 0.0, Vector3D(0, 0, 0));
    zeichnen(ansichtPuffer, D3D11_PRIMITIVE_TOPOLOGY_LINELIST, kreuz);

    z.swapChain->Present(0, 0);
}
