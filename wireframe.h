#ifndef WIREFRAME_H
#define WIREFRAME_H

// camera.h bindet wireframe.h ein (und darueber scene.h, das Camera braucht): vor allen Includes bekannt machen
class Camera;

#include "fenster.h"
#include "scene.h"
#include "light.h"
#include "drawmode.h"
#include <vector>
#include <memory>

class light;

struct DragState
{
    bool active = false;
    object *object = nullptr;
    Vector3D planePoint;
    Vector3D planeNormal;
    Vector3D offset;
    Vector3D lastWorldPos;
    bool hasLastWorldPos = false;
    Vector3D startPos;  // Lage beim Start des Verschiebens (Shift: nur waagrecht oder senkrecht davon weg)
    int startX = 0, startY = 0;
    // Einschnappen an Kanten anderer Objekte (ALT: aus)
    Vector3D startMin, startMax;                         // Grenzen der Auswahl beim Start
    std::vector<std::pair<Vector3D, Vector3D>> snapZiele; // Grenzen der anderen Objekte (auch Teile von Gruppen)
    bool snapBereit = false;

    // Drehen/Skalieren mit der Maus (Rotate/Scale)
    Vector3D pivot;              // fester Drehpunkt: Mitte des Objekts beim Start
    Vector3D letzteRichtung;     // Ortho-Drehen: letzte Richtung Mitte -> Maus in der Ansichtsebene
    bool hatRichtung = false;
    double winkelSumme[2] = {0, 0};     // Grad seit Start (Perspektive: [0] waagrecht, [1] senkrecht)
    double winkelAngewandt[2] = {0, 0}; // davon schon angewandt (Shift: 15-Grad-Raster)
    double letzterAbstand = 0;          // Skalieren: Bildabstand Maus -> Mitte
    double letzterAbstandX = 0, letzterAbstandY = 0; // Ortho: waagrecht/senkrecht getrennt
    int letztesX = 0, letztesY = 0;
};

enum class SelectedType
{
    NONE,
    OBJECT,
    LIGHT,
    CAMERA,
    LOOKAT
};

enum class SelectedAction
{
    MOVE,
    SCALE,
    ROTATE
};

struct MouseDrag
{
    bool active = false;

    bool hasLastWorldPos = false;

    Vector3D planeNormal;
    Vector3D planePoint;

    Vector3D offset;
    Vector3D lastWorldPos;
};

struct PickResult
{
    SelectedType type = SelectedType::NONE;
    void *ptr = nullptr;
    double t = 1e20;
    Vector3D position;
};

struct ScreenPoint
{
    int x;
    int y;
    double z;
};

struct Vertex
{
    double x, y, z; // Screen-Koordinaten + Tiefe
    Farbe color;
};

// Beschreibt eine Ansicht vollstaendig. project(), createRay() und die GPU-Matrizen
// werden alle daraus berechnet, damit Bild und Maus-Picking immer zusammenpassen.
struct AnsichtParameter
{
    bool perspektivisch = false;
    Vector3D auge;   // perspektivisch: Kameraposition, orthogonal: Bildmitte (Szenenmitte)
    Vector3D rechts; // Bildschirm-Rechts in Weltkoordinaten
    Vector3D oben;   // Bildschirm-Oben in Weltkoordinaten
    Vector3D vorne;  // Blickrichtung (in die Szene hinein)

    double pixelProEinheit = 1.0; // nur orthogonal

    double tanHalbX = 1.0; // nur perspektivisch
    double tanHalbY = 1.0;
};

class GpuAufzeichnung;

extern double scale;
extern bool rendernRunning;
extern bool useFlat;
extern SelectedType selectedType;
extern SelectedAction selectedAction;
extern object *selectedObject;
extern light *selectedLight;
extern Camera *selectedCamera;

class Wireframe
{
protected:
    DragState Mausdrag;
    Ray createRay(int x, int y);
    virtual Vector3D getDragPlaneNormal() const
    {                             // default
        return Vector3D(0, 1, 0); // z.B. Y-Achse (Top View Standard)
    }

    std::vector<double> zBuffer;    // Z-Werte für jedes Pixel
    std::vector<Farbe> frameBuffer; // optional für Farbpixels

public:
    Wireframe(Fenster &window);
    virtual ~Wireframe() = default;

    void setScene(const std::vector<std::unique_ptr<object>> &scene);

    void run();

    int w, h;

    // Jede Ansicht beschreibt hier ihre Kamera (Top/Front/Side/Perspektive)
    virtual AnsichtParameter ansicht() const = 0;

    // Radius um 'mitte', der Szene, Lichter, Kamera und LookAt sicher umschliesst
    double szenenRadius(const Vector3D &mitte) const;

    // Ist ein Zeiger gesetzt (nur im GPU-Renderthread), zeichnen project(), drawLine() und
    // fillTriangle() nicht, sondern sammeln Weltgeometrie fuer die GPU.
    static thread_local GpuAufzeichnung *aufzeichnung;

    Fenster &getFenster() { return fenster; }

    void project(const Vector3D &p, int &x, int &y);
    void project(const Vector3D &p, int &x, int &y, double &z);
    // void drawLine(int x0, int y0, int x1, int y1, const Material& mat);
    void drawLine(int x0, int y0, double z0, int x1, int y1, double z1, const Material &mat);

    void onKey(WPARAM key);

    virtual bool showLookAtHelper() const { return true; }

