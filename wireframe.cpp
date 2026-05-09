#include "wireframe.h"
#include "camera.h"
#include "scene.h"
#include "rotations.h"
#include "threading.h"
#include "fenster.h"
#include "light.h"
#include "material.h"
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <thread>
#include "mauspicking.h"
#include "mausray.h"
#include "object.h"
#include "wand.h"
#include "mesh.h"
#include "SceneLoader.h"
#include <mutex>
#include <math.h>

//Camera* camera = nullptr;
extern Fenster winRay; 
std::mutex sceneMutex;
double scale = 0.5;
bool rendernRunning = false;
bool useFlat = true;

// ======================================================================
// Konstruktor - zum übergebenen Fenster passende Werte setzen
// ======================================================================

Wireframe::Wireframe(Fenster& window)
    : fenster(window),
      w(window.breite()),
      h(window.hoehe())
{
    // Maus-Events auf die Instanz-Handler weiterleiten
    fenster.setMouseMoveHandler([this](int x, int y, Fenster& f){
        this->onMouseMove(x, y, f);
    });
    fenster.setMouseWheelHandler([this](int delta, Fenster& f){
        this->onMouseWheel(delta, f);
    });
    fenster.setMouseDownHandler([this](int x, int y, Fenster& f){
        this->onMouseDown(x, y, f);
    });
    fenster.setMouseUpHandler([this](){
        this->onMouseUp();
    });

    // Tastendrücke an onKey() weiterreichen
    fenster.setKeyHandler([this](WPARAM key){
        this->onKey(key);
    });
}


// ======================================================================
// Szene registrieren
// ======================================================================

void Wireframe::setScene(const std::vector<std::unique_ptr<object>>& scene) {
    sceneobjects.clear();
 
    for (const auto& obj : scene)
        sceneobjects.push_back(obj.get()); 
}


// ======================================================================
// Haupt-Renderloop
// ======================================================================

void Wireframe::run() {
    if (rendernRunning)
        return;

    const int frameTime = 1000 / 30; // ~30 FPS

    // Key-Handler setzen 
    fenster.setKeyHandler([this](WPARAM key){
        this->onKey(key);
    });

    while (fenster.isOpen()) {
        DWORD start = GetTickCount();

        // Fenster-Events abarbeiten
        fenster.processEvents();

        // Bildschirm löschen
        //for (int y = 0; y < h; ++y)
        //    for (int x = 0; x < w; ++x)
        //        fenster.pixel(x, y, Farbe(0,0,0));

        // Fenstergröße ermitteln
        int width = fenster.breite();
        int height = fenster.hoehe();

        // Z-Buffer und Framebuffer für diesen Frame initialisieren
        initBuffers(width, height);
        
        // Szene zeichnen 
        std::vector<object*> objectsCopy;
        {
            std::lock_guard<std::mutex> lock(sceneMutex);
            objectsCopy = sceneobjects;
        }

        for (auto& obj : objectsCopy)
        {
            if (!obj) continue;
            DrawMode mode = DrawMode::NORMAL;
            if (selectedType == SelectedType::OBJECT && obj == selectedObject)
                mode = DrawMode::HIGHLIGHT;
            else if (hoverType == SelectedType::OBJECT && obj == hoverObject)
                mode = DrawMode::HOVER; 

            if (useFlat)
            {
                obj->drawFlat(*this, mode);
                //obj->drawWireframePixels(*this, mode);
            }
            else
            {
                obj->drawWireframePixels(*this, mode);
            }
        }

        for (auto& obj : objectsCopy)
        {
            if (!obj) continue;  // nochmals durch alle Objekte und die hover oder selektierten nochmals darüber zeichnen
            if (!(selectedType == SelectedType::OBJECT && obj == selectedObject) && !(selectedType == SelectedType::OBJECT && obj == hoverObject))
                continue;

            DrawMode mode = DrawMode::NORMAL;
            if (selectedType == SelectedType::OBJECT && obj == selectedObject)
                mode = DrawMode::HIGHLIGHT;
            else if (hoverType == SelectedType::OBJECT && obj == hoverObject)
                mode = DrawMode::HOVER; 

            if (useFlat)
            {
                obj->drawFlat(*this, mode);
                //obj->drawWireframePixels(*this, mode);
            }
            else
                obj->drawWireframePixels(*this, mode);
        }

        for (auto& light : lights)
        {
            DrawMode mode = DrawMode::NORMAL;
            if (selectedType == SelectedType::LIGHT && &light == selectedLight)
                mode = DrawMode::HIGHLIGHT;
            else if (hoverType == SelectedType::LIGHT && &light == hoverLight)
                mode = DrawMode::HOVER;
            if (useFlat)
            {
                light.drawFlat(*this, mode);
                //light.drawWireframePixels(*this, mode);
            }
            else
                light.drawWireframePixels(*this, mode);
        }

        // Kamera
        DrawMode mode = DrawMode::NORMAL;
        if (selectedType == SelectedType::CAMERA)
            mode = DrawMode::HIGHLIGHT;
        else if (hoverType == SelectedType::CAMERA || hoverType == SelectedType::LOOKAT)
            mode = DrawMode::HOVER;
        
            if (useFlat)
            {
                cam[0].drawFlat(*this, mode);
                //cam[0].drawWireframePixels(*this, mode);
            }
            else
                cam[0].drawWireframePixels(*this, mode);

        // Update des Fensters
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
                fenster.pixel(x, y, frameBuffer[y * w + x]);

        fenster.update();

        // Frame-Time anpassen für ~60 FPS
        DWORD elapsed = GetTickCount() - start;
        if (elapsed < frameTime)
            Sleep(frameTime - elapsed);
    }
}

