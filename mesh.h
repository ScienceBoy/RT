#pragma once
#include "object.h"
#include "dreieck.h"
#include <vector>
#include <string>

class BVHNode;

class Mesh : public object {
public:
    std::vector<Dreieck> triangles;
    //BVHNode* bvhRoot = nullptr;
    object* bvhRoot;

    std::vector<Dreieck> tris;
    Vector3D position;

    Mesh(const std::vector<Dreieck>& tris, const Vector3D& pos);
    Mesh(const Material& m);
    //Mesh(const Mesh&);
    ~Mesh();
    Mesh(const Mesh&) = delete;
    Mesh& operator=(const Mesh&) = delete;

    bool loadOBJ(const std::string& filename);

    void buildBVH();
    std::vector<object*> bvhObjects;

    void intersect(const Ray& ray, Hit& hit) const override;

    void translate(const Vector3D& delta);
    void drawWireframePixels(Wireframe& wf, DrawMode mode) const override;
    void drawFlat(Wireframe& wf, DrawMode mode) const override;

    void setPosition(const Vector3D& pos);

    virtual void getWorldAABB(Vector3D& min, Vector3D& max) const;

    int getTriangleCount() const override { return triangles.size(); }

    virtual void setTexture(std::shared_ptr<Texture> tex) override;
    virtual void setTextureMode(TextureMode mode) override;
    virtual void generateTerrain(int gridResolution, double sizeXZ, double heightScale, int octaves);
};
