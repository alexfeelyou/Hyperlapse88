#include <cmath>
#include "System/Graphics.h"
#include "PostProcessEffects.h"

// Helper to generate D3D11 Samplers cleanly
namespace
{
    void CreateSampler(ID3D11Device* device, D3D11_FILTER filter, ID3D11SamplerState** outSampler) noexcept
    {
        D3D11_SAMPLER_DESC desc{};
        desc.Filter = filter;
        desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        device->CreateSamplerState(&desc, outSampler);
    }
}

// PSXEffect Implementation
PSXEffect::PSXEffect(ID3D11Device* device)
{
    GpuResourceUtils::LoadPixelShader(device, "Data/Shader/PSXPS.cso", m_pixelShader.GetAddressOf());
    GpuResourceUtils::CreateConstantBuffer(device, sizeof(CbPSX), m_constantBuffer.GetAddressOf());
    CreateSampler(device, D3D11_FILTER_MIN_MAG_MIP_POINT, m_pointSampler.GetAddressOf());
    m_currentData.resWidth = -1.0f; // Force initial upload
}

void PSXEffect::Draw(ID3D11DeviceContext* dc, ID3D11ShaderResourceView* srcSRV)
{
    if (!(m_currentData == m_data))
    {
        CbPSX cb{};
        cb.resWidth = (std::max)(1.0f, m_data.resWidth);
        cb.resHeight = (std::max)(1.0f, m_data.resHeight);
        cb.colorDepth = (std::max)(1.0f, m_data.colorDepth);
        cb.ditherStrength = m_data.ditherStrength;
        dc->UpdateSubresource(m_constantBuffer.Get(), 0, nullptr, &cb, 0, 0);
        m_currentData = m_data;
    }

    dc->PSSetShader(m_pixelShader.Get(), nullptr, 0);
    dc->PSSetConstantBuffers(0, 1, m_constantBuffer.GetAddressOf());
    dc->PSSetShaderResources(0, 1, &srcSRV);
    dc->PSSetSamplers(0, 1, m_pointSampler.GetAddressOf());
    dc->Draw(3, 0);
}

void PSXEffect::DrawGUI() noexcept
{
    ImGui::Checkbox("Enable PSX Retro", &m_data.enabled);
    if (!m_data.enabled) return;

    ImGui::SliderFloat("Resolution Width", &m_data.resWidth, 64.0f, 1920.0f, "%.0f");
    ImGui::SliderFloat("Resolution Height", &m_data.resHeight, 48.0f, 1080.0f, "%.0f");
    ImGui::SliderFloat("Color Depth", &m_data.colorDepth, 2.0f, 256.0f, "%.0f");
    ImGui::SliderFloat("Dither Strength", &m_data.ditherStrength, 0.0f, 2.0f);
}

// LensDistortionEffect Implementation
LensDistortionEffect::LensDistortionEffect(ID3D11Device* device)
{
    GpuResourceUtils::LoadPixelShader(device, "Data/Shader/LensDistortionPS.cso", m_pixelShader.GetAddressOf());
    GpuResourceUtils::CreateConstantBuffer(device, sizeof(CbDistortion), m_constantBuffer.GetAddressOf());
    CreateSampler(device, D3D11_FILTER_MIN_MAG_MIP_LINEAR, m_linearSampler.GetAddressOf());
    m_currentData.distortion = -999.0f;
}

void LensDistortionEffect::Draw(ID3D11DeviceContext* dc, ID3D11ShaderResourceView* srcSRV)
{
    if (!(m_currentData == m_data))
    {
        CbDistortion cb{};
        cb.center = m_data.center;
        cb.distortion = std::clamp(m_data.distortion, -0.5f, 0.5f);
        cb.chromaticAberration = m_data.chromaticAberration;
        cb.glitchStrength = m_data.glitchStrength;
        cb.time = m_data.time;
        dc->UpdateSubresource(m_constantBuffer.Get(), 0, nullptr, &cb, 0, 0);
        m_currentData = m_data;
    }

    dc->PSSetShader(m_pixelShader.Get(), nullptr, 0);
    dc->PSSetConstantBuffers(0, 1, m_constantBuffer.GetAddressOf());
    dc->PSSetShaderResources(0, 1, &srcSRV);
    dc->PSSetSamplers(0, 1, m_linearSampler.GetAddressOf());
    dc->Draw(3, 0);
}

