#pragma once
#include <string>
#include <unordered_map>
#include <memory>
#include "BitmapFont.h"
#include "System/Sprite.h"

// Singleton Resource Manager
class ResourceManager
{
public:
    // Singleton Access
    static ResourceManager& Instance() { static ResourceManager i; return i; }

    void LoadFont(const std::string& keyName, const std::string& texturePath, const std::string& dataPath);
    BitmapFont* GetFont(const std::string& keyName);

    void UnloadAll();

private:
    ResourceManager() = default;
    ~ResourceManager() = default;

    std::unordered_map<std::string, std::unique_ptr<BitmapFont>> fonts;

    // Nanti bisa ditambah:
    // std::unordered_map<std::string, std::unique_ptr<Sprite>> textures;
    // std::unordered_map<std::string, std::unique_ptr<Audio>> sounds;
};