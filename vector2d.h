#pragma once
#include <cmath>

class Vector2D {
public:
    double x, y;

    // Konstruktoren
    Vector2D();
    Vector2D(double x_, double y_);

    // Operatoren
    Vector2D operator+(const Vector2D& v) const;
    Vector2D operator-(const Vector2D& v) const;
    Vector2D operator*(double scalar) const;

    Vector2D& operator+=(const Vector2D& v);
    Vector2D& operator-=(const Vector2D& v);
    Vector2D& operator*=(double scalar);

    Vector2D operator-() const;

    double& operator[](int i);
    double operator[](int i) const;

    // Skalarprodukt
    double operator*(const Vector2D& v) const;

    // Länge
    double length() const;
    double lengthSquared() const;

    // Normalisierung
    Vector2D normalized() const;
    void normalize();

    // Abstand
    double distance(const Vector2D& v) const;
};

// Skalar * Vektor
Vector2D operator*(double scalar, const Vector2D& v);

