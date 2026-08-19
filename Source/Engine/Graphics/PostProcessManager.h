#pragma once

#include <algorithm>
#include <d3d11.h>
#include <wrl.h>
#include <memory>
#include <vector>

#include "UberShader.h"

class PostProcessManager
{
public:
    PostProcessManager();
    ~PostProcessManager() = default;

    void Initialize(int screenWidth, int screenHeight);
    void OnResize(int width, int height);

    void BeginCapture();
    void EndCapture(float dt);

    UberShader::UberData& GetData() { return m_data; }
    bool IsEnabled() const { return m_isEnabled; }
    void SetEnabled(bool enable) { m_isEnabled = enable; }

private:
    // --- Resources ---
    Microsoft::WRL::ComPtr<ID3D11Texture2D>          m_renderTargetTexture;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView>   m_renderTargetView;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_shaderResourceView;

    Microsoft::WRL::ComPtr<ID3D11Texture2D>          m_depthStencilTexture;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilView>   m_depthStencilView;

    // --- State Caching ---
    ID3D11RenderTargetView* m_originalRTV = nullptr;
    ID3D11DepthStencilView* m_originalDSV = nullptr;
    D3D11_VIEWPORT          m_originalViewport{};
    UINT                    m_originalViewportCount = 1;

    // --- Resolution Tracking ---
    int m_windowWidth = 1920;
    int m_windowHeight = 1080;
    int m_currentRTWidth = 1920;
    int m_currentRTHeight = 1080;

    std::unique_ptr<UberShader> m_uberShader;
    UberShader::UberData        m_data;

    bool m_isEnabled = true;
    float m_globalTime = 0.0f;

    void CreateBuffers(int width, int height);
};