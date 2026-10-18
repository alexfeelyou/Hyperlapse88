#include <imgui.h>
#include "Light.h"
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

    m_temporalAAEffect = std::make_unique<TemporalAAEffect>(device);
    m_colorGradingEffect = std::make_unique<ColorGradingEffect>(device);
    m_colorGradingEffect->SetManager(this);

    // Instantiate discrete passes in optimal visual execution order
    auto psx = std::make_unique<PSXEffect>(device);
    m_psxEffect = psx.get();
    m_effects.emplace_back(std::move(psx));

    auto ssao = std::make_unique<SsaoEffect>(device);
    m_ssaoEffect = ssao.get();
    m_effects.emplace_back(std::move(ssao));

    auto fog = std::make_unique<DepthFogEffect>(device);
    m_depthFogEffect = fog.get();
    m_effects.emplace_back(std::move(fog));

    auto sunShaft = std::make_unique<SunShaftEffect>(device);
    m_sunShaftEffect = sunShaft.get();
    m_effects.emplace_back(std::move(sunShaft));

    auto bloom = std::make_unique<BloomEffect>(device);
    m_bloomEffect = bloom.get();
    m_effects.emplace_back(std::move(bloom));

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
    texDesc.Format = DXGI_FORMAT_R11G11B10_FLOAT;
    texDesc.SampleDesc.Count = 1;
    texDesc.Usage = D3D11_USAGE_DEFAULT;
    texDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

    auto allocateRT = [&](RenderTargetResource& res, DXGI_FORMAT format) {
        D3D11_TEXTURE2D_DESC desc{ texDesc };
        desc.Format = format;

        HRESULT hr = device->CreateTexture2D(&desc, nullptr, res.texture.GetAddressOf());
        assert(SUCCEEDED(hr) && "Failed to create PostProcess Texture2D");
        hr = device->CreateRenderTargetView(res.texture.Get(), nullptr, res.rtv.GetAddressOf());
        assert(SUCCEEDED(hr) && "Failed to create PostProcess RTV");
        hr = device->CreateShaderResourceView(res.texture.Get(), nullptr, res.srv.GetAddressOf());
        assert(SUCCEEDED(hr) && "Failed to create PostProcess SRV");
        };

    // Allocate Primary Scene Capture Target in 32-bpp Linear HDR (Zero extra bandwidth vs RGBA8)
    allocateRT(m_sceneTarget, DXGI_FORMAT_R11G11B10_FLOAT);

    // Allocate Ping-Pong Targets in 32-bpp Linear HDR for Multi-Pass Post-Processing
    for (auto& pp : m_pingPong)
    {
        allocateRT(pp, DXGI_FORMAT_R11G11B10_FLOAT);
    }

	// Allocate Velocity Target for Temporal AA
    allocateRT(m_velocityTarget, DXGI_FORMAT_R16G16_FLOAT);

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

    m_temporalAAEffect->OnResize(device, width, height);
    for (auto& effect : m_effects)
    {
        effect->OnResize(device, width, height);
    }
}

void PostProcessManager::BeginCapture()
{
    // Update LightManager ambient colors and sun intensity before 3D scene rasterization
    HarmonizeAtmosphere();

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

    ID3D11RenderTargetView* velRTV{ m_velocityTarget.rtv.Get() };
    constexpr float clearVelocity[4]{ 0.0f, 0.0f, 0.0f, 0.0f };
    dc->ClearRenderTargetView(velRTV, clearVelocity);
}

