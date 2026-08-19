#pragma once
#include "IPooledAttackPattern.h"
#include <DirectXMath.h>

// ============================================================
// AttackFan - Phase 1 attack pattern.
//
// Fires multiple waves of a spread shot (shotgun) aimed at
// the player's position at the moment of trigger. The aim
// angle is locked on trigger, so the player can dodge by
// moving after the tell.
// ============================================================

struct FanParams {
    int   rows = 5;
    int   waves = 5;
    float waveDelay = 0.133f;  // Seconds between each wave
    float spreadAngle = 0.130f;  // Radians between adjacent lines
    float speed = 35.617f;
    int   damage = 2;
    float sfxVolume = 1.0f;

    int triggerCount = 1;
    float triggerDelay = 1.0f;
};

class AttackFan : public IPooledAttackPattern {
public:
    // lockedBaseAngle: atan2(playerX - bossX, playerZ - bossZ) at trigger time
    AttackFan(const FanParams& params, float lockedBaseAngle, Player* target = nullptr);
    ~AttackFan() override = default;

    // --- TAMBAHKAN FUNGSI INI UNTUK COMBO AI ---
    void SetParams(const FanParams& newParams) { m_params = newParams; }

    void StartPooled(Boss* boss, std::vector<std::unique_ptr<Bullet>>* pool) override;
    void Update(float dt, Boss* boss) override;
    void Render(ID3D11DeviceContext* context, Camera* camera, Boss* boss) override;
    void Stop(Boss* boss) override;

    bool IsFinished() const override;
    std::vector<Bullet*> GetActiveProjectiles() const override { return {}; }

private:
    void FireWave(Boss* boss);

    FanParams                             m_params;
    float                                 m_lockedBaseAngle = 0.0f;
    std::vector<std::unique_ptr<Bullet>>* m_pool = nullptr;
    Boss* m_boss = nullptr;
    Player* m_target = nullptr;

    bool  m_active = false;
    int   m_wavesFired = 0;
    float m_waveTimer = 0.0f;

	int m_currentTrigger = 0;
    float m_triggerTimer = 0.0f;
    bool m_isTriggerWaiting = false;
};