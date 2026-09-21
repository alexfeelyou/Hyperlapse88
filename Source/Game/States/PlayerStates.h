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

// Generic Data-Driven Attack handler
class PlayerAttackState final : public PlayerState
{
public:
    void Enter(PlayerControllerComponent* controller) override;
    void Update(PlayerControllerComponent* controller, float dt) override;
    void Exit(PlayerControllerComponent* controller) override;

    // Call this before transitioning to route to the Dash Attack
    void SetDashAttackNext(bool isDashAttack) noexcept { m_wantsDashAttack = isDashAttack; }

private:
    void PlayCurrentAttack(PlayerControllerComponent* controller) noexcept;

    int m_comboIndex{ 0 };
    float m_attackBufferTimer{ 0.0f };
    float m_exitTimer{ 0.0f };
    bool m_canCancel{ false };
    bool m_wantsDashAttack{ false }; 
    DirectX::XMFLOAT2 m_lungeDirection{ 0.0f, 1.0f };
};

// Generic Damage Flinch/Knockback handler
class PlayerHitReactState final : public PlayerState
{
public:
    void Enter(PlayerControllerComponent* controller) override;
    void Update(PlayerControllerComponent* controller, float dt) override;
    void Exit(PlayerControllerComponent* controller) override;
};