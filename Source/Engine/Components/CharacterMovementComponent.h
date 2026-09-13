#pragma once

#include <DirectXMath.h>
#include "IComponent.h"

// Forward declaration 
class CapsuleColliderComponent;

// Kinematic motor that calculates velocity and drives the CapsuleColliderComponent
class CharacterMovementComponent final : public IComponent
{
public:
    CharacterMovementComponent() noexcept = default;
    ~CharacterMovementComponent() override = default;

    CharacterMovementComponent(const CharacterMovementComponent&) = delete;
    CharacterMovementComponent& operator=(const CharacterMovementComponent&) = delete;

    void OnAttach(GameObject* owner) noexcept override;
    void Update(float dt) override;
    void DrawInspector() override;

    [[nodiscard]] const char* GetTypeName() const noexcept override { return "CharacterMovementComponent"; }

    // Locomotion Mutators
    void SetVelocity(const DirectX::XMFLOAT3& velocity) noexcept { m_velocity = velocity; }
    void AddVelocity(const DirectX::XMFLOAT3& delta) noexcept
    {
        m_velocity.x += delta.x; m_velocity.y += delta.y; m_velocity.z += delta.z;
    }
    void SetUseGravity(bool useGravity) noexcept { m_useGravity = useGravity; }

    // State Queries
    [[nodiscard]] const DirectX::XMFLOAT3& GetVelocity() const noexcept { return m_velocity; }

private:
    DirectX::XMFLOAT3 m_velocity{ 0.0f, 0.0f, 0.0f };
    bool m_useGravity{ true };

    // Configurable local gravity 
    static constexpr float GRAVITY{ -9.81f };

    // Sibling cache 
    CapsuleColliderComponent* m_capsule{ nullptr };
};