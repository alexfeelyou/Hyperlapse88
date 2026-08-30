#include <imgui.h>
#include "GameObject.h"
#include "Stage.h"
#include "StageComponent.h"

StageComponent::StageComponent(Stage* stage) noexcept
    : m_stage{ stage }
{
    if (m_stage)
    {
        m_lastFramePos = m_stage->position;
        m_lastFrameRot = m_stage->rotation;
        m_lastFrameScale = m_stage->scale;
    }
}

[[nodiscard]] constexpr bool StageComponent::IsFloatEqual(float a, float b, float epsilon) noexcept
{
    return (a >= b - epsilon) && (a <= b + epsilon);
}

void StageComponent::OnAttach(GameObject* owner) noexcept
{
    IComponent::OnAttach(owner);

    if (m_stage && m_owner)
    {
        m_owner->transform.position = m_stage->position;
        m_owner->transform.rotation = m_stage->rotation;
        m_owner->transform.scale = m_stage->scale;

        m_lastFramePos = m_owner->transform.position;
        m_lastFrameRot = m_owner->transform.rotation;
        m_lastFrameScale = m_owner->transform.scale;
    }
}

void StageComponent::Update(float dt)
{
    // Fast fail if references are dangling
    if (!m_stage || !m_owner) return;

    Transform& editorTransform{ m_owner->transform };

    // Detect if the Editor changed the Transform this frame
    const bool editorMovedX{ !IsFloatEqual(editorTransform.position.x, m_lastFramePos.x) };
    const bool editorMovedY{ !IsFloatEqual(editorTransform.position.y, m_lastFramePos.y) };
    const bool editorMovedZ{ !IsFloatEqual(editorTransform.position.z, m_lastFramePos.z) };
    const bool editorRotatedX{ !IsFloatEqual(editorTransform.rotation.x, m_lastFrameRot.x) };
    const bool editorRotatedY{ !IsFloatEqual(editorTransform.rotation.y, m_lastFrameRot.y) };
    const bool editorRotatedZ{ !IsFloatEqual(editorTransform.rotation.z, m_lastFrameRot.z) };
    const bool editorScaledX{ !IsFloatEqual(editorTransform.scale.x, m_lastFrameScale.x) };
    const bool editorScaledY{ !IsFloatEqual(editorTransform.scale.y, m_lastFrameScale.y) };
    const bool editorScaledZ{ !IsFloatEqual(editorTransform.scale.z, m_lastFrameScale.z) };

    const bool wasEditedInGUI{
        editorMovedX || editorMovedY || editorMovedZ ||
        editorRotatedX || editorRotatedY || editorRotatedZ ||
        editorScaledX || editorScaledY || editorScaledZ
    };

    if (wasEditedInGUI)
    {
        // Push the new Editor Transform down into the Game Logic
        m_stage->position = editorTransform.position;
        m_stage->rotation = editorTransform.rotation;
        m_stage->scale = editorTransform.scale;

        // Immediately sync the 3D model matrix to match the Gizmo
        m_stage->UpdateTransform();

        m_stage->RebuildPhysics();
    }
    else
    {
        // Pull the Game Logic Transform up into the Editor 
        editorTransform.position = m_stage->position;
        editorTransform.rotation = m_stage->rotation;
        editorTransform.scale = m_stage->scale;
    }

    // Cache the finalized state for next frame's comparison
    m_lastFramePos = editorTransform.position;
    m_lastFrameRot = editorTransform.rotation;
    m_lastFrameScale = editorTransform.scale;
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