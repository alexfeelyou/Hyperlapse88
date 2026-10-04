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

void CharacterMovementComponent::ApplyKinematicOverride(const DirectX::XMFLOAT2& velocity, float duration) noexcept
{
    m_overrideVelocity = velocity;
    m_overrideTimer = duration;
    m_locomotionVelocity = { 0.0f, 0.0f, 0.0f }; // Immediately strip lingering sprint momentum
}

void CharacterMovementComponent::ClearKinematicOverride() noexcept
{
    m_overrideTimer = 0.0f;
    m_overrideVelocity = { 0.0f, 0.0f };
}

void CharacterMovementComponent::HaltMomentum(float brakingFactor) noexcept
{
    const float retain{ std::clamp(1.0f - brakingFactor, 0.0f, 1.0f) };

    if (m_overrideTimer > 0.0f)
    {
        // Convert strict kinematic override velocity into natural impulse velocity
        // so it seamlessly decays via standard impulseDrag when retaining partial momentum.
        m_impulseVelocity.x = m_overrideVelocity.x * retain;
        m_impulseVelocity.z = m_overrideVelocity.y * retain;
        m_overrideTimer = 0.0f;
        m_overrideVelocity = { 0.0f, 0.0f };
    }
    else
    {
        m_impulseVelocity.x *= retain;
        m_impulseVelocity.z *= retain;
    }

    m_locomotionVelocity.x *= retain;
    m_locomotionVelocity.z *= retain;
}

void CharacterMovementComponent::AddImpulse(const DirectX::XMFLOAT3& impulse) noexcept
{
    m_impulseVelocity.x += impulse.x;
    m_impulseVelocity.y += impulse.y;
    m_impulseVelocity.z += impulse.z;
}

DirectX::XMFLOAT3 CharacterMovementComponent::GetTotalVelocity() const noexcept
{
    if (m_overrideTimer > 0.0f)
    {
        return {
            m_overrideVelocity.x,
            m_verticalVelocity + m_impulseVelocity.y, // Maintain gravity & vertical knockbacks
            m_overrideVelocity.y
        };
    }

    return {
        m_locomotionVelocity.x + m_impulseVelocity.x,
        m_verticalVelocity + m_impulseVelocity.y,
        m_locomotionVelocity.z + m_impulseVelocity.z
    };
}

bool CharacterMovementComponent::IsMoving() const noexcept
{
    constexpr float moveThresholdSq{ 0.01f };
    if (m_overrideTimer > 0.0f)
    {
        return LengthSq(m_overrideVelocity) > moveThresholdSq;
    }

    return LengthSq({ m_locomotionVelocity.x, m_locomotionVelocity.z }) > moveThresholdSq ||
        LengthSq({ m_impulseVelocity.x, m_impulseVelocity.z }) > moveThresholdSq;
}

bool CharacterMovementComponent::isGrounded() const noexcept
{
    return m_capsule ? m_capsule->IsGrounded() : true;
}