    void onMouseMove(int x, int y, Fenster &f);
    Vector3D getSelectedPosition();
    void setSelectedPosition(const Vector3D &pos);
    void onMouseDown(int x, int y, Fenster &f);
    void onMouseWheel(int delta, Fenster &f);
    double distanceRayToPoint(const Ray &ray, const Vector3D &p);
    void setPixel(int x, int y, double z, const Farbe &color);
    void fillTriangleZ(int x0, int y0, double z0, int x1, int y1, double z1, int x2, int y2, double z2, const Material &mat);
    void clear();
    void fillTriangle(int x0, int y0, double z0, int x1, int y1, double z1, int x2, int y2, double z2, const Material &mat);
    void onMouseUp();
    void drehenOderSkalieren(int x, int y, double dist);

    // Licht als Kreis mit Strahlen, immer etwa gleich gross auf dem Bildschirm
    void lichtMarkierungZeichnen(const light &l, DrawMode mode);
    // Welteinheiten je Bildschirmpixel am Punkt p (0: hinter der Kamera). Fuer Symbole mit fester
    // Pixelgroesse (Kamera, LookAt, Richtungslicht), die sonst in grossen Szenen winzig waren
    double weltJePixel(const Vector3D &p) const;
    void kanteAnklicken(int x, int y, bool dazu); // Kanten-Modus
    void einschnappenVorbereiten();                         // beim Start des Verschiebens
    Vector3D einschnappen(Vector3D d, bool nurBewegteAchsen) const; // Verschiebung an Kanten anderer Objekte

    // Ortho-Ansichten mit der mittleren Maustaste verschieben
    bool panAktiv = false;
    int panX = 0, panY = 0;

    // Auswahlrahmen: Ziehen auf freier Flaeche (Strg: zur Auswahl hinzu)
    // nach rechts gezogen: Objekte ganz im Rahmen, nach links: Objekte, die er beruehrt
    bool rahmenMoeglich = false;
    bool rahmenAktiv = false; // wird gezeichnet (GPU-Ansichten)
    int rahmenX0 = 0, rahmenY0 = 0, rahmenX1 = 0, rahmenY1 = 0;
    std::vector<object *> rahmenVorher;
    void rahmenAuswaehlen();

    // Hilfslinien beim Drehen (1: Kreis um die Mitte) bzw. Skalieren (2: Linie von der Mitte),
    // nur in der Ansicht, in der gezogen wird; Bildschirmkoordinaten
    int griffArt = 0;
    int griffX = 0, griffY = 0;           // Mitte der Auswahl
    int griffStartX = 0, griffStartY = 0; // Maus beim Druecken
    bool griffJeRichtung = false;         // Ortho-Skalieren: waagrecht/senkrecht getrennt
    virtual bool isPerspective() const { return false; }
    bool draw3DLine(const Vector3D &a, const Vector3D &b, const Farbe &color);
    bool draw3DTriangle(const Vector3D &a, const Vector3D &b, const Vector3D &c, const Farbe &ca, const Farbe &cb, const Farbe &cc);
    void drawFilledTriangle(const Vertex &v0, const Vertex &v1, const Vertex &v2);
    void drawGrid();
    bool projectPoint(const Vector3D &p, ScreenPoint &sp);
    void drawGroundGridLine(const Vector3D &a, const Vector3D &b, double extent, const Farbe &color);
    void setPixelAlpha(int x, int y, double z, const Farbe &color, double alpha);
    Vector3D getPixelCenter(const std::vector<Vector3D> &pixels);
    void surrounding(Material mat, int w, int h);
    bool sameColor(const Farbe &a, const Farbe &b);

    void initBuffers(int width, int height); // Z-Buffer initialisieren

    PickResult pick(const Ray &ray);

    void clearSelection();

    void clearScene();

    bool intersectRaySphere(const Ray &ray, const Vector3D &center, double radius, double &tHit);

    std::vector<PickResult> pickAll(const Ray &ray);

    // Objekt unter der Maus als Kette: getroffenes Objekt der Szene, dann die getroffenen Teile bis zum innersten
    // (leer: nichts getroffen). Rechtsklick-Menue und Material-Pinsel.
    std::vector<object *> objektKetteUnter(int x, int y, bool ohneFellHuellen = false);

    SelectedType hoverType = SelectedType::NONE;
    object *hoverObject = nullptr;
    light *hoverLight = nullptr;
    Camera *hoverCamera = nullptr;
    // SelectedType selectedType = SelectedType::NONE;
    // object* selectedObject = nullptr;
    // light*  selectedLight  = nullptr;
    Camera *selectedCamera = nullptr;

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
    std::vector<object *> sceneobjects;

protected:
    Fenster &fenster;
};


Vector3D rotateAroundAxis(const Vector3D &v, const Vector3D &axis, double angleDeg);

// Mitte der Ortho-Ansichten: bleibt beim Verschieben/Drehen stehen (siehe wireframe.cpp)
Vector3D ansichtsMitte(bool szeneLeer);
void ansichtenNeuZentrieren();
void ansichtsMitteSetzen(const Vector3D &mitte);
void ansichtsMitteVerschieben(const Vector3D &delta);

// Weltpunkt unter der Maus fuer die Statusleiste (zuletzt bewegte Ansicht)
struct MausPosition
{
    bool gueltig = false;
    Vector3D punkt;
    bool achse[3] = {false, false, false}; // welche Koordinaten bekannt sind
    const wchar_t *ansicht = L"";
};
extern MausPosition gMausPosition;

#endif // WIREFRAME_H
