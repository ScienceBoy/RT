#pragma once

#include <unordered_map>
#include <memory>
#include <string>
#include "texture.h"
#include <fstream>
#include <algorithm>
#include <cctype>
#include "TextureMode.h"

class TextureManager
{
public:
    // Singleton Zugriff
    static TextureManager& get();

    // Lädt Textur oder gibt bereits geladene zurück
    std::shared_ptr<Texture> load(const std::string& filename);

    // Zugriff auf bereits geladene Textur
    std::shared_ptr<Texture> getTexture(const std::string& name);

    static bool loadTextureList(const std::string &filename);

private:
    std::unordered_map<std::string, std::shared_ptr<Texture>> textures;
    std::unordered_map<std::string, std::string> texturePaths;
};

static inline void trim(std::string& s)
{
    s.erase(s.begin(),
        std::find_if(s.begin(), s.end(),
            [](unsigned char c){ return !std::isspace(c); }));

    s.erase(
        std::find_if(s.rbegin(), s.rend(),
            [](unsigned char c){ return !std::isspace(c); }).base(),
        s.end());
}