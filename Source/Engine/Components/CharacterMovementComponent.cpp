#include <imgui.h>
#include "CapsuleColliderComponent.h"
#include "CharacterMovementComponent.h"
#include "ComponentRegistry.h"
#include "GameObject.h"

void CharacterMovementComponent::OnAttach(GameObject* owner) noexcept
{
    IComponent::OnAttach(owner);

    if (m_owner)
    {
        // Cache the physics proxy immediately upon attachment 
        m_capsule = m_owner->GetComponent<CapsuleColliderComponent>();
    }
}

void CharacterMovementComponent::Update(const float dt)
{
    // Fast fail if orphaned or missing physics proxy
    if (!m_owner || !m_capsule) return;

    // Apply gravity cumulatively if not touching the floor
    if (m_useGravity && !m_capsule->IsGrounded())
    {
        m_velocity.y += GRAVITY * dt;
    }
    else if (m_useGravity && m_capsule->IsGrounded() && m_velocity.y < 0.0f)
    {
        // Apply slight downward pressure to stick to slopes/stairs smoothly, preventing jitter
        m_velocity.y = -1.0f;
    }

    // Convert continuous velocity into discrete frame displacement
    const DirectX::XMFLOAT3 displacement{
        m_velocity.x * dt,
        m_velocity.y * dt,
        m_velocity.z * dt
    };

    // Drive the physical proxy (Capsule directly updates GameObject Transform inside Move)
    m_capsule->Move(displacement, dt);
}

void CharacterMovementComponent::DrawInspector()
{
    ImGui::TextDisabled("Kinematic Locomotion Motor");
    ImGui::Separator();

    ImGui::Checkbox("Use Gravity", &m_useGravity);

    // Read-only visualization of current momentum
    ImGui::BeginDisabled();
    ImGui::InputFloat3("Velocity", &m_velocity.x, "%.2f");
    ImGui::EndDisabled();
}

// Automatically register component with dynamic Inspector factory
REGISTER_COMPONENT(CharacterMovementComponent)