void PostProcessManager::EndCapture(float dt)
{
    // Update post-process parameters (Fog, Shafts, Bloom, Color Grading)
    HarmonizeAtmosphere();

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

    // Detach Output Merger targets completely to prevent OM/SRV feedback hazards
    ID3D11RenderTargetView* nullRTV{ nullptr };
    dc->OMSetRenderTargets(1, &nullRTV, nullptr);

    // Bind Depth SRV to t1 and Velocity SRV to t2 globally for passes requiring scene metrics
    ID3D11ShaderResourceView* depthSRV{ m_depthSRV.Get() };
    dc->PSSetShaderResources(1, 1, &depthSRV);

    ID3D11ShaderResourceView* velocitySRV{ m_velocityTarget.srv.Get() };
    dc->PSSetShaderResources(2, 1, &velocitySRV);

    // Multi-Pass Ping-Pong Loop
    ID3D11ShaderResourceView* currentSourceSRV{ m_sceneTarget.srv.Get() };
    int pingPongIndex{ 0 };

    if (m_temporalAAEffect->IsEnabled())
    {
        ID3D11ShaderResourceView* const nullSRV[]{ nullptr };
        dc->PSSetShaderResources(3, 1, nullSRV); // Unbind history SRV before binding its buffer as RTV

        ID3D11RenderTargetView* resolveRTV{ m_temporalAAEffect->GetWriteRTV() };
        dc->OMSetRenderTargets(1, &resolveRTV, nullptr);

        m_temporalAAEffect->Draw(dc, currentSourceSRV);

        currentSourceSRV = m_temporalAAEffect->GetWriteSRV();
        m_temporalAAEffect->SwapHistoryBuffers();

        dc->PSSetShaderResources(0, 1, nullSRV);
    }

    // Execute active post-process passes through alternating ping-pong targets
    for (const auto& effect : m_effects)
    {
        if (!effect->IsEnabled())
        {
            continue;
        }

        ID3D11RenderTargetView* targetRTV{ m_pingPong[pingPongIndex].rtv.Get() };
        dc->OMSetRenderTargets(1, &targetRTV, nullptr);

        effect->Draw(dc, currentSourceSRV);

        // Break slot 0 binding to prevent feedback hazards when this buffer becomes an RTV
        ID3D11ShaderResourceView* const nullSRV[]{ nullptr };
        dc->PSSetShaderResources(0, 1, nullSRV);

        currentSourceSRV = m_pingPong[pingPongIndex].srv.Get();
        pingPongIndex = 1 - pingPongIndex;
    }

    // Final Pass: Blit the final processed texture directly into the host's original RTV
    ID3D11ShaderResourceView* nullSRVs[4]{ nullptr, nullptr, nullptr, nullptr };
    dc->PSSetShaderResources(0, 4, nullSRVs); // Unbind slots t0 through t3

    dc->OMSetRenderTargets(1, &m_originalRTV, m_originalDSV);
    dc->RSSetViewports(m_originalViewportCount, &m_originalViewport);

    Blit(dc, currentSourceSRV, m_originalRTV);

    // Unbind all SRVs to prevent pipeline warnings on subsequent passes or frames
    dc->PSSetShaderResources(0, 4, nullSRVs);

    if (m_originalRTV) { m_originalRTV->Release(); m_originalRTV = nullptr; }
    if (m_originalDSV) { m_originalDSV->Release(); m_originalDSV = nullptr; }
}

void PostProcessManager::Blit(ID3D11DeviceContext* dc, ID3D11ShaderResourceView* src, ID3D11RenderTargetView* /*dst*/)
{
    m_colorGradingEffect->Draw(dc, src);
}