// ======================================================================
// Default-Projektion – Draufsicht (falls Subklasse nicht überschreibt)
// ======================================================================

void Wireframe::project(const Vector3D& p, int& x, int& y) {
    x = w/2 + p.x * scale;
    y = h/2 - p.z * scale;
}

void Wireframe::project(const Vector3D& p, int& x, int& y, double& z)
{
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

    x = int(px * invZ * f * w/2 + w/2);
    y = int(-py * invZ * f * h/2 + h/2);

    z = -pz; // Tiefe positiv
}

// ======================================================================
// Linien zeichnen
// ======================================================================

void Wireframe::drawLine(int x0, int y0, double z0,
                        int x1, int y1, double z1,
                        const Material& mat)
{
    int dx = abs(x1 - x0);
    int dy = abs(y1 - y0);
    int steps = std::max(dx, dy);

    if (steps == 0) {
        if (x0 >= 0 && x0 < w && y0 >= 0 && y0 < h)
            setPixel(x0, y0, z0, mat.diffuse);
        return;
    }

    for (int i = 0; i <= steps; ++i) {
        double t = (double)i / steps;

        int x = int(x0 + (x1 - x0) * t);
        int y = int(y0 + (y1 - y0) * t);
        double z = z0 + t * (z1 - z0);   
        z -= 1e-5;  // kleiner Bias, damit Linie vor Fläche liegt

        if (x >= 0 && x < w && y >= 0 && y < h)
        {
            Farbe col = (i % 2 == 0)
                ? mat.diffuse + Farbe(0.3, 0.3, 0.3)
                : mat.diffuse;

            setPixel(x, y, z, col);  
        }
    }
}

Vector3D rotateAroundAxis(
    const Vector3D& v,
    const Vector3D& axis,
    double angleDeg)
{
    double a = angleDeg * 3.14159265 / 180.0;
    Vector3D k = axis.normalized();

    return v * cos(a)
         + k.cross(v) * sin(a)
         + k * (k * v) * (1 - cos(a));
}