void LensDistortionEffect::DrawGUI() noexcept
{
    ImGui::Checkbox("Enable Lens Distortion", &m_data.enabled);
    if (!m_data.enabled) return;

    ImGui::SliderFloat("Fisheye Distortion", &m_data.distortion, -0.2f, 0.2f);
    ImGui::SliderFloat("Chromatic Aberration", &m_data.chromaticAberration, 0.0f, 0.05f);
    ImGui::SliderFloat("Glitch Strength", &m_data.glitchStrength, 0.0f, 1.0f);
}

// RadialBlurEffect Implementation
RadialBlurEffect::RadialBlurEffect(ID3D11Device* device)
{
    GpuResourceUtils::LoadPixelShader(device, "Data/Shader/RadialBlurPS.cso", m_pixelShader.GetAddressOf());
    GpuResourceUtils::CreateConstantBuffer(device, sizeof(CbRadialBlur), m_constantBuffer.GetAddressOf());
    CreateSampler(device, D3D11_FILTER_MIN_MAG_MIP_LINEAR, m_linearSampler.GetAddressOf());
    m_currentData.blurStrength = -1.0f;
}

void RadialBlurEffect::Draw(ID3D11DeviceContext* dc, ID3D11ShaderResourceView* srcSRV)
{
    if (!(m_currentData == m_data))
    {
        CbRadialBlur cb{};
        cb.center = m_data.center;
        cb.blurStrength = std::clamp(m_data.blurStrength, 0.0f, 0.2f);
        dc->UpdateSubresource(m_constantBuffer.Get(), 0, nullptr, &cb, 0, 0);
        m_currentData = m_data;
    }

    dc->PSSetShader(m_pixelShader.Get(), nullptr, 0);
    dc->PSSetConstantBuffers(0, 1, m_constantBuffer.GetAddressOf());
    dc->PSSetShaderResources(0, 1, &srcSRV);
    dc->PSSetSamplers(0, 1, m_linearSampler.GetAddressOf());
    dc->Draw(3, 0);
}

void RadialBlurEffect::DrawGUI() noexcept
{
    ImGui::Checkbox("Enable Radial Blur", &m_data.enabled);
    if (!m_data.enabled) return;

    ImGui::SliderFloat("Blur Strength", &m_data.blurStrength, 0.0f, 0.1f);
}

// VignetteEffect Implementation
VignetteEffect::VignetteEffect(ID3D11Device* device)
{
    GpuResourceUtils::LoadPixelShader(device, "Data/Shader/VignettePS.cso", m_pixelShader.GetAddressOf());
    GpuResourceUtils::CreateConstantBuffer(device, sizeof(CbVignette), m_constantBuffer.GetAddressOf());
    CreateSampler(device, D3D11_FILTER_MIN_MAG_MIP_LINEAR, m_linearSampler.GetAddressOf());
    m_currentData.intensity = -1.0f;
}

void VignetteEffect::Draw(ID3D11DeviceContext* dc, ID3D11ShaderResourceView* srcSRV)
{
    if (!(m_currentData == m_data))
    {
        constexpr float intensityFactor{ 3.0f };
        constexpr float smoothnessFactor{ 5.0f };
        constexpr float roundnessPower{ 6.0f };

        CbVignette cb{};
        cb.color = m_data.color;
        cb.center = m_data.center;
        cb.intensity = m_data.intensity * intensityFactor;
        cb.smoothness = (std::max)(0.000001f, m_data.smoothness * smoothnessFactor);
        cb.rounded = m_data.rounded ? 1.0f : 0.0f;
        cb.roundness = roundnessPower * (1.0f - m_data.roundness) + m_data.roundness;

        dc->UpdateSubresource(m_constantBuffer.Get(), 0, nullptr, &cb, 0, 0);
        m_currentData = m_data;
    }

    dc->PSSetShader(m_pixelShader.Get(), nullptr, 0);
    dc->PSSetConstantBuffers(0, 1, m_constantBuffer.GetAddressOf());
    dc->PSSetShaderResources(0, 1, &srcSRV);
    dc->PSSetSamplers(0, 1, m_linearSampler.GetAddressOf());
    dc->Draw(3, 0);
}

