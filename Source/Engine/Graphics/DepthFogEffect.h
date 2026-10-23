#pragma once

#include <DirectXMath.h>
#include "PostProcessEffect.h"

class DepthFogEffect final : public PostProcessEffect
{
public:
    struct Data
    {
        bool              enabled{ false };
        bool              excludeSkybox{ true };
        DirectX::XMFLOAT4 color{ 0.55f, 0.72f, 0.90f, 1.0f };
        DirectX::XMFLOAT4 sunInscatterColor{ 1.0f, 0.78f, 0.42f, 1.0f };
        float             startDistance{ 10.0f };
        float             endDistance{ 120.0f };
        float             sunInscatterIntensity{ 2.5f };
        float             sunInscatterExponent{ 8.0f };
        float             heightFalloff{ 0.015f };
        float             heightOffset{ 0.0f };

        [[nodiscard]] bool operator==(const Data& other) const noexcept
        {
            auto isEqual = [](float a, float b) noexcept { return std::abs(a - b) < 0.0001f; };
            return enabled == other.enabled &&
                excludeSkybox == other.excludeSkybox &&
                isEqual(color.x, other.color.x) && isEqual(color.y, other.color.y) &&
                isEqual(color.z, other.color.z) && isEqual(color.w, other.color.w) &&
                isEqual(sunInscatterColor.x, other.sunInscatterColor.x) &&
                isEqual(sunInscatterColor.y, other.sunInscatterColor.y) &&
                isEqual(sunInscatterColor.z, other.sunInscatterColor.z) &&
                isEqual(sunInscatterColor.w, other.sunInscatterColor.w) &&
                isEqual(startDistance, other.startDistance) &&
                isEqual(endDistance, other.endDistance) &&
                isEqual(sunInscatterIntensity, other.sunInscatterIntensity) &&
                isEqual(sunInscatterExponent, other.sunInscatterExponent) &&
                isEqual(heightFalloff, other.heightFalloff) &&
                isEqual(heightOffset, other.heightOffset);
        }
    };

    explicit DepthFogEffect(ID3D11Device* device);
    ~DepthFogEffect() override = default;

    [[nodiscard]] bool IsEnabled() const noexcept override { return m_data.enabled; }
    void Draw(ID3D11DeviceContext* dc, ID3D11ShaderResourceView* srcSRV) override;
    void DrawGUI() noexcept override;
    [[nodiscard]] std::string_view GetName() const noexcept override { return "Depth Fog"; }

    [[nodiscard]] Data& GetData() noexcept { return m_data; }
    [[nodiscard]] const Data& GetData() const noexcept { return m_data; }

    void Serialize(nlohmann::json& out) const override;
    void Deserialize(const nlohmann::json& in) override;
    void ResetToDefault() noexcept override;

private:
    struct alignas(16) CbFog
    {
        DirectX::XMFLOAT4X4 inverseViewProjection{};
        DirectX::XMFLOAT4   cameraPosition{ 0.0f, 0.0f, 0.0f, 1.0f };
        DirectX::XMFLOAT4   fogColor{ 0.55f, 0.72f, 0.90f, 1.0f };
        DirectX::XMFLOAT4   sunInscatterColor{ 1.0f, 0.78f, 0.42f, 2.5f };
        DirectX::XMFLOAT4   sunDirectionAndPhase{ 0.0f, 0.707f, 0.707f, 8.0f };
        float               fogStart{ 10.0f };
        float               fogEnd{ 120.0f };
        float               heightFalloff{ 0.015f };
        float               heightOffset{ 0.0f };
        int                 excludeSkybox{ 1 };
        float               padding[3]{ 0.0f, 0.0f, 0.0f };
    };
    static_assert((sizeof(CbFog) % 16) == 0, "CbFog constant buffer must be 16-byte aligned!");

    Data  m_data{};
    CbFog m_cachedCb{};
    bool  m_hasCachedCb{ false };

    Microsoft::WRL::ComPtr<ID3D11PixelShader>   m_pixelShader{};
    Microsoft::WRL::ComPtr<ID3D11Buffer>        m_constantBuffer{};
    Microsoft::WRL::ComPtr<ID3D11SamplerState>  m_pointSampler{};
};