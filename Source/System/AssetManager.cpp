#include <filesystem>
#include "System/GpuResourceUtils.h"
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

    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> AssetManager::GetOrLoadTexture(ID3D11Device* device, std::string_view filepath)
    {
        if (filepath.empty())
        {
            return nullptr;
        }

        // Normalize directory separators to guarantee identical cache hits across relative/absolute paths
        const std::string path{ std::filesystem::path{ filepath }.lexically_normal().string() };

        // Check if the texture is already loaded and cached in VRAM
        if (const auto it{ m_textureCache.find(path) }; it != m_textureCache.end())
        {
            return it->second;
        }

        // Verify file exists on disk before attempting to decode
        if (!std::filesystem::exists(path))
        {
            Log::Error("AssetManager: Failed to find texture at path: " + path);
            return nullptr;
        }

        // Allocate and decode the GPU texture resource
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv{};
        const HRESULT hr{ GpuResourceUtils::LoadTexture(device, path.c_str(), srv.GetAddressOf()) };
        if (FAILED(hr) || !srv)
        {
            Log::Error("AssetManager: Failed to decode texture at path: " + path);
            return nullptr;
        }

        // Cache the newly loaded GPU resource and return it
        m_textureCache.emplace(path, srv);
        return srv;
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

        for (auto it = m_textureCache.begin(); it != m_textureCache.end(); )
        {
            if (it->second)
            {
                // Query COM ref count: if exactly 1, only the AssetManager cache owns this resource
                it->second->AddRef();
                if (it->second->Release() <= 1)
                {
                    it = m_textureCache.erase(it);
                    continue;
                }
            }
            ++it;
        }
    }

    void AssetManager::Clear() noexcept
    {
        m_modelCache.clear();
        m_textureCache.clear();
    }
}