void Wireframe::onKey(WPARAM key)
{
    if (key == VK_ADD || (key == VK_OEM_PLUS && (GetKeyState(VK_SHIFT) & 0x8000))) {
        scale *= 1.1;
        std::cout << "Zoom In, scale = " << scale << std::endl;
    }
    else if (key == VK_SUBTRACT || key == VK_OEM_MINUS) {
        scale /= 1.1;
        std::cout << "Zoom Out, scale = " << scale << std::endl;
    }

    if (scale < 0.001) scale = 0.001;

    if (key == 'F')
    {
        useFlat = !useFlat;
    }

       
    if (key == VK_ESCAPE)
    {
        selectedType = SelectedType::NONE;
        selectedObject = nullptr;
        selectedLight = nullptr;
        selectedCamera = nullptr;

        Mausdrag.active = false;
    }

    /*if (key == VK_LEFT) {
        // 🔁 Kamera drehen
        double angle = 5.0; // 5 Grad pro Tastendruck

        Vector3D pos = cam[0].position;
        Vector3D look = cam[0].lookAt;

        Vector3D dir = look - pos;
        dir = rotateY(dir, -angle);
                
        cam[0].lookAt = pos + dir;
        cam[0].update();
    }

    else if (key == VK_RIGHT) {
        // 🔁 Kamera drehen
        double angle = 5.0; // 5 Grad pro Tastendruck

        Vector3D pos = cam[0].position;
        Vector3D look = cam[0].lookAt;

        Vector3D dir = look - pos;
        dir = rotateY(dir, angle);
        
        cam[0].lookAt = pos + dir;
        cam[0].update();
    }*/

    double step = 0.5;
    double sensitivity = 1;

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

    double rotSpeed = 2.0; // Grad pro Tastendruck

    Vector3D look = cam[0].lookAt;
    Vector3D pos  = cam[0].position;

    // Vektor von LookAt → Kamera
    Vector3D offset = pos - look;

    // Yaw: LINKS / RECHTS (um Welt-Up)
    if (key == VK_LEFT)
    {
        offset = rotateAroundAxis(offset, Vector3D(0,1,0), +rotSpeed);
    }
    else if (key == VK_RIGHT)
    {
        offset = rotateAroundAxis(offset, Vector3D(0,1,0), -rotSpeed);
    }

    // Pitch: HOCH / RUNTER (um Kamera-Rechtsvektor)
    Vector3D right = cam[0].u;

    if (key == VK_UP)
    {
        offset = rotateAroundAxis(offset, right, +rotSpeed);
    }
    else if (key == VK_DOWN)
    {
        offset = rotateAroundAxis(offset, right, -rotSpeed);
    }

    // Neue Kameraposition
    cam[0].position = look + offset;
    cam[0].update();

}

PickResult Wireframe::pick(const Ray& ray)
{
    PickResult best;

    // ===== Objekte =====
    const double PICK_RADIUS_PIXELS = 6.0;

    for (auto& obj : sceneobjects)
    {
        // ===== 1. Screen-Space Test =====
        int sx, sy;
        project(obj->position, sx, sy);

        int dx = sx - mouseX;
        int dy = sy - mouseY;

        double dist2D = std::sqrt(dx*dx + dy*dy);

        if (dist2D > PICK_RADIUS_PIXELS)
            continue;

        // ===== 2. Feiner Test =====
        Hit hit;
        obj->intersect(ray, hit);

        double finalT = 1e20;
        Vector3D finalPos;

        if (hit.hit && hit.t > 0)
        {
            finalT = hit.t;
            finalPos = hit.position;
        }
        else
        {
            // fallback: Tiefe aus Projektion schätzen
            Vector3D toObj = obj->position - ray.origin;
            finalT = toObj * ray.direction;
            finalPos = obj->position;
        }

        // ===== 3. Bestes Objekt =====
        if (finalT > 0 && finalT < best.t)
        {
            best.t = finalT;
            best.type = SelectedType::OBJECT;
            best.ptr = obj;
            best.position = finalPos;
        }
    }
    
    // ===== Licht =====
    for (auto& light : lights)
    {
        int sx, sy;
        project(light.position, sx, sy);

        int dx = sx - mouseX;
        int dy = sy - mouseY;

        double dist2D = std::sqrt(dx*dx + dy*dy);
        if (dist2D > PICK_RADIUS_PIXELS)
            continue;

        // Tiefe berechnen
        Vector3D toL = light.position - ray.origin;
        double t = toL * ray.direction;

        if (t > 0 && t < best.t)
        {
            best.t = t;
            best.type = SelectedType::LIGHT;
            best.ptr = &light;
            best.position = light.position;
        }
    }

    // ===== Kamera =====
    {
        int sx, sy;
        project(cam[0].position, sx, sy);

        int dx = sx - mouseX;
        int dy = sy - mouseY;

        double dist2D = std::sqrt(dx*dx + dy*dy);
        if (dist2D <= PICK_RADIUS_PIXELS)
        {
            Vector3D toC = cam[0].position - ray.origin;
            double t = toC * ray.direction;

            if (t > 0 && t < best.t)
            {
                best.t = t;
                best.type = SelectedType::CAMERA;
                best.ptr = &cam[0];
                best.position = cam[0].position;
            }
        }
    }

    // ===== LookAt =====
    {
        int sx, sy;
        project(cam[0].lookAt, sx, sy);

        int dx = sx - mouseX;
        int dy = sy - mouseY;

        double dist2D = std::sqrt(dx*dx + dy*dy);
        if (dist2D <= PICK_RADIUS_PIXELS)
        {
            Vector3D toL = cam[0].lookAt - ray.origin;
            double t = toL * ray.direction;

            if (t > 0 && t < best.t)
            {
                best.t = t;
                best.type = SelectedType::LOOKAT;
                best.ptr = &cam[0];
                best.position = cam[0].lookAt;
            }
        }
    }

    return best;
}

