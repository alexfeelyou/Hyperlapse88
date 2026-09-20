#pragma once

#include <memory>
#include <DirectXMath.h>
#include "PlayerState.h"
#include "PlayerControllerComponent.h"

// Unified Idle/Walk/Run handler
class PlayerLocomotion final : public PlayerState
{
public:
    void Enter(PlayerControllerComponent* controller) override;
    void Update(PlayerControllerComponent* controller, float dt) override;
    void Exit(PlayerControllerComponent* controller) override;

private:
    enum class LocoState { None, Idle, Walk, Run }; 
    LocoState m_locoState{ LocoState::None };
};

// Unified Dodging/Sliding handler
class PlayerDash final : public PlayerState
{
public:
    void Enter(PlayerControllerComponent* controller) override;
    void Update(PlayerControllerComponent* controller, float dt) override;
    void Exit(PlayerControllerComponent* controller) override;

private:
    static constexpr float DASH_IMPULSE_FORCE{ 45.0f };

    float m_timer{ 0.0f };
    DirectX::XMFLOAT2 m_dashDir{ 0.0f, 0.0f };
    bool m_canCancel{ false };
};

// Generic Data-Driven Attack handler (Wiring in Phase 5)
class PlayerAttackState final : public PlayerState
{
public:
    void Enter(PlayerControllerComponent* controller) override;
    void Update(PlayerControllerComponent* controller, float dt) override;
    void Exit(PlayerControllerComponent* controller) override;
};

// Generic Damage Flinch/Knockback handler
class PlayerHitReactState final : public PlayerState
{
public:
    void Enter(PlayerControllerComponent* controller) override;
    void Update(PlayerControllerComponent* controller, float dt) override;
    void Exit(PlayerControllerComponent* controller) override;
};