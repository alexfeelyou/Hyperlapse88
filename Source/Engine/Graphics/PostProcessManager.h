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
#include "PostProcessEffects.h"

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

    // Direct accessors to individual effect modules (for Scene transitions & scripts)
    [[nodiscard]] PSXEffect& GetPSX() noexcept { return *m_psxEffect; }
    [[nodiscard]] LensDistortionEffect& GetLensDistortion() noexcept { return *m_lensDistortionEffect; }
    [[nodiscard]] RadialBlurEffect& GetRadialBlur() noexcept { return *m_radialBlurEffect; }
    [[nodiscard]] VignetteEffect& GetVignette() noexcept { return *m_vignetteEffect; }
    [[nodiscard]] ScanlineEffect& GetScanlines() noexcept { return *m_scanlineEffect; }

    // Container accessor for the EditorManager's automatic UI inspector
    [[nodiscard]] const std::vector<std::unique_ptr<PostProcessEffect>>& GetEffects() const noexcept
    {
        return m_effects;
    }

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
    Microsoft::WRL::ComPtr<ID3D11Texture2D>        m_depthStencilTexture{};
    Microsoft::WRL::ComPtr<ID3D11DepthStencilView> m_depthStencilView{};

    // Dual Ping-Pong Offscreen Targets
    std::array<RenderTargetResource, 2> m_pingPong{};

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
    LensDistortionEffect* m_lensDistortionEffect{ nullptr };
    RadialBlurEffect* m_radialBlurEffect{ nullptr };
    VignetteEffect* m_vignetteEffect{ nullptr };
    ScanlineEffect* m_scanlineEffect{ nullptr };
};