void VignetteEffect::DrawGUI() noexcept
{
    ImGui::Checkbox("Enable Vignette", &m_data.enabled);
    if (!m_data.enabled) return;

    ImGui::ColorEdit4("Color", &m_data.color.x);
    ImGui::SliderFloat("Intensity", &m_data.intensity, 0.0f, 5.0f);
    ImGui::SliderFloat("Smoothness", &m_data.smoothness, 0.001f, 7.0f);
    ImGui::Checkbox("Rounded", &m_data.rounded);
    ImGui::SliderFloat("Roundness", &m_data.roundness, 0.0f, 1.0f);
}

// ScanlineEffect Implementation
ScanlineEffect::ScanlineEffect(ID3D11Device* device)
{
    GpuResourceUtils::LoadPixelShader(device, "Data/Shader/ScanlinePS.cso", m_pixelShader.GetAddressOf());
    GpuResourceUtils::CreateConstantBuffer(device, sizeof(CbScanline), m_constantBuffer.GetAddressOf());
    CreateSampler(device, D3D11_FILTER_MIN_MAG_MIP_LINEAR, m_linearSampler.GetAddressOf());
    m_currentData.scanlineStrength = -1.0f;
}

void ScanlineEffect::Draw(ID3D11DeviceContext* dc, ID3D11ShaderResourceView* srcSRV)
{
    if (!(m_currentData == m_data))
    {
        CbScanline cb{};
        cb.scanlineStrength = m_data.scanlineStrength;
        cb.scanlineSpeed = m_data.scanlineSpeed;
        cb.scanlineSize = m_data.scanlineSize;
        cb.fineOpacity = m_data.fineOpacity;
        cb.fineDensity = m_data.fineDensity;
        cb.fineRotation = m_data.fineRotation;
        cb.time = m_data.time;

        dc->UpdateSubresource(m_constantBuffer.Get(), 0, nullptr, &cb, 0, 0);
        m_currentData = m_data;
    }

    dc->PSSetShader(m_pixelShader.Get(), nullptr, 0);
    dc->PSSetConstantBuffers(0, 1, m_constantBuffer.GetAddressOf());
    dc->PSSetShaderResources(0, 1, &srcSRV);
    dc->PSSetSamplers(0, 1, m_linearSampler.GetAddressOf());
    dc->Draw(3, 0);
}

void ScanlineEffect::DrawGUI() noexcept
{
    ImGui::Checkbox("Enable Scanlines", &m_data.enabled);
    if (!m_data.enabled) return;

    ImGui::SliderFloat("Scanline Strength", &m_data.scanlineStrength, 0.0f, 1.0f);
    ImGui::SliderFloat("Scanline Speed", &m_data.scanlineSpeed, 0.0f, 100.0f);
    ImGui::SliderFloat("Scanline Size", &m_data.scanlineSize, 1.0f, 500.0f);
    ImGui::SliderFloat("Fine Opacity", &m_data.fineOpacity, 0.0f, 1.0f);
    ImGui::SliderFloat("Fine Density", &m_data.fineDensity, 0.0f, 100.0f);
    ImGui::SliderFloat("Fine Rotation", &m_data.fineRotation, 0.0f, 6.28f);
}

// Serialization Implementations

// PSXEffect Serialization
void PSXEffect::Serialize(nlohmann::json& out) const
{
    out["enabled"] = m_data.enabled;
    out["resWidth"] = m_data.resWidth;
    out["resHeight"] = m_data.resHeight;
    out["colorDepth"] = m_data.colorDepth;
    out["ditherStrength"] = m_data.ditherStrength;
}

