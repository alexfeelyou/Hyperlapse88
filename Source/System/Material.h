#pragma once

#include <cereal/cereal.hpp>
#include <d3d11.h>
#include <DirectXMath.h>
#include <string>
#include <wrl/client.h>

enum class AlphaMode : std::uint8_t
{
    Opaque = 0,
    Mask,
    Blend
};

class Material
{
public:
    Material() = default;
    ~Material() = default;

    // Delete copy semantics to prevent accidental duplication of COM pointer ownership
    Material(const Material&) = delete;
    Material& operator=(const Material&) = delete;
    Material(Material&&) = default;
    Material& operator=(Material&&) = default;

    // Renders the ImGui UI for this specific material
    void DrawInspector() noexcept;

    std::string name{ "DefaultMaterial" };

    // Shading & Pipeline 
    // Maps to ShaderId (0 = Basic, 1 = Lambert, 2 = Phong, 3 = PBR)
    int shaderId{ 2 };
    AlphaMode alphaMode{ AlphaMode::Opaque };
    float alphaCutoff{ 0.5f };

    // Core PBR / Color Data 
    DirectX::XMFLOAT4 baseColor{ 1.0f, 1.0f, 1.0f, 1.0f };
    DirectX::XMFLOAT3 emissiveColor{ 0.0f, 0.0f, 0.0f };
    float metalness{ 0.0f };
    float roughness{ 0.5f };
    float occlusionStrength{ 1.0f };

    // Texture Paths (For Serialization) 
    std::string baseTextureFileName{};
    std::string normalTextureFileName{};
    std::string emissiveTextureFileName{};
    std::string occlusionTextureFileName{};
    std::string metalnessRoughnessTextureFileName{};

    // Hardware Resources 
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> baseMap{};
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> normalMap{};
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> emissiveMap{};
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> occlusionMap{};
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> metalnessRoughnessMap{};

    // Serialization support
    template<class Archive>
    void serialize(Archive& archive)
    {
        archive(
            cereal::make_nvp("name", name),
            cereal::make_nvp("baseTextureFileName", baseTextureFileName),
            cereal::make_nvp("normalTextureFileName", normalTextureFileName),
            cereal::make_nvp("emissiveTextureFileName", emissiveTextureFileName),
            cereal::make_nvp("occlusionTextureFileName", occlusionTextureFileName),
            cereal::make_nvp("metalnessRoughnessTextureFileName", metalnessRoughnessTextureFileName),
            cereal::make_nvp("baseColor", baseColor),
            cereal::make_nvp("emissiveColor", emissiveColor),
            cereal::make_nvp("metalness", metalness),
            cereal::make_nvp("roughness", roughness),
            cereal::make_nvp("occlusionStrength", occlusionStrength),
            cereal::make_nvp("alphaCutoff", alphaCutoff),
            cereal::make_nvp("alphaMode", alphaMode)
        );
    }
};