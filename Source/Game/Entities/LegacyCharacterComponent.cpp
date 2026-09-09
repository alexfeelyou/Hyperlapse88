#include <cmath>
#include <imgui.h>
#include "CharacterMovement.h"
#include "LegacyCharacterComponent.h"
#include "Player.h"

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

[[nodiscard]] constexpr bool LegacyCharacterComponent::IsFloatEqual(float a, float b, float epsilon) noexcept
{
    return (a >= b - epsilon) && (a <= b + epsilon);
}

void LegacyCharacterComponent::Update(float dt)
{
    if (!m_character || !m_owner) return;

    if (!m_character->IsActive())
    {
        m_owner->Destroy();
        return;
    }

    CharacterMovement* movement{ m_character->GetMovement() };
    if (!movement) return;

    Transform& editorTransform{ m_owner->transform };

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
        m_character->SetPosition(editorTransform.position);

        // FIX: Removed XMConvertToRadians. Game logic expects degrees.
        m_character->SetRotation(editorTransform.rotation);

        m_character->scale = editorTransform.scale;
        m_character->ForceVisualSync();
    }
    else
    {
        editorTransform.position = movement->GetPosition();

        // FIX: Removed XMConvertToDegrees. Game logic already provides degrees.
        editorTransform.rotation = movement->GetRotation();

        editorTransform.scale = m_character->scale;
    }

    m_lastFramePos = editorTransform.position;
    m_lastFrameRot = editorTransform.rotation;
    m_lastFrameScale = editorTransform.scale;
}

void LegacyCharacterComponent::OnAttach(GameObject* owner) noexcept
{
    IComponent::OnAttach(owner);

    if (m_character)
    {
        m_character->SetOwnerNode(owner);

        if (m_character->GetMovement() && m_owner)
        {
            m_owner->transform.position = m_character->GetMovement()->GetPosition();

            // FIX: Removed XMConvertToDegrees.
            m_owner->transform.rotation = m_character->GetMovement()->GetRotation();

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
    if (!m_character || !m_owner || !renderer) return;

    auto model{ m_character->GetModel() };
    if (!model) return;

    const DirectX::XMFLOAT4X4 worldMatrix{ m_owner->transform.GetWorldMatrix() };
    const DirectX::XMFLOAT4X4& previousWorldMatrix{ m_hasPreviousWorldMatrix ? m_previousWorldMatrix : worldMatrix };

    const std::vector<DirectX::XMFLOAT4X4>* currentNodeGlobals{ nullptr };
    const std::vector<DirectX::XMFLOAT4X4>* previousNodeGlobals{ nullptr };
    DirectX::XMFLOAT4 charColor{ 1.0f, 1.0f, 1.0f, 1.0f };

    if (auto* player = dynamic_cast<Player*>(m_character))
    {
        charColor = player->color;

        if (auto* animator = player->GetAnimator())
        {
            currentNodeGlobals = &animator->GetCurrentNodeGlobals();
            previousNodeGlobals = &animator->GetPreviousNodeGlobals();
        }
    }

    renderer->Draw(model, charColor, worldMatrix, previousWorldMatrix, currentNodeGlobals, previousNodeGlobals, true);

    m_previousWorldMatrix = worldMatrix;
    m_hasPreviousWorldMatrix = true;
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