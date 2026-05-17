#include "farbe.h"
#include <algorithm>
#include "material.h"

Material makeMaterial(
    const Farbe& f,            // Basisfarbe (diffuse)
    double reflection = 0.0,   // 0 = matt, 1 = Spiegel
    double IndexOfRefraction = 0.0,          // Brechungsindex (Glas z.B. 1.5)
    double transparency = 0.0, // 0 = opak, 1 = durchsichtig
    const Farbe& absorption = Farbe(0,0,0), // exponentielle Abschwächung
    double minDiffuseValue = 0.2, // minimale Farbe, falls diese (0,0,0) ist, wie bei Spiegel, damit diese in der Wireframe-Ansicht nicht schwarz erscheinen
    double kA = 0.2,            // Ambient-Faktor
    double kD = 0.8,            // Diffuse-Faktor
    
    double shininess = 50.0,            // Specular exponent
    double specularStrength = 0.2,   
    double roughness = 0.5
)
{
    Material m;

    // Diffuse minimal absichern
    Farbe diffuse = f;
    if(f.r + f.g + f.b == 0.0) 
        diffuse = Farbe(minDiffuseValue, minDiffuseValue, minDiffuseValue);
    m.diffuse = diffuse;

    // Ambient proportional zur Diffuse
    m.ambient = m.diffuse * kA;

    // Specular 
    m.specular = Farbe(0.0, 0.0, 0.0);

    // emission
    m.emission = m.diffuse;

    // Materialparameter
    m.kA = kA;
    m.kD = kD;
    m.shininess = shininess;

    m.reflection = reflection;      // 0..1
    m.IndexOfRefraction = IndexOfRefraction;                    // 0 = nicht transparent, Glas z.B. 1.5
    m.transparency = transparency;  // 0 = opak, 1 = komplett durchsichtig
    m.absorption = absorption;      // z.B. (0.1, 0.01, 0.01) wie stark jede Farbe r, g, b geschluckt wird

    return m;
}

Material makeMaterialSimple(const Farbe &f)
    {
    return makeMaterial(
        f,
        0.0,          // reflection
        1.0,          // IOR
        1.0,          // ✅ transparency (wichtig!)
        Farbe(0,0,0),

        0.0,
        0.1,
        0.9,

        200.0,        // ✅ shininess
        0.8,          // ✅ specularStrength
        0.0           // ✅ roughness (glatt!)
    );
}

/*Farbe getDebugColor(const Material& m)
{
    double sum =
        m.kA +
        m.kD +
        m.reflection +
        m.transparency +
        (m.kS / 100.0);

    if (sum < 1e-6)
        return m.diffuse;

    //if (m.reflection > 0.7)
    //    return Farbe(0.4,0.4,0.4);

    Farbe c =
        m.diffuse * m.kD +
        m.diffuse * m.kA +
        Farbe(1,1,1) * (m.kS / 100.0) +
        Farbe(0.8,0.8,0.8) * m.reflection +
        Farbe(0.6,0.8,1.0) * m.transparency;

    return c * (1.0 / sum);
}*/