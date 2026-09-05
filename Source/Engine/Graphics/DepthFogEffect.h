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
        DirectX::XMFLOAT4 color{ 0.15f, 0.02f, 0.25f, 1.0f };
        float             startDistance{ 10.0f };
        float             endDistance{ 80.0f };

        [[nodiscard]] bool operator==(const Data& other) const noexcept
        {
            auto isEqual = [](float a, float b) noexcept { return std::abs(a - b) < 0.0001f; };
            return enabled == other.enabled &&
                excludeSkybox == other.excludeSkybox && // NEW
                isEqual(color.x, other.color.x) && isEqual(color.y, other.color.y) &&
                isEqual(color.z, other.color.z) && isEqual(color.w, other.color.w) &&
                isEqual(startDistance, other.startDistance) &&
                isEqual(endDistance, other.endDistance);
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
        DirectX::XMFLOAT4 fogColor{ 0.15f, 0.02f, 0.25f, 1.0f };
        float fogStart{ 10.0f };
        float fogEnd{ 80.0f };
        float nearZ{ 0.1f };
        float farZ{ 1000.0f };
        int   excludeSkybox{ 1 };
        float padding[3]{ 0.0f, 0.0f, 0.0f };
    };

    Data m_data{};
    Data m_currentData{};
    float m_cachedNearZ{ -1.0f };
    float m_cachedFarZ{ -1.0f }; 

    Microsoft::WRL::ComPtr<ID3D11PixelShader>   m_pixelShader{};
    Microsoft::WRL::ComPtr<ID3D11Buffer>        m_constantBuffer{};
    Microsoft::WRL::ComPtr<ID3D11SamplerState>  m_pointSampler{};
};