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