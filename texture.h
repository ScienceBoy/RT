#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include "farbe.h"
#include "material.h"
#include "hit.h"
#include "stats.h"

class Texture {
public:
    // Laedt BMP (24/32 Bit) selbst; PNG, JPG, GIF, TIFF ueber die Windows-Bildbibliothek (WIC).
    // Danach werden die Mipmap-Stufen gebaut (je halbe Groesse, Mittelwert in linearen Farben).
    bool loadBMP(const std::string& filename);

    // u, v beliebig (das Bild wiederholt sich), Stufe 0, bilinear
    Farbe getPixel(float u, float v) const;

    Farbe getPixelAt(int x, int y) const;

    // Abtasten mit Mipmap-Stufe 'lod' (0 = volles Bild, 1 = halbe Groesse ...), trilinear
    Farbe abtasten(double u, double v, double lod) const;
    double alphaAbtasten(double u, double v, double lod) const; // 0..1 (ohne Alpha-Kanal: 1)
    double hellAbtasten(double u, double v, double lod) const;  // Helligkeit 0..1 (fuer Relief/Glanz/Alpha-Bild)
    // Mipmap-Stufe fuer eine Pixel-Groesse 'fuss' (Meter) bei 'meterJeBild' Metern je Bildbreite
    double stufeFuer(double fuss, double meterJeBild) const;

    bool hatAlpha() const { return m_hatAlpha; }
    int stufen() const { return int(m_stufen.size()); }
    int stufeBreite(int i) const { return m_stufen[size_t(i)].breite; }
    int stufeHoehe(int i) const { return m_stufen[size_t(i)].hoehe; }
    const std::vector<uint32_t> &stufePixel(int i) const { return m_stufen[size_t(i)].pixel; } // ARGB, Zeile 0 oben

    static Farbe lerp(Farbe v0, Farbe v1, double t);
    static Farbe backgroundCalc(std::string type);
    static Farbe backgroundCalc(std::string type, double t);
    static Farbe backgroundCalc(std::string type, Vector3D dir);

    // Raytracer: Grundfarbe mit Textur (Abbildung siehe textur_abbildung.h), weitere Bilder je Objekt
    static Farbe texture(Farbe farbe, Hit hit);
    static double alphaBei(const Hit &hit);                      // 1 = da, 0 = ausgeschnitten
    static Vector3D reliefNormale(const Hit &hit, const Vector3D &N); // Normale mit Relief (sonst N)
    static double glanzBei(const Hit &hit);                      // Faktor fuer Spiegelung/Glanz (1 = unveraendert)
    static Farbe leuchtenBei(const Hit &hit);                    // zusaetzliches Leuchten

    int width() const { return m_width; }
    int height() const { return m_height; }

private:
    struct Stufe
    {
        int breite = 0, hoehe = 0;
        std::vector<uint32_t> pixel;
    };
    bool ladenWIC(const std::string &filename);
    void stufenBauen();
    Farbe texel(const Stufe &s, int x, int y) const;
    template <class F> double bilinear(const Stufe &s, double u, double v, F &&wert) const;
    Farbe bilinearFarbe(const Stufe &s, double u, double v) const;

    int m_width = 0;
    int m_height = 0;
    std::vector<uint32_t> m_pixels;
    // ARGB oder RGBA
    std::vector<Stufe> m_stufen; // 0 = volles Bild (Kopie von m_pixels)
    bool m_hatAlpha = false;
};

// Groesse eines Bildpunkts in Metern am aktuellen Treffer (setzt der Raytracer je Strahl; 0 = volle Aufloesung)
void texturFussSetzen(double meter);
