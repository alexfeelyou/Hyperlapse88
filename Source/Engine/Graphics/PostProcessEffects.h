#pragma once

#include <array>
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
    void Serialize(nlohmann::json& out) const override;
    void Deserialize(const nlohmann::json& in) override;
    void ResetToDefault() noexcept override;

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
    void Serialize(nlohmann::json& out) const override;
    void Deserialize(const nlohmann::json& in) override;
    void ResetToDefault() noexcept override;

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
    void Serialize(nlohmann::json& out) const override;
    void Deserialize(const nlohmann::json& in) override;
    void ResetToDefault() noexcept override;

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
        bool              enabled{ false };
        DirectX::XMFLOAT4 color{ 0.0f, 0.0f, 0.0f, 1.0f };
        DirectX::XMFLOAT2 center{ 0.5f, 0.5f };
        float             intensity{ 0.0f };
        float             smoothness{ 0.0f };
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
    void Serialize(nlohmann::json& out) const override;
    void Deserialize(const nlohmann::json& in) override;
    void ResetToDefault() noexcept override;

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
    void Serialize(nlohmann::json& out) const override;
    void Deserialize(const nlohmann::json& in) override;
    void ResetToDefault() noexcept override;

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

// Multi-Scale 13-Tap Downsample & 9-Tap Tent Upsample HDR Bloom Effect
class BloomEffect final : public PostProcessEffect
{
public:
    static inline constexpr std::size_t kMipCount{ 5 };

    struct Data
    {
        bool              enabled{ true };
        float             threshold{ 1.0f };
        float             softKnee{ 0.5f };
        float             intensity{ 0.65f };
        float             scatter{ 0.75f };
        DirectX::XMFLOAT4 tint{ 1.0f, 0.94f, 0.82f, 1.0f };

        [[nodiscard]] bool operator==(const Data& other) const noexcept
        {
            auto isEqual = [](float a, float b) noexcept { return std::abs(a - b) < 0.0001f; };
            return enabled == other.enabled &&
                isEqual(threshold, other.threshold) &&
                isEqual(softKnee, other.softKnee) &&
                isEqual(intensity, other.intensity) &&
                isEqual(scatter, other.scatter) &&
                isEqual(tint.x, other.tint.x) &&
                isEqual(tint.y, other.tint.y) &&
                isEqual(tint.z, other.tint.z) &&
                isEqual(tint.w, other.tint.w);
        }
    };

    explicit BloomEffect(ID3D11Device* device);
    ~BloomEffect() override = default;

    [[nodiscard]] bool IsEnabled() const noexcept override
    {
        return m_data.enabled && m_data.intensity > 0.0001f;
    }

    void Draw(ID3D11DeviceContext* dc, ID3D11ShaderResourceView* srcSRV) override;
    void DrawGUI() noexcept override;
    [[nodiscard]] std::string_view GetName() const noexcept override { return "Bloom"; }

    [[nodiscard]] Data& GetData() noexcept { return m_data; }
    [[nodiscard]] const Data& GetData() const noexcept { return m_data; }

    void Serialize(nlohmann::json& out) const override;
    void Deserialize(const nlohmann::json& in) override;
    void ResetToDefault() noexcept override;
    void OnResize(ID3D11Device* device, int width, int height) noexcept override;

private:
    struct alignas(16) CbBloom
    {
        DirectX::XMFLOAT2 texelSize{ 1.0f / 1920.0f, 1.0f / 1080.0f };
        float             threshold{ 1.0f };
        float             softKnee{ 0.5f };
        DirectX::XMFLOAT4 bloomTintAndIntensity{ 1.0f, 0.94f, 0.82f, 0.65f };
        float             scatter{ 0.75f };
        int               passMode{ 0 };
        float             padding[2]{ 0.0f, 0.0f };
    };
    static_assert((sizeof(CbBloom) % 16) == 0, "CbBloom constant buffer must be 16-byte aligned!");

    struct MipLevel
    {
        Microsoft::WRL::ComPtr<ID3D11Texture2D>          texture{};
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView>   rtv{};
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv{};
        int                                              width{ 1 };
        int                                              height{ 1 };

        void Reset() noexcept
        {
            srv.Reset();
            rtv.Reset();
            texture.Reset();
            width = 1;
            height = 1;
        }
    };

    void CreateMipChain(ID3D11Device* device, int width, int height) noexcept;
    void UpdateConstantBuffer(ID3D11DeviceContext* dc, const CbBloom& cb) const noexcept;

    Data m_data{};
    std::array<MipLevel, kMipCount> m_mipChain{};
    int m_width{ 1920 };
    int m_height{ 1080 };

    Microsoft::WRL::ComPtr<ID3D11PixelShader>  m_pixelShader{};
    Microsoft::WRL::ComPtr<ID3D11Buffer>       m_constantBuffer{};
    Microsoft::WRL::ComPtr<ID3D11SamplerState> m_linearSampler{};
    Microsoft::WRL::ComPtr<ID3D11SamplerState> m_pointSampler{};
    Microsoft::WRL::ComPtr<ID3D11BlendState>   m_additiveBlendState{};
};