void Wireframe::clearSelection()
{
    selectedObject = nullptr;
    selectedLight = nullptr;
    selectedCamera = nullptr;

    hoverObject = nullptr;
    hoverLight = nullptr;
    hoverCamera = nullptr;

    selectedType = SelectedType::NONE;
    hoverType = SelectedType::NONE;
}

void Wireframe::clearScene() {
    std::lock_guard<std::mutex> lock(sceneMutex);
    sceneobjects.clear();
    clearSelection(); // jetzt kein Lock in clearSelection
}

bool Wireframe::intersectRaySphere(const Ray& ray,
                        const Vector3D& center,
                        double radius,
                        double& tHit)
{
    Vector3D oc = ray.origin - center;

    double a = ray.direction * ray.direction;
    double b = 2.0 * (oc * ray.direction);
    double c = (oc * oc) - radius * radius;

    double discriminant = b*b - 4*a*c;

    if (discriminant < 0)
        return false;

    double sqrtD = sqrt(discriminant);

    double t1 = (-b - sqrtD) / (2.0 * a);
    double t2 = (-b + sqrtD) / (2.0 * a);

    if (t1 > 0)
        tHit = t1;
    else if (t2 > 0)
        tHit = t2;
    else
        return false;

    return true;
}

std::vector<PickResult> Wireframe::pickAll(const Ray& ray)
{
    std::vector<PickResult> hits;
    std::vector<PickResult> gizmoHits;

    // ===== Objekte =====
    const double BASE_RADIUS = 50.0;

    for (auto& obj : sceneobjects)
    {
        // ===== 1. Abstand zum Ray =====
        //double dist = distanceRayToPoint(ray, obj->position);

        //  Radius abhängig von Entfernung 
        //Vector3D toObj = obj->position - ray.origin;
        //double t = toObj * ray.direction;

        //if (t <= 0) 
        //    continue;

        // Radius abhängig von Objektgröße UND ggf. Perspektive
        double factor = 2.0; // justierbar, z.B. 2 für etwas größere Picking-Region
        double radius = obj->boundingRadius * factor;

        if (isPerspective())
        {
            // Radius leicht an Entfernung anpassen
            // radius *= (1.0 + t * 0.01);
            //radius = dist * 0.1; // die perspektivische Ansicht soll nicht Anklicken ermöglichen, also einfach dist * 0.1 berechnen, dann ist sicher dist > radius
            //radius = 50;
            /*Vector3D toObj = obj->position - cam[0].position;
            double pz = toObj * cam[0].w;

            if(pz >= 0) 
                continue; // hinter der Kamera

            double zoom = 0.7;
            double f = 1.0 / tan((cam[0].fov * 3.14159265/180.0) / 2.0);

            // Perspektivisch skalierter Radius in 3D:
            std::cout << "radius vorher: " << radius << "dist: " << dist << '\n';
            radius = radius * (-1.0 / pz) * f * (h/2) * zoom * 10;
            std::cout << "radius nachher: " << radius << "dist: " << dist << '\n';*/
        }
        else
        {
            radius *= scale; // Orthografische Skalierung
        }

        /*
        // ===== 1a. Bounding-Radius debug (Pixel-Raster) =====
        const int N = 50; // Anzahl Pixel pro Achse
        for(int ix=-N; ix<=N; ++ix)
        {
            for(int iy=-N; iy<=N; ++iy)
            {
                for(int iz=-N; iz<=N; ++iz)
                {
                    Vector3D offset(ix*radius/N, iy*radius/N, iz*radius/N);
                    Vector3D p = obj->position + offset;
                    int px, py;
                    project(p, px, py);
                    if(px >= 0 && px < w && py >= 0 && py < h)
                        setPixel(px, py, p.z, Farbe(0,1,0)); // grün = Bounding-Radius
                }
            }
        }*/

        // Nur weiter, wenn Abstand kleiner als Radius
        //std::cout << "radius: " << radius << "   |    dist: " << dist << '\n';

        //if (dist > radius)
        //    continue;

        // Echter Intersection-Test
        double tHit;
        if (!intersectRaySphere(ray, obj->position, radius, tHit))
            continue;

        // ===== 2. exakte Intersection (optional) =====
        Hit hit;
        obj->intersect(ray, hit);

        //double finalT = t;
        //Vector3D finalPos = obj->position;
        double finalT = tHit;
        Vector3D finalPos = ray.origin + ray.direction * tHit;

        if (hit.hit && hit.t > 0 && hit.t < finalT)
        {
            finalT = hit.t;
            finalPos = hit.position;

            /*
            // 🔹 Trefferpunkt als Pixel visualisieren
            int px, py;
            project(finalPos, px, py);  // projiziere Weltkoordinaten in Bildschirm
            if(px >= 0 && px < w && py >= 0 && py < h)
                setPixel(px, py, finalPos.z, Farbe(1,0,0)); // rotes Pixel*/
        }
        //fenster.update(); 

        // ===== 3. bestes Objekt =====
        PickResult r;
        r.t = finalT;
        r.type = SelectedType::OBJECT;
        r.ptr = obj;
        r.position = finalPos;

        hits.push_back(r);
    }

    // ===== Licht =====
    for (auto& light : lights)
    {
        double dist = distanceRayToPoint(ray, light.position);

        Vector3D toL = light.position - ray.origin;
        double t = toL * ray.direction;

        if (t <= 0) continue;

        double radius = isPerspective()
            ? BASE_RADIUS * (t * 0.01)
            : BASE_RADIUS * scale;

        if (dist > radius)
            continue;

        PickResult r;
        r.t = t;
        r.type = SelectedType::LIGHT;
        r.ptr = &light;
        r.position = light.position;

        gizmoHits.push_back(r); 
    }
    
    // ===== Kamera =====
    {
        Vector3D toC = cam[0].position - ray.origin;
        double t = toC * ray.direction;

        Vector3D closest = ray.origin + ray.direction * t;
        double dist = (cam[0].position - closest).length();

        if (dist < BASE_RADIUS * scale && t > 0)
        {
            PickResult r;
            r.t = t;
            r.type = SelectedType::CAMERA;
            r.ptr = &cam[0];
            r.position = cam[0].position;

            gizmoHits.push_back(r);
        }
    }

    // ===== LookAt =====
    {
        Vector3D toL = cam[0].lookAt - ray.origin;
        double t = toL * ray.direction;

        Vector3D closest = ray.origin + ray.direction * t;
        double dist = (cam[0].lookAt - closest).length();

        if (dist < BASE_RADIUS * scale && t > 0)
        {
            PickResult r;
            r.t = t;
            r.type = SelectedType::LOOKAT;
            r.ptr = &cam[0];
            r.position = cam[0].lookAt;

            gizmoHits.push_back(r);
        }
    }

    // ===== Sortieren =====
    std::sort(gizmoHits.begin(), gizmoHits.end(), [](auto& a, auto& b){ return a.t < b.t; });
    std::sort(hits.begin(), hits.end(), [](auto& a, auto& b){ return a.t < b.t; });

    //  Gizmos zuerst!
    gizmoHits.insert(gizmoHits.end(), hits.begin(), hits.end());

    return gizmoHits;
}

