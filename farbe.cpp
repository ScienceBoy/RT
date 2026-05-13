#include "farbe.h"
#include <cmath>
#include <algorithm>
#include <windows.h>

// Konstruktoren
Farbe::Farbe() : r(0), g(0), b(0) {}
Farbe::Farbe(double r_, double g_, double b_) : r(r_), g(g_), b(b_) { clamp(); }

// Operatoren
Farbe Farbe::operator+(const Farbe& c) const {
    return Farbe(r + c.r, g + c.g, b + c.b);
}

Farbe Farbe::operator*(double s) const {
    return Farbe(r * s, g * s, b * s);
}

Farbe Farbe::operator*(const Farbe& c) const { // Modulation
    return Farbe(r * c.r, g * c.g, b * c.b);
}

Farbe& Farbe::operator+=(const Farbe& c) {
    r += c.r; g += c.g; b += c.b;
    clamp();
    return *this;
}

Farbe& Farbe::operator*=(double s) {
    r *= s; g *= s; b *= s;
    clamp();
    return *this;
}

Farbe& Farbe::operator*=(const Farbe& c) {
    r *= c.r; g *= c.g; b *= c.b;
    clamp();
    return *this;
}

// Werte auf 0-1 beschränken
void Farbe::clamp() {
    //r = std::max(0.0, std::min(1.0, r));
    // = std::max(0.0, std::min(1.0, g));
    //b = std::max(0.0, std::min(1.0, b));
}

// Umwandlung in COLORREF (Windows 0-255)
unsigned int Farbe::toCOLORREF() const {
    return RGB(static_cast<int>(r*255), static_cast<int>(g*255), static_cast<int>(b*255));
}

// sRGB -> Linear
Farbe sRGBtoLinear(const Farbe& sRGBColor)
{
    return Farbe(
        std::pow(sRGBColor.r, 2.2),
        std::pow(sRGBColor.g, 2.2),
        std::pow(sRGBColor.b, 2.2)
    );
}

// Linear -> sRGB
Farbe linearToSRGB(const Farbe& linearColor)
{
    const double gamma = 1.0 / 2.2;

    return Farbe(
        std::pow(linearColor.r, gamma),
        std::pow(linearColor.g, gamma),
        std::pow(linearColor.b, gamma)
    );
}

// Reinhard Tone Mapping
Farbe toneMapping(const Farbe& hdrColor, double exposure = 0.0)
{
    // Exposure-Skalierung
    double exposureScale = std::pow(2.0, exposure);

    Farbe scaled = hdrColor * exposureScale;

    // Reinhard Tone Mapping
    return Farbe(
        scaled.r / (scaled.r + 1.0),
        scaled.g / (scaled.g + 1.0),
        scaled.b / (scaled.b + 1.0)
    );
}