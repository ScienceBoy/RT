#include "texture.h"
#include <fstream>
#include <iostream>
#include <algorithm>
#include "farbe.h"
#include "vector3d.h"
#include "object.h"
#include "hit.h"
#include "kugel.h"
#include "rotations.h"
#include "textur_abbildung.h"
#include <math.h>
#include <cmath>
#include <cctype>
#include <cstring>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wincodec.h>
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")
#endif

#pragma pack(push, 1)

struct BMPHeader {
    uint16_t type;
    uint32_t size;
    uint16_t reserved1;
    uint16_t reserved2;
    uint32_t offset;
};

struct DIBHeader {
    uint32_t size;
    int32_t width;
    int32_t height;
    uint16_t planes;
    uint16_t bpp;
    uint32_t compression;
    uint32_t imgSize;
    int32_t xppm;
    int32_t yppm;
    uint32_t colors;
    uint32_t importantColors;
};
#pragma pack(pop)

Texture backgroundTexture;
extern bool umgebungsbild; // scene.cpp; hier ohne scene.h (das bindet texture.h ueber TextureManager.h ein)

// Himmel (render_einstellungen.h): Vorgaben wie der bisherige feste Verlauf
Farbe himmelHorizont(0.6, 0.8, 1.0);
Farbe himmelZenit(0.1, 0.3, 0.6);
double himmelHelligkeit = 1.0;

// sRGB (Byte) -> linear, als Tabelle (schneller als pow je Abtastung)
static const float *srgbTabelle()
{
    static const std::vector<float> t = []
    {
        std::vector<float> v(256);
        for (int i = 0; i < 256; ++i)
        {
            const double c = i / 255.0;
            v[size_t(i)] = float(c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4));
        }
        return v;
    }();
    return t.data();
}

static uint32_t linearZuByte(double c)
{
    c = std::clamp(c, 0.0, 1.0);
    const double s = c <= 0.0031308 ? 12.92 * c : 1.055 * std::pow(c, 1.0 / 2.4) - 0.055;
    return uint32_t(std::lround(std::clamp(s, 0.0, 1.0) * 255.0));
}

static thread_local double gTexturFuss = 0.0;
void texturFussSetzen(double meter) { gTexturFuss = std::isfinite(meter) && meter > 0 ? meter : 0.0; }

static bool endetAuf(const std::string &s, const char *endung)
{
    const size_t n = std::strlen(endung);
    if (s.size() < n)
        return false;
    for (size_t i = 0; i < n; ++i)
        if (std::tolower((unsigned char)s[s.size() - n + i]) != endung[i])
            return false;
    return true;
}

bool Texture::loadBMP(const std::string& filename) {
    // andere Formate (PNG, JPG ...) ueber WIC; BMP wie bisher selbst (auch ohne WIC)
    if (!endetAuf(filename, ".bmp"))
    {
        if (!ladenWIC(filename))
            return false;
        stufenBauen();
        return true;
    }
    std::ifstream file(filename, std::ios::binary);
    if (!file)
    {
        std::cerr << "FAILED TO OPEN: " << filename << std::endl;
        return false;
    }
    BMPHeader bmp;
    file.read((char*)&bmp, sizeof(bmp));

    if (bmp.type != 0x4D42) { // "BM"
        std::cerr << "Not a BMP file\n";
        return false;
    }

    DIBHeader dib;
    file.read((char*)&dib, sizeof(dib));

    if (dib.bpp != 24 && dib.bpp != 32) {
        std::cerr << "Only 24/32-bit BMP supported\n";
        return false;
    }

    m_width = dib.width;
    m_height = dib.height;
    m_pixels.resize(m_width * m_height);

    file.seekg(bmp.offset, std::ios::beg);

    int bytesPerPixel = dib.bpp / 8;
    int rowSize = ((dib.bpp * m_width + 31) / 32) * 4;

    std::vector<uint8_t> row(rowSize);

    for (int y = 0; y < m_height; y++) {
        file.read((char*)row.data(), rowSize);

        for (int x = 0; x < m_width; x++) {
            uint8_t b = row[x * bytesPerPixel + 0];
            uint8_t g = row[x * bytesPerPixel + 1];
            uint8_t r = row[x * bytesPerPixel + 2];
            uint8_t a = (bytesPerPixel == 4) ? row[x * 4 + 3] : 255;

            m_pixels[(m_height - 1 - y) * m_width + x] =
                (a << 24) | (r << 16) | (g << 8) | b;
        }
    }

    // 32 Bit: Alpha nur, wenn er wirklich benutzt wird (viele 32-Bit-BMPs haben ueberall 0 oder 255)
    m_hatAlpha = false;
    if (bytesPerPixel == 4)
    {
        bool teil = false, voll = false;
        for (uint32_t px : m_pixels)
        {
            const uint32_t a = px >> 24;
            teil = teil || a < 255;
            voll = voll || a > 0;
        }
        m_hatAlpha = teil && voll;
        if (!m_hatAlpha)
            for (uint32_t &px : m_pixels)
                px |= 0xFF000000u;
    }
    stufenBauen();
    return true;
}

