#include <filesystem>
#include "System/Logger.h" 
#include "AssetManager.h"

namespace Engine::System
{
    AssetManager& AssetManager::Instance() noexcept
    {
        // Static local avoids static initialization order fiasco (SIOF)
        static AssetManager s_instance{};
        return s_instance;
    }

    std::shared_ptr<Model> AssetManager::GetOrLoadModel(ID3D11Device* device, std::string_view filepath)
    {
        // lookup boilerplate, so we initialize a string for the lookup.
        const std::string path{ filepath };

        // Check if the model is already loaded and cached
        if (const auto it{ m_modelCache.find(path) }; it != m_modelCache.end())
        {
            return it->second; // Return the existing shared_ptr
        }

        // Verify file exists before attempting to load (prevents DirectX/Importer crashes)
        if (!std::filesystem::exists(path))
        {
            Log::Error("AssetManager: Failed to find model at path: " + path);
            return nullptr;
        }

        // Instantiate the new model.
        auto model{ std::make_shared<Model>(device, path.c_str()) };

        // Cache the newly loaded model and return it
        m_modelCache.emplace(path, model);
        return model;
    }

    void AssetManager::UnloadUnusedAssets() noexcept
    {
        for (auto it = m_modelCache.begin(); it != m_modelCache.end(); )
        {
            if (it->second.use_count() == 1)
            {
                it = m_modelCache.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    void AssetManager::Clear() noexcept
    {
        m_modelCache.clear();
    }
}