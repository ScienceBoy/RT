#pragma once
#include <string>

class SceneSaver
{
public:
    static void saveSceneToXML(const std::string& filename);

private:
    static void saveCamera(std::ostream& out);
    static void saveLights(std::ostream& out);
    static void saveMaterials(std::ostream& out);
    static void saveObjects(std::ostream& out);

    static std::string textureModeToString(int mode);
};