#pragma once

#include <imgui.h>
#include "PostProcessEffect.h"

// PSX / Retro Effect (Chunky Pixels + 4x4 Bayer Dithering)
class PSXEffect final : public PostProcessEffect
{
public:
    struct Data
    {
        bool  enabled{ false };
        float resWidth{ 320.0f };
        float resHeight{ 240.0f };
        float colorDepth{ 32.0f };
        float ditherStrength{ 1.0f };

        [[nodiscard]] bool operator==(const Data& other) const noexcept
        {
            auto isEqual = [](float a, float b) noexcept { return std::abs(a - b) < 0.0001f; };
            return enabled == other.enabled &&
                isEqual(resWidth, other.resWidth) &&
                isEqual(resHeight, other.resHeight) &&
                isEqual(colorDepth, other.colorDepth) &&
                isEqual(ditherStrength, other.ditherStrength);
        }
    };

    explicit PSXEffect(ID3D11Device* device);
    ~PSXEffect() override = default;

    [[nodiscard]] bool IsEnabled() const noexcept override { return m_data.enabled; }
    void Draw(ID3D11DeviceContext* dc, ID3D11ShaderResourceView* srcSRV) override;
    void DrawGUI() noexcept override;
    [[nodiscard]] std::string_view GetName() const noexcept override { return "PSX Retro"; }

    [[nodiscard]] Data& GetData() noexcept { return m_data; }
    [[nodiscard]] const Data& GetData() const noexcept { return m_data; }

private:
    struct alignas(16) CbPSX
    {
        float resWidth{ 320.0f };
        float resHeight{ 240.0f };
        float colorDepth{ 32.0f };
        float ditherStrength{ 1.0f };
    };

    Data m_data{};
    Data m_currentData{};

    Microsoft::WRL::ComPtr<ID3D11PixelShader>   m_pixelShader{};
    Microsoft::WRL::ComPtr<ID3D11Buffer>        m_constantBuffer{};
    Microsoft::WRL::ComPtr<ID3D11SamplerState>  m_pointSampler{};
};

// Lens Distortion & Chromatic Aberration & Glitch Effect
class LensDistortionEffect final : public PostProcessEffect
{
public:
    struct Data
    {
        bool  enabled{ false };
        float distortion{ 0.0f };
        float chromaticAberration{ 0.0f };
        float glitchStrength{ 0.0f };
        float time{ 0.0f };
        DirectX::XMFLOAT2 center{ 0.5f, 0.5f };

        [[nodiscard]] bool operator==(const Data& other) const noexcept
        {
            auto isEqual = [](float a, float b) noexcept { return std::abs(a - b) < 0.0001f; };
            return enabled == other.enabled &&
                isEqual(distortion, other.distortion) &&
                isEqual(chromaticAberration, other.chromaticAberration) &&
                isEqual(glitchStrength, other.glitchStrength) &&
                isEqual(time, other.time) &&
                isEqual(center.x, other.center.x) &&
                isEqual(center.y, other.center.y);
        }
    };

    explicit LensDistortionEffect(ID3D11Device* device);
    ~LensDistortionEffect() override = default;

    [[nodiscard]] bool IsEnabled() const noexcept override
    {
        return m_data.enabled && (std::abs(m_data.distortion) > 0.0001f ||
            m_data.chromaticAberration > 0.0001f ||
            m_data.glitchStrength > 0.0001f);
    }

    void Draw(ID3D11DeviceContext* dc, ID3D11ShaderResourceView* srcSRV) override;
    void DrawGUI() noexcept override;
    [[nodiscard]] std::string_view GetName() const noexcept override { return "Lens & Distortion"; }

    [[nodiscard]] Data& GetData() noexcept { return m_data; }
    [[nodiscard]] const Data& GetData() const noexcept { return m_data; }

private:
    struct alignas(16) CbDistortion
    {
        DirectX::XMFLOAT2 center{ 0.5f, 0.5f };
        float distortion{ 0.0f };
        float chromaticAberration{ 0.0f };
        float glitchStrength{ 0.0f };
        float time{ 0.0f };
        float padding[2]{ 0.0f, 0.0f };
    };

    Data m_data{};
    Data m_currentData{};

    Microsoft::WRL::ComPtr<ID3D11PixelShader>   m_pixelShader{};
    Microsoft::WRL::ComPtr<ID3D11Buffer>        m_constantBuffer{};
    Microsoft::WRL::ComPtr<ID3D11SamplerState>  m_linearSampler{};
};

// Radial Blur Effect (Isolated 10-tap loop - Zero penalty when disabled)
class RadialBlurEffect final : public PostProcessEffect
{
public:
    struct Data
    {
        bool  enabled{ false };
        float blurStrength{ 0.0f };
        DirectX::XMFLOAT2 center{ 0.5f, 0.5f };

        [[nodiscard]] bool operator==(const Data& other) const noexcept
        {
            auto isEqual = [](float a, float b) noexcept { return std::abs(a - b) < 0.0001f; };
            return enabled == other.enabled &&
                isEqual(blurStrength, other.blurStrength) &&
                isEqual(center.x, other.center.x) &&
                isEqual(center.y, other.center.y);
        }
    };

    explicit RadialBlurEffect(ID3D11Device* device);
    ~RadialBlurEffect() override = default;

    [[nodiscard]] bool IsEnabled() const noexcept override
    {
        return m_data.enabled && m_data.blurStrength > 0.0001f;
    }

    void Draw(ID3D11DeviceContext* dc, ID3D11ShaderResourceView* srcSRV) override;
    void DrawGUI() noexcept override;
    [[nodiscard]] std::string_view GetName() const noexcept override { return "Radial Blur"; }

