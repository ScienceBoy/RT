#include "texture.h"
#include <fstream>
#include <iostream>
#include <algorithm>
#include "farbe.h"
#include "vector3d.h"
#include "object.h"
#include "hit.h"

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

bool Texture::loadBMP(const std::string& filename) {
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

    return true;
}

Farbe Texture::getPixel(float u, float v) const {
    if (!std::isfinite(u) || !std::isfinite(v)) {
        return Farbe(1, 0, 1); // Debug
    }

    if (m_width <= 0 || m_height <= 0 ||
        m_pixels.size() < size_t(m_width * m_height)) {
        return Farbe(0,0,0);
    }

    float uWrapped = u - floor(u);
    float vWrapped = v - floor(v);

    vWrapped = 1.0f - vWrapped;
    uWrapped = 1.0f - uWrapped;

    float x = uWrapped * (m_width  - 1);
    float y = vWrapped * (m_height - 1);

    int x0 = std::clamp(int(x), 0, m_width  - 1);
    int y0 = std::clamp(int(y), 0, m_height - 1);

    int x1 = std::min(x0 + 1, m_width  - 1);
    int y1 = std::min(y0 + 1, m_height - 1);

    float tx = x - x0;
    float ty = y - y0;

    return lerp(
        lerp(getPixelAt(x0,y0), getPixelAt(x1,y0), tx),
        lerp(getPixelAt(x0,y1), getPixelAt(x1,y1), tx),
        ty
    );
}

Farbe Texture::getPixelAt(int x, int y) const {
    auto color = m_pixels[y * m_width + x];
    float r = ((color >> 16) & 0xFF) / 255.0f;
    float g = ((color >> 8)  & 0xFF) / 255.0f;
    float b = ( color        & 0xFF) / 255.0f;
    
    // sRGB -> Linear
    return sRGBtoLinear(Farbe(r, g, b));
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

    if (type == "sky")
    {
        // Kugelkoordinaten
        double phi = atan2(dir.x, dir.z);
        double theta = asin(dir.y);

        double u = 0.5 + phi / (2 * 3.14159265);
        double v = 0.5 + theta / 3.14159265;

        if (u < 0) u += 1;

        return backgroundTexture.getPixel(u, v);
    }
    if (type == "gradient")
    {
        double t = 0.5 * (dir.y + 1.0);
        return lerp(
            Farbe(0.6, 0.8, 1.0),
            Farbe(0.1, 0.3, 0.6),
            t
        );
    }

    return Farbe(0,0,0);
}

Farbe Texture::backgroundCalc(std::string type)
{
    return backgroundCalc(type, 0);
}

Farbe Texture::texture(Farbe farbe, Hit hit)
{
    if (!hit.obj || !hit.obj->texture)
        return farbe;

    auto obj = hit.obj;

    //std::cout << (obj->textureMode == TextureMode::PLANAR);
    
    if (obj->textureMode == TextureMode::SPHERE)
    {
        Vector3D d = (hit.position - obj->position).normalized();

        double phi = atan2(d.x, d.z);
        double theta = asin(d.y);

        double u = 0.5 + phi / (2 * 3.14159265);
        double v = 0.5 + theta / 3.14159265;

        return obj->texture->getPixel(u, v);
    }

    else if (obj->textureMode == TextureMode::PLANAR)
    {
        // Beispiel: XZ-Projection (typisch für Wände/Boden)

        Vector3D p = hit.position;

        double u = (p.x - obj->minBound.x) / (obj->maxBound.x - obj->minBound.x);
        double v = (p.z - obj->minBound.z) / (obj->maxBound.z - obj->minBound.z);

        return obj->texture->getPixel(u, v);
    }

    else if (obj->textureMode == TextureMode::UV)
    {
        // falls du später Mesh UVs hast
        return obj->texture->getPixel(hit.u, hit.v);
    }
    else if (obj->textureMode == TextureMode::CYLINDER)
    {
        Vector3D p = hit.position - obj->position;
        double epsilon = 1e-4;

        bool isTopCap    = std::abs(hit.position.y - obj->maxBound.y) < epsilon;
        bool isBottomCap = std::abs(hit.position.y - obj->minBound.y) < epsilon;

        // 🟡 Deckel
        if (isTopCap || isBottomCap)
        {
            double radius = (obj->maxBound.x - obj->minBound.x) * 0.5;

            double u = 0.5 + (p.x / (2 * radius));
            double v = 0.5 + (p.z / (2 * radius));

            return obj->texture->getPixel(u, v);
        }

        // 🔵 Mantel
        double phi = atan2(p.x, p.z);
        double u = 0.5 + phi / (2 * 3.14159265);

        double v = (p.y - obj->minBound.y) / (obj->maxBound.y - obj->minBound.y);

        return obj->texture->getPixel(u, v);
    }

    return farbe;
}