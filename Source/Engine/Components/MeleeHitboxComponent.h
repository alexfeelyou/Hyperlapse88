#pragma once

#include <array>
#include <cstdint>
#include <DirectXMath.h>
#include <json.hpp>
#include "System/CollisionLayer.h"
#include "IComponent.h"

class AnimationComponent;
struct AnimNode;

// Cleave sweep tuning parameters for wide melee weapons
struct MeleeHitboxConfig
{
    float radius{ 0.35f };                                // Capsule hemisphere radius & blade thickness (meters)
    float height{ 1.40f };                                // Length of the blade's cylindrical segment (meters)
    DirectX::XMFLOAT3 localOffset{ 0.0f, 0.6f, 0.0f };   // Position offset along blade from socket pivot
    DirectX::XMFLOAT3 localRotation{ 0.0f, 0.0f, 0.0f }; // Angular tilt in degrees (Pitch, Yaw, Roll) to wrap blade curvature

    float baseDamage{ 35.0f };
    float poiseDamage{ 45.0f };
    float knockbackImpulse{ 18.0f };
    float hitStopDuration{ 0.06f }; // Impact freeze

    std::uint32_t targetLayer{ CollisionLayer::Enemy };
};

// Evaluates multi-target overlapping PhysX sweeps during active animation frames
class MeleeHitboxComponent final : public IComponent
{
public:
    static constexpr std::size_t s_maxVictimsPerSwing{ 32 };

    MeleeHitboxComponent() noexcept = default;
    explicit MeleeHitboxComponent(const MeleeHitboxConfig& config) noexcept;
    ~MeleeHitboxComponent() override = default;

    MeleeHitboxComponent(const MeleeHitboxComponent&) = delete;
    MeleeHitboxComponent& operator=(const MeleeHitboxComponent&) = delete;
    MeleeHitboxComponent(MeleeHitboxComponent&&) noexcept = delete;
    MeleeHitboxComponent& operator=(MeleeHitboxComponent&&) noexcept = delete;

    void OnAttach(GameObject* owner) noexcept override;
    void Update(float dt) override;
    void DrawGizmo(const GizmoContext& ctx) noexcept override;
    void DrawInspector() override;

    void Serialize(nlohmann::json& outJson) const override;
    void Deserialize(const nlohmann::json& inJson) override;

    [[nodiscard]] const char* GetTypeName() const noexcept override { return "MeleeHitboxComponent"; }

    // Hitbox state controls
    void OpenHitbox(float damageMultiplier = 1.0f) noexcept;
    void CloseHitbox() noexcept;
    [[nodiscard]] bool IsActive() const noexcept { return m_isActive; }

    [[nodiscard]] MeleeHitboxConfig& GetConfig() noexcept { return m_config; }

private:
    void ExecuteCleaveSweep() noexcept;
    void ResolveAnimationSource() noexcept;

    MeleeHitboxConfig m_config{};
    bool m_isActive{ false };
    float m_activeDamageMultiplier{ 1.0f };

    AnimationComponent* m_animation{ nullptr };
    const AnimNode* m_lastEvaluatedNode{ nullptr };
    int m_activeWindowIndex{ -1 };

    // Deduplication buffer: ensures each enemy capsule is struck only once per swing
    std::array<GameObject*, s_maxVictimsPerSwing> m_hitVictims{};
    std::size_t m_victimCount{ 0 };
};