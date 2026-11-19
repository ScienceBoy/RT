
#include "kamera_bahn.h"
#include "wireframe_perspective.h"
#include "fenster.h"
#include "camera.h"
#include "mauspicking.h"
#include "mausray.h"
#include "object.h"
#include <iostream>

WireframePerspective::WireframePerspective(Fenster &f, Camera *cam, std::vector<object *> &objs)
    : Wireframe(f), camera(cam), objects(objs)
{
    f.setMouseMoveHandler([this](int x, int y, Fenster &f)
                          { this->onMouseMove(x, y, f); });
    f.setMouseWheelHandler([this](int delta, Fenster &f)
                           { this->onMouseWheel(delta, f); });
    /*f.setMouseDownHandler([this](int x, int y, Fenster &f)
                          { this->onMouseDown(x, y, f); });
    f.setMouseUpHandler([this]()
                        { this->onMouseUp(); });*/
    f.setMiddleMouseDownHandler([this](int x, int y, Fenster &f)
                                { this->onMiddleMouseDown(x, y, f); });
    f.setMiddleMouseUpHandler([this]()
                              { this->onMiddleMouseUp(); });
}

// Blick durch cam[0], Oeffnungswinkel wie die Raytracing-Kamera,
// jetzt mit Seitenverhaeltnis, damit das Bild nicht mehr horizontal gestreckt ist.
AnsichtParameter WireframePerspective::ansicht() const
{
    AnsichtParameter a;
    a.perspektivisch = true;

    if (cam.empty())
    {
        a.auge = Vector3D(0, 0, -100);
        a.rechts = Vector3D(1, 0, 0);
        a.oben = Vector3D(0, 1, 0);
        a.vorne = Vector3D(0, 0, 1);
        return a;
    }

    const Camera &c = cam[0];

    a.auge = c.position;
    a.rechts = c.u;
    a.oben = c.v;
    a.vorne = -c.w;

    // Oeffnungswinkel genau wie die Raytracing-Kamera (vorher tan(fov/2) * 7 / scale: hing am Zoom der
    // Ortho-Ansichten und zeigte viel mehr als das gerenderte Bild). Zoomen = Abstand zum Zielpunkt
    // (Mausrad), Einpassen-Knopf unten rechts.
    a.tanHalbY = std::tan(c.fov * M_PI / 180.0 * 0.5);
    a.tanHalbX = a.tanHalbY * double(w) / double(std::max(h, 1));
    return a;
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

void WireframePerspective::onMouseWheel(int delta, Fenster &f)
{
    /*if (firstWheel)
    {
        firstWheel = false;

        // Aktuelle Entfernung übernehmen,
        // damit der erste Wheel-Impuls nichts unerwartet verändert.
        Vector3D toCamera = camera->position - camera->target;
        camera->distance = toCamera.length();

        return;
    }*/

    // Nur reagieren, wenn Fenster aktiv
    // if (GetForegroundWindow() != f.getHWND())
    //    return;

    // Nur reagieren, wenn Maus über Fenster
    /*POINT cursor;
    GetCursorPos(&cursor);
    RECT rect;
    GetWindowRect(f.getHWND(), &rect);
    if (cursor.x < rect.left || cursor.x > rect.right ||
        cursor.y < rect.top || cursor.y > rect.bottom)
        return;*/

    double zoomStep = 0.1;
    // -----------------------------------------
    // SHIFT = FEINER / LANGSAMER
    // -----------------------------------------
    if (GetAsyncKeyState(VK_SHIFT) & 0x8000)
    {
        zoomStep = 0.02;
    }

    // -----------------------------------------
    // CTRL = SCHNELLER
    // -----------------------------------------
    if (GetAsyncKeyState(VK_CONTROL) & 0x8000)
    {
        zoomStep = 0.3;
    }

    // Richtung des Mausrads
    double direction =
        std::copysign(1.0, static_cast<double>(delta));

    // Zoomfaktor
    double zoomFactor =
        std::pow(1.0 - zoomStep, direction);

    // Kreisbahn erst aus der aktuellen Lage bestimmen: nach Verschieben der Kamera (Objektfenster,
    // Ziehen, Knoepfe) waren distance/yaw/pitch veraltet, die Kamera sprang beim ersten Raddreh
    kameraBahnAbgleichen(*camera);

    camera->distance *= zoomFactor;

    // Mindestabstand (vorher 1.0: in kleinen Szenen sprang die Kamera dadurch nach aussen)
    camera->distance =
        std::max(1e-4, camera->distance);

    camera->updateOrbit();
}

void WireframePerspective::onMouseDown(int x, int y, Fenster &f)
{
    // Wireframe::onMouseDown(x, y, f);

    // Mausposition speichern, um beim nächsten Move die richtige Delta zu berechnen
    lastMouseX = x;
    lastMouseY = y;
    firstMouse = true;
    firstWheel = true; // Mausrad-Flag zurücksetzen nach Klick
    if (camera)
        kameraBahnAbgleichen(*camera); // Drehen mit der mittleren Taste von der aktuellen Lage aus

    /*Ray ray = createRay(x, y);

    Hit nearestHit;
    nearestHit.t = 1e20;
    nearestHit.hit = false;

    Mausdrag.object = nullptr;

    // Iteriere über alle Objekte in der Szene
    for (auto &obj : scene)
    {
        Hit hit;
        obj->intersect(ray, hit);
        if (hit.hit && hit.t < nearestHit.t)
        {
            nearestHit = hit;
            Mausdrag.object = obj.get();
        }
    }

    // WICHTIG: Mausdrag.active NICHT sofort setzen!
    // Warte, bis sich die Maus genug bewegt hat (Distance Threshold)
    // Das verhindert, dass beim Mausrad-Drehen nach Klick die Kamera verschoben wird
    Mausdrag.active = false;

    if (Mausdrag.object)
    {
        Mausdrag.planePoint = nearestHit.position;
        Mausdrag.planeNormal = camera->w;
        Mausdrag.offset = Mausdrag.object->position - nearestHit.position;
        // Aktivierung erfolgt erst in onMouseMove, wenn Distance Threshold überschritten
    }*/
}

void WireframePerspective::onMouseMove(int x, int y, Fenster &f)
{
    // MMB:
    // Maus links/rechts → Orbit
    // Maus hoch/runter  → Orbit
    //
    // Shift + MMB:
    // Maus links/rechts → Kamera nach links/rechts
    // Maus hoch/runter  → Kamera nach oben/unten

    // Wireframe::onMouseMove(x, y, f);
    //  Erst die Mausposition initialisieren nach Linksklick, BEVOR irgendwas passiert
    /*if (firstMouse)
    {
        lastMouseX = x;
        lastMouseY = y;
        firstMouse = false;
        // Nicht weitermachen - kein Drag, kein Basisklasse-Aufruf!
        return;
    }*/

    // SICHERHEIT: Wenn die Maus plötzlich von (0,0) oder weit weg kommt (z.B. durch WM_SETFOCUS),
    // neu initialisieren, um einen großen Sprung zu vermeiden
    /*int dx_safety = x - lastMouseX;
    int dy_safety = y - lastMouseY;
    double dist_safety = std::sqrt(dx_safety * dx_safety + dy_safety * dy_safety);
    if (dist_safety > 500.0) // Wenn die Maus > 500 Pixel weit weg ist, neu initialisieren
    {
        lastMouseX = x;
        lastMouseY = y;
        return;
    }*/

    // ohne mittlere Maustaste: Hover, Verschieben/Drehen/Skalieren wie in den Ortho-Ansichten
    // (vorher kam die Mausbewegung hier nie bei Wireframe::onMouseMove an)
    if (!middleMouseDown)
    {
        Wireframe::onMouseMove(x, y, f);
        return;
    }

    // Erste Mausbewegung nach Drücken der mittleren Maustaste
    // nur zur Initialisierung verwenden.
    if (firstMiddleMouse)
    {
        lastMouseX = x;
        lastMouseY = y;
        firstMiddleMouse = false;
        return;
    }

    int dx = x - lastMouseX;
    int dy = y - lastMouseY;

    lastMouseX = x;
    lastMouseY = y;

    // -----------------------------------------
    // SHIFT + MMB = PAN
    // -----------------------------------------
    if (GetAsyncKeyState(VK_SHIFT) & 0x8000)
    {
        double panSpeed = 0.1;

        Vector3D delta =
            camera->u * (-dx) * panSpeed +
            camera->v * (dy) * panSpeed;

        camera->position += delta;
        camera->target += delta;
        camera->lookAt = camera->target;

        return;
    }

    // -----------------------------------------
    // MMB = ORBIT
    // -----------------------------------------

    double sensitivity = 0.005;

    camera->yaw -= dx * sensitivity;
    camera->pitch += dy * sensitivity;

    camera->pitch =
        std::clamp(camera->pitch, -1.5, 1.5);

    camera->updateOrbit();

    return;

    // Linksklick-Drag mit Distance Threshold
    /*if (Mausdrag.object != nullptr && !Mausdrag.active)
    {
        // Berechne Distanz seit Klick
        int dx = x - lastMouseX;
        int dy = y - lastMouseY;
        double dist = std::sqrt(dx * dx + dy * dy);

        // Distance Threshold: Nur aktivieren, wenn Maus genug bewegt wurde
        const double DISTANCE_THRESHOLD = 5.0;
        if (dist < DISTANCE_THRESHOLD)
            return; // Maus noch nicht genug bewegt → kein Drag

        // Schwelle überschritten → Drag aktivieren
        Mausdrag.active = true;
    }

    if (!Mausdrag.active || Mausdrag.object == nullptr)
        return;

    Ray ray = createRay(x, y);

    double t;
    if (!intersect(ray, Mausdrag.planePoint, Mausdrag.planeNormal, t))
        return;

    Vector3D maushit = ray.origin + ray.direction * t;

    Mausdrag.object->position =
        maushit + Mausdrag.offset;*/
}

void WireframePerspective::onMouseUp()
{
    Wireframe::onMouseUp();
    // Wir rufen NICHT Wireframe::onMouseUp() auf, um Interferenzen zu vermeiden
    // Mausdrag.active = false;
    // Mausdrag.object = nullptr;
}

Vector3D WireframePerspective::getDragPlaneNormal() const
{
    return camera->w; // Blickrichtung der Kamera
}

void WireframePerspective::onMiddleMouseDown(int x, int y, Fenster &)
{
    middleMouseDown = true;
    firstMiddleMouse = true; // Separates Flag für Mittelmaus
    lastMouseX = x;
    lastMouseY = y;

    // distance neu berechnen, um die aktuelle Kamera-Position zu erhalten
    // Wenn updateOrbit() später aufgerufen wird, soll die Position sich nicht ändern
    Vector3D toCamera = camera->position - camera->target;
    camera->distance = toCamera.length();
}

void WireframePerspective::onMiddleMouseUp()
{
    middleMouseDown = false;
    firstMiddleMouse = true; // Flag zurücksetzen für nächsten Klick
}