#ifdef _WIN32
bool Texture::ladenWIC(const std::string &filename)
{
    // Pfad: erst im ANSI-Zeichensatz (Dateiauswahl), sonst als UTF-8 (Szenen-XML)
    auto breit = [](const std::string &s, UINT cp)
    {
        int n = MultiByteToWideChar(cp, 0, s.c_str(), -1, nullptr, 0);
        std::wstring w(size_t(std::max(1, n)), L'\0');
        if (n > 0)
            MultiByteToWideChar(cp, 0, s.c_str(), -1, &w[0], n);
        w.resize(wcslen(w.c_str()));
        return w;
    };
    std::wstring pfad = breit(filename, CP_ACP);
    if (GetFileAttributesW(pfad.c_str()) == INVALID_FILE_ATTRIBUTES)
        pfad = breit(filename, CP_UTF8);

    const HRESULT co = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    bool ok = false;
    IWICImagingFactory *fabrik = nullptr;
    IWICBitmapDecoder *dekoder = nullptr;
    IWICBitmapFrameDecode *bild = nullptr;
    IWICFormatConverter *wandler = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_IWICImagingFactory,
                                   (void **)&fabrik)) &&
        SUCCEEDED(fabrik->CreateDecoderFromFilename(pfad.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand,
                                                    &dekoder)) &&
        SUCCEEDED(dekoder->GetFrame(0, &bild)) && SUCCEEDED(fabrik->CreateFormatConverter(&wandler)) &&
        SUCCEEDED(wandler->Initialize(bild, GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0.0,
                                      WICBitmapPaletteTypeCustom)))
    {
        UINT b = 0, h = 0;
        if (SUCCEEDED(wandler->GetSize(&b, &h)) && b > 0 && h > 0 && b < 65536 && h < 65536)
        {
            std::vector<uint32_t> px(size_t(b) * h);
            // BGRA im Speicher = 0xAARRGGBB als uint32 (wie loadBMP), Zeile 0 oben
            if (SUCCEEDED(wandler->CopyPixels(nullptr, b * 4, UINT(px.size() * 4), (BYTE *)px.data())))
            {
                m_width = int(b);
                m_height = int(h);
                m_pixels.swap(px);
                bool teil = false;
                for (uint32_t p : m_pixels)
                    teil = teil || (p >> 24) < 255;
                m_hatAlpha = teil;
                ok = true;
            }
        }
    }
    if (wandler)
        wandler->Release();
    if (bild)
        bild->Release();
    if (dekoder)
        dekoder->Release();
    if (fabrik)
        fabrik->Release();
    if (SUCCEEDED(co))
        CoUninitialize();
    if (!ok)
        std::cerr << "Bild konnte nicht geladen werden: " << filename << std::endl;
    return ok;
}
#else
bool Texture::ladenWIC(const std::string &filename)
{
    std::cerr << "Nur BMP ohne Windows: " << filename << std::endl;
    return false;
}
#endif

