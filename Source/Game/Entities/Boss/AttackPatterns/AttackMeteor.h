#pragma once
#include "IPooledAttackPattern.h"
#include <DirectXMath.h>
#include <vector>

// ============================================================
// AttackMeteor - Phase 1 attack pattern.
//
// Menembakkan peluru berukuran besar secara berurutan (bergantian) 
// dari sudut kanan atas layar menuju sudut kiri bawah secara diagonal.
// ============================================================

struct MeteorParams {
    int   count = 5;
    float spawnDelay = 0.4f;
    float speed = 35.0f;
    float speedVariance = 10.0f; // <--- TAMBAHKAN INI (Rentang acak kecepatan)
    float radius = 0.8f;
    float visualScale = 3.0f;

    float startX = 24.0f;
    float startZ = 15.0f;
    float targetX = -24.0f;
    float targetZ = -15.0f;

    float spreadOffset = 4.0f;
    int   damage = 3;
    float sfxVolume = 1.0f;

    int   triggerCount = 1;
    float triggerDelay = 1.0f;
}; 

class AttackMeteor : public IPooledAttackPattern {
public:
    explicit AttackMeteor(const MeteorParams& params);
    ~AttackMeteor() override = default;

    void StartPooled(Boss* boss, std::vector<std::unique_ptr<Bullet>>* pool) override;
    void Update(float dt, Boss* boss) override;
    void Render(ID3D11DeviceContext* context, Camera* camera, Boss* boss) override;
    void Stop(Boss* boss) override;

    bool IsFinished() const override;
    std::vector<Bullet*> GetActiveProjectiles() const override { return {}; }

private:
    void FireMeteor();

    MeteorParams                          m_params;
    std::vector<std::unique_ptr<Bullet>>* m_pool = nullptr;
    Boss* m_boss = nullptr;

    bool  m_active = false;
    int   m_spawnedCount = 0;
    float m_spawnTimer = 0.0f;

    int   m_currentTrigger = 0;
    float m_triggerTimer = 0.0f;
    bool  m_isTriggerWaiting = false;
};