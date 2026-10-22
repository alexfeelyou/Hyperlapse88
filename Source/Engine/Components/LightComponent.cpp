#include <algorithm>
#include <cmath>
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
    ImGui::Separator();
    ImGui::TextDisabled("SHADOWS (CASCADED)");

    ImGui::Checkbox("Cast Shadows", &m_castShadows);
    if (m_castShadows)
    {
        ImGui::SliderFloat("Attenuation", &m_shadowAttenuation, 0.0f, 1.0f);

        if (ImGui::TreeNodeEx("Cascade Settings", ImGuiTreeNodeFlags_DefaultOpen))
        {
            bool splitsDirty{ false };
            splitsDirty |= ImGui::DragFloat("Shadow Distance", &m_shadowDistance, 1.0f, 10.0f, 1000.0f, "%.1f m");
            splitsDirty |= ImGui::SliderFloat("Split Lambda", &m_splitLambda, 0.0f, 1.0f, "%.2f (Log-Linear)");

            if (splitsDirty)
            {
                CalculateSplitDistances(m_splitDistances[0]);
            }

            ImGui::Spacing();
            ImGui::TextDisabled("Auto PSSM Splits:");
            ImGui::TextDisabled("  Slice 0: %.1f m - %.1f m", m_splitDistances[0], m_splitDistances[1]);
            ImGui::TextDisabled("  Slice 1: %.1f m - %.1f m", m_splitDistances[1], m_splitDistances[2]);
            ImGui::TextDisabled("  Slice 2: %.1f m - %.1f m", m_splitDistances[2], m_splitDistances[3]);

            ImGui::Spacing();
            ImGui::DragFloat3("Depth Bias", m_shadowBias.data(), 0.0001f, 0.0f, 0.02f, "%.4f");
            ImGui::TreePop();
        }
    }
}

void DirectionalLightComponent::CalculateSplitDistances(float nearZ) noexcept
{
    m_splitDistances[0] = (std::max)(0.01f, nearZ);
    m_splitDistances[SHADOW_CASCADE_COUNT] = (std::max)(m_splitDistances[0] + 1.0f, m_shadowDistance);

    const float n{ m_splitDistances[0] };
    const float f{ m_splitDistances[SHADOW_CASCADE_COUNT] };
    const float ratio{ f / n };

    for (int i{ 1 }; i < SHADOW_CASCADE_COUNT; ++i)
    {
        const float p{ static_cast<float>(i) / static_cast<float>(SHADOW_CASCADE_COUNT) };
        const float logZ{ n * std::pow(ratio, p) };
        const float uniformZ{ n + (f - n) * p };
        m_splitDistances[i] = m_splitLambda * logZ + (1.0f - m_splitLambda) * uniformZ;
    }
}

void DirectionalLightComponent::Serialize(nlohmann::json& outJson) const
{
    outJson["Color"] = { m_color.x, m_color.y, m_color.z };
    outJson["Intensity"] = m_intensity;

    outJson["CastShadows"] = m_castShadows;
    outJson["ShadowAttenuation"] = m_shadowAttenuation;
    outJson["ShadowDistance"] = m_shadowDistance;
    outJson["SplitLambda"] = m_splitLambda;
    outJson["ShadowBias"] = m_shadowBias;
    outJson["SplitDistances"] = m_splitDistances;
}

void DirectionalLightComponent::Deserialize(const nlohmann::json& inJson)
{
    if (inJson.contains("Color"))
    {
        m_color = { inJson["Color"][0], inJson["Color"][1], inJson["Color"][2] };
    }
    m_intensity = inJson.value("Intensity", 1.0f);

    m_castShadows = inJson.value("CastShadows", true);
    m_shadowAttenuation = inJson.value("ShadowAttenuation", 0.5f);
    m_shadowDistance = inJson.value("ShadowDistance", 150.0f);
    m_splitLambda = inJson.value("SplitLambda", 0.85f);

    if (inJson.contains("ShadowBias"))
    {
        const auto& biasJson{ inJson["ShadowBias"] };
        for (std::size_t i{ 0 }; i < m_shadowBias.size() && i < biasJson.size(); ++i)
        {
            m_shadowBias[i] = biasJson[i].get<float>();
        }
    }
    if (inJson.contains("SplitDistances"))
    {
        const auto& splitJson{ inJson["SplitDistances"] };
        if (splitJson.size() > 3 && !inJson.contains("ShadowDistance"))
        {
            m_shadowDistance = splitJson[3].get<float>();
        }
    }
    CalculateSplitDistances(0.1f);
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