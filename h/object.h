#pragma once
#include "farbe.h"
#include "ray.h"
#include "hit.h"
#include "material.h"
#include "drawmode.h"
#include <iostream>
#include "texture.h"
#include <numbers>
#include "TextureManager.h"
#include <iostream>
#include "TextureMode.h"

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

    virtual void intersect(const Ray& ray, Hit& hit) const = 0;
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
};

