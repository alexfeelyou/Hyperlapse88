#pragma once

#include <memory>
#include <DirectXMath.h>
#include "PlayerState.h"
#include "PlayerControllerComponent.h"

// Forward declarations 
class CharacterMovementComponent;

class PlayerIdle final : public PlayerState
{
public:
    void Enter(PlayerControllerComponent* controller) override;
    void Update(PlayerControllerComponent* controller, float dt) override;
    void Exit(PlayerControllerComponent* controller) override;
};

class PlayerMoving final : public PlayerState
{
public:
    void Enter(PlayerControllerComponent* controller) override;
    void Update(PlayerControllerComponent* controller, float dt) override;
    void Exit(PlayerControllerComponent* controller) override;
};

class PlayerDash final : public PlayerState
{
public:
    void Enter(PlayerControllerComponent* controller) override;
    void Update(PlayerControllerComponent* controller, float dt) override;
    void Exit(PlayerControllerComponent* controller) override;

private:
    // State-specific encapsulated constants
    static constexpr float DASH_DURATION{ 0.15f };
    static constexpr float DASH_IMPULSE_FORCE{ 45.0f };
    static constexpr float DASH_IFRAME_DURATION{ 0.2f };

    float m_timer{ 0.0f };
    DirectX::XMFLOAT2 m_dashDir{ 0.0f, 0.0f };
    int m_dashGoVfxHandle{ -1 };
};

class PlayerSlash final : public PlayerState
{
public:
    void Enter(PlayerControllerComponent* controller) override;
    void Update(PlayerControllerComponent* controller, float dt) override;
    void Exit(PlayerControllerComponent* controller) override;

private:
    static constexpr float SLASH_DURATION{ 0.15f };
    static constexpr float SLASH_LUNGE_FORCE{ 40.0f };
    static constexpr float SLASH_DRAG_MULTIPLIER{ 10.0f };

    float m_timer{ 0.0f };
};

class PlayerShoot final : public PlayerState
{
public:
    void Enter(PlayerControllerComponent* controller) override;
    void Update(PlayerControllerComponent* controller, float dt) override;
    void Exit(PlayerControllerComponent* controller) override;

private:
    static constexpr float BASE_SHOOT_DELAY{ 0.15f };
    static constexpr float HOLD_PENALTY_MULTIPLIER{ 1.5f };

    float m_timer{ 0.0f };
    float m_minTapCooldown{ 0.0f };
};