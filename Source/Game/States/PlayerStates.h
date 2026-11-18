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
    bool m_canCancelEvade{ false };
    bool m_canCancelLocomotion{ false };
    bool m_hasAuthoredEvadeCancel{ false };
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
    bool m_canCancelAcrobaticEvade{ true };
    bool m_canCancelAcrobaticCombo{ true };
};

class PlayerLanding final : public PlayerState
{
public:
    void Enter(PlayerControllerComponent* controller) override;
    void Update(PlayerControllerComponent* controller, float dt) override;
    void Exit(PlayerControllerComponent* controller) override;

private:
    float m_timer{ 0.0f };
    bool m_canCancelCombo{ false };
    bool m_canCancelEvade{ false };
    bool m_canCancelLocomotion{ false };
    bool m_hasAuthoredLocomotionCancel{ false };
    bool m_isEdgeBraked{ false };
    DirectX::XMFLOAT2 m_edgeDirection{ 0.0f, 0.0f };
    DirectX::XMFLOAT2 m_rollDirection{ 0.0f, 0.0f };
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
    bool m_canCancelCombo{ false };
    bool m_canCancelEvade{ false };
    bool m_canCancelLocomotion{ false };
    bool m_hasAuthoredLocomotionCancel{ false };
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
    bool m_canCancelCombo{ false };
    bool m_canCancelEvade{ false };
    bool m_canCancelLocomotion{ false };
    bool m_hasAuthoredEvadeCancel{ false };
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
    bool m_canCancelCombo{ false };
    bool m_canCancelEvade{ false };
    bool m_canCancelLocomotion{ false };
    bool m_hasAuthoredEvadeCancel{ false };
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
    bool m_canCancelCombo{ false };
    bool m_canCancelEvade{ false };
    bool m_canCancelLocomotion{ false };
    bool m_hasAuthoredEvadeCancel{ false };
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
    float m_chargeRatio{ 0.40f }; 
    bool m_canCancelCombo{ false };
    bool m_canCancelEvade{ false };
    bool m_canCancelLocomotion{ false };
    bool m_hasAuthoredEvadeCancel{ false };
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
    bool m_canCancelCombo{ false };
    bool m_canCancelEvade{ false };
    bool m_hasAuthoredEvadeCancel{ false };
    bool m_wasGravityEnabled{ true };
    DirectX::XMFLOAT2 m_lungeDirection{ 0.0f, 1.0f };
};

class AIControllerComponent;

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
    bool m_isDiving{ false };
    bool m_canCancelCombo{ false };
    bool m_canCancelEvade{ false };
    bool m_canCancelLocomotion{ false };
    bool m_hasAuthoredEvadeCancel{ false };
    AIControllerComponent* m_lockedTarget{ nullptr };
    DirectX::XMFLOAT2 m_lungeDirection{ 0.0f, 1.0f };
};

// DEFENSE & REACTION
class PlayerParryCounter final : public PlayerState
{
public:
    void Enter(PlayerControllerComponent* controller) override;
    void Update(PlayerControllerComponent* controller, float dt) override;
    void Exit(PlayerControllerComponent* controller) override;

    [[nodiscard]] bool IsParryWindowActive() const noexcept { return m_isParryWindowOpen; }
    void NotifyParrySuccess(PlayerControllerComponent* controller, const DirectX::XMFLOAT3& attackerPos) noexcept;
    void SetTargetAttackerPos(const DirectX::XMFLOAT3& attackerPos) noexcept
    {
        m_targetAttackerPos = attackerPos;
        m_hasTargetAttacker = true;
    }

private:
    std::uint8_t m_nextParryNode{ 0 }; // Alternates 0 (Parry_01) <-> 1 (Parry_02)
    DirectX::XMFLOAT3 m_targetAttackerPos{ 0.0f, 0.0f, 0.0f };
    bool m_hasTargetAttacker{ false };
    float m_stateTimer{ 0.0f };
    float m_exitTimer{ 0.0f };
    BufferedAttackIntent m_bufferedAttack{};
    bool m_isParryWindowOpen{ false };
    bool m_hasAuthoredParryWindow{ false };
    bool m_parrySucceeded{ false };
    bool m_canCancelCombo{ false };
    bool m_canCancelEvade{ false };
    bool m_canCancelLocomotion{ false };
    bool m_hasAuthoredEvadeCancel{ false };
};

enum class HitReactPhase : std::uint8_t
{
    GroundedFlinch = 0, // 4-way Hit_Large / Hit_Combat_Large (F/B/L/R)
    AirFlinch,          // 4-way Hit_Combat_Air (F/B/L/R, no L/R inversion) -> hands off to normal air/landing
    Knockdown,          // Hit_Large_Knockdown / Hit_Combat_Large_Knockdown
    GetUp,              // Get_Up / Get_up_Combat with wake-up invulnerability
    Dead                // Terminal HP == 0 state (clamped at end of Knockdown clip, never gets up)
};

class PlayerHitReact final : public PlayerState
{
public:
    void Enter(PlayerControllerComponent* controller) override;
    void Update(PlayerControllerComponent* controller, float dt) override;
    void Exit(PlayerControllerComponent* controller) override;

private:
    void TransitionToGetUp(PlayerControllerComponent* controller) noexcept;

    HitReactPhase m_phase{ HitReactPhase::GroundedFlinch };
    float m_stateTimer{ 0.0f };
    bool m_canCancelEvade{ false };
    bool m_canCancelLocomotion{ false };
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
    bool m_canCancelCombo{ false };
    bool m_canCancelEvade{ false };
    bool m_canCancelLocomotion{ false };
    bool m_hasAuthoredEvadeCancel{ false };
    bool m_wasGravityEnabled{ true };
    DirectX::XMFLOAT2 m_facingDir{ 0.0f, 1.0f };
};