#pragma once
#include <map>
#include <memory>
#include <windows.h>
#include <d3d11.h>
#include <atomic>
#include <chrono>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#include "gpu_aufzeichnung.h"

class Wireframe;
class object;
class Texture;
class Instanz;
struct Affin;
struct AnsichtParameter;

// Kleiner COM-Zeiger: gibt das Objekt beim Zerstoeren frei
template <class T>
class GpuRef
{
public:
    GpuRef() = default;
    ~GpuRef() { reset(); }

    GpuRef(const GpuRef &) = delete;
    GpuRef &operator=(const GpuRef &) = delete;

    GpuRef(GpuRef &&o) noexcept : p(o.p) { o.p = nullptr; }
    GpuRef &operator=(GpuRef &&o) noexcept
    {
        if (this != &o)
        {
            reset();
            p = o.p;
            o.p = nullptr;
        }
        return *this;
    }

    T *get() const { return p; }
    T *operator->() const { return p; }
    explicit operator bool() const { return p != nullptr; }

    // Fuer Create...(..., x.put()): gibt das alte Objekt frei und liefert die Adresse des Zeigers
    T **put()
    {
        reset();
        return &p;
    }

    void reset()
    {
        if (p)
        {
            p->Release();
            p = nullptr;
        }
    }

private:
    T *p = nullptr;
};

// Zeichnet die vier Editor-Ansichten (Top, Front, Side, Perspektive) auf der GPU.
// Ein gemeinsames D3D11-Device, eine Swapchain pro Fenster, ein eigener Renderthread.
// Die Fenster muessen im Modus FensterModus::GPU erzeugt worden sein.
//
// Bekannte Objekttypen (Mesh, Tree, Kugel, Pfeil, Stab) liegen dauerhaft auf der GPU und werden
// nur nach einer Aenderung neu hochgeladen; ihre Beleuchtung rechnet der Shader.
// Alle anderen Objekte werden wie bisher in jedem Frame ueber ihr drawFlat() aufgezeichnet.
class GpuAnsichten
{
public:
    explicit GpuAnsichten(const std::vector<Wireframe *> &ansichten);
    ~GpuAnsichten();

    void start();
    void stop();

    // Weltgrenzen eines Objekts aus seiner tatsaechlichen Geometrie (minBound/maxBound sind bei
    // Mesh/Tree/Pfeil/Stab nicht zuverlaessig). Wird auch vom Tool-Fenster benutzt.
    static bool objektGrenzen(const object *obj, Vector3D &minimum, Vector3D &maximum);

private:
    struct Ziel
    {
        Wireframe *wf = nullptr;
        GpuRef<IDXGISwapChain> swapChain;
        GpuRef<ID3D11RenderTargetView> rtv;
        GpuRef<ID3D11Texture2D> tiefe;
        GpuRef<ID3D11DepthStencilView> dsv;
        GpuRef<ID3D11Texture2D> maske; // fuer die Umrandung von Auswahl/Hover
        GpuRef<ID3D11RenderTargetView> maskeRtv;
        GpuRef<ID3D11ShaderResourceView> maskeSrv;
        int breite = 0;
        int hoehe = 0;

        uint64_t signatur = 0; // Zustand beim letzten Zeichnen
        std::chrono::steady_clock::time_point gezeichnet;
    };

    struct Bereich
    {
        UINT start = 0;
        UINT anzahl = 0;
    };

    struct ObjektBereiche
    {
        Bereich dreiecke;
        Bereich linien;
    };

    struct Puffer
    {
        GpuRef<ID3D11Buffer> buffer;
        size_t kapazitaet = 0; // in Vertices
    };