void PostProcessManager::HarmonizeAtmosphere() noexcept
{
    // Fast return if master automation is toggled off or preset is set to full manual override
    if (!m_autoHarmonizeAtmosphere || m_atmospherePreset == AtmospherePreset::Custom)
    {
        return;
    }

    auto& lightManager = Graphics::Instance().GetLightManager();
    const auto& dirLight = lightManager.GetDirectionalLight();

    // Calculate normalized solar elevation from direction vector: h = -dir.y in [-1.0, 1.0]
    DirectX::XMVECTOR vDir = DirectX::XMLoadFloat3(&dirLight.direction);
    vDir = DirectX::XMVector3Normalize(vDir);
    DirectX::XMFLOAT3 normDir{};
    DirectX::XMStoreFloat3(&normDir, vDir);
    const float solarElevation = -normDir.y;

    const AtmosphereParameters params = AtmosphereProfile::Evaluate(m_atmospherePreset, solarElevation);

    // Harmonize LightManager forward scene lighting
    if (m_harmonizeAmbient)
    {
        lightManager.SetSkyColor(params.skyAmbientColor);
        lightManager.SetSkyIntensity(params.skyAmbientIntensity);
        lightManager.SetGroundColor(params.groundAmbientColor);
        lightManager.SetGroundIntensity(params.groundAmbientIntensity);
    }

    if (m_harmonizeSunLight)
    {
        lightManager.SetDirectionalLightColor(params.sunColor);
        lightManager.SetDirectionalLightIntensity(params.sunIntensity * (std::max)(0.0f, m_sunIntensityTrim));
    }

    // Harmonize Depth Fog (Mie Halo & Horizon) with non-destructive distance & harshness trims
    if (m_harmonizeFog && m_depthFogEffect)
    {
        auto& fog = m_depthFogEffect->GetData();
        fog.color = params.fogColor;
        fog.sunInscatterColor = params.sunInscatterColor;
        fog.sunInscatterIntensity = params.sunInscatterIntensity * (std::max)(0.0f, m_mieInscatterTrim);
        fog.sunInscatterExponent = params.sunInscatterExponent;
        fog.heightFalloff = params.heightFalloff;
        fog.startDistance = params.startDistance * (std::max)(0.1f, m_fogDistanceTrim);
        fog.endDistance = (std::max)(fog.startDistance + 1.0f, params.endDistance * (std::max)(0.1f, m_fogDistanceTrim));
    }

    // Harmonize Volumetric Sun Shafts & Optical Flare with intensity trims
    if (m_harmonizeShafts && m_sunShaftEffect)
    {
        auto& shafts = m_sunShaftEffect->GetData();
        shafts.shaftColor = params.shaftColor;
        shafts.shaftIntensity = params.shaftIntensity * (std::max)(0.0f, m_shaftIntensityTrim);
        shafts.density = params.shaftDensity;
        shafts.decay = params.shaftDecay;
        shafts.flareIntensity = params.flareIntensity * (std::max)(0.0f, m_flareIntensityTrim);
    }

    // Harmonize HDR Bloom Bleed with non-destructive intensity scaling
    if (m_harmonizeBloom && m_bloomEffect)
    {
        auto& bloom = m_bloomEffect->GetData();
        bloom.tint = params.bloomTint;
        bloom.intensity = params.bloomIntensity * (std::max)(0.0f, m_bloomIntensityTrim);
        bloom.threshold = params.bloomThreshold;
        bloom.scatter = params.bloomScatter;
    }

    // Harmonize ACES Filmic Watercolor Color Grading with exposure & saturation offsets
    if (m_harmonizeColorGrading && m_colorGradingEffect)
    {
        auto& grade = m_colorGradingEffect->GetData();
        grade.exposure = params.exposure + m_exposureTrim;
        grade.temperature = params.temperature;
        grade.tint = params.tint;
        grade.contrast = params.contrast;
        grade.saturation = params.saturation * (std::max)(0.0f, m_saturationTrim);
        grade.shadowLiftColor = params.shadowLiftColor;
        grade.highlightGainColor = params.highlightGainColor;
    }
}

