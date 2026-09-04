#include "PostProcessManager.h"

PostProcessManager::PostProcessManager()
{
    auto device = Graphics::Instance().GetDevice();

    // Load the shared Fullscreen Vertex Shader (generates 3 vertices from SV_VertexID)
    GpuResourceUtils::LoadVertexShader(device, "Data/Shader/PostProcessVS.cso", nullptr, 0, nullptr, m_fullscreenVS.GetAddressOf());

    // Load the lightweight Passthrough Pixel Shader (used for zero-overhead direct blits)
    GpuResourceUtils::LoadPixelShader(device, "Data/Shader/PassthroughPS.cso", m_passthroughPS.GetAddressOf());

    // Create point sampler for lossless copy blits
    D3D11_SAMPLER_DESC samplerDesc{};
    samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    device->CreateSamplerState(&samplerDesc, m_pointSampler.GetAddressOf());

    // Instantiate discrete passes in optimal visual execution order
    auto psx = std::make_unique<PSXEffect>(device);
    m_psxEffect = psx.get();
    m_effects.emplace_back(std::move(psx));

    auto fog = std::make_unique<DepthFogEffect>(device);
    m_depthFogEffect = fog.get();
    m_effects.emplace_back(std::move(fog));

    auto lens = std::make_unique<LensDistortionEffect>(device);
    m_lensDistortionEffect = lens.get();
    m_effects.emplace_back(std::move(lens));

    auto blur = std::make_unique<RadialBlurEffect>(device);
    m_radialBlurEffect = blur.get();
    m_effects.emplace_back(std::move(blur));

    auto scan = std::make_unique<ScanlineEffect>(device);
    m_scanlineEffect = scan.get();
    m_effects.emplace_back(std::move(scan));

    auto vig = std::make_unique<VignetteEffect>(device);
    m_vignetteEffect = vig.get();
    m_effects.emplace_back(std::move(vig));
}

void PostProcessManager::Initialize(int screenWidth, int screenHeight)
{
    m_windowWidth = screenWidth;
    m_windowHeight = screenHeight;
    CreateBuffers(screenWidth, screenHeight);
}

void PostProcessManager::OnResize(int width, int height)
{
    if (width <= 0 || height <= 0) return;
    if (width == m_windowWidth && height == m_windowHeight) return;

    m_windowWidth = width;
    m_windowHeight = height;
    CreateBuffers(width, height);
}

void PostProcessManager::CreateBuffers(int width, int height)
{
    auto device = Graphics::Instance().GetDevice();

    // Safe teardown of prior render target allocations
    m_sceneTarget.Reset();
    m_depthStencilTexture.Reset();
    m_depthStencilView.Reset();
    m_depthSRV.Reset();
    for (auto& pp : m_pingPong)
    {
        pp.Reset();
    }

    D3D11_TEXTURE2D_DESC texDesc{};
    texDesc.Width = static_cast<UINT>(width);
    texDesc.Height = static_cast<UINT>(height);
    texDesc.MipLevels = 1;
    texDesc.ArraySize = 1;
    texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    texDesc.SampleDesc.Count = 1;
    texDesc.Usage = D3D11_USAGE_DEFAULT;
    texDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

    auto allocateRT = [&](RenderTargetResource& res) {
        HRESULT hr = device->CreateTexture2D(&texDesc, nullptr, res.texture.GetAddressOf());
        assert(SUCCEEDED(hr) && "Failed to create PostProcess Texture2D");

        hr = device->CreateRenderTargetView(res.texture.Get(), nullptr, res.rtv.GetAddressOf());
        assert(SUCCEEDED(hr) && "Failed to create PostProcess RTV");

        hr = device->CreateShaderResourceView(res.texture.Get(), nullptr, res.srv.GetAddressOf());
        assert(SUCCEEDED(hr) && "Failed to create PostProcess SRV");
        };

    // Allocate Scene Capture Buffer
    allocateRT(m_sceneTarget);

    // Allocate Ping-Pong Offscreen Buffers A & B
    for (auto& pp : m_pingPong)
    {
        allocateRT(pp);
    }

    // Allocate 3D Depth Buffer (Typeless for Shader Resource sharing)
    D3D11_TEXTURE2D_DESC depthDesc = texDesc;
    depthDesc.Format = DXGI_FORMAT_R24G8_TYPELESS;
    depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;

    HRESULT hr = device->CreateTexture2D(&depthDesc, nullptr, m_depthStencilTexture.GetAddressOf());
    assert(SUCCEEDED(hr) && "Failed to create Typeless Depth Texture2D");

    D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
    dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    dsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
    hr = device->CreateDepthStencilView(m_depthStencilTexture.Get(), &dsvDesc, m_depthStencilView.GetAddressOf());
    assert(SUCCEEDED(hr) && "Failed to create DSV");

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;
    hr = device->CreateShaderResourceView(m_depthStencilTexture.Get(), &srvDesc, m_depthSRV.GetAddressOf());
    assert(SUCCEEDED(hr) && "Failed to create Depth SRV");
}

