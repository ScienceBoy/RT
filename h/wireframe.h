#ifndef WIREFRAME_H
#define WIREFRAME_H

#include "fenster.h"
#include "scene.h"
#include "light.h"
#include "drawmode.h"
#include <vector>
#include <memory>

class light;

struct DragState {
    bool active = false;
    object* object = nullptr;
    Vector3D planePoint;
    Vector3D planeNormal;
    Vector3D offset;
};

enum class SelectedType {
    NONE,
    OBJECT,
    LIGHT,
    CAMERA,
    LOOKAT
};

struct PickResult
{
    SelectedType type = SelectedType::NONE;
    void* ptr = nullptr;
    double t = 1e20;
    Vector3D position;
};

extern double scale;
extern bool rendernRunning;
extern bool useFlat;

class Wireframe {
protected:
    DragState Mausdrag;
    virtual Ray createRay(int x, int y) = 0; 
    virtual Vector3D getDragPlaneNormal() const {     // default
        return Vector3D(0, 1, 0);                     // z.B. Y-Achse (Top View Standard)
    }

    std::vector<double> zBuffer;          // Z-Werte für jedes Pixel
    std::vector<Farbe> frameBuffer;       // optional für Farbpixels

    
public:
    Wireframe(Fenster& window);
    virtual ~Wireframe() = default;

    void setScene(const std::vector<std::unique_ptr<object>> &scene);

    void run();

    int w, h;

    virtual void project(const Vector3D& p, int& x, int& y);
    virtual void project(const Vector3D &p, int &x, int &y, double &z);
    //void drawLine(int x0, int y0, int x1, int y1, const Material& mat);
    void drawLine(int x0, int y0, double z0, int x1, int y1, double z1, const Material &mat);

    void onKey(WPARAM key);

    virtual bool showLookAtHelper() const { return true; }

    void onMouseMove(int x, int y, Fenster& f);
    Vector3D getSelectedPosition();
    void setSelectedPosition(const Vector3D &pos);
    void onMouseDown(int x, int y, Fenster &f);
    void onMouseWheel(int delta, Fenster& f);
    double distanceRayToPoint(const Ray & ray, const Vector3D & p);
    void setPixel(int x, int y, double z, const Farbe & color);
    void fillTriangleZ(int x0,int y0,double z0, int x1,int y1,double z1, int x2,int y2,double z2, const Material &mat);
    void clear();
    void fillTriangle(int x0, int y0, double z0, int x1, int y1, double z1, int x2, int y2, double z2, const Material &mat);
    void onMouseUp();
    virtual bool isPerspective() const { return false; }

    void initBuffers(int width, int height); // Z-Buffer initialisieren

    PickResult pick(const Ray& ray);

    void clearSelection();

    void clearScene();

    bool intersectRaySphere(const Ray & ray, const Vector3D & center, double radius, double & tHit);

    std::vector<PickResult> pickAll(const Ray &ray);

    SelectedType selectedType = SelectedType::NONE;
    object* selectedObject = nullptr;
    light*  selectedLight  = nullptr;
    Camera* selectedCamera = nullptr;

    SelectedType hoverType = SelectedType::NONE;
    object* hoverObject = nullptr;
    light* hoverLight = nullptr;
    Camera* hoverCamera = nullptr;

    std::vector<PickResult> lastHits;
    int pickIndex = 0;
    int lastMouseX = -1;
    int lastMouseY = -1;
    int mouseDownX = 0;
    int mouseDownY = 0;
    int mouseX = 0;
    int mouseY = 0;
    bool mousePressed = false;

    const int DISTANCE_THRESHOLD = 5;
    bool isDragging = false;

protected:
    Fenster& fenster;
    std::vector<object*> sceneobjects;
};

#endif

Vector3D rotateAroundAxis(const Vector3D &v, const Vector3D &axis, double angleDeg);
