#include "ResourceManager.h"

void ResourceManager::LoadFont(const std::string& keyName, const std::string& texturePath, const std::string& dataPath)
{
    if (fonts.find(keyName) != fonts.end()) return;
    fonts[keyName] = std::make_unique<BitmapFont>(texturePath, dataPath);
}

BitmapFont* ResourceManager::GetFont(const std::string& keyName)
{
    auto it = fonts.find(keyName);

    if (it != fonts.end())
    {
        return it->second.get();
    }

    return nullptr;
}

void ResourceManager::UnloadAll()
{
    fonts.clear();
}