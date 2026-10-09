#pragma once

#include <memory>
#include <DirectXMath.h>
#include "PlayerState.h"
#include "PlayerControllerComponent.h"

// GROUND & LOCOMOTION
class PlayerLocomotion final : public PlayerState
{
public:
    void Enter(PlayerControllerComponent* controller) override;
    void Update(PlayerControllerComponent* controller, float dt) override;
    void Exit(PlayerControllerComponent* controller) override;

private:
    float m_fallTimer{ 0.0f };
    float m_startTimer{ 0.0f };
    bool m_wasActivelyMoving{ false };
    bool m_wasCombatActive{ false };
};

// Add the PlayerStop class below Locomotion
class PlayerStop final : public PlayerState
{
public:
    void Enter(PlayerControllerComponent* controller) override;
    void Update(PlayerControllerComponent* controller, float dt) override;
    void Exit(PlayerControllerComponent* controller) override;

private:
    float m_stopTimer{ 0.0f };
};

class PlayerPivotTurn final : public PlayerState
{
public:
    void Enter(PlayerControllerComponent* controller) override;
    void Update(PlayerControllerComponent* controller, float dt) override;
    void Exit(PlayerControllerComponent* controller) override;
};

enum class SlideSubPhase : std::uint8_t
{
    Entry_Drop = 0,    // Uncancellable drop to the ground
    Sustain_Glide,     // Animation playback clamped; coasting with low friction
    Exit_Recovery      // Unfrozen animation; standing back up
};

class PlayerSlide final : public PlayerState
{
public:
    void Enter(PlayerControllerComponent* controller) override;
    void Update(PlayerControllerComponent* controller, float dt) override;
    void Exit(PlayerControllerComponent* controller) override;

private:
    SlideSubPhase m_phase{ SlideSubPhase::Entry_Drop };
    DirectX::XMFLOAT2 m_slideDir{ 0.0f, 0.0f };
    bool m_canCancel{ false };
    float m_standingHeight{ 1.0f };
    float m_standingRadius{ 0.5f };
};

// AERIAL & PARKOUR
class PlayerAirTraversal final : public PlayerState
{
public:
    void Enter(PlayerControllerComponent* controller) override;
    void Update(PlayerControllerComponent* controller, float dt) override;
    void Exit(PlayerControllerComponent* controller) override;

private:
    float m_airTimer{ 0.0f }; // Tracks how long we've been in the air
    bool m_isAcrobatic{ false };
    bool m_canCancelAcrobatic{ true };
};

class PlayerLanding final : public PlayerState
{
public:
    void Enter(PlayerControllerComponent* controller) override;
    void Update(PlayerControllerComponent* controller, float dt) override;
    void Exit(PlayerControllerComponent* controller) override;

private:
    float m_timer{ 0.0f };
    bool m_canCancel{ false };
};

class PlayerParkourWall final : public PlayerState
{
public:
    void Enter(PlayerControllerComponent* controller) override;
    void Update(PlayerControllerComponent* controller, float dt) override;
    void Exit(PlayerControllerComponent* controller) override;

private:
    DirectX::XMFLOAT3 m_wallNormal{ 0.0f, 0.0f, 0.0f };
    int m_wallSide{ 0 };
    float m_wallRunTimer{ 0.0f };
    bool m_wasGravityEnabled{ true };
    bool m_isParabolicMount{ false };
};

class PlayerDashEvade final : public PlayerState
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

// COMBAT (GROUND)
struct BufferedAttackIntent
{
    float timer{ 0.0f };
    int targetCommandNormal{ -1 }; // -1 = Neutral, 0 = Up, 1 = Back
};

class PlayerAttackPrimary final : public PlayerState
{
public:
    void Enter(PlayerControllerComponent* controller) override;
    void Update(PlayerControllerComponent* controller, float dt) override;
    void Exit(PlayerControllerComponent* controller) override;

private:
    void PlayCurrentAttack(PlayerControllerComponent* controller) noexcept;

    int m_trackIndex{ 2 }; // Starts at 2 so the first cycle hits Track 0 (Combo01)
    int m_stepIndex{ 0 };
    int m_activeNode{ 0 };
    BufferedAttackIntent m_bufferedAttack{};
    float m_exitTimer{ 0.0f };
    int m_cancelDeferFrames{ 0 };
    bool m_canCancel{ false };
    DirectX::XMFLOAT2 m_lungeDirection{ 0.0f, 1.0f };
};

class PlayerAttackContextual final : public PlayerState
{
public:
    void Enter(PlayerControllerComponent* controller) override;
    void Update(PlayerControllerComponent* controller, float dt) override;
    void Exit(PlayerControllerComponent* controller) override;

private:
    void PlayCurrentAttack(PlayerControllerComponent* controller) noexcept;

