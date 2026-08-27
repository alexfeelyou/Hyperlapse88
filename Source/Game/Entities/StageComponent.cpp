#include <imgui.h>
#include "StageComponent.h"

StageComponent::StageComponent(Stage* stage) noexcept
    : m_stage{ stage }
{}

void StageComponent::Update(float dt)
{
    // Fast fail if references are dangling
    if (!m_stage || !m_owner) return;
}

void StageComponent::DrawInspector()
{
    if (!m_stage)
    {
        ImGui::TextColored(ImVec4{ 1.0f, 0.2f, 0.2f, 1.0f }, "Warning: Stage Reference is NULL");
        return;
    }

    ImGui::Text("Stage Bridge Active");
    ImGui::TextDisabled("Static environment geometry and PhysX colliders.");
}