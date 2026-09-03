#include <algorithm>
#include <imgui.h>
#include "GameObject.h"
#include "LightComponent.h"
#include "System/Graphics.h"

LightComponent::~LightComponent()
{
    Graphics::Instance().GetLightManager().UnregisterLight(this);
}

void LightComponent::OnAttach(GameObject* const owner) noexcept
{
    m_owner = owner;
    Graphics::Instance().GetLightManager().RegisterLight(this);
}

DirectX::XMFLOAT3 LightComponent::GetDirection() const noexcept
{
    if (!m_owner) return { 0.0f, -1.0f, 0.0f };

    const DirectX::XMFLOAT4X4 world{ m_owner->transform.GetWorldMatrix() };
    DirectX::XMVECTOR forward{ DirectX::XMVectorSet(world._31, world._32, world._33, 0.0f) };

    if (DirectX::XMVector3Equal(forward, DirectX::XMVectorZero()))
    {
        return { 0.0f, -1.0f, 0.0f };
    }

    forward = DirectX::XMVector3Normalize(forward);
    DirectX::XMFLOAT3 result{};
    DirectX::XMStoreFloat3(&result, forward);
    return result;
}

DirectX::XMFLOAT3 LightComponent::GetWorldPosition() const noexcept
{
    if (!m_owner) return { 0.0f, 0.0f, 0.0f };
    const DirectX::XMFLOAT4X4 world{ m_owner->transform.GetWorldMatrix() };
    return { world._41, world._42, world._43 };
}

// Directional Light 
void DirectionalLightComponent::DrawInspector()
{
    ImGui::ColorEdit3("Color", &m_color.x);
    ImGui::DragFloat("Intensity", &m_intensity, 0.1f, 0.0f, 100.0f, "%.2f");
    ImGui::Spacing();
    ImGui::TextDisabled("Direction is controlled via GameObject Rotation.");
}

void DirectionalLightComponent::Serialize(nlohmann::json& outJson) const
{
    outJson["Color"] = { m_color.x, m_color.y, m_color.z };
    outJson["Intensity"] = m_intensity;
}

void DirectionalLightComponent::Deserialize(const nlohmann::json& inJson)
{
    if (inJson.contains("Color"))
    {
        m_color = { inJson["Color"][0], inJson["Color"][1], inJson["Color"][2] };
    }
    m_intensity = inJson.value("Intensity", 1.0f);
}
REGISTER_COMPONENT(DirectionalLightComponent)


// Point Light 
void PointLightComponent::DrawInspector()
{
    ImGui::ColorEdit3("Color", &m_color.x);
    ImGui::DragFloat("Intensity", &m_intensity, 0.1f, 0.0f, 100.0f, "%.2f");
    ImGui::DragFloat("Range", &m_range, 0.2f, 0.1f, 1000.0f, "%.1f");
}

void PointLightComponent::Serialize(nlohmann::json& outJson) const
{
    outJson["Color"] = { m_color.x, m_color.y, m_color.z };
    outJson["Intensity"] = m_intensity;
    outJson["Range"] = m_range;
}

void PointLightComponent::Deserialize(const nlohmann::json& inJson)
{
    if (inJson.contains("Color"))
    {
        m_color = { inJson["Color"][0], inJson["Color"][1], inJson["Color"][2] };
    }
    m_intensity = inJson.value("Intensity", 1.0f);
    m_range = inJson.value("Range", 10.0f);
}
REGISTER_COMPONENT(PointLightComponent)


// Spot Light 
void SpotLightComponent::DrawInspector()
{
    ImGui::ColorEdit3("Color", &m_color.x);
    ImGui::DragFloat("Intensity", &m_intensity, 0.1f, 0.0f, 100.0f, "%.2f");
    ImGui::DragFloat("Range", &m_range, 0.2f, 0.1f, 1000.0f, "%.1f");
    ImGui::SliderFloat("Spot Angle", &m_spotAngle, 1.0f, 179.0f, "%.1f deg");
}

void SpotLightComponent::Serialize(nlohmann::json& outJson) const
{
    outJson["Color"] = { m_color.x, m_color.y, m_color.z };
    outJson["Intensity"] = m_intensity;
    outJson["Range"] = m_range;
    outJson["SpotAngle"] = m_spotAngle;
}

void SpotLightComponent::Deserialize(const nlohmann::json& inJson)
{
    if (inJson.contains("Color"))
    {
        m_color = { inJson["Color"][0], inJson["Color"][1], inJson["Color"][2] };
    }
    m_intensity = inJson.value("Intensity", 1.0f);
    m_range = inJson.value("Range", 10.0f);
    m_spotAngle = inJson.value("SpotAngle", 45.0f);
}
REGISTER_COMPONENT(SpotLightComponent)