#pragma once
#include "farbe.h"
#include "ray.h"
#include "hit.h"
#include "material.h"
#include "drawmode.h"
#include <iostream>
#include "texture.h"
#include "TextureManager.h"
#include <iostream>
#include "TextureMode.h"
#include "textur_param.h"
#include <atomic>

class Wireframe;   // Forward declaration
class BVHNode;  // forward declaration

class object
{
public:
    //Farbe farbe;
    Material mat;
    std::string textureImg = "";
    TextureMode textureMode = TextureMode::None;
    std::shared_ptr<Texture> texture = nullptr;
    std::string materialName = "";
    std::string textureName= "";

    // Bezugsbox fuer die Textur: bei Dreiecken eines Mesh die Box des ganzen Mesh (sonst legte jedes
    // Dreieck die Textur ueber seine eigene winzige Box, und Kugel-Mapping lief um den Ursprung)
    bool texturBox = false;
    Vector3D texturMin, texturMax;
    // Ausrichtung der Textur (Material-Reiter): spiegeln links/rechts bzw. oben/unten, Drehung in 90-Grad-Schritten
    bool texturSpiegelU = false, texturSpiegelV = false;
    int texturDrehung = 0; // 0..3
    // weitere Textur-Einstellungen (Kachelgroesse, Versatz, Winkel, objektfest, Relief/Alpha/Glanz/Leuchten);
    // nullptr = Vorgaben. Dreiecke eines Netzes teilen sich die Einstellungen ihres Netzes.
    std::shared_ptr<const TexturParameter> texPar;
    const TexturParameter &texturPar() const { return texPar ? *texPar : TexturParameter::vorgabe(); }
    std::string name = ""; // Anzeigename (UTF-8), z.B. aus der OBJ-Datei oder der Szenen-XML

    // zaehlt Aenderungen, die nur einzelne Ecken verschieben (Terrain-Pinsel, Terrain absenken): die Ansichten
    // pruefen grosse Netze nur stichprobenweise und laden das Netz bei einem neuen Wert neu hoch
    unsigned geometrieZaehler = 0;

    // In den Ansichten ausgeblendet (Haken in der Objektliste); der Raytracer rendert es trotzdem
    bool versteckt = false;

    // Drehung seit Erstellen/Laden als 3x3-Matrix (zeilenweise), nur fuer die Anzeige der
    // Drehwinkel; wird von den Funktionen in drehung.h mitgefuehrt
    double ausrichtung[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};

    Vector3D minBound;
    Vector3D maxBound;
    double boundingRadius;
    Vector3D boundingCenter;
    Vector3D position; 

    //object(const Farbe& f) : farbe(f) {}
    object(const Material& m)
    : mat(m),
      minBound(0,0,0),
      maxBound(0,0,0),
      boundingRadius(0.0),
      boundingCenter(0,0,0),
      position(0,0,0)
    {}
    virtual ~object() {};
    virtual const wchar_t* getTypeName() const {return L"Object";}
    virtual void intersect(const Ray& ray, Hit& hit) const = 0;
    virtual void translate(const Vector3D& delta) = 0;
    virtual void rotate(const Vector3D& rotation) = 0;
    virtual void scale(const Vector3D& rotation) = 0;
    void setPosition(const Vector3D& pos) { position = pos; }
    virtual void drawWireframePixels(Wireframe& wf, DrawMode mode) const = 0;
    virtual void drawFlat(Wireframe& wf, DrawMode mode) const = 0;
    static bool intersectAABB(const Ray& ray, const Vector3D& minB, const Vector3D& maxB)
    {
        double tmin = -INFINITY;
        double tmax = INFINITY;

        // --- X ---
        if (ray.direction.x != 0.0)
        {
            double invDx = ray.invDirection[0]; //  1.0 / ray.direction.x;
            double tx1 = (minB.x - ray.origin.x) * invDx;
            double tx2 = (maxB.x - ray.origin.x) * invDx;

            if (tx1 > tx2) std::swap(tx1, tx2);

            tmin = std::max(tmin, tx1);
            tmax = std::min(tmax, tx2);
        }
        else
        {
            // Ray parallel zur X-Achse
            if (ray.origin.x < minB.x || ray.origin.x > maxB.x)
                return false;
        }

        // --- Y ---
        if (ray.direction.y != 0.0)
        {
            double invDy = ray.invDirection[1]; //  1.0 / ray.direction.y;
            double ty1 = (minB.y - ray.origin.y) * invDy;
            double ty2 = (maxB.y - ray.origin.y) * invDy;

            if (ty1 > ty2) std::swap(ty1, ty2);

            tmin = std::max(tmin, ty1);
            tmax = std::min(tmax, ty2);
        }
        else
        {
            if (ray.origin.y < minB.y || ray.origin.y > maxB.y)
                return false;
        }

        // --- Z ---
        if (ray.direction.z != 0.0)
        {
            double invDz = ray.invDirection[2]; //  1.0 / ray.direction.z;
            double tz1 = (minB.z - ray.origin.z) * invDz;
            double tz2 = (maxB.z - ray.origin.z) * invDz;

            if (tz1 > tz2) std::swap(tz1, tz2);

            tmin = std::max(tmin, tz1);
            tmax = std::min(tmax, tz2);
        }
        else
        {
            if (ray.origin.z < minB.z || ray.origin.z > maxB.z)
                return false;
        }

        return tmax >= tmin;
    }
    virtual bool intersectBounding(const Ray& ray) const{return intersectAABB(ray, minBound, maxBound);}
    virtual void getWorldAABB(Vector3D& min, Vector3D& max) const {min = minBound;max = maxBound;}
    virtual int getTriangleCount() const { return 0; }
    virtual void setTexture(std::shared_ptr<Texture> tex)
    {
        texture = tex;
    }
    virtual void setTextureMode(TextureMode mode)
    {
        textureMode = mode;
    }
    virtual void collectPrimitives(std::vector<object*>& out)
    {
        out.push_back(this);
    }
};

// zaehlt jedes Ein-/Ausblenden (die Ansichten zeichnen dann neu)
inline std::atomic<unsigned> gVersteckAenderungen{0};

// zaehlt jede Bearbeitung der Szene (gSzeneAendern, Rueckgaengig, Animation, Terrain-Pinsel ...); die Ansichten
// pruefen grosse Netze nur dann vollstaendig auf Aenderungen
inline std::atomic<unsigned> gGeometrieStand{0};