void Wireframe::onMouseDown(int x, int y, Fenster& f)
{
    mouseDownX = x;
    mouseDownY = y;
    this->mouseX = x;
    this->mouseY = y;
    mousePressed = true;

    Mausdrag.active = false;

    Ray ray = createRay(x, y);
    auto hits = pickAll(ray);

    bool sameClickPos = (abs(x - lastMouseX) < 10 && abs(y - lastMouseY) < 10);

    //  Prüfen ob aktuelles Objekt angeklickt wurde
    bool clickedSelected = false;

    if (!hits.empty())
    {
        PickResult& front = hits[0];

        if (selectedType == front.type)
        {
            if (selectedType == SelectedType::OBJECT && front.ptr == selectedObject)
                clickedSelected = true;

            else if (selectedType == SelectedType::LIGHT && front.ptr == selectedLight)
                clickedSelected = true;

            else if ((selectedType == SelectedType::CAMERA || selectedType == SelectedType::LOOKAT)
                     && front.ptr == selectedCamera)
                clickedSelected = true;
        }
    }

    //  Wenn auf aktuelles Objekt geklickt, KEIN Cycling
    /*if (clickedSelected)
    {
        return; //  wichtig!
    }*/

    //  Normales Picking / Cycling
    if (!sameClickPos)
    {
        lastHits = hits;
        pickIndex = 0;
    }
    else
    {
        pickIndex++;
    }

    // Reset
    selectedObject = nullptr;
    selectedLight = nullptr;
    selectedCamera = nullptr;
    selectedType = SelectedType::NONE;

    if (!lastHits.empty())
    {
        pickIndex %= lastHits.size();
        PickResult& hit = lastHits[pickIndex];

        selectedType = hit.type;

        if (hit.type == SelectedType::OBJECT)
            selectedObject = (object*)hit.ptr;

        else if (hit.type == SelectedType::LIGHT)
            selectedLight = (light*)hit.ptr;

        else
            selectedCamera = (Camera*)hit.ptr;
    }

    lastMouseX = x;
    lastMouseY = y;
}

