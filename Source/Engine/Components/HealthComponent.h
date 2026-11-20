#pragma once
#pragma once

#include <DirectXMath.h>
#include <json.hpp>
#include "IComponent.h"

// Configuration parameters for entity durability and stagger resilience
struct HealthConfig
{
    float maxHp{ 100.0f };
    float maxPoise{ 50.0f };
    float poiseRecoveryRate{ 15.0f }; // Poise recovered per second
    float poiseRecoveryDelay{ 2.5f }; // Delay before poise begins regenerating
};

// Manages entity life, poise breakdown, and damage reception
class HealthComponent final : public IComponent
{
public:
    HealthComponent() noexcept = default;
    explicit HealthComponent(const HealthConfig& config) noexcept;
    ~HealthComponent() override = default;

    HealthComponent(const HealthComponent&) = delete;
    HealthComponent& operator=(const HealthComponent&) = delete;
    HealthComponent(HealthComponent&&) noexcept = delete;
    HealthComponent& operator=(HealthComponent&&) noexcept = delete;

    void OnAttach(GameObject* owner) noexcept override;
    void Update(float dt) override;
    void DrawInspector() override;

    void Serialize(nlohmann::json& outJson) const override;
    void Deserialize(const nlohmann::json& inJson) override;

    [[nodiscard]] const char* GetTypeName() const noexcept override { return "HealthComponent"; }

    // Core combat reception interface
    // Returns true if this hit depleted poise or killed the entity
    bool ApplyDamage(
        float damage,
        float poiseDamage,
        const DirectX::XMFLOAT3& impulse = { 0.0f, 0.0f, 0.0f },
        float hitStopDuration = 0.05f) noexcept;

    void Heal(float amount) noexcept;
    void ResetPoise() noexcept;

    // State queries
    [[nodiscard]] bool IsDead() const noexcept { return m_isDead; }
    [[nodiscard]] bool IsHitStopped() const noexcept { return m_hitStopTimer > 0.0f; }
    [[nodiscard]] float GetCurrentHp() const noexcept { return m_currentHp; }
    [[nodiscard]] float GetMaxHp() const noexcept { return m_config.maxHp; }
    [[nodiscard]] float GetCurrentPoise() const noexcept { return m_currentPoise; }
    [[nodiscard]] float GetMaxPoise() const noexcept { return m_config.maxPoise; }

    void SetInvincible(bool invincible) noexcept { m_isInvincible = invincible; }

private:
    HealthConfig m_config{};

    float m_currentHp{ 100.0f };
    float m_currentPoise{ 50.0f };
    float m_poiseDelayTimer{ 0.0f };
    float m_hitStopTimer{ 0.0f };

    bool m_isDead{ false };
    bool m_isInvincible{ false };
};