#include <algorithm>
#include <cmath>
#include <imgui.h>
#include "CapsuleColliderComponent.h"
#include "CharacterMovementComponent.h"
#include "ComponentRegistry.h"
#include "GameObject.h"

CharacterMovementComponent::CharacterMovementComponent(const CharacterMovementConfig& config) noexcept
    : m_config{ config }
{}

void CharacterMovementComponent::OnAttach(GameObject* owner) noexcept
{
    IComponent::OnAttach(owner);

    if (m_owner)
    {
        // Cache the physics proxy strictly once during initialization
        m_capsule = m_owner->GetComponent<CapsuleColliderComponent>();
    }
}

void CharacterMovementComponent::SetDesiredDirection(const DirectX::XMFLOAT2& direction) noexcept
{
    // Ensure the input vector is clamped to a magnitude of 1.0 to prevent diagonal speed boosting
    const float sqLength{ LengthSq(direction) };
    if (sqLength > 1.0f)
    {
        const float invLength{ 1.0f / std::sqrt(sqLength) };
        m_desiredDirection = { direction.x * invLength, direction.y * invLength };
    }
    else
    {
        m_desiredDirection = direction;
    }
}

void CharacterMovementComponent::AddImpulse(const DirectX::XMFLOAT3& impulse) noexcept
{
    m_impulseVelocity.x += impulse.x;
    m_impulseVelocity.y += impulse.y;
    m_impulseVelocity.z += impulse.z;
}

DirectX::XMFLOAT3 CharacterMovementComponent::GetTotalVelocity() const noexcept
{
    return {
        m_locomotionVelocity.x + m_impulseVelocity.x,
        m_verticalVelocity + m_impulseVelocity.y,
        m_locomotionVelocity.z + m_impulseVelocity.z
    };
}

bool CharacterMovementComponent::IsMoving() const noexcept
{
    constexpr float moveThresholdSq{ 0.01f };
    return LengthSq({ m_locomotionVelocity.x, m_locomotionVelocity.z }) > moveThresholdSq ||
        LengthSq({ m_impulseVelocity.x, m_impulseVelocity.z }) > moveThresholdSq;
}

void CharacterMovementComponent::Update(const float dt)
{
    if (!m_capsule) return;

    // Process Input Locomotion (Accelerate towards desired direction)
    const DirectX::XMFLOAT2 targetVelocity{
        m_desiredDirection.x * m_config.maxWalkSpeed,
        m_desiredDirection.y * m_config.maxWalkSpeed
    };

    const float accelRate{ (LengthSq(m_desiredDirection) > 0.01f) ? m_config.acceleration : m_config.deceleration };

    m_locomotionVelocity.x += (targetVelocity.x - m_locomotionVelocity.x) * accelRate * dt;
    m_locomotionVelocity.z += (targetVelocity.y - m_locomotionVelocity.z) * accelRate * dt;

    // Process Impulse Decay (Friction)
    // Impulses independently decay to zero over time, allowing dashes to slide smoothly
    const float currentDrag{ m_config.impulseDrag * m_frictionMultiplier };
    m_impulseVelocity.x += (0.0f - m_impulseVelocity.x) * currentDrag * dt;
    m_impulseVelocity.y += (0.0f - m_impulseVelocity.y) * currentDrag * dt;
    m_impulseVelocity.z += (0.0f - m_impulseVelocity.z) * currentDrag * dt;

    // Snap tiny impulses to zero to prevent floating point drift
    if (std::abs(m_impulseVelocity.x) < 0.05f) m_impulseVelocity.x = 0.0f;
    if (std::abs(m_impulseVelocity.y) < 0.05f) m_impulseVelocity.y = 0.0f;
    if (std::abs(m_impulseVelocity.z) < 0.05f) m_impulseVelocity.z = 0.0f;

    // Process Gravity
    if (m_config.useGravity && !m_capsule->IsGrounded())
    {
        m_verticalVelocity += m_config.gravity * dt;
    }
    else if (m_config.useGravity && m_capsule->IsGrounded() && m_verticalVelocity < 0.0f)
    {
        // Downward pressure prevents jittering on staircases and downward slopes
        m_verticalVelocity = -2.0f;
    }
    else if (!m_config.useGravity)
    {
        m_verticalVelocity = 0.0f;
    }

    // Combine and Move
    const DirectX::XMFLOAT3 totalVel{ GetTotalVelocity() };
    const DirectX::XMFLOAT3 displacement{ totalVel.x * dt, totalVel.y * dt, totalVel.z * dt };

    // The CapsuleColliderComponent directly pushes the solved PhysX position back to the Transform
    m_capsule->Move(displacement, dt);
}

void CharacterMovementComponent::DrawInspector()
{
    ImGui::TextDisabled("Kinematic Locomotion Motor");
    ImGui::Separator();

    ImGui::DragFloat("Max Walk Speed", &m_config.maxWalkSpeed, 0.1f, 1.0f, 100.0f);
    ImGui::DragFloat("Acceleration", &m_config.acceleration, 0.5f, 1.0f, 200.0f);
    ImGui::DragFloat("Deceleration", &m_config.deceleration, 0.5f, 1.0f, 200.0f);
    ImGui::DragFloat("Impulse Drag", &m_config.impulseDrag, 0.1f, 0.1f, 50.0f);
    ImGui::Checkbox("Use Gravity", &m_config.useGravity);

    ImGui::Spacing();
    ImGui::TextDisabled("LIVE DATA");
    ImGui::BeginDisabled();
    DirectX::XMFLOAT3 totalVel{ GetTotalVelocity() };
    ImGui::InputFloat3("Velocity", &totalVel.x, "%.2f");
    ImGui::EndDisabled();
}

void CharacterMovementComponent::Serialize(nlohmann::json& outJson) const
{
    outJson["MaxWalkSpeed"] = m_config.maxWalkSpeed;
    outJson["Acceleration"] = m_config.acceleration;
    outJson["Deceleration"] = m_config.deceleration;
    outJson["ImpulseDrag"] = m_config.impulseDrag;
    outJson["UseGravity"] = m_config.useGravity;
}

void CharacterMovementComponent::Deserialize(const nlohmann::json& inJson)
{
    m_config.maxWalkSpeed = inJson.value("MaxWalkSpeed", 15.0f);
    m_config.acceleration = inJson.value("Acceleration", 50.0f);
    m_config.deceleration = inJson.value("Deceleration", 60.0f);
    m_config.impulseDrag = inJson.value("ImpulseDrag", 5.0f);
    m_config.useGravity = inJson.value("UseGravity", true);
}

REGISTER_COMPONENT(CharacterMovementComponent)