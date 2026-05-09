#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include "farbe.h"
#include "material.h"
#include "hit.h"

class Texture {
public:
    bool loadBMP(const std::string& filename);

    // u, v ∈ [0,1]
    Farbe getPixel(float u, float v) const;

    Farbe getPixelAt(int x, int y) const;

    static Farbe lerp(Farbe v0, Farbe v1, double t);
    static Farbe backgroundCalc(std::string type);
    static Farbe texture(Farbe farbe, Hit hit);
    static Farbe backgroundCalc(std::string type, double t);
    static Farbe backgroundCalc(std::string type, Vector3D dir);


    int width() const { return m_width; }
    int height() const { return m_height; }

private:
    int m_width = 0;
    int m_height = 0;
    std::vector<uint32_t> m_pixels;
    // ARGB oder RGBA
};
