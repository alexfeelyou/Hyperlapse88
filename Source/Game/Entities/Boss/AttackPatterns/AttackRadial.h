#pragma once
#include "IPooledAttackPattern.h"
#include <DirectXMath.h>

// ============================================================
// AttackRadial - Phase 1 attack pattern.
//
// Fires three interlocked radial bursts in quick succession.
// Each burst fires N bullets in a full circle. Odd-numbered
// bursts are offset by half a step to create a denser pattern.
// ============================================================

struct RadialParams {
    int   count = 55;
    float speed = 22.14f;
    float burstDelay = 0.156f;  // Seconds between each of the bursts
    int   burstCount = 3;       // Total bursts per trigger (Dipakai jika activeDuration == 0)
    int   damage = 1;
    float sfxVolume = 1.0f;
    DirectX::XMFLOAT4 color = { 1.0f, 0.2f, 0.2f, 1.0f };
    float activeDuration = 0.0f; // Jika > 0, akan menyembur terus selama X detik
};

class AttackRadial : public IPooledAttackPattern {
public:
    explicit AttackRadial(const RadialParams& params);
    ~AttackRadial() override = default;

    // --- TAMBAHKAN FUNGSI INI UNTUK COMBO AI ---
    void SetParams(const RadialParams& newParams) { m_params = newParams; }

    void StartPooled(Boss* boss, std::vector<std::unique_ptr<Bullet>>* pool) override;
    void Update(float dt, Boss* boss) override;
    void Render(ID3D11DeviceContext* context, Camera* camera, Boss* boss) override;
    void Stop(Boss* boss) override;

    bool IsFinished() const override;
    std::vector<Bullet*> GetActiveProjectiles() const override { return {}; }

private:
    void FireBurst(Boss* boss, float angleOffset);

    RadialParams                          m_params;
    std::vector<std::unique_ptr<Bullet>>* m_pool = nullptr;
    Boss* m_boss = nullptr;

    bool  m_active = false;
    int   m_burstsFired = 0;
    float m_burstTimer = 0.0f;
    float m_lifeTimer = 0.0f;
};