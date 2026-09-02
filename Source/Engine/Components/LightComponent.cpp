#include <algorithm>
#include <imgui.h>
#include "GameObject.h"
#include "LightComponent.h"
#include "System/Graphics.h"

LightComponent::~LightComponent()
{
    // Auto-unregister to prevent dangling pointers in LightManager
    Graphics::Instance().GetLightManager().UnregisterLight(this);
}

void LightComponent::OnAttach(GameObject* const owner) noexcept
{
    m_owner = owner;
    // Register immediately with the engine's active LightManager
    Graphics::Instance().GetLightManager().RegisterLight(this);
}

DirectX::XMFLOAT3 LightComponent::GetDirection() const noexcept
{
    if (!m_owner) return { 0.0f, -1.0f, 0.0f };

    // Extract world transformation matrix
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

void LightComponent::DrawInspector()
{
    // Light type selector
    static constexpr const char* const s_lightTypes[]{ "Directional", "Point", "Spot" };
    int currentType{ static_cast<int>(m_type) };
    if (ImGui::Combo("Light Type", &currentType, s_lightTypes, IM_ARRAYSIZE(s_lightTypes)))
    {
        m_type = static_cast<LightType>(currentType);
    }

    ImGui::ColorEdit3("Color", &m_color.x);
    ImGui::DragFloat("Intensity", &m_intensity, 0.1f, 0.0f, 100.0f, "%.2f");

    // Contextual property drawing depending on light type
    switch (m_type)
    {
    case LightType::Directional:
        ImGui::TextDisabled("Direction is controlled via GameObject Rotation.");
        break;

    case LightType::Point:
        ImGui::DragFloat("Range", &m_range, 0.2f, 0.1f, 1000.0f, "%.1f");
        break;

    case LightType::Spot:
        ImGui::DragFloat("Range", &m_range, 0.2f, 0.1f, 1000.0f, "%.1f");
        ImGui::SliderFloat("Spot Angle", &m_spotAngle, 1.0f, 179.0f, "%.1f deg");
        break;
    }
}

// Automatically registers LightComponent into the ComponentRegistry factory
REGISTER_COMPONENT(LightComponent)