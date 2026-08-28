#include <cmath>
#include <imgui.h>
#include "CharacterMovement.h"
#include "LegacyCharacterComponent.h"

// Initialize the component and cache the initial state
LegacyCharacterComponent::LegacyCharacterComponent(Character* character) noexcept
    : m_character{ character }
{
    if (m_character && m_character->GetMovement())
    {
        m_lastFramePos = m_character->GetMovement()->GetPosition();
        m_lastFrameRot = m_character->GetMovement()->GetRotation();
        m_lastFrameScale = m_character->scale;
    }
}

// Float comparison to detect manual Editor overrides
[[nodiscard]] constexpr bool LegacyCharacterComponent::IsFloatEqual(float a, float b, float epsilon) noexcept
{
    return (a >= b - epsilon) && (a <= b + epsilon);
}

void LegacyCharacterComponent::Update(float dt)
{
    // Fast fail if references are dangling
    if (!m_character || !m_owner) return;

    // If the game logic kills the entity, destroy its Editor wrapper
    if (!m_character->IsActive())
    {
        m_owner->Destroy();
        return;
    }

    CharacterMovement* movement{ m_character->GetMovement() };
    if (!movement) return;

    Transform& editorTransform{ m_owner->transform };

    // Detect if the Editor changed the Transform this frame
    const bool editorMovedX{ !IsFloatEqual(editorTransform.position.x, m_lastFramePos.x) };
    const bool editorMovedY{ !IsFloatEqual(editorTransform.position.y, m_lastFramePos.y) };
    const bool editorMovedZ{ !IsFloatEqual(editorTransform.position.z, m_lastFramePos.z) };
    const bool editorRotated{ !IsFloatEqual(editorTransform.rotation.y, m_lastFrameRot.y) };
    const bool editorScaled{ !IsFloatEqual(editorTransform.scale.x, m_lastFrameScale.x) };

    const bool wasEditedInGUI{ editorMovedX || editorMovedY || editorMovedZ || editorRotated || editorScaled };

    if (wasEditedInGUI)
    {
        // Push the new Editor Transform down into the Game Logic
        movement->SetPosition(editorTransform.position);
        movement->SetRotation(editorTransform.rotation);
        m_character->scale = editorTransform.scale;
    }
    else
    {
        // Pull the Game Logic Transform up into the Editor (e.g., gravity, walking)
        editorTransform.position = movement->GetPosition();
        editorTransform.rotation = movement->GetRotation();
        editorTransform.scale = m_character->scale;
    }

    // Cache the finalized state for next frame's comparison
    m_lastFramePos = editorTransform.position;
    m_lastFrameRot = editorTransform.rotation;
    m_lastFrameScale = editorTransform.scale;
}

void LegacyCharacterComponent::OnAttach(GameObject* owner) noexcept
{
    // Always call the base class implementation first to set m_owner
    IComponent::OnAttach(owner);

    // Push the Character's starting data into the GameObject's transform
    if (m_character && m_character->GetMovement() && m_owner)
    {
        m_owner->transform.position = m_character->GetMovement()->GetPosition();
        m_owner->transform.rotation = m_character->GetMovement()->GetRotation();
        m_owner->transform.scale = m_character->scale;

        // Reset the cache to match
        m_lastFramePos = m_owner->transform.position;
        m_lastFrameRot = m_owner->transform.rotation;
        m_lastFrameScale = m_owner->transform.scale;
    }
}

void LegacyCharacterComponent::DrawInspector()
{
    if (!m_character)
    {
        ImGui::TextColored(ImVec4{ 1.0f, 0.2f, 0.2f, 1.0f }, "Warning: Character Reference is NULL");
        return;
    }

    ImGui::Text("Character Bridge Active");
    ImGui::TextDisabled("Entity syncs bi-directionally with CharacterMovement.");
}