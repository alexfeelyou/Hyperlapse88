#pragma once

#include <imgui.h>
#include "PostProcessEffect.h"

class TemporalAAEffect final : public PostProcessEffect
{
public:
    struct Data
    {
        bool  enabled{ false };
        float historyWeight{ 0.90f };
        float sharpenAmount{ 0.025f };

        [[nodiscard]] bool operator==(const Data& other) const noexcept
        {
            auto isEqual = [](float a, float b) noexcept { return std::abs(a - b) < 0.0001f; };
            return enabled == other.enabled &&
                isEqual(historyWeight, other.historyWeight) &&
                isEqual(sharpenAmount, other.sharpenAmount);
        }
    };

    explicit TemporalAAEffect(ID3D11Device* device);
    ~TemporalAAEffect() override = default;

    [[nodiscard]] bool IsEnabled() const noexcept override { return m_data.enabled; }
    void Draw(ID3D11DeviceContext* dc, ID3D11ShaderResourceView* srcSRV) override;
    void DrawGUI() noexcept override;
    [[nodiscard]] std::string_view GetName() const noexcept override { return "Temporal AA"; }
    void Serialize(nlohmann::json& out) const override;
    void Deserialize(const nlohmann::json& in) override;
    void ResetToDefault() noexcept override;
    void OnResize(ID3D11Device* device, int width, int height) noexcept override;

    [[nodiscard]] Data& GetData() noexcept { return m_data; }
    [[nodiscard]] const Data& GetData() const noexcept { return m_data; }

    // Forces the next Draw() to skip history blending entirely — call after scene loads or camera cuts,
    // otherwise one frame of stale/garbage history briefly ghosts into the new scene.
    void InvalidateHistory() noexcept { m_historyValid = false; }

    [[nodiscard]] ID3D11RenderTargetView* GetWriteRTV() const noexcept { return m_history[m_writeIndex].rtv.Get(); }
    [[nodiscard]] ID3D11ShaderResourceView* GetWriteSRV() const noexcept { return m_history[m_writeIndex].srv.Get(); }
    void SwapHistoryBuffers() noexcept { m_writeIndex = 1 - m_writeIndex; }

private:
    struct alignas(16) CbTemporalAA
    {
        float historyWeight{ 0.90f };
        float sharpenAmount{ 0.025f };
        DirectX::XMFLOAT2 texelSize{ 1.0f / 1920.0f, 1.0f / 1080.0f };
    };

    struct HistoryBuffer
    {
        Microsoft::WRL::ComPtr<ID3D11Texture2D>          texture{};
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView>   rtv{};
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv{};

        void Reset() noexcept { srv.Reset(); rtv.Reset(); texture.Reset(); }
    };

    void CreateHistoryBuffers(ID3D11Device* device, int width, int height);

    [[nodiscard]] ID3D11ShaderResourceView* GetReadSRV() const noexcept { return m_history[1 - m_writeIndex].srv.Get(); }

    Data m_data{};
    Data m_currentData{};

    std::array<HistoryBuffer, 2> m_history{};
    int  m_writeIndex{ 0 };
    bool m_historyValid{ false };

    Microsoft::WRL::ComPtr<ID3D11PixelShader>  m_pixelShader{};
    Microsoft::WRL::ComPtr<ID3D11Buffer>       m_constantBuffer{};
    Microsoft::WRL::ComPtr<ID3D11SamplerState> m_linearSampler{};
    int m_width{ 1920 };
    int m_height{ 1080 };
};