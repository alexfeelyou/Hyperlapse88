#include <cmath>
#include <cstring>
#include <imgui.h>
#include "System/Graphics.h"
#include "CameraController.h"
#include "Camera.h"
#include "DepthFogEffect.h"

namespace
{
    // Pre-linearizes sRGB color on the CPU to eliminate per-pixel pow(2.2) ALU cost in the fullscreen shader
    [[nodiscard]] DirectX::XMFLOAT4 LinearizeSRGB(const DirectX::XMFLOAT4& srgb, float hdrMultiplier = 1.0f) noexcept
    {
        return {
            std::pow((std::max)(0.0f, srgb.x), 2.2f) * hdrMultiplier,
            std::pow((std::max)(0.0f, srgb.y), 2.2f) * hdrMultiplier,
            std::pow((std::max)(0.0f, srgb.z), 2.2f) * hdrMultiplier,
            srgb.w
        };
    }
}

DepthFogEffect::DepthFogEffect(ID3D11Device* device)
{
    GpuResourceUtils::LoadPixelShader(device, "Data/Shader/DepthFogPS.cso", m_pixelShader.GetAddressOf());
    GpuResourceUtils::CreateConstantBuffer(device, sizeof(CbFog), m_constantBuffer.GetAddressOf());

    D3D11_SAMPLER_DESC desc{};
    desc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    device->CreateSamplerState(&desc, m_pointSampler.GetAddressOf());
}

void DepthFogEffect::Draw(ID3D11DeviceContext* dc, ID3D11ShaderResourceView* srcSRV)
{
    CbFog cb{};

    if (const auto camera{ CameraController::Instance().GetActiveCamera() })
    {
        const DirectX::XMMATRIX view{ DirectX::XMLoadFloat4x4(&camera->GetView()) };
        const DirectX::XMMATRIX proj{ DirectX::XMLoadFloat4x4(&camera->GetProjection()) };
        const DirectX::XMMATRIX invViewProj{ DirectX::XMMatrixInverse(nullptr, DirectX::XMMatrixMultiply(view, proj)) };
        DirectX::XMStoreFloat4x4(&cb.inverseViewProjection, invViewProj);

        const DirectX::XMFLOAT3& camPos{ camera->GetPosition() };
        cb.cameraPosition = { camPos.x, camPos.y, camPos.z, 1.0f };
    }
    else
    {
        DirectX::XMStoreFloat4x4(&cb.inverseViewProjection, DirectX::XMMatrixIdentity());
    }

    // Extract normalized vector pointing TOWARD the sun (-lightDirection) for Mie forward scattering
    const DirectionalLight& dirLight{ Graphics::Instance().GetLightManager().GetDirectionalLight() };
    DirectX::XMVECTOR toSun{ DirectX::XMVectorSet(-dirLight.direction.x, -dirLight.direction.y, -dirLight.direction.z, 0.0f) };
    if (DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(toSun)) > 0.0001f)
    {
        toSun = DirectX::XMVector3Normalize(toSun);
    }
    else
    {
        toSun = DirectX::XMVectorSet(0.0f, 0.707f, 0.707f, 0.0f);
    }

    DirectX::XMFLOAT3 sunDir{};
    DirectX::XMStoreFloat3(&sunDir, toSun);

    cb.fogColor = LinearizeSRGB(m_data.color, 1.0f);
    cb.sunInscatterColor = LinearizeSRGB(m_data.sunInscatterColor, (std::max)(0.0f, m_data.sunInscatterIntensity));
    cb.sunDirectionAndPhase = {
        sunDir.x,
        sunDir.y,
        sunDir.z,
        (std::max)(1.0f, m_data.sunInscatterExponent)
    };
    cb.fogStart = m_data.startDistance;
    cb.fogEnd = (std::max)(m_data.startDistance + 0.01f, m_data.endDistance);
    cb.heightFalloff = (std::max)(0.0f, m_data.heightFalloff);
    cb.heightOffset = m_data.heightOffset;
    cb.excludeSkybox = m_data.excludeSkybox ? 1 : 0;

    // Upload to GPU only when camera matrices, sun direction, or inspector parameters change
    if (!m_hasCachedCb || std::memcmp(&cb, &m_cachedCb, sizeof(CbFog)) != 0)
    {
        dc->UpdateSubresource(m_constantBuffer.Get(), 0, nullptr, &cb, 0, 0);
        m_cachedCb = cb;
        m_hasCachedCb = true;
    }

    dc->PSSetShader(m_pixelShader.Get(), nullptr, 0);
    dc->PSSetConstantBuffers(0, 1, m_constantBuffer.GetAddressOf());
    dc->PSSetShaderResources(0, 1, &srcSRV);
    dc->PSSetSamplers(0, 1, m_pointSampler.GetAddressOf());
    dc->Draw(3, 0);
}