    [[nodiscard]] Data& GetData() noexcept { return m_data; }
    [[nodiscard]] const Data& GetData() const noexcept { return m_data; }

private:
    struct alignas(16) CbRadialBlur
    {
        DirectX::XMFLOAT2 center{ 0.5f, 0.5f };
        float blurStrength{ 0.0f };
        float padding{ 0.0f };
    };

    Data m_data{};
    Data m_currentData{};

    Microsoft::WRL::ComPtr<ID3D11PixelShader>   m_pixelShader{};
    Microsoft::WRL::ComPtr<ID3D11Buffer>        m_constantBuffer{};
    Microsoft::WRL::ComPtr<ID3D11SamplerState>  m_linearSampler{};
};

// Vignette Effect (Clean Smoothstep Darkening)
class VignetteEffect final : public PostProcessEffect
{
public:
    struct Data
    {
        bool              enabled{ true };
        DirectX::XMFLOAT4 color{ 0.0f, 0.0f, 0.0f, 1.0f };
        DirectX::XMFLOAT2 center{ 0.5f, 0.5f };
        float             intensity{ 0.38f };
        float             smoothness{ 0.2f };
        bool              rounded{ false };
        float             roundness{ 0.0f };

        [[nodiscard]] bool operator==(const Data& other) const noexcept
        {
            auto isEqual = [](float a, float b) noexcept { return std::abs(a - b) < 0.0001f; };
            return enabled == other.enabled &&
                isEqual(color.x, other.color.x) && isEqual(color.y, other.color.y) &&
                isEqual(color.z, other.color.z) && isEqual(color.w, other.color.w) &&
                isEqual(center.x, other.center.x) && isEqual(center.y, other.center.y) &&
                isEqual(intensity, other.intensity) &&
                isEqual(smoothness, other.smoothness) &&
                rounded == other.rounded &&
                isEqual(roundness, other.roundness);
        }
    };

    explicit VignetteEffect(ID3D11Device* device);
    ~VignetteEffect() override = default;

    [[nodiscard]] bool IsEnabled() const noexcept override
    {
        return m_data.enabled && m_data.intensity > 0.0001f;
    }

    void Draw(ID3D11DeviceContext* dc, ID3D11ShaderResourceView* srcSRV) override;
    void DrawGUI() noexcept override;
    [[nodiscard]] std::string_view GetName() const noexcept override { return "Vignette"; }

    [[nodiscard]] Data& GetData() noexcept { return m_data; }
    [[nodiscard]] const Data& GetData() const noexcept { return m_data; }

private:
    struct alignas(16) CbVignette
    {
        DirectX::XMFLOAT4 color{ 0.0f, 0.0f, 0.0f, 1.0f };
        DirectX::XMFLOAT2 center{ 0.5f, 0.5f };
        float intensity{ 0.38f };
        float smoothness{ 0.2f };
        float rounded{ 0.0f };
        float roundness{ 0.0f };
        float padding[2]{ 0.0f, 0.0f };
    };

    Data m_data{};
    Data m_currentData{};

    Microsoft::WRL::ComPtr<ID3D11PixelShader>   m_pixelShader{};
    Microsoft::WRL::ComPtr<ID3D11Buffer>        m_constantBuffer{};
    Microsoft::WRL::ComPtr<ID3D11SamplerState>  m_linearSampler{};
};

// CRT Scanlines & Mesh Effect
class ScanlineEffect final : public PostProcessEffect
{
public:
    struct Data
    {
        bool  enabled{ false };
        float scanlineStrength{ 0.0f };
        float scanlineSpeed{ 0.0f };
        float scanlineSize{ 0.0f };
        float fineOpacity{ 0.0f };
        float fineDensity{ 0.0f };
        float fineRotation{ 0.0f };
        float time{ 0.0f };

        [[nodiscard]] bool operator==(const Data& other) const noexcept
        {
            auto isEqual = [](float a, float b) noexcept { return std::abs(a - b) < 0.0001f; };
            return enabled == other.enabled &&
                isEqual(scanlineStrength, other.scanlineStrength) &&
                isEqual(scanlineSpeed, other.scanlineSpeed) &&
                isEqual(scanlineSize, other.scanlineSize) &&
                isEqual(fineOpacity, other.fineOpacity) &&
                isEqual(fineDensity, other.fineDensity) &&
                isEqual(fineRotation, other.fineRotation) &&
                isEqual(time, other.time);
        }
    };

    explicit ScanlineEffect(ID3D11Device* device);
    ~ScanlineEffect() override = default;

    [[nodiscard]] bool IsEnabled() const noexcept override
    {
        return m_data.enabled && (m_data.scanlineStrength > 0.0001f || m_data.fineOpacity > 0.0001f);
    }

    void Draw(ID3D11DeviceContext* dc, ID3D11ShaderResourceView* srcSRV) override;
    void DrawGUI() noexcept override;
    [[nodiscard]] std::string_view GetName() const noexcept override { return "CRT & Scanlines"; }

    [[nodiscard]] Data& GetData() noexcept { return m_data; }
    [[nodiscard]] const Data& GetData() const noexcept { return m_data; }

private:
    struct alignas(16) CbScanline
    {
        float scanlineStrength{ 0.0f };
        float scanlineSpeed{ 0.0f };
        float scanlineSize{ 0.0f };
        float fineOpacity{ 0.0f };
        float fineDensity{ 0.0f };
        float fineRotation{ 0.0f };
        float time{ 0.0f };
        float padding{ 0.0f };
    };

    Data m_data{};
    Data m_currentData{};

    Microsoft::WRL::ComPtr<ID3D11PixelShader>   m_pixelShader{};
    Microsoft::WRL::ComPtr<ID3D11Buffer>        m_constantBuffer{};
    Microsoft::WRL::ComPtr<ID3D11SamplerState>  m_linearSampler{};
};