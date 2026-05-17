
#include "wireframe_perspective.h"
#include "fenster.h"
#include "camera.h"
#include "mauspicking.h"
#include "mausray.h"
#include "object.h"
#include <iostream>

WireframePerspective::WireframePerspective(Fenster& f, Camera* cam, std::vector<object*>& objs) 
: Wireframe(f), camera(cam), objects(objs)
{
    /*f.setMouseMoveHandler([this](int x, int y, Fenster& f)
    {
        this->onMouseMove(x, y, f);
    });
    f.setMouseWheelHandler([this](int delta, Fenster& f)
    {
        this->onMouseWheel(delta, f);
    });
    f.setMouseDownHandler([this](int x, int y, Fenster& f)
    {
        this->onMouseDown(x, y, f);
    });
    f.setMouseUpHandler([this]()
    {
        this->onMouseUp();
    });*/
}

void WireframePerspective::project(const Vector3D& p, int& x, int& y, double& z) 
{
    double zoom = 0.7; // nur hier benutzt, da Szene zu nahe scheint, im Vergleich zum RayTracing-Fenster
    Vector3D rel = p - cam[0].position;

    double px = rel * cam[0].u;
    double py = rel * cam[0].v;
    double pz = rel * cam[0].w;

    // hinter Kamera abbrechen
    if (pz >= 0)
    {
        x = y = -10000;
        z = std::numeric_limits<double>::infinity();
        return;
    }

    double f = scale;

    // Perspektivische Division
    double invZ = -1.0 / pz;

    x = int(w/2 - px * invZ * f * w/2);
    y = int(-py * invZ * f * h/2 + h/2);

    z = -pz; // Tiefe positiv
}


// für kippen / drehen der Kamera
/*void WireframePerspective::onMouseMove(int x, int y, Fenster& f)
{
    // Prüfen, ob das Fenster den Fokus hat
    if (GetForegroundWindow() != f.getHWND())
        return; // Maus ignorieren, wenn Fenster nicht aktiv
        
    // 1️⃣ Nur reagieren, wenn Maus über diesem Fenster
    POINT cursor;
    GetCursorPos(&cursor);

    RECT rect;
    GetWindowRect(f.getHWND(), &rect);

    if (cursor.x < rect.left || cursor.x > rect.right ||
        cursor.y < rect.top || cursor.y > rect.bottom)
    {
        return;
    }

    // 2️⃣ Ignore-Flag für zentrierte Maus
    if (ignoreMouse) { ignoreMouse = false; return; }
    
    // 3️⃣ Erste Mausposition initialisieren
    if (firstMouse)
    {
        lastMouseX = x;
        lastMouseY = y;
        firstMouse = false;
        return;
    }

    // 4️⃣ Differenzen berechnen
    int dx = x - lastMouseX;
    int dy = y - lastMouseY;

    lastMouseX = x;
    lastMouseY = y;

    // 5️⃣ Kamera bewegen
    double sensitivity = 0.005; 
    camera->yaw   += dx * sensitivity;
    camera->pitch -= dy * sensitivity;

    // Pitch clampen
    if (camera->pitch > 1.2)  camera->pitch = 1.2;
    if (camera->pitch < -1.2) camera->pitch = -1.2;

    camera->updateDirection();

    // 6️⃣ Maus zentrieren, damit nächste Bewegung delta liefert
    //POINT center = { rect.right / 2, rect.bottom / 2 };
    //ClientToScreen(f.getHWND(), &center);
    //ignoreMouse = true;
    //SetCursorPos(center.x, center.y);
}*/

