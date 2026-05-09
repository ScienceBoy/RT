#include "vector3d.h"

// Konstruktoren
Vector3D::Vector3D() : x(0), y(0), z(0) {}
Vector3D::Vector3D(double x_, double y_, double z_) : x(x_), y(y_), z(z_) {}

// Operatoren
Vector3D Vector3D::operator+(const Vector3D& v) const { return Vector3D(x+v.x, y+v.y, z+v.z); }
Vector3D Vector3D::operator-(const Vector3D& v) const { return Vector3D(x-v.x, y-v.y, z-v.z); }
Vector3D Vector3D::operator*(double scalar) const { return Vector3D(x*scalar, y*scalar, z*scalar); }

Vector3D& Vector3D::operator+=(const Vector3D& v) { x+=v.x; y+=v.y; z+=v.z; return *this; }
Vector3D& Vector3D::operator-=(const Vector3D& v) { x-=v.x; y-=v.y; z-=v.z; return *this; }
Vector3D& Vector3D::operator*=(double scalar) { x*=scalar; y*=scalar; z*=scalar; return *this; }
Vector3D Vector3D::operator-() const {return Vector3D(-x, -y, -z);}
Vector3D operator*(double scalar, const Vector3D& v) {return Vector3D(v.x * scalar, v.y * scalar, v.z * scalar);}

double& Vector3D::operator[](int i){return *(&x + i);}
double Vector3D::operator[](int i) const {return *(&x + i);}

// Skalarprodukt
double Vector3D::operator*(const Vector3D& v) const { return x*v.x + y*v.y + z*v.z; }
//double Vector3D::dot(const Vector3D& v) const { return x*v.x + y*v.y + z*v.z; }

// Kreuzprodukt
Vector3D Vector3D::cross(const Vector3D& v) const {
    return Vector3D(
        y*v.z - z*v.y,
        z*v.x - x*v.z,
        x*v.y - y*v.x
    );
}

// length / Länge
double Vector3D::length() const { return std::sqrt(x*x + y*y + z*z); }
double Vector3D::lengthSquared() const { return x*x + y*y + z*z; }

// Normieren (neuer Vektor)
Vector3D Vector3D::normalized() const {
    double mag = length();
    return mag == 0 ? Vector3D(0,0,0) : (*this)*(1.0/mag);
}

// Normieren (ändert den Vektor selbst)
void Vector3D::normalize() {
    double mag = length();
    if(mag != 0) { x/=mag; y/=mag; z/=mag; }
}

// Abstand
double Vector3D::distance(const Vector3D& v) const {
    return (*this - v).length();
}

// Lineare Interpolation
Vector3D Vector3D::lerp(const Vector3D& v, double t) const {
    return (*this)*(1.0-t) + v*t;
}

Vector3D Vector3D::minVec(const Vector3D& a, const Vector3D& b)
{
    return Vector3D(
        std::min(a.x, b.x),
        std::min(a.y, b.y),
        std::min(a.z, b.z)
    );
}

Vector3D Vector3D::maxVec(const Vector3D& a, const Vector3D& b)
{
    return Vector3D(
        std::max(a.x, b.x),
        std::max(a.y, b.y),
        std::max(a.z, b.z)
    );
}

