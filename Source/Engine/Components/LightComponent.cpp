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
    ImGui::Separator();
    ImGui::TextDisabled("SHADOWS (CASCADED)");

    ImGui::Checkbox("Cast Shadows", &m_castShadows);
    if (m_castShadows)
    {
        ImGui::SliderFloat("Attenuation", &m_shadowAttenuation, 0.0f, 1.0f);

        if (ImGui::TreeNodeEx("Cascade Splits & Bias", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::DragFloat("Near Plane", &m_splitDistances[0], 0.1f, 0.1f, m_splitDistances[1]);
            ImGui::DragFloat("Split 1", &m_splitDistances[1], 1.0f, m_splitDistances[0], m_splitDistances[2]);
            ImGui::DragFloat("Split 2", &m_splitDistances[2], 1.0f, m_splitDistances[1], m_splitDistances[3]);
            ImGui::DragFloat("Split 3", &m_splitDistances[3], 1.0f, m_splitDistances[2], m_splitDistances[4]);
            ImGui::DragFloat("Far Plane", &m_splitDistances[4], 1.0f, m_splitDistances[3], 2000.0f);

            ImGui::Spacing();
            ImGui::DragFloat4("Depth Bias", m_shadowBias.data(), 0.0001f, 0.0f, 0.02f, "%.4f");
            ImGui::TreePop();
        }

        if (ImGui::TreeNode("Shadow Map Previews"))
        {
            const auto* srvs = Graphics::Instance().GetLightManager().GetCascadeSRVs();
            for (int i = 0; i < 4; ++i)
            {
                ImGui::Text("Cascade %d", i);
                if (srvs[i])
                {
                    ImGui::Image(reinterpret_cast<ImTextureID>(srvs[i]), ImVec2{ 256, 256 },
                        ImVec2{ 0, 0 }, ImVec2{ 1, 1 }, ImVec4{ 1, 1, 1, 1 }, ImVec4{ 0.5f, 0.5f, 0.5f, 1.0f });
                }
            }
            ImGui::TreePop();
        }
    }
}

void DirectionalLightComponent::Serialize(nlohmann::json& outJson) const
{
    outJson["Color"] = { m_color.x, m_color.y, m_color.z };
    outJson["Intensity"] = m_intensity;

    outJson["CastShadows"] = m_castShadows;
    outJson["ShadowAttenuation"] = m_shadowAttenuation;
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

    if (inJson.contains("ShadowBias"))
    {
        m_shadowBias = inJson["ShadowBias"].get<std::array<float, 4>>();
    }
    if (inJson.contains("SplitDistances"))
    {
        m_splitDistances = inJson["SplitDistances"].get<std::array<float, 5>>();
    }
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