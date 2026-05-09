#include "SceneSaver.h"
#include "SceneLoader.h"
#include "scene.h"
#include "camera.h"
#include "light.h"
#include "kugel.h"
#include "stab.h"
#include "wand.h"
#include "mesh.h"
#include "material_library.h"

#include <fstream>
#include <iostream>

extern std::vector<std::unique_ptr<object>> scene;
extern std::vector<light> lights;
extern std::vector<Camera> cam;
extern std::unordered_map<std::string, Material> materialMap;

void SceneSaver::saveSceneToXML(const std::string& filename)
{
    std::cout << "Speichere: " << filename << std::endl;
    std::ofstream out(filename);
    if (!out)
        throw std::runtime_error("Cannot open file for writing");

    out << "<scene>\n\n";

    std::cout << "- Kamera..." << std::endl;
    saveCamera(out);
    std::cout << "- Lichter..." << std::endl;
    saveLights(out);
    std::cout << "- Material..." << std::endl;
    saveMaterials(out);
    std::cout << "- Objekte..." << std::endl;
    saveObjects(out);

    out << "\n</scene>\n";

    std::cout << "Scene saved to: " << filename << "\n";
}

//////////////////////////////////////////////////////////////
// CAMERA
//////////////////////////////////////////////////////////////
void SceneSaver::saveCamera(std::ostream& out)
{
    if (cam.empty()) return;

    const Camera& c = cam[0];

    out << "    <camera>\n";

    out << "        <position x=\"" << c.position.x
        << "\" y=\"" << c.position.y
        << "\" z=\"" << c.position.z << "\"/>\n";

    out << "        <target x=\"" << c.lookAt.x
        << "\" y=\"" << c.lookAt.y
        << "\" z=\"" << c.lookAt.z << "\"/>\n";

    out << "        <fov>" << c.fov << "</fov>\n";
    out << "        <aspect>" << c.aspectRatio << "</aspect>\n";

    out << "    </camera>\n\n";
}

//////////////////////////////////////////////////////////////
// LIGHTS
//////////////////////////////////////////////////////////////
void SceneSaver::saveLights(std::ostream& out)
{
    out << "    <lights>\n";

    for (const auto& l : lights)
    {
        out << "        <light>\n";

        out << "            <position x=\"" << l.position.x
            << "\" y=\"" << l.position.y
            << "\" z=\"" << l.position.z << "\"/>\n";

        out << "            <color r=\"" << l.farbe.r
            << "\" g=\"" << l.farbe.g
            << "\" b=\"" << l.farbe.b << "\"/>\n";

        out << "        </light>\n\n";
    }

    out << "    </lights>\n\n";
}

//////////////////////////////////////////////////////////////
// MATERIALS
//////////////////////////////////////////////////////////////
void SceneSaver::saveMaterials(std::ostream& out)
{
    out << "    <materials>\n\n";

    for (const auto& [id, mat] : SceneLoader::materialMap)
    {
        out << "        <material id=\"" << id << "\"/>\n";
    }

    out << "    </materials>\n\n";
}

//////////////////////////////////////////////////////////////
// OBJECTS
//////////////////////////////////////////////////////////////
void SceneSaver::saveObjects(std::ostream& out)
{
    out << "    <objects>\n\n";

    std::cout << "-- Kugeln..." << std::endl;
    for (const auto& obj : scene)
    {
        ////////////////////////////////////////////////////////
        // SPHERE
        ////////////////////////////////////////////////////////
        if (auto s = dynamic_cast<Kugel*>(obj.get()))
        {
            out << "        <SPHERE material=\"" << s->materialName
                << "\" radius=\"" << s->radius << "\"";

            if (s->texture)
            {
                out << " texture=\"" << s->textureName << "\"";
            }
            
            if ((int)s->textureMode)
            {
                out << " textureMode=\"" << textureModeToString((int)s->textureMode) << "\"";
            }

            out << ">\n";

            out << "            <position x=\"" << s->position.x
                << "\" y=\"" << s->position.y
                << "\" z=\"" << s->position.z << "\"/>\n";

            out << "        </SPHERE>\n\n";
        }

        ////////////////////////////////////////////////////////
        // CYLINDER
        ////////////////////////////////////////////////////////
        else if (auto c = dynamic_cast<Stab*>(obj.get()))
        {
            std::cout << "-- Zylinder..." << std::endl;
            out << "        <cylinder material=\"" << c->materialName
                << "\" radius=\"" << c->radius << "\"";

            if (c->texture)
            {
                out << " texture=\"" << c->textureName << "\"";
            }

            if ((int)c->textureMode)
            {
                out << " textureMode=\"" << textureModeToString((int)c->textureMode) << "\"";
            }

            out << ">\n";

            out << "            <start x=\"" << c->position.x
                << "\" y=\"" << c->position.y
                << "\" z=\"" << c->position.z << "\"/>\n";

            out << "            <end x=\"" << c->p2.x
                << "\" y=\"" << c->p2.y
                << "\" z=\"" << c->p2.z << "\"/>\n";

            out << "        </cylinder>\n\n";
        }

        ////////////////////////////////////////////////////////
        // WALL
        ////////////////////////////////////////////////////////
        /*else if (auto w = dynamic_cast<Wand*>(obj.get()))
        {
            std::cout << "-- Wall..." << std::endl;
            out << "        <wall material=\"" << w->texture << "\">\n";

            for (int i = 0; i < 4; i++)
            {
                out << "            <p x=\"" << w->t1[i].x
                    << "\" y=\"" << w->t1[i].y
                    << "\" z=\"" << w->t1[i].z << "\"/>\n";
            }

            out << "        </wall>\n\n";
        }*/

        ////////////////////////////////////////////////////////
        // MESH
        ////////////////////////////////////////////////////////
        else if (auto m = dynamic_cast<Mesh*>(obj.get()))
        {
            std::cout << "-- Mesh..." << std::endl;
            if (m->materialName != "")
            {
                std::cout << "--- Name..." << m->materialName << std::endl;
                out << "        <mesh material=\"bluePlastic\" file=\"" << m->materialName << "\"";
                if (m->textureName != "")
                    std::cout << "--- textureName..." << m->textureName << std::endl;
                    out << " texture=\"" << m->textureName << "\"";
                if ((int)m->textureMode)
                    std::cout << "--- textureMode..." << textureModeToString((int)m->textureMode) << std::endl;
                     out << " textureMode=\"" << textureModeToString((int)m->textureMode) << "\"";
                out << "/>\n\n";
            }
        }

        ////////////////////////////////////////////////////////
        // OTHER
        ////////////////////////////////////////////////////////
        else
        {
            std::cout << "-- unknown object..." << std::endl;   
            // fallback (optional)
            std::cout << "Unknown object type during save\n";
        }
    }

    out << "    </objects>\n";
}

//////////////////////////////////////////////////////////////
// TEXTURE MODE
//////////////////////////////////////////////////////////////
std::string SceneSaver::textureModeToString(int mode)
{
    switch (mode)
    {
        case 1: return "SPHERE";
        case 2: return "PLANAR";
        case 3: return "UV";
        default: return "None";
    }
}