    int m_runAttackToggle{ 0 }; // DOD Ping-Pong Flag
    BufferedAttackIntent m_bufferedAttack{};
    float m_exitTimer{ 0.0f };
    int m_cancelDeferFrames{ 0 };
    bool m_canCancel{ false };
    DirectX::XMFLOAT2 m_lungeDirection{ 0.0f, 1.0f };
};

class PlayerAttackDirectional final : public PlayerState
{
public:
    void Enter(PlayerControllerComponent* controller) override;
    void Update(PlayerControllerComponent* controller, float dt) override;
    void Exit(PlayerControllerComponent* controller) override;

private:
    BufferedAttackIntent m_bufferedAttack{};
    float m_exitTimer{ 0.0f };
    int m_activeNode{ 0 };
    bool m_canCancel{ false };
    DirectX::XMFLOAT2 m_lungeDirection{ 0.0f, 1.0f };
};

enum class ChargeSubPhase : std::uint8_t
{
    Anticipation = 0,
    Sustain_Hold,
    Release_Lunge,
    Recovery
};

class PlayerAttackCharged final : public PlayerState
{
public:
    void Enter(PlayerControllerComponent* controller) override;
    void Update(PlayerControllerComponent* controller, float dt) override;
    void Exit(PlayerControllerComponent* controller) override;

private:
    ChargeSubPhase m_phase{ ChargeSubPhase::Anticipation };
    DirectX::XMFLOAT2 m_lungeDirection{ 0.0f, 1.0f };
    float m_chargeTimer{ 0.0f };
    float m_chargeRatio{ 0.40f }; // Tier 1 (Tap) default multiplier
    bool m_canCancel{ false };
    bool m_earlyRelease{ false };
};

// COMBAT (AERIAL)
class PlayerAttackAerial final : public PlayerState
{
public:
    void Enter(PlayerControllerComponent* controller) override;
    void Update(PlayerControllerComponent* controller, float dt) override;
    void Exit(PlayerControllerComponent* controller) override;

private:
    void PlayCurrentAttack(PlayerControllerComponent* controller) noexcept;

    int m_comboIndex{ 0 };
    BufferedAttackIntent m_bufferedAttack{};
    float m_exitTimer{ 0.0f };
    bool m_canCancel{ false };
    bool m_wasGravityEnabled{ true };
    DirectX::XMFLOAT2 m_lungeDirection{ 0.0f, 1.0f };
};

enum class PlungeSubPhase : std::uint8_t
{
    Start = 0,
    Loop,
    End
};

class PlayerAttackPlunge final : public PlayerState
{
public:
    void Enter(PlayerControllerComponent* controller) override;
    void Update(PlayerControllerComponent* controller, float dt) override;
    void Exit(PlayerControllerComponent* controller) override;

private:
    PlungeSubPhase m_phase{ PlungeSubPhase::Start };
    int m_variation{ 0 };
    float m_stateTimer{ 0.0f };
    bool m_wasGravityEnabled{ true };
    bool m_canCancel{ false };
    DirectX::XMFLOAT2 m_lungeDirection{ 0.0f, 1.0f };
};

// DEFENSE & REACTION
class PlayerParryCounter final : public PlayerState
{
public:
    void Enter(PlayerControllerComponent* controller) override;
    void Update(PlayerControllerComponent* controller, float dt) override;
    void Exit(PlayerControllerComponent* controller) override;
};

class PlayerHitReact final : public PlayerState
{
public:
    void Enter(PlayerControllerComponent* controller) override;
    void Update(PlayerControllerComponent* controller, float dt) override;
    void Exit(PlayerControllerComponent* controller) override;
};

class PlayerSkillBuff final : public PlayerState
{
public:
    void Enter(PlayerControllerComponent* controller) override;
    void Update(PlayerControllerComponent* controller, float dt) override;
    void Exit(PlayerControllerComponent* controller) override;
private:
    float m_timer{ 0.0f };
};

enum class SpeedAttackPhase : std::uint8_t
{
    Start = 0,
    Loop,
    End
};

class PlayerAttackSpeed final : public PlayerState
{
public:
    void Enter(PlayerControllerComponent* controller) override;
    void Update(PlayerControllerComponent* controller, float dt) override;
    void Exit(PlayerControllerComponent* controller) override;
private:
    SpeedAttackPhase m_phase{ SpeedAttackPhase::Start };
    float m_stateTimer{ 0.0f };
    float m_loopTimer{ 0.0f };
    float m_mashGraceTimer{ 0.0f }; // Supports button mashing
    bool m_isAerial{ false };
    bool m_canCancel{ false };
    bool m_wasGravityEnabled{ true };
    DirectX::XMFLOAT2 m_facingDir{ 0.0f, 1.0f };
};