// Mipmap-Stufen: jede halb so gross, Mittelwert von 2 x 2 Bildpunkten in linearen Farben
void Texture::stufenBauen()
{
    m_stufen.clear();
    if (m_width <= 0 || m_height <= 0 || m_pixels.size() < size_t(m_width) * size_t(m_height))
        return;
    m_stufen.push_back({m_width, m_height, m_pixels});
    const float *lin = srgbTabelle();
    while (m_stufen.back().breite > 1 || m_stufen.back().hoehe > 1)
    {
        const Stufe &a = m_stufen.back();
        Stufe n;
        n.breite = std::max(1, a.breite / 2);
        n.hoehe = std::max(1, a.hoehe / 2);
        n.pixel.resize(size_t(n.breite) * n.hoehe);
        for (int y = 0; y < n.hoehe; ++y)
            for (int x = 0; x < n.breite; ++x)
            {
                double r = 0, g = 0, b = 0, al = 0;
                for (int k = 0; k < 4; ++k)
                {
                    const int sx = std::min(a.breite - 1, 2 * x + (k & 1)), sy = std::min(a.hoehe - 1, 2 * y + (k >> 1));
                    const uint32_t p = a.pixel[size_t(sy) * a.breite + sx];
                    r += lin[(p >> 16) & 0xFF];
                    g += lin[(p >> 8) & 0xFF];
                    b += lin[p & 0xFF];
                    al += (p >> 24) / 255.0;
                }
                n.pixel[size_t(y) * n.breite + x] = (uint32_t(std::lround(al * 0.25 * 255.0)) << 24) |
                                                    (linearZuByte(r * 0.25) << 16) | (linearZuByte(g * 0.25) << 8) |
                                                    linearZuByte(b * 0.25);
            }
        m_stufen.push_back(std::move(n));
    }
}

Farbe Texture::texel(const Stufe &st, int x, int y) const
{
    const uint32_t c = st.pixel[size_t(y) * st.breite + x];
    const float *lin = srgbTabelle();
    return Farbe(lin[(c >> 16) & 0xFF], lin[(c >> 8) & 0xFF], lin[c & 0xFF]);
}

// bilinear mit Wiederholung an allen Raendern (vorher am Rand abgeklemmt: feine Naht bei u = 0/1 an Kugel und
// Zylinder); Bildpunkt-Mitten bei (i + 0,5) / Breite; v = 1 oben im Bild
template <class F> double Texture::bilinear(const Stufe &st, double u, double v, F &&wert) const
{
    const double x = (u - std::floor(u)) * st.breite - 0.5;
    const double y = (1.0 - (v - std::floor(v))) * st.hoehe - 0.5;
    const double fx = std::floor(x), fy = std::floor(y);
    const double tx = x - fx, ty = y - fy;
    auto mod = [](int i, int n) { i %= n; return i < 0 ? i + n : i; };
    const int x0 = mod(int(fx), st.breite), x1 = mod(int(fx) + 1, st.breite);
    const int y0 = mod(int(fy), st.hoehe), y1 = mod(int(fy) + 1, st.hoehe);
    auto w = [&](int xx, int yy) { return wert(st.pixel[size_t(yy) * st.breite + xx]); };
    return (w(x0, y0) * (1 - tx) + w(x1, y0) * tx) * (1 - ty) + (w(x0, y1) * (1 - tx) + w(x1, y1) * tx) * ty;
}

Farbe Texture::bilinearFarbe(const Stufe &st, double u, double v) const
{
    const float *lin = srgbTabelle();
    const double r = bilinear(st, u, v, [&](uint32_t c) { return double(lin[(c >> 16) & 0xFF]); });
    const double g = bilinear(st, u, v, [&](uint32_t c) { return double(lin[(c >> 8) & 0xFF]); });
    const double b = bilinear(st, u, v, [&](uint32_t c) { return double(lin[c & 0xFF]); });
    return Farbe(r, g, b);
}

Farbe Texture::getPixel(float u, float v) const {
    if (!std::isfinite(u) || !std::isfinite(v)) {
        return Farbe(1, 0, 1); // Debug
    }
    if (m_stufen.empty())
        return Farbe(0,0,0);
    // Umgebungsbild: oben und unten nicht ueber den Pol hinweg wiederholen
    v = std::clamp(v, 0.5f / float(m_height), 1.0f - 0.5f / float(m_height));
    return bilinearFarbe(m_stufen[0], u, v);
}

Farbe Texture::getPixelAt(int x, int y) const {
    if (m_stufen.empty())
        return Farbe(0, 0, 0);
    return texel(m_stufen[0], std::clamp(x, 0, m_width - 1), std::clamp(y, 0, m_height - 1));
}