void Wireframe::onMouseMove(int x, int y, Fenster& f)
{
    // ------------------------------
    // 1. Hover-Erkennung (ohne Drag)
    // ------------------------------
    Ray ray = createRay(x, y);
    auto hits = pickAll(ray);

    if (!hits.empty())
    {
        PickResult& front = hits[0]; // Vorderstes Element
        hoverType = front.type;

        hoverObject = (front.type == SelectedType::OBJECT) ? (object*)front.ptr : nullptr;
        hoverLight  = (front.type == SelectedType::LIGHT) ? (light*)front.ptr : nullptr;
        hoverCamera = (front.type == SelectedType::CAMERA || front.type == SelectedType::LOOKAT) ? (Camera*)front.ptr : nullptr;
    }
    else
    {
        hoverType = SelectedType::NONE;
        hoverObject = nullptr;
        hoverLight = nullptr;
        hoverCamera = nullptr;
    }

    // ------------------------------
    // 2. Drag-Code 
    // ------------------------------
    if (!mousePressed)
        return;

    int dx = x - mouseDownX;
    int dy = y - mouseDownY;
    double dist = std::sqrt(dx*dx + dy*dy);

    if (!Mausdrag.active)
    {
        if (dist < DISTANCE_THRESHOLD)
            return;
        if (selectedType == SelectedType::NONE)
            return;

        Mausdrag.active = true;
        Mausdrag.planeNormal = getDragPlaneNormal();
        Vector3D pos = getSelectedPosition();
        Mausdrag.planePoint = pos;

        double t;
        Ray ray2 = createRay(x, y);
        if (!intersect(ray2, Mausdrag.planePoint, Mausdrag.planeNormal, t))
            return;

        Mausdrag.offset = pos - (ray2.origin + ray2.direction * t);
    }

    if (!Mausdrag.active)
        return;

    double t;
    if (!intersect(ray, Mausdrag.planePoint, Mausdrag.planeNormal, t))
        return;

    Vector3D hit = ray.origin + ray.direction * t;
    Vector3D newPos = hit + Mausdrag.offset;

    // ------------------------------
    // 3. Verschiebe ausgewähltes Objekt oder Mesh
    // ------------------------------
    if (selectedType == SelectedType::OBJECT && selectedObject)
    {
        // Wenn es ein Mesh ist, verschiebe alle Dreiecke
        Mesh* mesh = dynamic_cast<Mesh*>(selectedObject);
        if (mesh)
        {
            Vector3D delta = newPos - getSelectedPosition();
            mesh->translate(delta);
        }
        else
        {
            // Normales Objekt verschieben
            setSelectedPosition(newPos);
        }
    }
    else
    {
        // Optional: andere Typen behandeln (Lights, Camera ...)
        setSelectedPosition(newPos);
    }

    computeSceneBounds();
}

