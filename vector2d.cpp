#include "vector2d.h"

// Konstruktoren
Vector2D::Vector2D() : x(0), y(0) {}
Vector2D::Vector2D(double x_, double y_) : x(x_), y(y_) {}

// Operatoren
Vector2D Vector2D::operator+(const Vector2D& v) const {
    return Vector2D(x + v.x, y + v.y);
}

Vector2D Vector2D::operator-(const Vector2D& v) const {
    return Vector2D(x - v.x, y - v.y);
}

Vector2D Vector2D::operator*(double scalar) const {
    return Vector2D(x * scalar, y * scalar);
}

Vector2D& Vector2D::operator+=(const Vector2D& v) {
    x += v.x; y += v.y;
    return *this;
}

Vector2D& Vector2D::operator-=(const Vector2D& v) {
    x -= v.x; y -= v.y;
    return *this;
}

Vector2D& Vector2D::operator*=(double scalar) {
    x *= scalar; y *= scalar;
    return *this;
}

Vector2D Vector2D::operator-() const {
    return Vector2D(-x, -y);
}

Vector2D operator*(double scalar, const Vector2D& v) {
    return Vector2D(v.x * scalar, v.y * scalar);
}

double& Vector2D::operator[](int i) {
    return *(&x + i);
}

double Vector2D::operator[](int i) const {
    return *(&x + i);
}

// Skalarprodukt
double Vector2D::operator*(const Vector2D& v) const {
    return x * v.x + y * v.y;
}

// Länge
double Vector2D::length() const {
    return std::sqrt(x * x + y * y);
}

double Vector2D::lengthSquared() const {
    return x * x + y * y;
}

// Normalisierung
Vector2D Vector2D::normalized() const {
    double mag = length();
    return mag == 0 ? Vector2D(0, 0) : (*this) * (1.0 / mag);
}

void Vector2D::normalize() {
    double mag = length();
    if (mag != 0) {
        x /= mag;
        y /= mag;
    }
}

// Abstand
double Vector2D::distance(const Vector2D& v) const {
    return (*this - v).length();
}