#pragma once
#include <vector>
#include "object.h"
#include "camera.h"

object* pickObject(
    int mouseX,
    int mouseY,
    int w,
    int h,
    Camera* camera,
    std::vector<object*>& objects
);

bool intersect(const Ray& ray, const Vector3D& planePoint, const Vector3D& planeNormal, double& t);