void Wireframe::onMouseUp()
{
    mousePressed = false;
    Mausdrag.active = false;
}

Vector3D Wireframe::getSelectedPosition()
{
    if (selectedType == SelectedType::OBJECT && selectedObject)
        return selectedObject->position;

    if (selectedType == SelectedType::LIGHT && selectedLight)
        return selectedLight->position;

    if (selectedType == SelectedType::CAMERA && selectedCamera)
        return selectedCamera->position;

    if (selectedType == SelectedType::LOOKAT && selectedCamera)
        return selectedCamera->lookAt;

    return Vector3D();
}

void Wireframe::setSelectedPosition(const Vector3D& pos)
{
    if (selectedType == SelectedType::OBJECT && selectedObject)
        selectedObject->position = pos;

    else if (selectedType == SelectedType::LIGHT && selectedLight)
        selectedLight->position = pos;

    else if (selectedType == SelectedType::CAMERA && selectedCamera)
    {
        selectedCamera->position = pos;
        selectedCamera->update();
    }

    else if (selectedType == SelectedType::LOOKAT && selectedCamera)
    {
        selectedCamera->lookAt = pos;
        selectedCamera->update();
    }
}

void Wireframe::onMouseWheel(int delta, Fenster& f)
{
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

    cam[0].position += cam[0].w * dz * sensitivity;*/

    
    double zoomSpeed = 2.0;

    cam[0].fov -= (delta / 120.0) * zoomSpeed;

    cam[0].fov = std::clamp(cam[0].fov, 10.0, 120.0);

    cam[0].update();

}

void Wireframe::initBuffers(int width, int height)
{
    w = width;
    h = height;
    zBuffer.assign(w*h, std::numeric_limits<double>::infinity());
    frameBuffer.assign(w*h, Farbe(0,0,0));
}

void Wireframe::fillTriangleZ(
    int x0,int y0,double z0,
    int x1,int y1,double z1,
    int x2,int y2,double z2,
    const Material &mat)
{
    // Bounding box
    int minX = std::max(0, std::min({x0,x1,x2}));
    int maxX = std::min(w-1, std::max({x0,x1,x2}));
    int minY = std::max(0, std::min({y0,y1,y2}));
    int maxY = std::min(h-1, std::max({y0,y1,y2}));
    minX = std::max(0, minX);
    maxX = std::min(w-1, maxX);

    // Vektor-Koordinaten
    double det = (y1 - y2)*(x0 - x2) + (x2 - x1)*(y0 - y2);
    if(det == 0) return; // Degeneriertes Dreieck

    int step = 1;
    for(int y = minY; y <= maxY; y = y + step)
    {
        for(int x = minX; x <= maxX; x = x + step)
        {
            // Baryzentrische Koordinaten
            double px = x + 0.5;
            double py = y + 0.5;

            double w0 = ((y1 - y2)*(px - x2) + (x2 - x1)*(py - y2)) / det;
            double w1 = ((y2 - y0)*(px - x2) + (x0 - x2)*(py - y2)) / det;
            double w2 = 1.0 - w0 - w1;

            const double EPS = -1e-6;
            if(w0 > EPS && w1 > EPS && w2 > EPS)
            {
                double z = w0*z0 + w1*z1 + w2*z2;  // lineare Interpolation
                setPixel(x, y, z, mat.diffuse);
            }
        }
    }
}

