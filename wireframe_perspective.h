#pragma once
#include "wireframe.h"
#include "fenster.h"
#include "object.h"
#include "camera.h"

struct MausDragState {
    bool active = false;
    Vector3D planePoint;
    Vector3D planeNormal;
    Vector3D offset;
    object* object = nullptr; // pointer auf selektiertes object
};

class WireframePerspective : public Wireframe {
public:
    Camera* camera;
    
    WireframePerspective(Fenster& f, Camera* cam, std::vector<object*>& objs);
    std::vector<object*>& objects;
    object* selectedObject = nullptr;
    MausDragState Mausdrag;

    void project(const Vector3D& p, int& x, int& y, double& z) override;

    int lastMouseX = 0;
    int lastMouseY = 0;
    bool firstMouse = true;
    bool ignoreMouse = false;

    void onMouseMove(int x, int y, Fenster& f);
    void onMouseWheel(int delta, Fenster& f);
    void onMouseDown(int x, int y, Fenster& f);
    void onMouseUp();
    bool isPerspective() const override { return true; }

    Ray createRay(int x, int y);

    bool showLookAtHelper() const override { return false; }

    Vector3D getDragPlaneNormal() const;

};


//void resetMouse();