double Texture::stufeFuer(double fuss, double meterJeBild) const
{
    if (!(fuss > 0) || !(meterJeBild > 0) || m_width <= 0 || m_stufen.size() < 2)
        return 0.0;
    const double texel = meterJeBild / std::max(m_width, m_height);
    const double lod = std::log2(fuss / texel);
    return std::clamp(lod, 0.0, double(m_stufen.size() - 1));
}

Farbe Texture::abtasten(double u, double v, double lod) const
{
    if (m_stufen.empty() || !std::isfinite(u) || !std::isfinite(v))
        return Farbe(0, 0, 0);
    lod = std::clamp(lod, 0.0, double(m_stufen.size() - 1));
    const int l0 = int(lod);
    const double f = lod - l0;
    Farbe c = bilinearFarbe(m_stufen[size_t(l0)], u, v);
    if (f > 1e-3 && l0 + 1 < int(m_stufen.size()))
        c = c * (1.0 - f) + bilinearFarbe(m_stufen[size_t(l0 + 1)], u, v) * f;
    return c;
}

double Texture::alphaAbtasten(double u, double v, double lod) const
{
    if (m_stufen.empty() || !m_hatAlpha || !std::isfinite(u) || !std::isfinite(v))
        return 1.0;
    lod = std::clamp(lod, 0.0, double(m_stufen.size() - 1));
    const int l0 = int(lod);
    const double f = lod - l0;
    auto a = [](uint32_t c) { return (c >> 24) / 255.0; };
    double r = bilinear(m_stufen[size_t(l0)], u, v, a);
    if (f > 1e-3 && l0 + 1 < int(m_stufen.size()))
        r = r * (1.0 - f) + bilinear(m_stufen[size_t(l0 + 1)], u, v, a) * f;
    return r;
}

double Texture::hellAbtasten(double u, double v, double lod) const
{
    const Farbe c = abtasten(u, v, lod);
    return 0.2126 * c.r + 0.7152 * c.g + 0.0722 * c.b;
}

Farbe Texture::lerp(Farbe v0, Farbe v1, double t) {
  return v0 * (1 - t) + v1 * t;
}

Farbe Texture::backgroundCalc(std::string type, double t)
{
    if (type == "sky") // Himmel als Farbverlauf
        return lerp(
            Farbe(0.6, 0.8, 1.0), // Horizont Hellblau
            Farbe(0.1, 0.3, 0.6), // Zenit tiefer Blauton
            t                     // vertikale Richtung
        );
    
    return Farbe(0,0,0); // einfache Hintergrundfarbe
}

Farbe Texture::backgroundCalc(std::string type, Vector3D dir)
{
    dir = dir.normalized();

    // Umgebungsbild aus oder kein Bild geladen: Himmelsverlauf
    // (vorher immer das Bild; ohne geladenes Bild lieferte getPixel() Schwarz)
    if (type == "sky" && (!umgebungsbild || backgroundTexture.width() <= 0 || backgroundTexture.height() <= 0))
        type = "gradient";

    if (type == "sky")
    {
        // Kugelkoordinaten
        double phi = atan2(dir.x, dir.z);
        double theta = asin(dir.y);

        double u = 0.5 + phi / (2 * M_PI);
        double v = 0.5 + theta / M_PI;

        if (u < 0) u += 1;

        return backgroundTexture.getPixel(u, v) * himmelHelligkeit;
    }
    if (type == "gradient")
    {
        // Verlauf von unten (Horizontfarbe) nach oben (Zenitfarbe), einstellbar
        double t = 0.5 * (dir.y + 1.0);
        return lerp(himmelHorizont, himmelZenit, t) * himmelHelligkeit;
    }

    return Farbe(0,0,0);
}

Farbe Texture::backgroundCalc(std::string type)
{
    return backgroundCalc(type, 0);
}

// ---------------------------------------------------------------------------------------------------------
// Raytracer: Textur am Treffer (Abbildung in textur_abbildung.h)
// ---------------------------------------------------------------------------------------------------------

static Vector3D trefferNormale(const Hit &hit)
{
    return hit.geomNormale.length() > 1e-12 ? hit.geomNormale.normalized() : hit.normale.normalized();
}

