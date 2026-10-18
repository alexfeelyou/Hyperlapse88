#pragma once

#include <algorithm>
#include <array>
#include <cassert>
#include <d3d11.h>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string_view>
#include <vector>
#include <wrl/client.h>
#include "System/Graphics.h"
#include "System/Logger.h"
#include "AtmosphereProfile.h"
#include "DepthFogEffect.h"
#include "PostProcessEffects.h"
#include "TemporalAAEffect.h"

class PostProcessManager
{
public:
    PostProcessManager();
    ~PostProcessManager() = default;

    // Delete copy/move semantics to enforce strict single-owner RAII
    PostProcessManager(const PostProcessManager&) = delete;
    PostProcessManager& operator=(const PostProcessManager&) = delete;
    PostProcessManager(PostProcessManager&&) noexcept = default;
    PostProcessManager& operator=(PostProcessManager&&) noexcept = default;

    void Initialize(int screenWidth, int screenHeight);
    void OnResize(int width, int height);

    // Redirects D3D11 rendering to the off-screen scene capture target
    void BeginCapture();

    // Runs the scheduled ping-pong post-process passes and outputs to the restored RTV
    void EndCapture(float dt);

    [[nodiscard]] bool IsEnabled() const noexcept { return m_isEnabled; }
    void SetEnabled(bool enable) noexcept { m_isEnabled = enable; }

    // Direct accessors to individual effect modules 
    [[nodiscard]] TemporalAAEffect& GetTemporalAA() noexcept { return *m_temporalAAEffect; }
    [[nodiscard]] ColorGradingEffect& GetColorGrading() noexcept { return *m_colorGradingEffect; }
    [[nodiscard]] SsaoEffect& GetSSAO() noexcept { return *m_ssaoEffect; }
    [[nodiscard]] SunShaftEffect& GetSunShafts() noexcept { return *m_sunShaftEffect; }
    [[nodiscard]] DepthFogEffect& GetDepthFog() noexcept { return *m_depthFogEffect; }
    [[nodiscard]] BloomEffect& GetBloom() noexcept { return *m_bloomEffect; }
    [[nodiscard]] PSXEffect& GetPSX() noexcept { return *m_psxEffect; }

    // Atmospheric Solar Harmonizer API
    [[nodiscard]] bool IsAtmosphereHarmonizeEnabled() const noexcept { return m_autoHarmonizeAtmosphere; }
    void SetAtmosphereHarmonize(bool enabled) noexcept { m_autoHarmonizeAtmosphere = enabled; }
    [[nodiscard]] AtmospherePreset GetAtmospherePreset() const noexcept { return m_atmospherePreset; }
    void SetAtmospherePreset(AtmospherePreset preset) noexcept { m_atmospherePreset = preset; }

    // Granular Subsystem Decoupling Masks
    [[nodiscard]] bool IsAmbientHarmonizeEnabled() const noexcept { return m_harmonizeAmbient; }
    void SetAmbientHarmonize(bool enabled) noexcept { m_harmonizeAmbient = enabled; }
    [[nodiscard]] bool IsSunLightHarmonizeEnabled() const noexcept { return m_harmonizeSunLight; }
    void SetSunLightHarmonize(bool enabled) noexcept { m_harmonizeSunLight = enabled; }
    [[nodiscard]] bool IsFogHarmonizeEnabled() const noexcept { return m_harmonizeFog; }
    void SetFogHarmonize(bool enabled) noexcept { m_harmonizeFog = enabled; }
    [[nodiscard]] bool IsShaftsHarmonizeEnabled() const noexcept { return m_harmonizeShafts; }
    void SetShaftsHarmonize(bool enabled) noexcept { m_harmonizeShafts = enabled; }
    [[nodiscard]] bool IsBloomHarmonizeEnabled() const noexcept { return m_harmonizeBloom; }
    void SetBloomHarmonize(bool enabled) noexcept { m_harmonizeBloom = enabled; }
    [[nodiscard]] bool IsColorGradingHarmonizeEnabled() const noexcept { return m_harmonizeColorGrading; }
    void SetColorGradingHarmonize(bool enabled) noexcept { m_harmonizeColorGrading = enabled; }

    void HarmonizeAtmosphere() noexcept;
    void DrawAtmosphereGUI() noexcept;
    [[nodiscard]] LensDistortionEffect& GetLensDistortion() noexcept { return *m_lensDistortionEffect; }
    [[nodiscard]] RadialBlurEffect& GetRadialBlur() noexcept { return *m_radialBlurEffect; }
    [[nodiscard]] VignetteEffect& GetVignette() noexcept { return *m_vignetteEffect; }
    [[nodiscard]] ScanlineEffect& GetScanlines() noexcept { return *m_scanlineEffect; }

