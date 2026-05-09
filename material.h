#pragma once
#include "farbe.h"

class Material {
public:
    Farbe ambient;
    Farbe diffuse;
    Farbe specular;

    double kA;        // Ambient-Faktor
    double kD;        // Diffuse-Faktor
    double kS;        // Shininess / Specular exponent

    double reflection; // Basisreflexion (0 = matt, 1 = Spiegel)
    double IndexOfRefraction;        // Index of Refraction (0 = nicht transparent, z.B. Glas = 1.5)
    double transparency; // 0 = opak, 1 = komplett durchsichtig
    Farbe absorption;  // z.B. (0.1, 0.01, 0.01) wie stark jede Farbe r, g, b geschluckt wird

    // Standard-Konstruktor: matt, nicht transparent
    Material()
        : ambient(0.1f,0.1f,0.1f),
          diffuse(0.7f,0.7f,0.7f),
          specular(1.0,1.0,1.0),
          kA(0.2), kD(0.8), kS(50.0),
          reflection(0.0), IndexOfRefraction(0.0), transparency(0), absorption(Farbe(0,0,0)) {}

    // Konstruktor mit allen Parametern
    Material(const Farbe& ambient, const Farbe& diffuse, const Farbe& specular,
             double AmbientFaktor, double DiffuseFaktor, double Shininess,
             double reflection = 0.0, double IndexOfRefraction = 0.0, double transparency = 0.0, Farbe absorption = Farbe(0,0,0))
        : ambient(ambient), diffuse(diffuse), specular(specular),
          kA(AmbientFaktor), kD(DiffuseFaktor), kS(Shininess),
          reflection(reflection), IndexOfRefraction(IndexOfRefraction), transparency(transparency), absorption(absorption) {}
};

// Hilfsfunktion für einfaches Material
Material makeMaterial(const Farbe &f, double reflection, double IndexOfRefraction, double transparency, const Farbe &absorption, double minDiffuseValue, double kA, double kD, double kS);

Material makeMaterialSimple(const Farbe &f);

Farbe getDebugColor(const Material &m);
