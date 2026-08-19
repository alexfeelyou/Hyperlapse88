#pragma once
#include "IPooledAttackPattern.h"
#include <DirectXMath.h>
#include <random>

// ============================================================
// AttackPhalanx - Phase 1 attack pattern.
//
// Boss slides to one side of the arena, summons N orbiting
// homing bullets in a spread fan facing the player, holds
// briefly, then launches them one by one.
//
// States:
//   1 = Moving + Charging (spawning bullets one by one)
//   2 = Holding (all bullets orbit, waiting before fire)
//   3 = Firing (launch one bullet at a time)
//   4 = Post-fire pause
//   5 = Returning to center
// ============================================================

struct PhalanxParams {
    int   count = 8;
    float chargeDelay = 0.2f;   // Delay between each bullet spawn
    float holdDuration = 2.5f;   // How long bullets orbit before firing
    float fireDelay = 0.05f;  // Delay between each bullet launch
    float speed = 70.0f;
    float hoverRadius = 4.0f;   // Orbit radius around boss
    float turnSpeed = 3.0f;   // Homing turn speed after launch
    float smoothSpeed = 12.0f;  // Orbit slot lerp speed
    float postFireDelay = 1.0f;
    float attackMoveSpeed = 3.0f;   // Boss lateral move speed during attack
    float returnMoveSpeed = 2.0f;
    int   damage = 3;
    float sfxVolume = 1.0f;
};

class Player;

class AttackPhalanx : public IPooledAttackPattern {
public:
    AttackPhalanx(const PhalanxParams& params, Player* target);
    ~AttackPhalanx() override = default;

    void StartPooled(Boss* boss, std::vector<std::unique_ptr<Bullet>>* pool) override;
    void Update(float dt, Boss* boss) override;
    void Render(ID3D11DeviceContext* context, Camera* camera, Boss* boss) override;
    void Stop(Boss* boss) override;

    bool IsFinished() const override;
    std::vector<Bullet*> GetActiveProjectiles() const override { return {}; }

    // The phase reads these to update boss movement target externally
    bool                      IsMovingBoss()       const { return m_state >= 1 && m_state <= 5; }
    DirectX::XMFLOAT3         GetTargetPosition()  const { return m_targetPosition; }
    float                     GetMoveLerpSpeed()   const { return m_moveLerpSpeed; }
    bool                      ShouldResetLerp()    const { return m_resetLerpFlag; }
    void                      ClearResetFlag() { m_resetLerpFlag = false; }

private:
    void TriggerRainCallback(); // Calls back into the phase to trigger rain

    PhalanxParams                         m_params;
    Player* m_target = nullptr;
    std::vector<std::unique_ptr<Bullet>>* m_pool = nullptr;
    Boss* m_boss = nullptr;

    int   m_state = 0;
    float m_timer = 0.0f;
    int   m_spawned = 0;
    int   m_fired = 0;
    bool  m_flareTriggered = false;

    DirectX::XMFLOAT3 m_targetPosition = { 0.0f, 0.0f, 0.0f };
    float             m_moveLerpSpeed = 3.0f;
    bool              m_resetLerpFlag = false;

    std::vector<Bullet*> m_bullets; // Raw pointers into the shared pool
};