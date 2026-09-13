#pragma once

#include <DirectXMath.h>
#include <json.hpp>
#include "IComponent.h"

class CapsuleColliderComponent;

// Data-driven configuration for locomotion mechanics
struct CharacterMovementConfig
{
    float maxWalkSpeed{ 15.0f };
    float acceleration{ 50.0f };
    float deceleration{ 60.0f };
    float impulseDrag{ 5.0f };     // How quickly external forces (dashes/knockbacks) decay
    float gravity{ -9.81f };
    bool  useGravity{ true };
};

// Kinematic locomotion motor
// Separates sustained input velocity from external impulses (dashes/knockback) 
// to prevent input from artificially braking combat physics.
class CharacterMovementComponent final : public IComponent
{
public:
    CharacterMovementComponent() noexcept = default;
    explicit CharacterMovementComponent(const CharacterMovementConfig& config) noexcept;
    ~CharacterMovementComponent() override = default;

    CharacterMovementComponent(const CharacterMovementComponent&) = delete;
    CharacterMovementComponent& operator=(const CharacterMovementComponent&) = delete;

    void OnAttach(GameObject* owner) noexcept override;
    void Update(float dt) override;
    void DrawInspector() override;

    void Serialize(nlohmann::json& outJson) const override;
    void Deserialize(const nlohmann::json& inJson) override;

    [[nodiscard]] const char* GetTypeName() const noexcept override { return "CharacterMovementComponent"; }

    // Locomotion Interface
    void SetDesiredDirection(const DirectX::XMFLOAT2& direction) noexcept;
    void AddImpulse(const DirectX::XMFLOAT3& impulse) noexcept;

    // Friction Override (e.g., locking the player in place during a sword slash)
    void SetFrictionMultiplier(float multiplier) noexcept { m_frictionMultiplier = multiplier; }

    // State Queries
    [[nodiscard]] DirectX::XMFLOAT3 GetTotalVelocity() const noexcept;
    [[nodiscard]] bool IsMoving() const noexcept;
    [[nodiscard]] CharacterMovementConfig& GetConfig() noexcept { return m_config; }

private:
    CharacterMovementConfig m_config{};

    // Sibling cache 
    CapsuleColliderComponent* m_capsule{ nullptr };

    // Split velocity accumulators for precise game feel
    DirectX::XMFLOAT2 m_desiredDirection{ 0.0f, 0.0f };
    DirectX::XMFLOAT3 m_locomotionVelocity{ 0.0f, 0.0f, 0.0f }; // Driven by input
    DirectX::XMFLOAT3 m_impulseVelocity{ 0.0f, 0.0f, 0.0f };    // Driven by combat/damage

    float m_verticalVelocity{ 0.0f };
    float m_frictionMultiplier{ 1.0f };

    [[nodiscard]] static constexpr float LengthSq(const DirectX::XMFLOAT2& v) noexcept
    {
        return (v.x * v.x) + (v.y * v.y);
    }
};