void PSXEffect::Deserialize(const nlohmann::json& in)
{
    m_data.enabled = in.value("enabled", false);
    m_data.resWidth = in.value("resWidth", 320.0f);
    m_data.resHeight = in.value("resHeight", 240.0f);
    m_data.colorDepth = in.value("colorDepth", 32.0f);
    m_data.ditherStrength = in.value("ditherStrength", 1.0f);
}

void PSXEffect::ResetToDefault() noexcept
{
    m_data = Data{};
}

// LensDistortionEffect Serialization
void LensDistortionEffect::Serialize(nlohmann::json& out) const
{
    out["enabled"] = m_data.enabled;
    out["distortion"] = m_data.distortion;
    out["chromaticAberration"] = m_data.chromaticAberration;
    out["glitchStrength"] = m_data.glitchStrength;
    out["time"] = m_data.time;
    out["center"] = { m_data.center.x, m_data.center.y };
}

void LensDistortionEffect::Deserialize(const nlohmann::json& in)
{
    m_data.enabled = in.value("enabled", false);
    m_data.distortion = in.value("distortion", 0.0f);
    m_data.chromaticAberration = in.value("chromaticAberration", 0.0f);
    m_data.glitchStrength = in.value("glitchStrength", 0.0f);
    m_data.time = in.value("time", 0.0f);

    if (in.contains("center") && in["center"].is_array() && in["center"].size() == 2)
    {
        m_data.center.x = in["center"][0];
        m_data.center.y = in["center"][1];
    }
}

void LensDistortionEffect::ResetToDefault() noexcept
{
    m_data = Data{};
}

// RadialBlurEffect Serialization
void RadialBlurEffect::Serialize(nlohmann::json& out) const
{
    out["enabled"] = m_data.enabled;
    out["blurStrength"] = m_data.blurStrength;
    out["center"] = { m_data.center.x, m_data.center.y };
}

void RadialBlurEffect::Deserialize(const nlohmann::json& in)
{
    m_data.enabled = in.value("enabled", false);
    m_data.blurStrength = in.value("blurStrength", 0.0f);

    if (in.contains("center") && in["center"].is_array() && in["center"].size() == 2)
    {
        m_data.center.x = in["center"][0];
        m_data.center.y = in["center"][1];
    }
}

void RadialBlurEffect::ResetToDefault() noexcept
{
    m_data = Data{};
}

// VignetteEffect Serialization
void VignetteEffect::Serialize(nlohmann::json& out) const
{
    out["enabled"] = m_data.enabled;
    out["color"] = { m_data.color.x, m_data.color.y, m_data.color.z, m_data.color.w };
    out["center"] = { m_data.center.x, m_data.center.y };
    out["intensity"] = m_data.intensity;
    out["smoothness"] = m_data.smoothness;
    out["rounded"] = m_data.rounded;
    out["roundness"] = m_data.roundness;
}

void VignetteEffect::Deserialize(const nlohmann::json& in)
{
    m_data.enabled = in.value("enabled", false);

    if (in.contains("color") && in["color"].is_array() && in["color"].size() == 4)
    {
        m_data.color.x = in["color"][0];
        m_data.color.y = in["color"][1];
        m_data.color.z = in["color"][2];
        m_data.color.w = in["color"][3];
    }

    if (in.contains("center") && in["center"].is_array() && in["center"].size() == 2)
    {
        m_data.center.x = in["center"][0];
        m_data.center.y = in["center"][1];
    }

    m_data.intensity = in.value("intensity", 0.0f);
    m_data.smoothness = in.value("smoothness", 0.0f);
    m_data.rounded = in.value("rounded", false);
    m_data.roundness = in.value("roundness", 0.0f);
}

void VignetteEffect::ResetToDefault() noexcept
{
    m_data = Data{};
}

