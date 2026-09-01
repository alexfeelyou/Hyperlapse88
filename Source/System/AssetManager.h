#pragma once

#include <d3d11.h>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

#include "System/Model.h"

namespace Engine::System
{
    // Centralized cache for heavy GPU resources to prevent VRAM duplication
    class AssetManager
    {
    public:
        // Delete copy/move constructors to enforce strict singleton ownership
        AssetManager(const AssetManager&) = delete;
        AssetManager& operator=(const AssetManager&) = delete;
        AssetManager(AssetManager&&) = delete;
        AssetManager& operator=(AssetManager&&) = delete;

        // Returns a reference to the static local instance
        [[nodiscard]] static AssetManager& Instance() noexcept;

        // Loads a model from disk or returns the cached instance if already loaded
        [[nodiscard]] std::shared_ptr<Model> GetOrLoadModel(ID3D11Device* device, std::string_view filepath);

        // Clears all unreferenced assets from the cache
        void UnloadUnusedAssets() noexcept;

        // Completely flushes the cache
        void Clear() noexcept;

    private:
        AssetManager() = default;
        ~AssetManager() = default;

        // Cache mapping file paths to loaded Model instances
        std::unordered_map<std::string, std::shared_ptr<Model>> m_modelCache{};
    };
}