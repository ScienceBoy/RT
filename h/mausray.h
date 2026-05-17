#pragma once
#include "vector3d.h"
#include "camera.h"

// Ray aus Maus erzeugen
Ray createRayFromMouse(int x, int y, int w, int h, Camera* camera);