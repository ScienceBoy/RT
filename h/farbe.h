#pragma once
#include <algorithm>
#include <iostream>
#include <ostream>

class Farbe {
public:
    double r, g, b; // Werte 0.0 - 1.0

    // Konstruktoren
    Farbe();                       // Standard: schwarz (0,0,0)
    Farbe(double r_, double g_, double b_);

    // Operatoren
    Farbe operator+(const Farbe& c) const;   // Farben addieren
    Farbe operator*(double s) const;          // Farbe mit Skalar multiplizieren
    Farbe operator*(const Farbe& c) const;   // Farbe mit Farbe multiplizieren (Modulation)

    Farbe& operator+=(const Farbe& c);
    Farbe& operator*=(double s);
    Farbe& operator*=(const Farbe& c);

    // Clamp: Werte auf [0,1] beschränken
    void clamp();

    // Umwandlung in Windows COLORREF (0-255)
    unsigned int toCOLORREF() const;
    friend std::ostream &operator<<(std::ostream &os, const Farbe &vec);
};

inline Farbe invertColor(const Farbe &c)
{
    return Farbe(
        1.0 - std::clamp(c.r, 0.0, 1.0),
        1.0 - std::clamp(c.g, 0.0, 1.0),
        1.0 - std::clamp(c.b, 0.0, 1.0));
}

inline Farbe invertColorHighlight(const Farbe &c)
{
    return Farbe(
        1.0 - std::clamp(c.r, 0.0, 1.0),
        1.0 - std::clamp(c.g, 0.0, 1.0),
        1.0 - std::clamp(c.b, 0.0, 1.0));
}

Farbe sRGBtoLinear(const Farbe &sRGBColor);

Farbe linearToSRGB(const Farbe &linearColor);

Farbe toneMapping(const Farbe &hdrColor, double exposure);
