#pragma once
#include "material.h"

enum class MaterialType
{
    Mirror,
    Gold,
    BluePlastic,
    Glass,
    GreenGlass,
    Wall,
    Simple,
    Wasser
};

class MaterialLibrary
{
public:
    static Material get(MaterialType type);

private:
    static const Material MIRROR;
    static const Material GOLD;
    static const Material BLUE_PLASTIC;
    static const Material GLASS;
    static const Material GREEN_GLASS;
    static const Material WALL;
    static const Material SIMPLE;
    static const Material WASSER;
};