    // Vertex fuer beleuchtete Geometrie: Beleuchtung pro Flaeche wie in den drawFlat()-Funktionen
    struct BeleuchtetVertex
    {
        float x, y, z;    // Position relativ zum Objekt-Ursprung
        float nx, ny, nz; // Flaechennormale
        float mx, my, mz; // Flaechenmitte (Lichtrichtung wird dort berechnet)
        float r, g, b;    // Grundfarbe (mat.diffuse)
        float flags;      // 1 = Normale zur Kamera drehen, 2 = Auswahl ohne Schattierung, 4 = mit Textur
        float u, v;       // Bildkoordinaten (nur mit Textur)
    };

    // Ein bekanntes Objekt auf der GPU
    struct ObjektEintrag
    {
        uint64_t fingerabdruck = 0;
        Vector3D ursprung;
        double radius = 0.0;
        bool benutzt = false;

        // Flach: beleuchtete Dreiecke, danach Linien
        bool beleuchtetOk = false;
        GpuRef<ID3D11Buffer> beleuchtet;
        UINT dreiecke = 0;
        UINT linien = 0;
        GpuRef<ID3D11ShaderResourceView> bild; // Textur (nur Flaechen mit Textur)

        // Drahtgitter: aufgezeichnet, je DrawMode (0 normal, 1 Auswahl, 2 Hover)
        bool drahtOk[3] = {false, false, false};
        GpuRef<ID3D11Buffer> draht[3];
        UINT drahtDreiecke[3] = {0, 0, 0};
        UINT drahtLinien[3] = {0, 0, 0};
    };

    std::vector<Ziel> ziele;
    std::thread thread;
    std::atomic<bool> laufen{false};

    GpuRef<ID3D11Device> device;
    GpuRef<ID3D11DeviceContext> context;
    GpuRef<IDXGIFactory> factory;

    GpuRef<ID3D11VertexShader> vsGeometrie;
    GpuRef<ID3D11VertexShader> vsBeleuchtet;
    GpuRef<ID3D11PixelShader> psGeometrie;
    GpuRef<ID3D11PixelShader> psMaske;
    GpuRef<ID3D11VertexShader> vsVollbild;
    GpuRef<ID3D11PixelShader> psUmriss;
    GpuRef<ID3D11PixelShader> psTextur;   // Flaechen mit Textur
    GpuRef<ID3D11SamplerState> abtaster;  // Texturen: wiederholt, Mipmaps, anisotrop
    bool maskeAktiv = false;              // Umriss-Maske wird gezeichnet (ohne Textur)
    // hochgeladene Bilder je (Bild, Alpha-Bild); haelt die Bilder selbst fest, damit kein Zeiger wiederverwendet wird
    struct BildEintrag
    {
        std::shared_ptr<Texture> bild, alpha;
        GpuRef<ID3D11ShaderResourceView> srv;
        bool benutzt = false;
    };
    std::map<std::pair<const Texture *, const Texture *>, BildEintrag> bilder;
    ID3D11ShaderResourceView *bildHochladen(const std::shared_ptr<Texture> &bild, const std::shared_ptr<Texture> &alpha);
    GpuRef<ID3D11InputLayout> layout;
    GpuRef<ID3D11InputLayout> layoutBeleuchtet;
    GpuRef<ID3D11Buffer> cbGeometrie;
    GpuRef<ID3D11Buffer> cbFarben;
    GpuRef<ID3D11Buffer> cbBeleuchtung;
    GpuRef<ID3D11Texture2D> farbTabelle; // bildet Farben exakt wie toCOLORREF() ab
    GpuRef<ID3D11ShaderResourceView> farbTabelleSrv;

    GpuRef<ID3D11RasterizerState> rasterizer;
    GpuRef<ID3D11DepthStencilState> tiefeSchreiben;
    GpuRef<ID3D11DepthStencilState> tiefeNurTesten;
    GpuRef<ID3D11DepthStencilState> tiefeAus;
    GpuRef<ID3D11BlendState> blendAlpha;
    GpuRef<ID3D11BlendState> blendAus;

    Puffer objektPuffer;  // unbekannte Objekttypen, einmal pro Frame
    Puffer ansichtPuffer; // Hintergrund, Lichter, Kamera, Gitter, Fadenkreuz einer Ansicht