void DepthFogEffect::DrawGUI() noexcept
{
    ImGui::Checkbox("Enable Depth Fog", &m_data.enabled);
    if (!m_data.enabled) return;

    ImGui::Checkbox("Exclude Skybox", &m_data.excludeSkybox);
    ImGui::ColorEdit4("Horizon Fog Color", &m_data.color.x);
    ImGui::SliderFloat("Start Distance", &m_data.startDistance, 0.0f, 100.0f);
    ImGui::SliderFloat("End Distance", &m_data.endDistance, 10.0f, 1000.0f);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextDisabled("SUN INSCATTERING (MIE GLOW)");
    ImGui::ColorEdit4("Sun Glow Color", &m_data.sunInscatterColor.x);
    ImGui::SliderFloat("Sun Glow Intensity (HDR)", &m_data.sunInscatterIntensity, 0.0f, 10.0f, "%.2f");
    ImGui::SliderFloat("Sun Phase Exponent", &m_data.sunInscatterExponent, 1.0f, 32.0f, "%.1f");

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextDisabled("ALTITUDE ATTENUATION");
    ImGui::SliderFloat("Height Falloff", &m_data.heightFalloff, 0.0f, 0.2f, "%.4f");
    ImGui::DragFloat("Base Altitude Offset", &m_data.heightOffset, 0.5f, -500.0f, 500.0f, "%.1f");
}

void DepthFogEffect::Serialize(nlohmann::json& out) const
{
    out["enabled"] = m_data.enabled;
    out["excludeSkybox"] = m_data.excludeSkybox;
    out["color"] = { m_data.color.x, m_data.color.y, m_data.color.z, m_data.color.w };
    out["sunInscatterColor"] = { m_data.sunInscatterColor.x, m_data.sunInscatterColor.y, m_data.sunInscatterColor.z, m_data.sunInscatterColor.w };
    out["startDistance"] = m_data.startDistance;
    out["endDistance"] = m_data.endDistance;
    out["sunInscatterIntensity"] = m_data.sunInscatterIntensity;
    out["sunInscatterExponent"] = m_data.sunInscatterExponent;
    out["heightFalloff"] = m_data.heightFalloff;
    out["heightOffset"] = m_data.heightOffset;
}

void DepthFogEffect::Deserialize(const nlohmann::json& in)
{
    m_data.enabled = in.value("enabled", false);
    m_data.excludeSkybox = in.value("excludeSkybox", true);
    if (in.contains("color") && in["color"].is_array() && in["color"].size() == 4)
    {
        m_data.color.x = in["color"][0];
        m_data.color.y = in["color"][1];
        m_data.color.z = in["color"][2];
        m_data.color.w = in["color"][3];
    }
    if (in.contains("sunInscatterColor") && in["sunInscatterColor"].is_array() && in["sunInscatterColor"].size() == 4)
    {
        m_data.sunInscatterColor.x = in["sunInscatterColor"][0];
        m_data.sunInscatterColor.y = in["sunInscatterColor"][1];
        m_data.sunInscatterColor.z = in["sunInscatterColor"][2];
        m_data.sunInscatterColor.w = in["sunInscatterColor"][3];
    }
    m_data.startDistance = in.value("startDistance", 10.0f);
    m_data.endDistance = in.value("endDistance", 120.0f);
    m_data.sunInscatterIntensity = in.value("sunInscatterIntensity", 2.5f);
    m_data.sunInscatterExponent = in.value("sunInscatterExponent", 8.0f);
    m_data.heightFalloff = in.value("heightFalloff", 0.015f);
    m_data.heightOffset = in.value("heightOffset", 0.0f);
}

void DepthFogEffect::ResetToDefault() noexcept
{
    m_data = Data{};
}