// für Bewegung der Kamera
/*void WireframePerspective::onMouseMove(int x, int y, Fenster& f)
{
    // Nur reagieren, wenn Fenster aktiv
    if (GetForegroundWindow() != f.getHWND())
        return;
    
    // Nur reagieren, wenn Maus über Fenster
    POINT cursor;
    GetCursorPos(&cursor);
    RECT rect;
    GetWindowRect(f.getHWND(), &rect);
    if (cursor.x < rect.left || cursor.x > rect.right ||
        cursor.y < rect.top  || cursor.y > rect.bottom)
        return;

    if (ignoreMouse) { ignoreMouse = false; return; }

    if (firstMouse)
    {
        lastMouseX = x;
        lastMouseY = y;
        firstMouse = false;
        return;
    }

    int dx = x - lastMouseX;
    int dy = y - lastMouseY;

    lastMouseX = x;
    lastMouseY = y;

    double sensitivity = 0.1;

    // Wenn SHIFT gedrückt, 10x schneller
    if (GetAsyncKeyState(VK_SHIFT) & 0x8000)
    {
        sensitivity *= 10.0;
    }

    // Wenn CTRL gedrückt, 100x schneller
    if (GetAsyncKeyState(VK_CONTROL) & 0x8000)
    {
        sensitivity *= 100.0;
    }

    // Lineare Bewegung entlang der Kameraachsen 
    // w = Vorwärts, u = Rechts, v = Oben
    camera->position += camera->u * dx * sensitivity; // Bewegung in x-Richtung (Rechts/Links)
    camera->position += camera->v * dy * sensitivity; // Bewegung in y-Richtung (Oben/Unten)
    
    // Optional: Mit Mausrad z.B. Vorwärts/Rückwärts bewegen
    // camera->position += camera->w * dz * sensitivity;

    // Keine Änderung an yaw/pitch → keine Drehung
}*/

/*void WireframePerspective::resetMouse()
{
    firstMouse = true;
    ignoreMouse = false;
}*/

void WireframePerspective::onMouseWheel(int delta, Fenster& f)
{
    // Nur reagieren, wenn Fenster aktiv
    if (GetForegroundWindow() != f.getHWND())
        return;
    
    // Nur reagieren, wenn Maus über Fenster
    POINT cursor;
    GetCursorPos(&cursor);
    RECT rect;
    GetWindowRect(f.getHWND(), &rect);
    if (cursor.x < rect.left || cursor.x > rect.right ||
        cursor.y < rect.top  || cursor.y > rect.bottom)
        return;

    double sensitivity = 0.1;

    // Wenn SHIFT gedrückt, 10x schneller
    if (GetAsyncKeyState(VK_SHIFT) & 0x8000)
    {
        sensitivity *= 10.0;
    }

    // Wenn CTRL gedrückt, 100x schneller
    if (GetAsyncKeyState(VK_CONTROL) & 0x8000)
    {
        sensitivity *= 100.0;
    }

    /*double dz = delta / WHEEL_DELTA;

    camera->position += camera->w * dz * sensitivity;*/

    
    double zoomSpeed = 2.0;

    cam[0].fov -= (delta / 120.0) * zoomSpeed;

    cam[0].fov = std::clamp(cam[0].fov, 10.0, 120.0);

    cam[0].update();

}

void WireframePerspective::onMouseDown(int x, int y, Fenster& f) {
    Ray ray = createRayFromMouse(w-x, y, w, h, camera);

    Hit nearestHit;
    nearestHit.t = 1e20;
    nearestHit.hit = false;

    Mausdrag.object = nullptr;

    // Iteriere über alle Objekte in der Szene
    for(auto& obj : scene) {
        Hit hit;
        obj->intersect(ray, hit);
        if(hit.hit && hit.t < nearestHit.t) {
            nearestHit = hit;
            Mausdrag.object = obj.get();
        }
    }

    if(Mausdrag.object) {
        Mausdrag.active = true;
        Mausdrag.planePoint  = nearestHit.position;
        Mausdrag.planeNormal = camera->w; 
        //Mausdrag.planeNormal = getDragPlaneNormal();
        Mausdrag.offset      = Mausdrag.object->position - nearestHit.position;
    }
}

void WireframePerspective::onMouseMove(int x, int y, Fenster& f) {
    if(!Mausdrag.active || !Mausdrag.object) return;

    Ray ray = createRayFromMouse(w-x, y, w, h, camera);

    double t;
    if(!intersect(ray, Mausdrag.planePoint, Mausdrag.planeNormal, t))
        return;

    Vector3D maushit = ray.origin + ray.direction * t;

    // neue Position setzen
    Mausdrag.object->position = maushit + Mausdrag.offset;
}

void WireframePerspective::onMouseUp()
{
    Mausdrag.active = false;
    Mausdrag.object = nullptr;
}

Ray WireframePerspective::createRay(int x, int y)
{
    return createRayFromMouse(w-x, y, w, h, camera);
}

Vector3D WireframePerspective::getDragPlaneNormal() const {
    return camera->w;  // Blickrichtung der Kamera
}