// Abbildung fuer die weiteren Bilder: ohne gewaehlte Art wie "Wuerfel weich"
static TexturProbe probeAm(const object &o, const Vector3D &p, const Vector3D &n, const Hit &hit)
{
    const TexturParameter &par = o.texturPar();
    const TextureMode modus = o.textureMode != TextureMode::None ? o.textureMode : TextureMode::TRIPLANAR;
    TexturProbe pr = texturRoh(modus, par, texturRaum(o, p, n, hit.u, hit.v, true));
    for (int i = 0; i < pr.n; ++i)
        texturNach(o, par, pr.u[i], pr.v[i]);
    return pr;
}

static double hellMit(const Texture &t, const TexturProbe &pr, double lod)
{
    double h = 0;
    for (int i = 0; i < pr.n; ++i)
        h += t.hellAbtasten(pr.u[i], pr.v[i], lod) * pr.w[i];
    return h;
}

Farbe Texture::texture(Farbe farbe, Hit hit)
{
    if (!hit.obj || !hit.obj->texture || hit.obj->textureMode == TextureMode::None)
        return farbe;
    const object &o = *hit.obj;
    const TexturProbe pr = texturAbbilden(o, hit.position, trefferNormale(hit), hit.u, hit.v, true);
    if (pr.n == 0)
        return farbe;
    const Texture &t = *o.texture;
    const double lod = t.stufeFuer(gTexturFuss, pr.meterJeU);
    Farbe c(0, 0, 0);
    for (int i = 0; i < pr.n; ++i)
        c = c + t.abtasten(pr.u[i], pr.v[i], lod) * pr.w[i];
    // Unterseite (z.B. weisser Bauch): unterhalb der Grenze (Anteil der Hoehe im Textur-Raum, also mit "haftet am
    // Objekt" fest am Koerper) die Farbe des zweiten Bilds; weicher Uebergang, ausgefranst nach dessen Helligkeit
    const TexturParameter &par = o.texturPar();
    if (const Texture *unten = par.bild[BILD_UNTEN].get())
    {
        const TexturRaum r = texturRaum(o, hit.position, trefferNormale(hit), hit.u, hit.v, true);
        const double hoehe = r.lmax.y - r.lmin.y;
        if (hoehe > 1e-12 && unten->width() > 0)
        {
            const double lodU = unten->stufeFuer(gTexturFuss, pr.meterJeU);
            Farbe cu(0, 0, 0);
            for (int i = 0; i < pr.n; ++i)
                cu = cu + unten->abtasten(pr.u[i], pr.v[i], lodU) * pr.w[i];
            const double hellU = 0.2126 * cu.r + 0.7152 * cu.g + 0.0722 * cu.b;
            const double hellO = 0.2126 * c.r + 0.7152 * c.g + 0.0722 * c.b;
            const double f = (r.q.y - r.lmin.y) / hoehe + 0.25 * (hellO - hellU); // Haare greifen ineinander
            const double grenze = std::clamp(par.staerke[BILD_UNTEN], 0.0, 1.0), breite = 0.06;
            double x = std::clamp((f - (grenze - breite)) / (2.0 * breite), 0.0, 1.0);
            x = x * x * (3.0 - 2.0 * x); // 0 unten (zweites Bild), 1 oben
            c = cu * (1.0 - x) + c * x;
        }
    }
    return c;
}

double Texture::alphaBei(const Hit &hit)
{
    if (!hit.obj)
        return 1.0;
    const object &o = *hit.obj;
    const TexturParameter &par = o.texturPar();
    const Texture *bildAlpha = par.bild[BILD_ALPHA].get();
    const bool grundAlpha = o.texture && o.texture->hatAlpha() && o.textureMode != TextureMode::None;
    if (!bildAlpha && !grundAlpha)
        return 1.0;
    const TexturProbe pr = probeAm(o, hit.position, trefferNormale(hit), hit);
    double a = 0;
    // Alpha immer aus der vollen Aufloesung: gemittelte Stufen wuerden feine Stellen (Haarspitzen, Blattraender)
    // unter die Schwelle 0,5 druecken und aus der Entfernung ganz verschwinden lassen; die Kantenglaettung
    // ueber mehrere Abtastungen je Pixel mittelt stattdessen
    const double lod = 0.0;
    if (bildAlpha)
    {
        a = hellMit(*bildAlpha, pr, lod); // hell = da, dunkel = Loch
        if (bildAlpha->hatAlpha())        // Bild mit eigenem Alpha-Kanal: der zaehlt zusaetzlich
        {
            double ka = 0;
            for (int i = 0; i < pr.n; ++i)
                ka += bildAlpha->alphaAbtasten(pr.u[i], pr.v[i], lod) * pr.w[i];
            a = std::min(a, ka);
        }
    }
    else
    {
        for (int i = 0; i < pr.n; ++i)
            a += o.texture->alphaAbtasten(pr.u[i], pr.v[i], lod) * pr.w[i];
    }
    return a;
}

