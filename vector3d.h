#pragma once
#include <cmath>

class Vector3D {
public:
    double x, y, z;

    // Konstruktoren
    Vector3D();
    Vector3D(double x_, double y_, double z_);

    // Operatoren
    Vector3D operator+(const Vector3D& v) const;
    Vector3D operator-(const Vector3D& v) const;
    Vector3D operator*(double scalar) const;

    Vector3D& operator+=(const Vector3D& v);
    Vector3D& operator-=(const Vector3D& v);
    Vector3D& operator*=(double scalar);
    Vector3D operator-() const;
    double & operator[](int i);
    double operator[](int i) const;
    friend Vector3D operator*(double scalar, const Vector3D& v);

    // Skalarprodukt
    double operator*(const Vector3D& v) const;
    //double dot(const Vector3D& v) const;

    // Kreuzprodukt
    Vector3D cross(const Vector3D& v) const;

    // Betrag / Länge
    double length() const;

    // Normieren (unit vector)
    Vector3D normalized() const;

    // Abstand zwischen zwei Vektoren
    double distance(const Vector3D& v) const;

    // Zusätzliche nützliche Methoden
    void normalize();      // ändert den Vektor selbst
    double lengthSquared() const; // length^2 (oft schneller als sqrt)
    Vector3D lerp(const Vector3D& v, double t) const; // linear interpolation
    Vector3D minVec(const Vector3D & a, const Vector3D & b);
    Vector3D maxVec(const Vector3D & a, const Vector3D & b);

};

