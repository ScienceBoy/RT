#pragma once
#include "wireframe.h"
#include "fenster.h"
#include "object.h"
#include "camera.h"

struct MausDragState
{
    bool active = false;
    Vector3D planePoint;
    Vector3D planeNormal;
    Vector3D offset;
    object *object = nullptr; // pointer auf selektiertes object
};

class WireframePerspective : public Wireframe
{
public:
    Camera *camera;

    WireframePerspective(Fenster &f, Camera *cam, std::vector<object *> &objs);
    std::vector<object *> &objects;
    object *selectedObject = nullptr;
    MausDragState Mausdrag;

    AnsichtParameter ansicht() const override;

    int lastMouseX = 0;
    int lastMouseY = 0;
    bool firstMouse = true;
    bool firstMiddleMouse = true;  // Separates Flag für Mittelmaus
    bool firstWheel = true;
    bool ignoreMouse = false;
    bool middleMouseDown = false;

    void onMouseMove(int x, int y, Fenster &f);
    void onMouseWheel(int delta, Fenster &f);
    void onMouseDown(int x, int y, Fenster &f);
    void onMouseUp();
    void onMiddleMouseDown(int x, int y, Fenster &f);
    void onMiddleMouseUp();
    bool isPerspective() const override { return true; }

    bool showLookAtHelper() const override { return false; }

    Vector3D getDragPlaneNormal() const;
};

// void resetMouse();