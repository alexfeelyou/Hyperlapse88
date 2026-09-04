#include <imgui.h>
#include "CameraController.h"
#include "Camera.h"
#include "DepthFogEffect.h"

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
    float currentNearZ{ 0.1f };
    float currentFarZ{ 1000.0f };

    // Fetch the live camera projection mathematically 
    if (const auto camera{ CameraController::Instance().GetActiveCamera() })
    {
        currentNearZ = camera->GetNearZ();
        currentFarZ = camera->GetFarZ();
    }

    // Compare against the dedicated camera cache variables
    if (!(m_currentData == m_data) || m_cachedNearZ != currentNearZ || m_cachedFarZ != currentFarZ)
    {
        CbFog cb{};
        cb.fogColor = m_data.color;
        cb.fogStart = m_data.startDistance;
        cb.fogEnd = m_data.endDistance;
        cb.nearZ = currentNearZ;
        cb.farZ = currentFarZ;

        dc->UpdateSubresource(m_constantBuffer.Get(), 0, nullptr, &cb, 0, 0);

        m_currentData = m_data;       // Cache the inspector state
        m_cachedNearZ = currentNearZ; // Cache the camera near plane
        m_cachedFarZ = currentFarZ;   // Cache the camera far plane
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

    ImGui::ColorEdit4("Fog Color", &m_data.color.x);
    ImGui::SliderFloat("Start Distance", &m_data.startDistance, 0.0f, 100.0f);
    ImGui::SliderFloat("End Distance", &m_data.endDistance, 10.0f, 500.0f);
}

void DepthFogEffect::Serialize(nlohmann::json& out) const
{
    out["enabled"] = m_data.enabled;
    out["color"] = { m_data.color.x, m_data.color.y, m_data.color.z, m_data.color.w };
    out["startDistance"] = m_data.startDistance;
    out["endDistance"] = m_data.endDistance;
}

void DepthFogEffect::Deserialize(const nlohmann::json& in)
{
    m_data.enabled = in.value("enabled", false);
    if (in.contains("color") && in["color"].is_array() && in["color"].size() == 4) {
        m_data.color.x = in["color"][0];
        m_data.color.y = in["color"][1];
        m_data.color.z = in["color"][2];
        m_data.color.w = in["color"][3];
    }
    m_data.startDistance = in.value("startDistance", 10.0f);
    m_data.endDistance = in.value("endDistance", 80.0f);
}

void DepthFogEffect::ResetToDefault() noexcept
{
    m_data = Data{};
}