    // Container accessor for the EditorManager's automatic UI inspector
    [[nodiscard]] const std::vector<std::unique_ptr<PostProcessEffect>>& GetEffects() const noexcept
    {
        return m_effects;
    }

	// Direct access to the velocity buffer RTV for ModelRenderer's velocity pass
    [[nodiscard]] ID3D11RenderTargetView* GetVelocityRTV() const noexcept { return m_velocityTarget.rtv.Get(); }

	// Serialization Interface for saving/loading the entire post-process graph
    void SaveConfig(std::string_view filepath) const;
    void LoadConfig(std::string_view filepath);
    void ResetToDefaults() noexcept;

private:
    struct RenderTargetResource
    {
        Microsoft::WRL::ComPtr<ID3D11Texture2D>          texture{};
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView>   rtv{};
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv{};

        void Reset() noexcept
        {
            srv.Reset();
            rtv.Reset();
            texture.Reset();
        }
    };

    void CreateBuffers(int width, int height);
    void Blit(ID3D11DeviceContext* dc, ID3D11ShaderResourceView* src, ID3D11RenderTargetView* dst);

    // Primary 3D Scene Capture Target
    RenderTargetResource m_sceneTarget{};
    Microsoft::WRL::ComPtr<ID3D11Texture2D>             m_depthStencilTexture{};
    Microsoft::WRL::ComPtr<ID3D11DepthStencilView>      m_depthStencilView{};
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>    m_depthSRV{};

    // Dual Ping-Pong Offscreen Targets
    std::array<RenderTargetResource, 2> m_pingPong{};

    RenderTargetResource m_velocityTarget{}; // written by ModelRenderer's velocity pass
    std::unique_ptr<TemporalAAEffect>   m_temporalAAEffect{};
    std::unique_ptr<ColorGradingEffect> m_colorGradingEffect{};

    // Shared Full-Screen Triangle Vertex Shader & Passthrough Blit Pixel Shader
    Microsoft::WRL::ComPtr<ID3D11VertexShader> m_fullscreenVS{};
    Microsoft::WRL::ComPtr<ID3D11PixelShader>  m_passthroughPS{};
    Microsoft::WRL::ComPtr<ID3D11SamplerState> m_pointSampler{};

    // State Caching for restoring the host's viewport & backbuffer
    ID3D11RenderTargetView* m_originalRTV{ nullptr };
    ID3D11DepthStencilView* m_originalDSV{ nullptr };
    D3D11_VIEWPORT          m_originalViewport{};
    UINT                    m_originalViewportCount{ 1 };

    int   m_windowWidth{ 1920 };
    int   m_windowHeight{ 1080 };
    bool  m_isEnabled{ true };
    float m_globalTime{ 0.0f };

    // Ordered execution graph of discrete effects
    std::vector<std::unique_ptr<PostProcessEffect>> m_effects{};

    // Non-owning fast pointers directly aliasing elements in m_effects
    PSXEffect* m_psxEffect{ nullptr };
    SsaoEffect* m_ssaoEffect{ nullptr };
    SunShaftEffect* m_sunShaftEffect{ nullptr };
    LensDistortionEffect* m_lensDistortionEffect{ nullptr };
    RadialBlurEffect* m_radialBlurEffect{ nullptr };
    VignetteEffect* m_vignetteEffect{ nullptr };
    ScanlineEffect* m_scanlineEffect{ nullptr };
    DepthFogEffect* m_depthFogEffect{ nullptr };
    BloomEffect* m_bloomEffect{ nullptr };

    // Atmospheric Harmonizer Configuration & Granular Decoupling Masks
    bool             m_autoHarmonizeAtmosphere{ true };
    bool             m_harmonizeAmbient{ true };
    bool             m_harmonizeSunLight{ true };
    bool             m_harmonizeFog{ true };
    bool             m_harmonizeShafts{ true };
    bool             m_harmonizeBloom{ true };
    bool             m_harmonizeColorGrading{ true };
    AtmospherePreset m_atmospherePreset{ AtmospherePreset::StylizedWatercolor };

    // Non-Destructive Artist Trims & Modifiers
    float m_fogDistanceTrim{ 1.0f };      // Multiplier on fog start/end distances
    float m_mieInscatterTrim{ 1.0f };     // Multiplier on sun halo inscattering intensity
    float m_bloomIntensityTrim{ 1.0f };   // Multiplier on bloom intensity
    float m_shaftIntensityTrim{ 1.0f };   // Multiplier on volumetric sun shaft brightness
    float m_flareIntensityTrim{ 1.0f };   // Multiplier on optical lens flare intensity
    float m_sunIntensityTrim{ 1.0f };     // Multiplier on direct sun light intensity
    float m_exposureTrim{ 0.0f };         // Additive EV bias on tonemapping exposure
    float m_saturationTrim{ 1.0f };       // Multiplier on watercolor saturation
};