void PostProcessManager::DrawAtmosphereGUI() noexcept
{
    ImGui::TextColored(ImVec4{ 0.2f, 0.85f, 1.0f, 1.0f }, "SOLAR ATMOSPHERE HARMONIZER");
    ImGui::Checkbox("Auto-Drive Atmosphere from Sun", &m_autoHarmonizeAtmosphere);

    const char* const presetNames[] = {
        "Stylized Watercolor (Default)",
        "Vibrant Fantasy",
        "Clear Natural Daylight",
        "Custom (Manual Override)"
    };

    int currentPreset = static_cast<int>(m_atmospherePreset);
    if (ImGui::Combo("Atmosphere Preset", &currentPreset, presetNames, IM_ARRAYSIZE(presetNames)))
    {
        m_atmospherePreset = static_cast<AtmospherePreset>(currentPreset);
        HarmonizeAtmosphere();
    }

    const auto& dirLight = Graphics::Instance().GetLightManager().GetDirectionalLight();
    const float elevation = -dirLight.direction.y;
    char gaugeBuf[64];
    sprintf_s(gaugeBuf, sizeof(gaugeBuf), "Solar Elevation: %+.2f (%s)", elevation,
        elevation >= 0.05f ? "Day" : (elevation >= -0.05f ? "Twilight" : "Night"));
    ImGui::ProgressBar((elevation + 1.0f) * 0.5f, ImVec2{ -1.0f, 0.0f }, gaugeBuf);

    if (!m_autoHarmonizeAtmosphere || m_atmospherePreset == AtmospherePreset::Custom)
    {
        ImGui::TextDisabled("Atmosphere automation paused (All sliders are 100% manual).");
        return;
    }

    // Subsystem Decoupling Masks: Allows disabling automation per subsystem without fighting sliders
    if (ImGui::TreeNodeEx("Subsystem Automation Masks", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::Checkbox("Ambient Dome Fill (Sky/Ground)", &m_harmonizeAmbient);
        ImGui::Checkbox("Sun Light (Color/Intensity)", &m_harmonizeSunLight);
        ImGui::Checkbox("Depth Fog & Mie Inscattering", &m_harmonizeFog);
        ImGui::Checkbox("Volumetric Sun Shafts & Flare", &m_harmonizeShafts);
        ImGui::Checkbox("HDR Bloom Bleed", &m_harmonizeBloom);
        ImGui::Checkbox("Filmic Color Grading", &m_harmonizeColorGrading);
        ImGui::TreePop();
    }

    // Non-Destructive Artist Trims: Modulates curve without overwriting manual intent
    if (ImGui::TreeNodeEx("Artist Trims & Modifiers (Curve Scaling)", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::SliderFloat("Fog Distance Scale", &m_fogDistanceTrim, 0.2f, 3.0f, "%.2fx");
        ImGui::SliderFloat("Mie Glow Harshness", &m_mieInscatterTrim, 0.0f, 2.5f, "%.2fx");
        ImGui::SliderFloat("Bloom Intensity Scale", &m_bloomIntensityTrim, 0.0f, 2.5f, "%.2fx");
        ImGui::SliderFloat("Sun Shaft Intensity Scale", &m_shaftIntensityTrim, 0.0f, 2.5f, "%.2fx");
        ImGui::SliderFloat("Lens Flare Intensity Scale", &m_flareIntensityTrim, 0.0f, 2.5f, "%.2fx");
        ImGui::SliderFloat("Sun Light Intensity Scale", &m_sunIntensityTrim, 0.0f, 2.5f, "%.2fx");
        ImGui::SliderFloat("Exposure Trim (EV)", &m_exposureTrim, -2.0f, 2.0f, "%+.2f EV");
        ImGui::SliderFloat("Saturation Scale", &m_saturationTrim, 0.5f, 1.5f, "%.2fx");

        if (ImGui::Button("Reset Trims to 1.0x"))
        {
            m_fogDistanceTrim = 1.0f;
            m_mieInscatterTrim = 1.0f;
            m_bloomIntensityTrim = 1.0f;
            m_shaftIntensityTrim = 1.0f;
            m_flareIntensityTrim = 1.0f;
            m_sunIntensityTrim = 1.0f;
            m_exposureTrim = 0.0f;
            m_saturationTrim = 1.0f;
            HarmonizeAtmosphere();
        }
        ImGui::TreePop();
    }
}

void PostProcessManager::SaveConfig(std::string_view filepath) const
{
    nlohmann::json root;

    root["Atmosphere_AutoHarmonize"] = m_autoHarmonizeAtmosphere;
    root["Atmosphere_HarmonizeAmbient"] = m_harmonizeAmbient;
    root["Atmosphere_HarmonizeSunLight"] = m_harmonizeSunLight;
    root["Atmosphere_HarmonizeFog"] = m_harmonizeFog;
    root["Atmosphere_HarmonizeShafts"] = m_harmonizeShafts;
    root["Atmosphere_HarmonizeBloom"] = m_harmonizeBloom;
    root["Atmosphere_HarmonizeColorGrading"] = m_harmonizeColorGrading;
    root["Atmosphere_Preset"] = static_cast<int>(m_atmospherePreset);

    root["Atmosphere_FogDistanceTrim"] = m_fogDistanceTrim;
    root["Atmosphere_MieInscatterTrim"] = m_mieInscatterTrim;
    root["Atmosphere_BloomIntensityTrim"] = m_bloomIntensityTrim;
    root["Atmosphere_ShaftIntensityTrim"] = m_shaftIntensityTrim;
    root["Atmosphere_FlareIntensityTrim"] = m_flareIntensityTrim;
    root["Atmosphere_SunIntensityTrim"] = m_sunIntensityTrim;
    root["Atmosphere_ExposureTrim"] = m_exposureTrim;
    root["Atmosphere_SaturationTrim"] = m_saturationTrim;

    for (const auto& effect : m_effects)
    {
        nlohmann::json effectJson;
        effect->Serialize(effectJson);

        // Convert the human-readable name into a safe JSON key (e.g., "Radial Blur" -> "Radial_Blur")
        std::string key{ effect->GetName() };
        std::replace(key.begin(), key.end(), ' ', '_');

        root[key] = effectJson;
    }

    nlohmann::json taaJson{};
    m_temporalAAEffect->Serialize(taaJson);
    root["Temporal_AA"] = taaJson;

    nlohmann::json colorGradingJson{};
    m_colorGradingEffect->Serialize(colorGradingJson);
    root["Color_Grading"] = colorGradingJson;

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

        m_autoHarmonizeAtmosphere = root.value("Atmosphere_AutoHarmonize", true);
        m_harmonizeAmbient = root.value("Atmosphere_HarmonizeAmbient", true);
        m_harmonizeSunLight = root.value("Atmosphere_HarmonizeSunLight", true);
        m_harmonizeFog = root.value("Atmosphere_HarmonizeFog", true);
        m_harmonizeShafts = root.value("Atmosphere_HarmonizeShafts", true);
        m_harmonizeBloom = root.value("Atmosphere_HarmonizeBloom", true);
        m_harmonizeColorGrading = root.value("Atmosphere_HarmonizeColorGrading", true);
        m_atmospherePreset = static_cast<AtmospherePreset>(root.value("Atmosphere_Preset", 0));

        m_fogDistanceTrim = root.value("Atmosphere_FogDistanceTrim", 1.0f);
        m_mieInscatterTrim = root.value("Atmosphere_MieInscatterTrim", 1.0f);
        m_bloomIntensityTrim = root.value("Atmosphere_BloomIntensityTrim", 1.0f);
        m_shaftIntensityTrim = root.value("Atmosphere_ShaftIntensityTrim", 1.0f);
        m_flareIntensityTrim = root.value("Atmosphere_FlareIntensityTrim", 1.0f);
        m_sunIntensityTrim = root.value("Atmosphere_SunIntensityTrim", 1.0f);
        m_exposureTrim = root.value("Atmosphere_ExposureTrim", 0.0f);
        m_saturationTrim = root.value("Atmosphere_SaturationTrim", 1.0f);

        for (auto& effect : m_effects)
        {
            std::string key{ effect->GetName() };
            std::replace(key.begin(), key.end(), ' ', '_');

            if (root.contains(key))
            {
                effect->Deserialize(root[key]);
            }
        }

        if (root.contains("Temporal_AA"))
        {
            m_temporalAAEffect->Deserialize(root["Temporal_AA"]);
        }

        if (root.contains("Color_Grading"))
        {
            m_colorGradingEffect->Deserialize(root["Color_Grading"]);
        }

        Log::Success("Loaded post-process profile: " + std::string{ filepath });
    }
    catch (const std::exception& e)
    {
        Log::Error("JSON parse error in " + std::string{ filepath } + ": " + e.what());
    }
}

void PostProcessManager::ResetToDefaults() noexcept
{
    m_autoHarmonizeAtmosphere = true;
    m_harmonizeAmbient = true;
    m_harmonizeSunLight = true;
    m_harmonizeFog = true;
    m_harmonizeShafts = true;
    m_harmonizeBloom = true;
    m_harmonizeColorGrading = true;
    m_atmospherePreset = AtmospherePreset::StylizedWatercolor;

    m_fogDistanceTrim = 1.0f;
    m_mieInscatterTrim = 1.0f;
    m_bloomIntensityTrim = 1.0f;
    m_shaftIntensityTrim = 1.0f;
    m_flareIntensityTrim = 1.0f;
    m_sunIntensityTrim = 1.0f;
    m_exposureTrim = 0.0f;
    m_saturationTrim = 1.0f;

    for (auto& effect : m_effects)
    {
        effect->ResetToDefault();
    }
    m_temporalAAEffect->ResetToDefault();
    m_colorGradingEffect->ResetToDefault();

    HarmonizeAtmosphere();
}