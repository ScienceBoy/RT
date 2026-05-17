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
    Farbe(0,0,0),
    0.0, 0.0,
    1000.0,   // shininess
    1.0,      // specularStrength ✅
    1.0,      // reflection ✅
    0.0,      // roughness ✅
    0.0,
    0.0
);

const Material MaterialLibrary::EARTH = Material(
    Farbe(0.02,0.02,0.02),
    Farbe(0.3,0.25,0.15),
    Farbe(0.1,0.1,0.1),
    Farbe(0.3,0.25,0.15),

    0.2,
    0.9,
    16.0,   // shininess klein

    0.05,   // specularStrength ✅ sehr wenig
    0.0,    // reflection
    0.9,    // roughness ✅

    0.0,
    0.0
);

const Material MaterialLibrary::GOLD = Material(
    Farbe(0.03, 0.02, 0.01),
    Farbe(0.75, 0.60, 0.25),
    Farbe(1.0, 0.9, 0.6),
    Farbe(0.75, 0.60, 0.25),

    0.0, 0.0,
    200.0,

    0.6,   // specularStrength ✅ hoch
    0.2,   // reflection ✅ moderat
    0.2,   // roughness ✅ leicht weich

    0.0,
    0.0
);

const Material MaterialLibrary::BLUE_PLASTIC = Material(
    Farbe(0.02,0.02,0.05),
    Farbe(0.1,0.2,0.8),
    Farbe(1.0,1.0,1.0),
    Farbe(0.0,0.0,1.0),

    0.1, 0.7,
    50.0,

    0.4,   // specularStrength ✅
    0.05,  // reflection
    0.4,   // roughness ✅

    0.0,
    0.0
);

const Material MaterialLibrary::GLASS = Material(
    Farbe(0.0, 0.0, 0.0),
    Farbe(0.05, 0.05, 0.05),
    Farbe(1.0, 1.0, 1.0),
    Farbe(0.05, 0.05, 0.05),

    0.0, 0.0,
    200.0,

    0.9,   // specularStrength
    0.05,  // reflection (Fresnel übernimmt viel)
    0.0,   // roughness (glatt)

    1.5,   // IOR ✅
    1.0,   // transparent
    Farbe(0,0,0)
);

const Material MaterialLibrary::GREEN_GLASS = Material(
    Farbe(0,0,0),                // ambient
    Farbe(0.03, 0.05, 0.03),     // diffuse (leicht grünlich)
    Farbe(1.0, 1.0, 1.0),        // specular
    Farbe(0.03, 0.05, 0.03),     // emission (leicht grünlich)

    0.0, 0.0,                    // kA, kD
    200.0,                       // shininess (glatt!)

    0.9,                         // ✅ specularStrength (Glanz stark)
    0.05,                        // ✅ reflection (klein, Fresnel übernimmt)

    0.0,                         // ✅ roughness (glatt!)

    1.5,                         // ✅ IndexOfRefraction (Glas!)
    1.0,                         // ✅ transparency

    Farbe(0.3, 0.0, 0.3)         // ✅ absorption (grün/violett-Stich)
);

const Material MaterialLibrary::WASSER = Material(
    Farbe(0.0, 0.05, 0.1),
    Farbe(0.0, 0.1, 0.2),
    Farbe(1.0, 1.0, 1.0),
    Farbe(0.0, 0.1, 0.2),

    0.1, 0.1,
    300.0,

    0.8,
    0.1,
    0.05,   // leicht rau

    1.33,
    0.9,
    Farbe(0.0, 0.1, 0.2)
);

const Material MaterialLibrary::WALL = Material(
    Farbe(0.1,0.1,0.1),
    Farbe(0.8,0.8,0.8),
    Farbe(0,0,0),
    Farbe(0.8,0.8,0.8),

    0.4, 0.8,
    1.0,

    0.0,   // specularStrength ✅
    0.0,   // reflection
    1.0,   // roughness ✅

    0.0,
    0.0
);

const Material MaterialLibrary::SIMPLE = Material(
    Farbe(0,0,0),
    Farbe(0,0,0),
    Farbe(0,0,0),
    Farbe(0,0,0),
    0.0, 0.0, 0.0,
    0.0, 0.0, 0.0,
    0.0, 0.0
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