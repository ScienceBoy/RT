#include "light.h"
#include "wireframe.h"
#include <cmath>
#include <vector>
#include <numbers>

light::light()
: position(0,0,0), farbe(1,1,1)
{}

light::light(const Vector3D& pos, const Farbe& col)
: position(pos), farbe(col)
{}

void light::drawFlat(Wireframe& wf, DrawMode mode) const
{
    // solange nicht implementiert → Wireframe benutzen
    drawWireframePixels(wf, mode);
}

void light::drawWireframePixels(Wireframe& wf, DrawMode mode = DrawMode::NORMAL) const
{
    const int steps = 32;
    const double radius = 10.0;
    int x0,y0,x1,y1;
    double z0,z1;
    Farbe color;
    if (mode == DrawMode::HIGHLIGHT)
        color = Farbe(1,1,0);      // Gelb für Auswahl
    else if (mode == DrawMode::HOVER)
        color = Farbe(0,1,1);      // Cyan für Hover
    else
        color = farbe;             // Standardfarbe

    // Kreis XZ
    for(int i=0;i<steps;i++)
    {
        double a0 = 2*3.14159265*i/steps;
        double a1 = 2*3.14159265*(i+1)/steps;
        Vector3D p0 = position + Vector3D(radius*cos(a0),0,radius*sin(a0));
        Vector3D p1 = position + Vector3D(radius*cos(a1),0,radius*sin(a1));
        wf.project(p0,x0,y0,z0);
        wf.project(p1,x1,y1,z1);
        wf.drawLine(x0,y0,z0,x1,y1,z1,makeMaterialSimple(color));
    }

    // Kreis XY
    for(int i=0;i<steps;i++)
    {
        double a0 = 2*3.14159265*i/steps;
        double a1 = 2*3.14159265*(i+1)/steps;
        Vector3D p0 = position + Vector3D(radius*cos(a0),radius*sin(a0),0);
        Vector3D p1 = position + Vector3D(radius*cos(a1),radius*sin(a1),0);
        wf.project(p0,x0,y0,z0);
        wf.project(p1,x1,y1,z1);
        wf.drawLine(x0,y0,z0,x1,y1,z1,makeMaterialSimple(color));
    }

    // Kreis YZ
    for(int i=0;i<steps;i++)
    {
        double a0 = 2*3.14159265*i/steps;
        double a1 = 2*3.14159265*(i+1)/steps;
        Vector3D p0 = position + Vector3D(0,radius*cos(a0),radius*sin(a0));
        Vector3D p1 = position + Vector3D(0,radius*cos(a1),radius*sin(a1));
        wf.project(p0,x0,y0,z0);
        wf.project(p1,x1,y1,z1);
        wf.drawLine(x0,y0,z0,x1,y1,z1,makeMaterialSimple(color));
    }

    // Strahlen
    std::vector<Vector3D> dirs = {
        {1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1},
        {1,1,1},{-1,1,1},{1,-1,1},{1,1,-1},{-1,-1,1},{-1,1,-1},{1,-1,-1},{-1,-1,-1}
    };

    double rayLength = radius*2;
    for(const auto& d : dirs)
    {
        Vector3D dir = d.normalized();
        Vector3D p0 = position;
        Vector3D p1 = position + dir * rayLength;
        wf.project(p0,x0,y0,z0);
        wf.project(p1,x1,y1,z1);
        wf.drawLine(x0,y0,z0,x1,y1,z1,makeMaterialSimple(color));
    }
}

void light::intersect(const Ray& ray, Hit& hit) const
{
    double radius = 10.0;
    Vector3D v = ray.origin - position;
    double a = ray.direction*ray.direction;
    double b = 2.0*(v*ray.direction);
    double c = v*v - radius*radius;

    double d = b*b - 4*a*c;
    if(d<0) return;

    double t = (-b - sqrt(d)) / (2*a);
    if(t < 1e-6) return;

    if(t < hit.t)
    {
        hit.t = t;
        hit.hit = true;
        hit.position = ray.origin + ray.direction * t;
    }
}