void PostProcessManager::BeginCapture()
{
    auto dc = Graphics::Instance().GetDeviceContext();

    // Save previous RTV / DSV and Viewport so we can cleanly restore them in EndCapture
    m_originalRTV = nullptr;
    m_originalDSV = nullptr;
    dc->OMGetRenderTargets(1, &m_originalRTV, &m_originalDSV);

    m_originalViewportCount = 1;
    dc->RSGetViewports(&m_originalViewportCount, &m_originalViewport);

    // Redirect rasterization output to our offscreen Scene capture target
    ID3D11RenderTargetView* rtv = m_sceneTarget.rtv.Get();
    ID3D11DepthStencilView* dsv = m_depthStencilView.Get();
    dc->OMSetRenderTargets(1, &rtv, dsv);

    D3D11_VIEWPORT vp{};
    vp.Width = static_cast<float>(m_windowWidth);
    vp.Height = static_cast<float>(m_windowHeight);
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    vp.TopLeftX = 0.0f;
    vp.TopLeftY = 0.0f;
    dc->RSSetViewports(1, &vp);

    constexpr float clearColor[4]{ 0.0f, 0.0f, 0.0f, 1.0f };
    dc->ClearRenderTargetView(rtv, clearColor);
    dc->ClearDepthStencilView(dsv, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
}

void PostProcessManager::EndCapture(float dt)
{
    auto dc = Graphics::Instance().GetDeviceContext();

    // Advance simulation time
    m_globalTime += dt;
    if (m_globalTime > 1000.0f) m_globalTime -= 1000.0f;

    m_lensDistortionEffect->GetData().time = m_globalTime;
    m_scanlineEffect->GetData().time = m_globalTime;

    // Configure Pipeline for Fullscreen 2D Post-Processing
    auto rs = Graphics::Instance().GetRenderState();
    dc->OMSetDepthStencilState(rs->GetDepthStencilState(DepthState::NoTestNoWrite), 0);
    dc->RSSetState(rs->GetRasterizerState(RasterizerState::SolidCullNone));
    dc->OMSetBlendState(rs->GetBlendState(BlendState::Opaque), nullptr, 0xFFFFFFFF);

    dc->VSSetShader(m_fullscreenVS.Get(), nullptr, 0);
    dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    dc->IASetInputLayout(nullptr);

    ID3D11Buffer* nullBuffer{ nullptr };
    constexpr UINT zeroStride{ 0 };
    constexpr UINT zeroOffset{ 0 };
    dc->IASetVertexBuffers(0, 1, &nullBuffer, &zeroStride, &zeroOffset);

    // Unbind DSV from the Output Merger to prevent Read/Write Hazards
    ID3D11RenderTargetView* initialRTV{ m_sceneTarget.rtv.Get() };
    dc->OMSetRenderTargets(1, &initialRTV, nullptr);

    // Bind the Depth SRV to register(t1) globally for all post-process effects
    ID3D11ShaderResourceView* depthSRV{ m_depthSRV.Get() };
    dc->PSSetShaderResources(1, 1, &depthSRV);

    // Multi-Pass Ping-Pong Loop
    ID3D11ShaderResourceView* currentSourceSRV = m_sceneTarget.srv.Get();
    int pingPongIndex{ 0 };

    if (m_isEnabled)
    {
        for (const auto& effect : m_effects)
        {
            // Disabled effects issue zero GPU draw calls
            if (!effect->IsEnabled()) continue;

            auto& destTarget = m_pingPong[pingPongIndex];

            // CRITICAL D3D11 HAZARD PREVENTION: Unbind SRVs before binding as RTV
            ID3D11ShaderResourceView* nullSRV[] = { nullptr };
            dc->PSSetShaderResources(0, 1, nullSRV);

            // Bind current ping-pong destination target
            ID3D11RenderTargetView* rtv = destTarget.rtv.Get();
            dc->OMSetRenderTargets(1, &rtv, nullptr);

            // Execute isolated effect pass
            effect->Draw(dc, currentSourceSRV);

            // Swap roles for next iteration
            currentSourceSRV = destTarget.srv.Get();
            pingPongIndex = 1 - pingPongIndex;
        }
    }

    // Final Pass: Blit the final processed texture directly into the host's original RTV
    ID3D11ShaderResourceView* nullSRVs[2]{ nullptr, nullptr }; // Expand array to 2
    dc->PSSetShaderResources(0, 2, nullSRVs); // Unbind both t0 and t1

    dc->OMSetRenderTargets(1, &m_originalRTV, m_originalDSV);
    dc->RSSetViewports(m_originalViewportCount, &m_originalViewport);

    Blit(dc, currentSourceSRV, m_originalRTV);

    // Unbind SRVs to prevent pipeline warnings on the next frame
    dc->PSSetShaderResources(0, 2, nullSRVs);

    if (m_originalRTV) { m_originalRTV->Release(); m_originalRTV = nullptr; }
    if (m_originalDSV) { m_originalDSV->Release(); m_originalDSV = nullptr; }
}

void PostProcessManager::Blit(ID3D11DeviceContext* dc, ID3D11ShaderResourceView* src, ID3D11RenderTargetView* dst)
{
    dc->PSSetShader(m_passthroughPS.Get(), nullptr, 0);
    dc->PSSetShaderResources(0, 1, &src);
    dc->PSSetSamplers(0, 1, m_pointSampler.GetAddressOf());
    dc->Draw(3, 0);
}

void PostProcessManager::SaveConfig(std::string_view filepath) const
{
    nlohmann::json root;

    for (const auto& effect : m_effects)
    {
        nlohmann::json effectJson;
        effect->Serialize(effectJson);

        // Convert the human-readable name into a safe JSON key (e.g., "Radial Blur" -> "Radial_Blur")
        std::string key{ effect->GetName() };
        std::replace(key.begin(), key.end(), ' ', '_');

        root[key] = effectJson;
    }

    // Extract the folder path from the full filepath and ensure it exists
    // std::ofstream will silently fail if the target directory doesn't exist
    std::filesystem::path pathObj{ filepath };
    std::filesystem::path directory = pathObj.parent_path();

    if (!directory.empty() && !std::filesystem::exists(directory))
    {
        std::filesystem::create_directories(directory);
    }

    // Convert string_view to string because fstream constructor requires a null-terminated string
    std::ofstream file{ std::string{ filepath } };
    if (file.is_open())
    {
        // Force the JSON library to replace invalid Shift-JIS bytes with a safe placeholder ()
        file << root.dump(4, ' ', false, nlohmann::json::error_handler_t::replace);
        Log::Success("Saved Scene to: " + std::string{ filepath });
    }
    else
    {
        Log::Error("Failed to open file for saving: " + std::string{ filepath });
    }
}

void PostProcessManager::LoadConfig(std::string_view filepath)
{
    std::ifstream file{ std::string{filepath} };
    if (!file.is_open())
    {
        Log::Warn("Profile not found, keeping defaults: " + std::string{ filepath });
        return;
    }

    try
    {
        nlohmann::json root;
        file >> root; // Parse the JSON

        for (auto& effect : m_effects)
        {
            std::string key{ effect->GetName() };
            std::replace(key.begin(), key.end(), ' ', '_');

            if (root.contains(key))
            {
                effect->Deserialize(root[key]);
            }
        }
        Log::Info("Loaded post-process profile: " + std::string{ filepath });
    }
    catch (const std::exception& e)
    {
        Log::Error("JSON parse error in " + std::string{ filepath } + ": " + e.what());
    }
}

void PostProcessManager::ResetToDefaults() noexcept
{
    for (auto& effect : m_effects)
    {
        effect->ResetToDefault();
    }
}