// ScanlineEffect Serialization
void ScanlineEffect::Serialize(nlohmann::json& out) const
{
    out["enabled"] = m_data.enabled;
    out["scanlineStrength"] = m_data.scanlineStrength;
    out["scanlineSpeed"] = m_data.scanlineSpeed;
    out["scanlineSize"] = m_data.scanlineSize;
    out["fineOpacity"] = m_data.fineOpacity;
    out["fineDensity"] = m_data.fineDensity;
    out["fineRotation"] = m_data.fineRotation;
    out["time"] = m_data.time;
}

void ScanlineEffect::Deserialize(const nlohmann::json& in)
{
    m_data.enabled = in.value("enabled", false);
    m_data.scanlineStrength = in.value("scanlineStrength", 0.0f);
    m_data.scanlineSpeed = in.value("scanlineSpeed", 0.0f);
    m_data.scanlineSize = in.value("scanlineSize", 0.0f);
    m_data.fineOpacity = in.value("fineOpacity", 0.0f);
    m_data.fineDensity = in.value("fineDensity", 0.0f);
    m_data.fineRotation = in.value("fineRotation", 0.0f);
    m_data.time = in.value("time", 0.0f);
}

void ScanlineEffect::ResetToDefault() noexcept
{
    m_data = Data{};
}

// BloomEffect Implementation
BloomEffect::BloomEffect(ID3D11Device* device)
{
    GpuResourceUtils::LoadPixelShader(device, "Data/Shader/BloomPS.cso", m_pixelShader.GetAddressOf());

    // Allocate a dynamic constant buffer for zero-stall Map(WRITE_DISCARD) updates across mip passes
    D3D11_BUFFER_DESC cbDesc{};
    cbDesc.ByteWidth = sizeof(CbBloom);
    cbDesc.Usage = D3D11_USAGE_DYNAMIC;
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    device->CreateBuffer(&cbDesc, nullptr, m_constantBuffer.GetAddressOf());

    CreateSampler(device, D3D11_FILTER_MIN_MAG_MIP_LINEAR, m_linearSampler.GetAddressOf());
    CreateSampler(device, D3D11_FILTER_MIN_MAG_MIP_POINT, m_pointSampler.GetAddressOf());

    // ONE + ONE additive blend state for in-place upsample accumulation
    D3D11_BLEND_DESC blendDesc{};
    blendDesc.RenderTarget[0].BlendEnable = TRUE;
    blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
    blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
    blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
    blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    device->CreateBlendState(&blendDesc, m_additiveBlendState.GetAddressOf());

    CreateMipChain(device, m_width, m_height);
}

void BloomEffect::CreateMipChain(ID3D11Device* device, int width, int height) noexcept
{
    m_width = (std::max)(1, width);
    m_height = (std::max)(1, height);

    int mipW{ (std::max)(1, m_width / 2) };
    int mipH{ (std::max)(1, m_height / 2) };

    for (auto& mip : m_mipChain)
    {
        mip.Reset();
        mip.width = mipW;
        mip.height = mipH;

        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = static_cast<UINT>(mipW);
        desc.Height = static_cast<UINT>(mipH);
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R11G11B10_FLOAT;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

        device->CreateTexture2D(&desc, nullptr, mip.texture.GetAddressOf());
        device->CreateRenderTargetView(mip.texture.Get(), nullptr, mip.rtv.GetAddressOf());
        device->CreateShaderResourceView(mip.texture.Get(), nullptr, mip.srv.GetAddressOf());

        mipW = (std::max)(1, mipW / 2);
        mipH = (std::max)(1, mipH / 2);
    }
}

void BloomEffect::OnResize(ID3D11Device* device, int width, int height) noexcept
{
    if (width <= 0 || height <= 0) return;
    if (width == m_width && height == m_height) return;
    CreateMipChain(device, width, height);
}