void Wireframe::clear()
{
    zBuffer.assign(w * h, std::numeric_limits<double>::infinity());
}

void Wireframe::fillTriangle(
    int x0, int y0, double z0,
    int x1, int y1, double z1,
    int x2, int y2, double z2,
    const Material& mat)
{
    // =========================================================
    // Bounding box
    // =========================================================
    int minX = std::max(0, std::min({x0, x1, x2}));
    int maxX = std::min(w - 1, std::max({x0, x1, x2}));
    int minY = std::max(0, std::min({y0, y1, y2}));
    int maxY = std::min(h - 1, std::max({y0, y1, y2}));

    auto edge = [](double ax, double ay,
                   double bx, double by,
                   double px, double py)
    {
        return (bx - ax) * (py - ay)
             - (by - ay) * (px - ax);
    };

    // =========================================================
    // Triangle area
    // =========================================================
    double area = edge(x0, y0, x1, y1, x2, y2);

    if (area == 0.0)
        return;

    // CCW fix
    if (area < 0.0)
    {
        std::swap(x1, x2);
        std::swap(y1, y2);
        std::swap(z1, z2);
        area = -area;
    }

    double invArea = 1.0 / area;

    // =========================================================
    // Edge coefficients (incremental)
    // =========================================================
    const double A01 = y0 - y1;
    const double A12 = y1 - y2;
    const double A20 = y2 - y0;

    const double B01 = x1 - x0;
    const double B12 = x2 - x1;
    const double B20 = x0 - x2;

    // =========================================================
    // Pixel start (center)
    // =========================================================
    double px = minX + 0.5;
    double py = minY + 0.5;

    // =========================================================
    // Initial edge values
    // =========================================================
    double w0_row = edge(x1, y1, x2, y2, px, py);
    double w1_row = edge(x2, y2, x0, y0, px, py);
    double w2_row = edge(x0, y0, x1, y1, px, py);

    // =========================================================
    // Pointer optimization (IMPORTANT)
    // =========================================================
    Farbe* fb = frameBuffer.data();
    double* zb = zBuffer.data();

    int width = w;

    // =========================================================
    // Raster loop
    // =========================================================
    for (int y = minY; y <= maxY; y++)
    {
        double w0 = w0_row;
        double w1 = w1_row;
        double w2 = w2_row;

        int idx = y * width + minX;

        Farbe* fbRow = fb + idx;
        double* zRow = zb + idx;

        for (int x = minX; x <= maxX; x++)
        {
            // =================================================
            // Inside test (unchanged)
            // =================================================
            if (w0 >= 0.0 && w1 >= 0.0 && w2 >= 0.0)
            {
                double alpha = w0 * invArea;
                double beta  = w1 * invArea;
                double gamma = w2 * invArea;

                double z =
                    alpha * z0 +
                    beta  * z1 +
                    gamma * z2;

                // =================================================
                // Branch optimized Z-test (safe version)
                // =================================================
                if (z < *zRow)
                {
                    *zRow = z;
                    *fbRow = mat.diffuse;
                }
            }

            // =================================================
            // Incremental edge stepping
            // =================================================
            w0 += A12;
            w1 += A20;
            w2 += A01;

            fbRow++;
            zRow++;
        }

        // =====================================================
        // next row
        // =====================================================
        w0_row += B12;
        w1_row += B20;
        w2_row += B01;
    }
}

double Wireframe::distanceRayToPoint(const Ray& ray, const Vector3D& p)
{
    Vector3D toP = p - ray.origin;
    double t = toP * ray.direction;

    Vector3D closest = ray.origin + ray.direction * t;
    return (p - closest).length();
}

void Wireframe::setPixel(int x, int y, double z, const Farbe& color)
{
    if(x < 0 || x >= w || y < 0 || y >= h) return;

    if(z < zBuffer[y * w + x])
    {
        zBuffer[y * w + x] = z;
        frameBuffer[y * w + x] = color;
    }
}