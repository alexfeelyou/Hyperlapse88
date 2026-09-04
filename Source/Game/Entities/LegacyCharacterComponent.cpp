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

    // Evaluate all rotation axes
    const bool editorRotatedX{ !IsFloatEqual(editorTransform.rotation.x, m_lastFrameRot.x) };
    const bool editorRotatedY{ !IsFloatEqual(editorTransform.rotation.y, m_lastFrameRot.y) };
    const bool editorRotatedZ{ !IsFloatEqual(editorTransform.rotation.z, m_lastFrameRot.z) };

    // Evaluate all scale axes
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
        // Push the new Editor Transform down into Game Logic (Convert Degrees -> Radians)
        m_character->SetPosition(editorTransform.position);

        DirectX::XMFLOAT3 radRot;
        radRot.x = DirectX::XMConvertToRadians(editorTransform.rotation.x);
        radRot.y = DirectX::XMConvertToRadians(editorTransform.rotation.y);
        radRot.z = DirectX::XMConvertToRadians(editorTransform.rotation.z);
        m_character->SetRotation(radRot);

        m_character->scale = editorTransform.scale;
        m_character->ForceVisualSync();
    }
    else
    {
        // Pull the Game Logic Transform up into the Editor (Convert Radians -> Degrees)
        editorTransform.position = movement->GetPosition();

        DirectX::XMFLOAT3 radRot = movement->GetRotation();
        editorTransform.rotation.x = DirectX::XMConvertToDegrees(radRot.x);
        editorTransform.rotation.y = DirectX::XMConvertToDegrees(radRot.y);
        editorTransform.rotation.z = DirectX::XMConvertToDegrees(radRot.z);

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

    if (m_character)
    {
        // Link the gameplay entity back to this editor node
        m_character->SetOwnerNode(owner);

        // Push the Character's starting data into the GameObject's transform
        if (m_character->GetMovement() && m_owner)
        {
            m_owner->transform.position = m_character->GetMovement()->GetPosition();

            // Convert initial Radians back to Degrees for the Inspector
            DirectX::XMFLOAT3 radRot = m_character->GetMovement()->GetRotation();
            m_owner->transform.rotation.x = DirectX::XMConvertToDegrees(radRot.x);
            m_owner->transform.rotation.y = DirectX::XMConvertToDegrees(radRot.y);
            m_owner->transform.rotation.z = DirectX::XMConvertToDegrees(radRot.z);

            m_owner->transform.scale = m_character->scale;

            m_lastFramePos = m_owner->transform.position;
            m_lastFrameRot = m_owner->transform.rotation;
            m_lastFrameScale = m_owner->transform.scale;

            m_character->ForceVisualSync();
        }
    }
}

void LegacyCharacterComponent::Render(ModelRenderer* renderer)
{
    // Fast fail if references are dangling or the component was disabled
    if (!m_character || !m_owner || !renderer) return;

    // Call the legacy character render pipeline (which preserves skeletal animations)
    m_character->Render(renderer);
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