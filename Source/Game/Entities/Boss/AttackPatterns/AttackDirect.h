#pragma once
#include "IPooledAttackPattern.h"
#include <DirectXMath.h>
#include <vector>

// ============================================================
// AttackDirect - Phase 1 attack pattern.
//
// Menembakkan peluru satu per satu lurus ke arah pemain.
// Arah tembakan selalu diperbarui mengikuti posisi pemain 
// secara real-time setiap kali peluru baru di-spawn.
// ============================================================

struct DirectParams {
    int   count = 10;           // Jumlah peluru yang ditembakkan
    float spawnDelay = 0.2f;    // Jeda antar tembakan
    float speed = 45.0f;        // Kecepatan peluru
    float radius = 0.35f;       // Ukuran hitbox
    float visualScale = 1.5f;   // Skala visual
    int   damage = 2;
    float sfxVolume = 1.0f;

    int   triggerCount = 1;     // Untuk AI Combo
    float triggerDelay = 1.0f;
};

class Player;

class AttackDirect : public IPooledAttackPattern {
public:
    AttackDirect(const DirectParams& params, Player* target);
    ~AttackDirect() override = default;

    void StartPooled(Boss* boss, std::vector<std::unique_ptr<Bullet>>* pool) override;
    void Update(float dt, Boss* boss) override;
    void Render(ID3D11DeviceContext* context, Camera* camera, Boss* boss) override;
    void Stop(Boss* boss) override;

    bool IsFinished() const override;
    std::vector<Bullet*> GetActiveProjectiles() const override { return {}; }

private:
    void FireBullet();

    DirectParams                          m_params;
    std::vector<std::unique_ptr<Bullet>>* m_pool = nullptr;
    Boss* m_boss = nullptr;
    Player* m_target = nullptr; // <--- Butuh Player untuk Tracking

    bool  m_active = false;
    int   m_spawnedCount = 0;
    float m_spawnTimer = 0.0f;

    int   m_currentTrigger = 0;
    float m_triggerTimer = 0.0f;
    bool  m_isTriggerWaiting = false;
};