void BloomEffect::UpdateConstantBuffer(ID3D11DeviceContext* dc, const CbBloom& cb) const noexcept
{
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (SUCCEEDED(dc->Map(m_constantBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
    {
        *static_cast<CbBloom*>(mapped.pData) = cb;
        dc->Unmap(m_constantBuffer.Get(), 0);
    }
}

void BloomEffect::Draw(ID3D11DeviceContext* dc, ID3D11ShaderResourceView* srcSRV)
{
    // Cache destination ping-pong RTV and fullscreen viewport bound by PostProcessManager
    ID3D11RenderTargetView* destRTV{ nullptr };
    dc->OMGetRenderTargets(1, &destRTV, nullptr);

    UINT vpCount{ 1 };
    D3D11_VIEWPORT destViewport{};
    dc->RSGetViewports(&vpCount, &destViewport);

    auto* const rs{ Graphics::Instance().GetRenderState() };
    ID3D11ShaderResourceView* const nullSRV[]{ nullptr };

    // Pre-linearize sRGB bloom tint on the CPU and pre-multiply by intensity
    const float safeIntensity{ (std::max)(0.0f, m_data.intensity) };
    const DirectX::XMFLOAT4 linearTint{
        std::pow((std::max)(0.0f, m_data.tint.x), 2.2f) * safeIntensity,
        std::pow((std::max)(0.0f, m_data.tint.y), 2.2f) * safeIntensity,
        std::pow((std::max)(0.0f, m_data.tint.z), 2.2f) * safeIntensity,
        1.0f
    };

    CbBloom cb{};
    cb.threshold = (std::max)(0.0f, m_data.threshold);
    cb.softKnee = std::clamp(m_data.softKnee, 0.0f, 1.0f);
    cb.bloomTintAndIntensity = linearTint;
    cb.scatter = std::clamp(m_data.scatter, 0.05f, 0.95f);

    dc->PSSetShader(m_pixelShader.Get(), nullptr, 0);
    dc->PSSetConstantBuffers(0, 1, m_constantBuffer.GetAddressOf());

    ID3D11SamplerState* const samplers[]{ m_linearSampler.Get(), m_pointSampler.Get() };
    dc->PSSetSamplers(0, 2, samplers);

    // DOWNSAMPLE PYRAMID (Full-Res -> 1/2 -> 1/4 -> 1/8 -> 1/16 -> 1/32)
    dc->OMSetBlendState(rs->GetBlendState(BlendState::Opaque), nullptr, 0xFFFFFFFF);

    for (std::size_t i{ 0 }; i < kMipCount; ++i)
    {
        D3D11_VIEWPORT mipVP{};
        mipVP.Width = static_cast<float>(m_mipChain[i].width);
        mipVP.Height = static_cast<float>(m_mipChain[i].height);
        mipVP.MinDepth = 0.0f;
        mipVP.MaxDepth = 1.0f;
        dc->RSSetViewports(1, &mipVP);

        ID3D11RenderTargetView* const mipRTV{ m_mipChain[i].rtv.Get() };
        dc->OMSetRenderTargets(1, &mipRTV, nullptr);

        const int srcW{ (i == 0) ? m_width : m_mipChain[i - 1].width };
        const int srcH{ (i == 0) ? m_height : m_mipChain[i - 1].height };
        cb.texelSize = { 1.0f / static_cast<float>(srcW), 1.0f / static_cast<float>(srcH) };
        cb.passMode = (i == 0) ? 0 : 1; // 0 = Karis Prefilter + 13-Tap, 1 = 13-Tap Downsample
        UpdateConstantBuffer(dc, cb);

        ID3D11ShaderResourceView* const inputSRV{ (i == 0) ? srcSRV : m_mipChain[i - 1].srv.Get() };
        dc->PSSetShaderResources(0, 1, &inputSRV);
        dc->Draw(3, 0);
        dc->PSSetShaderResources(0, 1, nullSRV);
    }

    // UPSAMPLE PYRAMID (1/32 -> 1/16 -> 1/8 -> 1/4 -> 1/2 via In-Place Additive Tent Filter)
    dc->OMSetBlendState(m_additiveBlendState.Get(), nullptr, 0xFFFFFFFF);

    for (std::size_t i{ kMipCount - 1 }; i > 0; --i)
    {
        const std::size_t dstIdx{ i - 1 };

        D3D11_VIEWPORT mipVP{};
        mipVP.Width = static_cast<float>(m_mipChain[dstIdx].width);
        mipVP.Height = static_cast<float>(m_mipChain[dstIdx].height);
        mipVP.MinDepth = 0.0f;
        mipVP.MaxDepth = 1.0f;
        dc->RSSetViewports(1, &mipVP);

        ID3D11RenderTargetView* const mipRTV{ m_mipChain[dstIdx].rtv.Get() };
        dc->OMSetRenderTargets(1, &mipRTV, nullptr);

        cb.texelSize = {
            1.0f / static_cast<float>(m_mipChain[i].width),
            1.0f / static_cast<float>(m_mipChain[i].height)
        };
        cb.passMode = 2; // 2 = 9-Tap 3x3 Tent Filter Upsample
        UpdateConstantBuffer(dc, cb);

        ID3D11ShaderResourceView* const lowResSRV{ m_mipChain[i].srv.Get() };
        dc->PSSetShaderResources(0, 1, &lowResSRV);
        dc->Draw(3, 0);
        dc->PSSetShaderResources(0, 1, nullSRV);
    }

    // FINAL COMPOSITE PASS (Combine Full-Res Scene with Upsampled Mip 0 into Destination RTV)
    dc->OMSetBlendState(rs->GetBlendState(BlendState::Opaque), nullptr, 0xFFFFFFFF);
    dc->OMSetRenderTargets(1, &destRTV, nullptr);
    dc->RSSetViewports(1, &destViewport);

    cb.texelSize = { 1.0f / static_cast<float>(m_mipChain[0].width), 1.0f / static_cast<float>(m_mipChain[0].height) };
    cb.passMode = 3; // 3 = Final Composite
    UpdateConstantBuffer(dc, cb);

    ID3D11ShaderResourceView* const bloomSRV{ m_mipChain[0].srv.Get() };
    dc->PSSetShaderResources(0, 1, &srcSRV);
    dc->PSSetShaderResources(3, 1, &bloomSRV);
    dc->Draw(3, 0);

    // Unbind bloom SRV at slot 3 to prevent hazards on the next frame
    dc->PSSetShaderResources(3, 1, nullSRV);

    if (destRTV)
    {
        destRTV->Release();
    }
}

void BloomEffect::DrawGUI() noexcept
{
    ImGui::Checkbox("Enable Bloom", &m_data.enabled);
    if (!m_data.enabled) return;

    ImGui::ColorEdit3("Bloom Tint", &m_data.tint.x);
    ImGui::SliderFloat("Intensity", &m_data.intensity, 0.0f, 3.0f, "%.2f");
    ImGui::SliderFloat("Threshold (HDR)", &m_data.threshold, 0.1f, 5.0f, "%.2f");
    ImGui::SliderFloat("Soft Knee", &m_data.softKnee, 0.0f, 1.0f, "%.2f");
    ImGui::SliderFloat("Scatter Radius", &m_data.scatter, 0.1f, 0.95f, "%.2f");
}

void BloomEffect::Serialize(nlohmann::json& out) const
{
    out["enabled"] = m_data.enabled;
    out["threshold"] = m_data.threshold;
    out["softKnee"] = m_data.softKnee;
    out["intensity"] = m_data.intensity;
    out["scatter"] = m_data.scatter;
    out["tint"] = { m_data.tint.x, m_data.tint.y, m_data.tint.z, m_data.tint.w };
}

void BloomEffect::Deserialize(const nlohmann::json& in)
{
    m_data.enabled = in.value("enabled", true);
    m_data.threshold = in.value("threshold", 1.0f);
    m_data.softKnee = in.value("softKnee", 0.5f);
    m_data.intensity = in.value("intensity", 0.65f);
    m_data.scatter = in.value("scatter", 0.75f);
    if (in.contains("tint") && in["tint"].is_array() && in["tint"].size() == 4)
    {
        m_data.tint.x = in["tint"][0];
        m_data.tint.y = in["tint"][1];
        m_data.tint.z = in["tint"][2];
        m_data.tint.w = in["tint"][3];
    }
}

void BloomEffect::ResetToDefault() noexcept
{
    m_data = Data{};
}