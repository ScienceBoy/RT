#include "material_library.h"

// =======================
// Presets definieren
// =======================
// Beispiel:
// ambient(0.1f,0.1f,0.1f),
// diffuse(0.7f,0.7f,0.7f),
// specular(1.0,1.0,1.0),
// kA(0.2), kD(0.8), kS(50.0),
// reflection(0.0), IndexOfRefraction(0.0), transparency(0), 
// absorption(Farbe(0,0,0))


const Material MaterialLibrary::MIRROR = Material(
    Farbe(0,0,0),
    Farbe(0,0,0),
    Farbe(1,1,1),
    0.0, 0.0, 1000.0,
    1.0, 0.0, 0.0
);

const Material MaterialLibrary::EARTH = Material(
    Farbe(0.02,0.02,0.02),   // ambient
    Farbe(0.3,0.5,0.9),   // diffuse
    Farbe(0.2,0.2,0.2),   // specular

    0.2,    // kA
    0.9,    // kD
    32,    // kS (shininess)

    0.0f,    // reflection
    0.0f,     // IndexOfRefraction
    0.0f      // transparency
);

const Material MaterialLibrary::GOLD = Material(
    Farbe(0,0,0),
    Farbe(0,0,0),
    Farbe(1.0, 0.78, 0.34),
    0.0, 0.0, 200.0,
    0.9, 0.0, 0.0
);

const Material MaterialLibrary::BLUE_PLASTIC = Material(
    Farbe(0.02,0.02,0.05),
    Farbe(0.1,0.2,0.8),
    Farbe(1,1,1),
    0.1, 0.7, 50.0,
    0.05, 0.0, 0.0
);

const Material MaterialLibrary::GLASS = Material(
    Farbe(1,1,1),
    Farbe(1,1,1),
    Farbe(1,1,1),
    0.0, 0.0, 100.0,
    0.0, 1.5, 1.0,
    Farbe(0,0,0)
);

const Material MaterialLibrary::GREEN_GLASS = Material(
    Farbe(0,0,0),
    Farbe(1,1,1),
    Farbe(0,0,0),
    0.0, 0.0, 100.0,
    0.0, 1.5, 1.0,
    Farbe(0.3, 0.0, 0.3)
);

const Material MaterialLibrary::WASSER = Material(
    Farbe(0.0, 0.05, 0.1),   // ambient (leicht bläulich)
    Farbe(0.0, 0.1, 0.2),    // diffuse (sehr gering)
    Farbe(1.0, 1.0, 1.0),    // specular (weiss für Glanz)
    0.1,     // kA (Ambient)
    0.1,     // kD (Diffus klein)
    100.0,   // Shininess (sehr hoch für glatte Oberfläche)
    0.7,     // reflection (stark)
    1.33,    // Index of Refraction
    0.5,     // transparency (hoch)
    Farbe(0.0, 0.1, 0.2) // absorption (blau/grün für Tiefe)
);

const Material MaterialLibrary::WALL = Material(
    Farbe(0.1,0.1,0.1),
    Farbe(0.8,0.8,0.8),
    Farbe(0.2,0.2,0.2),
    0.2, 0.8, 10.0,
    0.0, 0.0, 0.0
);

const Material MaterialLibrary::SIMPLE = Material(
    Farbe(0,0,0),
    Farbe(0,0,0),
    Farbe(0,0,0),
    0.0, 0.0, 0.0,
    0.0, 0.0, 0.0
);

// =======================
// Zugriff über Enum
// =======================

Material MaterialLibrary::get(MaterialType type)
{
    switch(type)
    {
        case MaterialType::Mirror:      return MIRROR;
        case MaterialType::Gold:        return GOLD;
        case MaterialType::Earth:       return EARTH;        
        case MaterialType::BluePlastic: return BLUE_PLASTIC;
        case MaterialType::Glass:       return GLASS;
        case MaterialType::GreenGlass:  return GREEN_GLASS;
        case MaterialType::Wall:        return WALL;
        case MaterialType::Simple:      return SIMPLE;
        case MaterialType::Wasser:      return WASSER;
        default:                        return SIMPLE;
    }
}