Vector3D Texture::reliefNormale(const Hit &hit, const Vector3D &N)
{
    if (!hit.obj)
        return N;
    const object &o = *hit.obj;
    const TexturParameter &par = o.texturPar();
    const Texture *relief = par.bild[BILD_RELIEF].get();
    if (!relief || relief->width() <= 0 || std::fabs(par.staerke[BILD_RELIEF]) < 1e-12)
        return N;
    const Vector3D ng = trefferNormale(hit);
    const TexturProbe p0 = probeAm(o, hit.position, ng, hit);
    if (p0.n == 0)
        return N;
    const double lod = relief->stufeFuer(gTexturFuss, p0.meterJeU);
    // Schrittweite: ein Bildpunkt der gewaehlten Stufe (mindestens die Pixelgroesse)
    const double texel = p0.meterJeU / std::max(relief->width(), relief->height()) * std::pow(2.0, lod);
    const double e = std::max({texel, gTexturFuss, 1e-6});
    const Vector3D hilf = std::fabs(ng.x) < 0.9 ? Vector3D(1, 0, 0) : Vector3D(0, 1, 0);
    const Vector3D t1 = ng.cross(hilf).normalized(), t2 = ng.cross(t1).normalized();
    const double h0 = hellMit(*relief, p0, lod);
    const double h1 = hellMit(*relief, probeAm(o, hit.position + t1 * e, ng, hit), lod);
    const double h2 = hellMit(*relief, probeAm(o, hit.position + t2 * e, ng, hit), lod);
    const double tiefe = par.staerke[BILD_RELIEF];
    // Oberflaeche um tiefe * h entlang der Normale verschoben: Steigung (h1 - h0)/e in Richtung t1
    const Vector3D steigung = t1 * ((h1 - h0) / e) + t2 * ((h2 - h0) / e);
    Vector3D n = N - steigung * tiefe;
    const double l = n.length();
    return l > 1e-12 ? n * (1.0 / l) : N;
}

double Texture::glanzBei(const Hit &hit)
{
    if (!hit.obj)
        return 1.0;
    const object &o = *hit.obj;
    const TexturParameter &par = o.texturPar();
    const Texture *glanz = par.bild[BILD_GLANZ].get();
    if (!glanz || glanz->width() <= 0)
        return 1.0;
    const TexturProbe pr = probeAm(o, hit.position, trefferNormale(hit), hit);
    if (pr.n == 0)
        return 1.0;
    const double g = hellMit(*glanz, pr, glanz->stufeFuer(gTexturFuss, pr.meterJeU));
    const double s = std::clamp(par.staerke[BILD_GLANZ], 0.0, 1.0);
    return std::clamp(1.0 + s * (g - 1.0), 0.0, 1.0);
}

Farbe Texture::leuchtenBei(const Hit &hit)
{
    if (!hit.obj)
        return Farbe(0, 0, 0);
    const object &o = *hit.obj;
    const TexturParameter &par = o.texturPar();
    const Texture *leuchten = par.bild[BILD_LEUCHTEN].get();
    if (!leuchten || leuchten->width() <= 0 || par.staerke[BILD_LEUCHTEN] <= 0)
        return Farbe(0, 0, 0);
    const TexturProbe pr = probeAm(o, hit.position, trefferNormale(hit), hit);
    const double lod = leuchten->stufeFuer(gTexturFuss, pr.meterJeU);
    Farbe c(0, 0, 0);
    for (int i = 0; i < pr.n; ++i)
        c = c + leuchten->abtasten(pr.u[i], pr.v[i], lod) * pr.w[i];
    return c * par.staerke[BILD_LEUCHTEN];
}
