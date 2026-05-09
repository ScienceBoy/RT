#include "TextureManager.h"
#include <memory>
#include <string>
#include "texture.h"
#include <iostream>
#include <fstream>

// Singleton Zugriff
TextureManager& TextureManager::get()
{
    static TextureManager instance;
    return instance;
}


// helper: trim + remove quotes
static void clean(std::string& s)
{
    // trim spaces
    while (!s.empty() && std::isspace((unsigned char)s.front()))
        s.erase(0, 1);

    while (!s.empty() && std::isspace((unsigned char)s.back()))
        s.pop_back();

    // remove quotes
    if (!s.empty() && s.front() == '"') s.erase(0, 1);
    if (!s.empty() && s.back() == '"') s.pop_back();
}


// Texture laden oder zurückgeben
std::shared_ptr<Texture> TextureManager::load(const std::string& filename)
{
    std::string name = filename.substr(filename.find_last_of("/\\") + 1);

    auto it = textures.find(name);
    if (it != textures.end())
        return it->second;

    auto tex = std::make_shared<Texture>();

    if (!tex->loadBMP(filename))
    {
        std::cerr << "[TextureManager] Failed to load: " << filename << std::endl;
        return nullptr;
    }

    textures[name] = tex;
    return tex;
}


// Zugriff auf Texture (lazy loading)
std::shared_ptr<Texture> TextureManager::getTexture(const std::string& name)
{
    // already loaded
    auto it = textures.find(name);
    if (it != textures.end())
        return it->second;

    // path lookup
    auto pathIt = texturePaths.find(name);

    if (pathIt == texturePaths.end())
    {
        std::cerr << "[TextureManager] Missing texture: " << name << std::endl;
        return nullptr;
    }

    std::string path = pathIt->second;
    clean(path);

    std::cout << "[TextureManager] Loading texture: "
              << name << " from " << path << std::endl;

    auto tex = std::make_shared<Texture>();

    if (!tex->loadBMP(path))
    {
        std::cerr << "[TextureManager] Failed to load: " << path << std::endl;
        return nullptr;
    }

    textures[name] = tex;
    return tex;
}


// load texture list
bool TextureManager::loadTextureList(const std::string& filename)
{
    std::ifstream file(filename);

    if (!file)
    {
        std::cout << "Konnte Texturen-Datei nicht laden" << std::endl;
        return false;
    }

    std::cout << "\n[TextureManager] Lade Texturen-Datei:\n";

    std::string line;

    while (std::getline(file, line))
    {
        if (line.empty()) continue;

        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string name = line.substr(0, eq);
        std::string path = line.substr(eq + 1);

        clean(name);
        clean(path);

        TextureManager::get().texturePaths[name] = path;

        std::cout << "Name: " << name << "\n";
    }

    return true;
}