    // Bekannte Objekte
    std::unordered_map<object *, ObjektEintrag> cache;
    std::vector<object *> bekannte; // in Szenenreihenfolge
    // Instanzen: je Teil der Vorlage (im Cache) die Instanz, mit deren Abbildung es gezeichnet wird
    std::vector<std::pair<object *, Instanz *>> instanzTeile;
    std::vector<Instanz *> instanzenAusgewaehlt, instanzenHover;
    std::unordered_set<Instanz *> instanzAuswahlMenge, instanzHoverMenge; // schnelles Nachschlagen
    std::unordered_set<object *> ausgewaehltMenge;
    // Fingerabdruecke grosser Netze: volle Pruefsumme nur nach Aenderungen (siehe bekanntesObjekt)
    struct Finger
    {
        unsigned stand = 0;
        uint64_t kurz = 0, voll = 0;
        Vector3D ursprung;
        bool benutzt = false;
    };
    mutable std::unordered_map<const object *, Finger> fingerSpeicher;
    GpuAufzeichnung einzel;         // zum Aufzeichnen eines einzelnen Drahtgitters

    // Unbekannte Objekte (Ergebnis von szeneAufzeichnen)
    GpuAufzeichnung szene;
    GpuAufzeichnung ansichtsObjekte;
    std::vector<GpuVertex> objektVertices;
    std::vector<GpuVertex> ansichtVertices;
    ObjektBereiche alleObjekte;
    std::vector<std::pair<object *, ObjektBereiche>> ausgewaehltBereiche; // unbekannte ausgewaehlte Objekte
    std::vector<std::pair<object *, ObjektBereiche>> hoverBereiche;

    Vector3D ursprung; // Bezugspunkt dieses Frames (Szenenmitte)
    std::vector<object *> ausgewaehlt; // alle ausgewaehlten Objekte (Mehrfachauswahl)
    uint64_t szenenSignatur = 0;

    void schleife();
    bool initGeraet();
    bool initShader();
    bool initZustaende();
    bool initFarbTabelle();
    bool zielErstellen(Ziel &z, int breite, int hoehe);
    bool zielPuffer(Ziel &z, int breite, int hoehe);
    void zielFreigeben(Ziel &z);
    void allesFreigeben();

    void szeneAufzeichnen();
    void beleuchtungAufzeichnen(uint64_t &signatur);
    bool bekanntesObjekt(object *obj, uint64_t &fingerabdruck, Vector3D &objektUrsprung) const;
    bool bekanntesObjektVoll(object *obj, uint64_t &fingerabdruck, Vector3D &objektUrsprung) const;
    void beleuchtetErzeugen(object *obj, ObjektEintrag &e);
    void drahtErzeugen(object *obj, ObjektEintrag &e, int modus, Wireframe &wf);
    bool unveraenderlicherPuffer(GpuRef<ID3D11Buffer> &buffer, const void *daten, size_t bytes);

    void ansichtZeichnen(Ziel &z);
    void gitterErzeugen(bool perspektivisch, int breite, int hoehe, const AnsichtParameter &a, Bereich &boden,
                        Bereich &gitter, Bereich &achsen);

    bool hochladen(Puffer &p, const std::vector<GpuVertex> &daten);
    void geometrieKonstanten(const double m[16], double biasMul, double biasAdd, double modus, const Vector3D &versatz,
                             const Affin *abb = nullptr);
    void farbKonstanten(uint32_t a, uint32_t b);
    void geometriePipeline();
    void beleuchtetPipeline();
    void zeichnen(Puffer &p, D3D11_PRIMITIVE_TOPOLOGY topologie, const Bereich &b);
    void zeichnen(Puffer &p, const ObjektBereiche &b, const double m[16], double linienMul, double linienAdd, double modus = 0.0);
    void objektZeichnen(ObjektEintrag &e, int modus, const double m[16], double linienMul, double linienAdd,
                        const Affin *abb = nullptr);
};