// Fused Final-Blit ACES Tonemapping, Parametric Watercolor Grade & Trilinear 3D LUT Effect
class ColorGradingEffect final : public PostProcessEffect
{
public:
    struct Data
    {
        bool              enabled{ true };
        float             exposure{ 0.15f };              // EV stops
        float             temperature{ 0.12f };           // -1.0 (Cool) to +1.0 (Warm Golden)
        float             tint{ 0.0f };                   // -1.0 (Green) to +1.0 (Magenta)
        float             contrast{ 1.06f };              // Perceptual midpoint contrast
        float             saturation{ 1.12f };            // Watercolor vibrancy
        DirectX::XMFLOAT4 shadowLiftColor{ 0.14f, 0.22f, 0.32f, 0.12f }; // rgb: Lifted shadow tint, w: Lift strength
        DirectX::XMFLOAT4 highlightGainColor{ 1.0f, 0.95f, 0.84f, 1.0f }; // rgb: Golden highlight tint, w: Gain multiplier
        float             lutIntensity{ 1.0f };
        std::string       lutFilePath{};

        [[nodiscard]] bool operator==(const Data& other) const noexcept
        {
            auto isEqual = [](float a, float b) noexcept { return std::abs(a - b) < 0.0001f; };
            return enabled == other.enabled &&
                isEqual(exposure, other.exposure) &&
                isEqual(temperature, other.temperature) &&
                isEqual(tint, other.tint) &&
                isEqual(contrast, other.contrast) &&
                isEqual(saturation, other.saturation) &&
                isEqual(shadowLiftColor.x, other.shadowLiftColor.x) &&
                isEqual(shadowLiftColor.y, other.shadowLiftColor.y) &&
                isEqual(shadowLiftColor.z, other.shadowLiftColor.z) &&
                isEqual(shadowLiftColor.w, other.shadowLiftColor.w) &&
                isEqual(highlightGainColor.x, other.highlightGainColor.x) &&
                isEqual(highlightGainColor.y, other.highlightGainColor.y) &&
                isEqual(highlightGainColor.z, other.highlightGainColor.z) &&
                isEqual(highlightGainColor.w, other.highlightGainColor.w) &&
                isEqual(lutIntensity, other.lutIntensity) &&
                lutFilePath == other.lutFilePath;
        }
    };

    explicit ColorGradingEffect(ID3D11Device* device);
    ~ColorGradingEffect() override = default;

    [[nodiscard]] bool IsEnabled() const noexcept override { return m_data.enabled; }

    // Executes the fused Tonemap + Color Grade + LUT + IGN Dither blit directly into the bound RTV
    void Draw(ID3D11DeviceContext* dc, ID3D11ShaderResourceView* srcSRV) override;
    void DrawGUI() noexcept override;
    [[nodiscard]] std::string_view GetName() const noexcept override { return "Color Grading"; }

    [[nodiscard]] Data& GetData() noexcept { return m_data; }
    [[nodiscard]] const Data& GetData() const noexcept { return m_data; }

    bool LoadLutTexture(ID3D11Device* device, std::string_view filepath) noexcept;
    void ClearLutTexture() noexcept;

    void Serialize(nlohmann::json& out) const override;
    void Deserialize(const nlohmann::json& in) override;
    void ResetToDefault() noexcept override;

private:
    struct alignas(16) CbColorGrading
    {
        DirectX::XMFLOAT4 exposureAndWhiteBalance{ 1.0f, 1.0f, 1.0f, 1.0f }; // rgb: 2^EV * WhiteBalance, w: contrast
        DirectX::XMFLOAT4 shadowLift{ 0.0f, 0.0f, 0.0f, 1.0f };              // rgb: Pre-linearized shadow lift, w: saturation
        DirectX::XMFLOAT4 highlightGain{ 1.0f, 1.0f, 1.0f, 0.0f };           // rgb: Pre-linearized highlight gain, w: unused
        DirectX::XMFLOAT4 lutParams{ 32.0f, 1.0f / 32.0f, 0.0f, 0.0f };      // x: lutSize, y: 1/lutSize, z: activeLutIntensity, w: padding
    };
    static_assert((sizeof(CbColorGrading) % 16) == 0, "CbColorGrading constant buffer must be 16-byte aligned!");

    Data           m_data{};
    CbColorGrading m_cachedCb{};
    bool           m_hasCachedCb{ false };
    float          m_lutSize{ 32.0f };

    Microsoft::WRL::ComPtr<ID3D11PixelShader>        m_pixelShader{};
    Microsoft::WRL::ComPtr<ID3D11Buffer>             m_constantBuffer{};
    Microsoft::WRL::ComPtr<ID3D11SamplerState>       m_pointSampler{};
    Microsoft::WRL::ComPtr<ID3D11SamplerState>       m_linearSampler{};
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_lutSRV{};
};