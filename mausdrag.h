#pragma once
#include "vector3d.h"
#include "object.h"

struct DragState
{
    bool active = false;
    object* object = nullptr;

    Vector3D offset;
    Vector3D planeNormal;
    Vector3D planePoint;
};