void CharacterMovementComponent::Update(const float dt)
{
    // Lazy Initialization 
    // Guarantees the motor finds the capsule regardless of JSON load order
    if (!m_capsule)
    {
        m_capsule = m_owner->GetComponent<CapsuleColliderComponent>();
        if (!m_capsule) return;
    }

    // Process Input Locomotion (Accelerate towards desired direction)
    if (m_overrideTimer > 0.0f)
    {
        m_overrideTimer -= dt;
        // Suppress analog input build-up so we don't shoot forward when the override ends
        m_locomotionVelocity = { 0.0f, 0.0f, 0.0f };
    }
    else
    {
        // Process Input Locomotion (Accelerate towards desired direction)
        const float currentMaxSpeed = m_isSprinting ? m_config.sprintSpeed : m_config.maxRunSpeed;
        const DirectX::XMFLOAT2 targetVelocity{
            m_desiredDirection.x * currentMaxSpeed,
            m_desiredDirection.y * currentMaxSpeed
        };

        const float accelRate{ (LengthSq(m_desiredDirection) > 0.01f) ? m_config.acceleration : m_config.deceleration };

        m_locomotionVelocity.x += (targetVelocity.x - m_locomotionVelocity.x) * accelRate * dt;
        m_locomotionVelocity.z += (targetVelocity.y - m_locomotionVelocity.z) * accelRate * dt;
    }

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
    // Anti-Gravity Lockout  
    // We intentionally removed the explicit m_verticalVelocity = 0.0f clamp when !useGravity.
    // This allows custom states (like ParkourWall) to sculpt explicit vertical 
    // kinematics (parabolic lifts and downward fatigue slides) without the motor erasing them.

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

    ImGui::DragFloat("Max Run Speed", &m_config.maxRunSpeed, 0.1f, 1.0f, 100.0f);
    ImGui::DragFloat("Sprint Speed", &m_config.sprintSpeed, 0.1f, 1.0f, 100.0f);
    ImGui::DragFloat("Acceleration", &m_config.acceleration, 0.5f, 1.0f, 200.0f);
    ImGui::DragFloat("Deceleration", &m_config.deceleration, 0.5f, 1.0f, 200.0f);
    ImGui::DragFloat("Impulse Drag", &m_config.impulseDrag, 0.1f, 0.1f, 50.0f);
    ImGui::DragFloat("Jump Fwd Impulse", &m_config.jumpForwardImpulse, 0.05f, 0.0f, 5.0f);

    ImGui::Spacing();
    ImGui::TextDisabled("DASH / EVADE TUNING");
    ImGui::DragFloat("Ground Dash Dist", &m_config.dashGroundDistance, 0.1f, 1.0f, 15.0f, "%.2f m");
    ImGui::DragFloat("Air Dash Impulse", &m_config.dashAirImpulse, 1.0f, 10.0f, 100.0f);
    ImGui::Spacing();

    ImGui::TextDisabled("SLIDE TUNING");
    ImGui::DragFloat("Slide Impulse", &m_config.slideImpulse, 0.5f, 0.0f, 50.0f);
    ImGui::DragFloat("Slide Friction", &m_config.slideFrictionMultiplier, 0.01f, 0.01f, 1.0f, "%.2f (Lower = Slippery)");
    ImGui::Spacing();

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
    outJson["MaxRunSpeed"] = m_config.maxRunSpeed;
    outJson["SprintSpeed"] = m_config.sprintSpeed;
    outJson["Acceleration"] = m_config.acceleration;
    outJson["Deceleration"] = m_config.deceleration;
    outJson["ImpulseDrag"] = m_config.impulseDrag;
    outJson["JumpForwardImpulse"] = m_config.jumpForwardImpulse;
    outJson["DashGroundDistance"] = m_config.dashGroundDistance;
    outJson["DashAirImpulse"] = m_config.dashAirImpulse;
    outJson["SlideImpulse"] = m_config.slideImpulse;
    outJson["SlideFriction"] = m_config.slideFrictionMultiplier;
    outJson["UseGravity"] = m_config.useGravity;
}

void CharacterMovementComponent::Deserialize(const nlohmann::json& inJson)
{
    m_config.maxRunSpeed = inJson.value("MaxRunSpeed", 6.5f);
    m_config.sprintSpeed = inJson.value("SprintSpeed", 12.0f);
    m_config.acceleration = inJson.value("Acceleration", 32.0f);
    m_config.deceleration = inJson.value("Deceleration", 38.0f);
    m_config.impulseDrag = inJson.value("ImpulseDrag", 7.5f);
    m_config.jumpForwardImpulse = inJson.value("JumpForwardImpulse", 1.0f);
    m_config.dashGroundDistance = inJson.value("DashGroundDistance", 4.5f);
    m_config.dashAirImpulse = inJson.value("DashAirImpulse", 35.0f);
    m_config.slideImpulse = inJson.value("SlideImpulse", 15.0f);
    m_config.slideFrictionMultiplier = inJson.value("SlideFriction", 0.15f);
    m_config.useGravity = inJson.value("UseGravity", true);
}

REGISTER_COMPONENT(CharacterMovementComponent)