#pragma once
#include <string>
#include "material.h"
#include "material_library.h"
#include <unordered_map>
#include "TextureManager.h"

class Fenster;
struct XMLNode;

class SceneLoader
{
public:
    static void loadScene(const std::string& path);

    static MaterialType stringToMaterialType(const std::string& s);
    static std::string loadFile(const std::string& path);
    static void loadSceneFromXML(const std::string& path, Fenster& f);
    static void loadMaterials(XMLNode * materialsNode);
    static std::unordered_map<std::string, Material> materialMap;
    static XMLNode * findNode(XMLNode * root, const std::string & name);
    static TextureMode parseTextureMode(const std::string& m);
};



