#include <imgui.h>
#include "CombatDirector.h"
#include "CombatDirectorComponent.h"
#include "ComponentRegistry.h"
#include "EditorManager.h"
#include "GameObject.h"

void CombatDirectorComponent::PushSettingsToDirector() const noexcept
{
    auto& director{ CombatDirector::Instance() };
    director.SetInnerRadius(m_innerRadius);
    director.SetOuterRadius(m_outerRadius);
    director.SetMaxAttackTokens(m_maxAttackTokens);
    director.SetCadenceInterval(m_cadenceInterval);
}

void CombatDirectorComponent::OnAttach(GameObject* owner) noexcept
{
    IComponent::OnAttach(owner);
    PushSettingsToDirector();
}

void CombatDirectorComponent::OnEnable() noexcept
{
    PushSettingsToDirector();
}

CombatDirectorComponent::~CombatDirectorComponent()
{
    CombatDirector::Instance().Clear();
}

void CombatDirectorComponent::OnDisable() noexcept
{
    CombatDirector::Instance().Clear();
}

void CombatDirectorComponent::Update(const float dt)
{
    if (!m_owner) return;

    const EditorMode mode{ EditorManager::Instance().GetEditorMode() };

    if (mode == EditorMode::Play)
    {
        CombatDirector::Instance().Update(dt, m_owner);
    }
    else if (mode == EditorMode::Edit)
    {
        // Keep concentric slots following the player transform in real time in the Scene View
        CombatDirector::Instance().UpdateSlotPositions(m_owner);
    }
}

void CombatDirectorComponent::DrawGizmo(const GizmoContext& ctx) noexcept
{
    CombatDirector::Instance().DrawGizmos(ctx);
}

void CombatDirectorComponent::DrawInspector()
{
    ImGui::TextDisabled("Swarm Combat Director");
    ImGui::Separator();

    bool changed{ false };
    changed |= ImGui::DragFloat("Inner Radius", &m_innerRadius, 0.1f, 1.0f, 15.0f, "%.1f m");
    changed |= ImGui::DragFloat("Outer Radius", &m_outerRadius, 0.1f, 2.0f, 25.0f, "%.1f m");

    int tokens{ static_cast<int>(m_maxAttackTokens) };
    if (ImGui::SliderInt("Max Attack Tokens", &tokens, 1, 10))
    {
        m_maxAttackTokens = static_cast<std::uint32_t>(tokens);
        changed = true;
    }

    changed |= ImGui::DragFloat("Cadence Interval", &m_cadenceInterval, 0.05f, 0.05f, 2.0f, "%.2f s");

    if (changed)
    {
        PushSettingsToDirector();
    }

    ImGui::Spacing();
    ImGui::TextDisabled("SWARM TELEMETRY");
    ImGui::BeginDisabled();
    ImGui::Text("Registered Enemies: %zu", CombatDirector::Instance().GetRegisteredEnemyCount());
    ImGui::Text("Inner Slots Occupied: %zu / 6", CombatDirector::Instance().GetOccupiedInnerSlotCount());
    ImGui::Text("Outer Slots Occupied: %zu / 12", CombatDirector::Instance().GetOccupiedOuterSlotCount());
    ImGui::Text("Active Attack Tokens: %u / %u", CombatDirector::Instance().GetActiveTokenCount(), m_maxAttackTokens);
    ImGui::EndDisabled();
}

void CombatDirectorComponent::Serialize(nlohmann::json& outJson) const
{
    outJson["InnerRadius"] = m_innerRadius;
    outJson["OuterRadius"] = m_outerRadius;
    outJson["MaxAttackTokens"] = m_maxAttackTokens;
    outJson["CadenceInterval"] = m_cadenceInterval;
}

void CombatDirectorComponent::Deserialize(const nlohmann::json& inJson)
{
    m_innerRadius = inJson.value("InnerRadius", 2.8f);
    m_outerRadius = inJson.value("OuterRadius", 5.5f);
    m_maxAttackTokens = inJson.value("MaxAttackTokens", 3u);
    m_cadenceInterval = inJson.value("CadenceInterval", 0.25f);

    PushSettingsToDirector();
}

REGISTER_COMPONENT(CombatDirectorComponent)