#include "PostProcessManager.h"
#include "System/Graphics.h"
#include "Framework.h" // Untuk akses Window info jika perlu

PostProcessManager::PostProcessManager()
{
    // Ambil device dari Singleton Graphics
    auto device = Graphics::Instance().GetDevice();

    // Inisialisasi Shader
    m_uberShader = std::make_unique<UberShader>(device);

    // Set default data (bisa disesuaikan)
    m_data.enabled = true;
}

void PostProcessManager::Initialize(int screenWidth, int screenHeight)
{
    m_windowWidth = screenWidth;
    m_windowHeight = screenHeight;
    m_currentRTWidth = screenWidth;
    m_currentRTHeight = screenHeight;
    CreateBuffers(screenWidth, screenHeight);
}

void PostProcessManager::OnResize(int width, int height)
{
    m_windowWidth = width;
    m_windowHeight = height;

    // If PSX filter is off, resize the internal buffer to match the new window
    if (!m_data.psxEnabled)
    {
        m_currentRTWidth = width;
        m_currentRTHeight = height;
        CreateBuffers(m_currentRTWidth, m_currentRTHeight);
    }
}

void PostProcessManager::CreateBuffers(int width, int height)
{
    auto device = Graphics::Instance().GetDevice();

    // Reset Pointer lama sebelum membuat baru
    m_renderTargetTexture.Reset();
    m_renderTargetView.Reset();
    m_shaderResourceView.Reset();
    m_depthStencilTexture.Reset();
    m_depthStencilView.Reset();

    // 1. Create Texture & RTV (Color Buffer)
    D3D11_TEXTURE2D_DESC textureDesc = {};
    textureDesc.Width = static_cast<UINT>(width);
    textureDesc.Height = static_cast<UINT>(height);
    textureDesc.MipLevels = 1;
    textureDesc.ArraySize = 1;

    // -------------------------------------------------------------
    // ---> THE AAA HDR UPGRADE <---
    // Changed from DXGI_FORMAT_R8G8B8A8_UNORM to R16G16B16A16_FLOAT
    // This allows colors to exceed 1.0f (Pure White) and become physically emissive!
    // -------------------------------------------------------------
    textureDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;

    textureDesc.SampleDesc.Count = 1;
    textureDesc.Usage = D3D11_USAGE_DEFAULT;
    textureDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

    HRESULT hr;
    hr = device->CreateTexture2D(&textureDesc, nullptr, m_renderTargetTexture.GetAddressOf());
    if (FAILED(hr)) return; // Handle error properly in prod

    hr = device->CreateRenderTargetView(m_renderTargetTexture.Get(), nullptr, m_renderTargetView.GetAddressOf());
    if (FAILED(hr)) return;

    hr = device->CreateShaderResourceView(m_renderTargetTexture.Get(), nullptr, m_shaderResourceView.GetAddressOf());
    if (FAILED(hr)) return;

    // 2. Create Depth Buffer (Penting agar objek 3D di scene bisa di-sort depth-nya)
    textureDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    textureDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;

    hr = device->CreateTexture2D(&textureDesc, nullptr, m_depthStencilTexture.GetAddressOf());
    if (FAILED(hr)) return;

    hr = device->CreateDepthStencilView(m_depthStencilTexture.Get(), nullptr, m_depthStencilView.GetAddressOf());
    if (FAILED(hr)) return;
}

void PostProcessManager::BeginCapture()
{
    auto dc = Graphics::Instance().GetDeviceContext();

    // -------------------------------------------------------------
    // DYNAMIC RESOLUTION CHECK
    // If GUI sliders changed, physically recreate the Render Target!
    // -------------------------------------------------------------
    int targetWidth = m_data.psxEnabled ? (std::max)(16, static_cast<int>(m_data.psxResWidth)) : m_windowWidth;
    int targetHeight = m_data.psxEnabled ? (std::max)(16, static_cast<int>(m_data.psxResHeight)) : m_windowHeight;

    if (targetWidth != m_currentRTWidth || targetHeight != m_currentRTHeight)
    {
        m_currentRTWidth = targetWidth;
        m_currentRTHeight = targetHeight;
        CreateBuffers(m_currentRTWidth, m_currentRTHeight);
    }

    // 1. Save original Render Target AND Viewport
    m_originalRTV = nullptr;
    m_originalDSV = nullptr;
    dc->OMGetRenderTargets(1, &m_originalRTV, &m_originalDSV);

    m_originalViewportCount = 1;
    dc->RSGetViewports(&m_originalViewportCount, &m_originalViewport);

    // 2. Switch to our internal Render Target
    ID3D11RenderTargetView* rtv = m_renderTargetView.Get();
    ID3D11DepthStencilView* dsv = m_depthStencilView.Get();
    dc->OMSetRenderTargets(1, &rtv, dsv);

    // 3. Shrink the Viewport to match the Render Target!
    D3D11_VIEWPORT vp{};
    vp.Width = static_cast<float>(m_currentRTWidth);
    vp.Height = static_cast<float>(m_currentRTHeight);
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    vp.TopLeftX = 0;
    vp.TopLeftY = 0;
    dc->RSSetViewports(1, &vp);

    // 4. Clear Screen
    float clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    dc->ClearRenderTargetView(rtv, clearColor);
    dc->ClearDepthStencilView(dsv, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
}

void PostProcessManager::EndCapture(float dt)
{
    auto dc = Graphics::Instance().GetDeviceContext();

    // 1. Restore the original Render Target AND the original 1080p Viewport!
    dc->OMSetRenderTargets(1, &m_originalRTV, m_originalDSV);
    dc->RSSetViewports(m_originalViewportCount, &m_originalViewport);

    if (m_originalRTV) { m_originalRTV->Release(); m_originalRTV = nullptr; }
    if (m_originalDSV) { m_originalDSV->Release(); m_originalDSV = nullptr; }

    if (!m_isEnabled) return;

    m_globalTime += dt;
    if (m_globalTime > 1000.0f) m_globalTime -= 1000.0f;

    auto rs = Graphics::Instance().GetRenderState();
    dc->OMSetDepthStencilState(rs->GetDepthStencilState(DepthState::NoTestNoWrite), 0);
    dc->RSSetState(rs->GetRasterizerState(RasterizerState::SolidCullNone));
    dc->OMSetBlendState(rs->GetBlendState(BlendState::Opaque), nullptr, 0xFFFFFFFF);

    m_data.time = m_globalTime;

    // Draw the tiny texture upscaled to the 1080p screen
    m_uberShader->Draw(dc, m_shaderResourceView.Get(), m_data);

    ID3D11ShaderResourceView* nullSRV[] = { nullptr };
    dc->PSSetShaderResources(0, 1, nullSRV);
}