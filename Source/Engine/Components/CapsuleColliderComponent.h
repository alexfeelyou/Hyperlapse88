#pragma once

#include <algorithm>
#include <cstdint>
#include <DirectXMath.h>
#include <json.hpp>
#include <PxPhysicsAPI.h>
#include <characterkinematic/PxController.h>
#include <characterkinematic/PxCapsuleController.h>
#include "System/CollisionLayer.h"
#include "IComponent.h"

// Configuration parameters for the capsule shape and PhysX kinematic character controller
struct CapsuleColliderConfig
{
    float radius{ 0.5f };          // Radius of the hemispherical caps (meters)
    float height{ 1.0f };          // Height of the inner cylindrical trunk (meters)
    float stepOffset{ 0.3f };      // Maximum stair step height the capsule can climb without jumping
    float slopeLimitDeg{ 45.0f };  // Maximum incline slope limit before sliding down (degrees)
    float contactOffset{ 0.02f };  // Skin width cushion to avoid jitter against level geometry

    DirectX::XMFLOAT3 localOffset{ 0.0f, 0.0f, 0.0f }; // Pivot offset relative to GameObject Transform

    std::uint32_t layer{ CollisionLayer::Player };              // Self layer identity
    std::uint32_t collidesWith{ CollisionLayer::Mask::Player }; // Interaction bitmask
};

class CapsuleColliderComponent final : public IComponent
{
public:
    CapsuleColliderComponent() noexcept = default;
    explicit CapsuleColliderComponent(const CapsuleColliderConfig& config) noexcept;
    ~CapsuleColliderComponent() noexcept override;

    // Strict 1:1 entity-component lifecycle: non-copyable and non-movable
    CapsuleColliderComponent(const CapsuleColliderComponent&) = delete;
    CapsuleColliderComponent& operator=(const CapsuleColliderComponent&) = delete;
    CapsuleColliderComponent(CapsuleColliderComponent&&) noexcept = delete;
    CapsuleColliderComponent& operator=(CapsuleColliderComponent&&) noexcept = delete;

    // Lifecycle hooks
    void OnAttach(GameObject* owner) noexcept override;
    void OnEnable() noexcept override;
    void OnDisable() noexcept override;
    void Update(float dt) override;
    void DrawInspector() override;
    void DrawGizmo(const GizmoContext& ctx) noexcept override;

    // Polymorphic Serialization
    void Serialize(nlohmann::json& outJson) const override;
    void Deserialize(const nlohmann::json& inJson) override;

    [[nodiscard]] const char* GetTypeName() const noexcept override { return "CapsuleColliderComponent"; }

    // Kinematic Motor Interface
    // Sweeps the capsule through the scene; returns PhysX collision flags
    physx::PxControllerCollisionFlags Move(const DirectX::XMFLOAT3& displacement, float dt) noexcept;

    // Snaps the capsule directly to a world coordinate without velocity accumulation
    void Teleport(const DirectX::XMFLOAT3& worldPos) noexcept;

    // Runtime resize (e.g., crouching or state swaps) preserving simulation integrity
    void Resize(float radius, float height) noexcept;

    // Queries
    [[nodiscard]] bool IsGrounded() const noexcept { return m_isGrounded; }
    [[nodiscard]] DirectX::XMFLOAT3 GetFootPosition() const noexcept;
    [[nodiscard]] DirectX::XMFLOAT3 GetCenterPosition() const noexcept;
    [[nodiscard]] float GetTotalHalfHeight() const noexcept;

    // Inspector & Editor Interop
    [[nodiscard]] CapsuleColliderConfig& GetConfig() noexcept { return m_config; }
    [[nodiscard]] const CapsuleColliderConfig& GetConfig() const noexcept { return m_config; }
    void MarkDirty() noexcept;

private:
    void CreateController() noexcept;
    void DestroyController() noexcept;
    void ApplyFilterData() const noexcept;

    [[nodiscard]] static constexpr bool IsFloatEqual(float a, float b, float epsilon = 0.0001f) noexcept
    {
        return (a >= b - epsilon) && (a <= b + epsilon);
    }

    CapsuleColliderConfig m_config{};
    physx::PxController* m_controller{ nullptr };

    bool m_isGrounded{ false };
    bool m_isDirty{ false };

    // Cached state to detect external Editor Transform edits
    DirectX::XMFLOAT3 m_lastFramePos{ 0.0f, 0.0f, 0.0f };
};