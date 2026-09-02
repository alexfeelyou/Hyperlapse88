#include <cmath>
#include <imgui.h>
#include "GameObject.h"
#include "Light.h"
#include "LightComponent.h"

void LightManager::Update() noexcept
{
    // Default sun direction if no active directional light exists
    bool hasDirectionalLight{ false };

    m_pointLightCount = 0;
    m_spotLightCount = 0;

    for (const auto* const light : m_lights)
    {
        if (!light || !light->GetOwner() || !light->GetOwner()->IsActive())
        {
            continue;
        }

        const DirectX::XMFLOAT3 pos{ light->GetWorldPosition() };
        const DirectX::XMFLOAT3 col{ light->GetColor() };
        const float intensity{ light->GetIntensity() };
        const float range{ light->GetRange() };

        switch (light->GetLightType())
        {
        case LightType::Directional:
            if (!hasDirectionalLight)
            {
                m_directionalLight.direction = light->GetDirection();
                m_directionalLight.color = col;
                m_directionalLight.intensity = intensity;
                hasDirectionalLight = true;
            }
            break;

        case LightType::Point:
            if (m_pointLightCount < 8)
            {
                auto& p{ m_pointLights[m_pointLightCount++] };
                p.positionAndRange = { pos.x, pos.y, pos.z, range };
                p.colorAndIntensity = { col.x, col.y, col.z, intensity };
            }
            break;

        case LightType::Spot:
            if (m_spotLightCount < 8)
            {
                auto& s{ m_spotLights[m_spotLightCount++] };
                const DirectX::XMFLOAT3 dir{ light->GetDirection() };
                const float angleCos{ std::cos(DirectX::XMConvertToRadians(light->GetSpotAngle() * 0.5f)) };

                s.positionAndRange = { pos.x, pos.y, pos.z, range };
                s.directionAndAngle = { dir.x, dir.y, dir.z, angleCos };
                s.colorAndIntensity = { col.x, col.y, col.z, intensity };
            }
            break;
        }
    }

    if (!hasDirectionalLight)
    {
        m_directionalLight.direction = { 0.0f, -0.707f, -0.707f };
        m_directionalLight.color = { 1.0f, 1.0f, 1.0f };
        m_directionalLight.intensity = 1.0f;
    }
}

void LightManager::DrawEnvironmentGUI() noexcept
{
    ImGui::TextDisabled("ENVIRONMENT ILLUMINATION");
    ImGui::Separator();

    ImGui::ColorEdit3("Sky Ambient Color", &m_skyColor.x);
    ImGui::SliderFloat("Sky Intensity", &m_skyIntensity, 0.0f, 2.0f);

    ImGui::ColorEdit3("Ground Ambient Color", &m_groundColor.x);
    ImGui::SliderFloat("Ground Intensity", &m_groundIntensity, 0.0f, 2.0f);

    ImGui::Spacing();
    ImGui::Text("Active Registered Lights: %zu", m_lights.size());
    ImGui::TextDisabled("Points: %d/8 | Spots: %d/8", m_pointLightCount, m_spotLightCount);
}