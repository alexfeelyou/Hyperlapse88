#pragma once
#include "IPooledAttackPattern.h"
#include <DirectXMath.h>

// ============================================================
// AttackUltimate - Phase 1 boss ultimate attack (Bijuudama).
//
// Boss slides to the top of the arena and charges a massive
// energy ball while a shrinking ring telegraphs the timing.
// A parry window opens near the end. On fire, the ball
// launches toward the player and can be shattered by a parry
// (ShatterBijuudama), splitting into many fragments aimed back
// at the boss.
//
// States:
//   Moving  = Boss sliding to charge position
//   Charging = Ball growing, ring shrinking (parry window opens at end)
//   Fired   = Ball launched, pattern complete
// ============================================================

struct UltimateParams {
    // Charge timing
    float laserDuration = 4.0f;   // Total charge + ring shrink time
    float parryWindow = 0.5f;   // Seconds before end where parry is valid
    float laserStartRadius = 8.0f;   // Outer ring start size
    float laserTargetRadius = 1.5f;   // Inner ring final size

    // Ball properties
    float baseHitbox = 0.5f;
    float maxHitboxGrow = 2.0f;
    float visualMultiplier = 4.0f;
    float spawnOffsetZ = 2.0f;   // How far in front of boss the ball spawns
    float shootSpeed = 50.0f;
    int   ballDamage = 40;

    // Boss movement
    float attackMoveSpeed = 3.0f;
    float returnMoveSpeed = 2.0f;
    float postFireDelay = 1.0f;

    // Shatter (parry burst)
    int   shatterMinFragments = 8;
    int   shatterMaxFragments = 10;
    float shatterMinRadius = 0.2f;
    float shatterMaxRadius = 0.8f;
    float shatterMinDuration = 0.5f;
    float shatterMaxDuration = 0.7f;
    float shatterCurveOffset = 15.0f;

    DirectX::XMFLOAT4 ballColor = { 1.0f, 0.0f, 0.0f, 1.0f };
    float sfxVolume = 1.0f;
};

class Player;

class AttackUltimate : public IPooledAttackPattern {
public:
    AttackUltimate(const UltimateParams& params, Player* target);
    ~AttackUltimate() override = default;

    void StartPooled(Boss* boss, std::vector<std::unique_ptr<Bullet>>* pool) override;
    void Update(float dt, Boss* boss) override;
    void Render(ID3D11DeviceContext* context, Camera* camera, Boss* boss) override;
    void Stop(Boss* boss) override;

    bool IsFinished() const override;
    std::vector<Bullet*> GetActiveProjectiles() const override { return {}; }

    // Called externally (e.g. collision system) when the ball is parried
    void ShatterBijuudama(DirectX::XMFLOAT3 parryPos, Boss* boss);

    // Phase movement target — read by BossPhase01 to update boss position
    bool              IsMovingBoss()     const { return m_state != State::Done && m_state != State::Recovering; }
    DirectX::XMFLOAT3 GetTargetPosition()const { return m_targetPosition; }
    float             GetMoveLerpSpeed() const { return m_moveLerpSpeed; }
    bool              ShouldResetLerp()  const { return m_resetLerpFlag; }
    void              ClearResetFlag() { m_resetLerpFlag = false; }

    bool IsCharging()        const { return m_state == State::Charging; }
    bool IsInParryWindow()   const;
    float GetChargeTimer()   const { return m_chargeTimer; }
    Bullet* GetBall()        const { return m_ball; }

private:
    void LaunchBall(Boss* boss);
    void CancelCharge();

    enum class State { Moving, Charging, Fired, Recovering, Done };

    UltimateParams                        m_params;
    Player* m_target = nullptr;
    std::vector<std::unique_ptr<Bullet>>* m_pool = nullptr;

    State m_state = State::Done;

    float   m_chargeTimer = 0.0f;
    float   m_recoveryTimer = 0.0f;
    Bullet* m_ball = nullptr;

    int m_chargeEffectHandle = -1;

    DirectX::XMFLOAT3 m_targetPosition = {};
    float             m_moveLerpSpeed = 3.0f;
    bool              m_resetLerpFlag = false;
};