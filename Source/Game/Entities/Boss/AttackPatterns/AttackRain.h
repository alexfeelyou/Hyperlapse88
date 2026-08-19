#pragma once
#include "IPooledAttackPattern.h"
#include "Primitive.h"
#include <DirectXMath.h>
#include <vector>

// ============================================================
// AttackRain - Phase 1 attack pattern.
// ============================================================

struct RainParams {
    float minSpeed = 50.0f;
    float maxSpeed = 90.0f;
    float warningDuration = 1.2f;
    float activeDuration = 2.0f;
    float width = 25.0f;
    float depth = 40.0f;
    float damage = 0.2f;
    float sfxVolume = 1.0f;

    int   triggerCount = 1;     // Berapa kali hujan turun
    float triggerDelay = 0.5f;
};

enum class RainMode {
    HorizontalSweep,
    VerticalSweep,
    DualPillar,
    Targeted
};

class AttackRain : public IPooledAttackPattern {
public:
    AttackRain(const RainParams& params, RainMode mode, bool isPositiveSide, float sweepDir, Player* target);
    ~AttackRain() override = default;

    void StartPooled(Boss* boss, std::vector<std::unique_ptr<Bullet>>* pool) override;
    void Update(float dt, Boss* boss) override;
    void Render(ID3D11DeviceContext* context, Camera* camera, Boss* boss) override;
    void Stop(Boss* boss) override;

    bool IsFinished() const override;
    std::vector<Bullet*> GetActiveProjectiles() const override { return {}; }

    // State query — disesuaikan agar membaca dari zona pertama (Aman untuk BossPhase01)
    int               GetState()   const { return m_zones.empty() ? 0 : m_zones[0].state; }
    DirectX::XMFLOAT3 GetCenter()  const { return m_zones.empty() ? DirectX::XMFLOAT3(0, 0, 0) : m_zones[0].center1; }
    DirectX::XMFLOAT3 GetCenter2() const { return m_zones.empty() ? DirectX::XMFLOAT3(0, 0, 0) : m_zones[0].center2; }
    float             GetActualWidth() const;
    float             GetActualDepth() const;

private:
    struct RainZone {
        int state = 0;
        float stateTimer = 0.0f;
        float waitTimer = 0.0f;
        DirectX::XMFLOAT3 center1 = { 0.0f, 0.0f, 0.0f };
        DirectX::XMFLOAT3 center2 = { 0.0f, 0.0f, 0.0f };
    };

    void ClearVFX();

    RainParams                            m_params;
    RainMode                              m_mode;
    bool                                  m_isPositiveSide;
    float                                 m_sweepDir;

    std::vector<RainZone>                 m_zones;
    Player* m_target = nullptr;
    std::vector<std::unique_ptr<Bullet>>* m_pool = nullptr;
    Boss* m_boss = nullptr;

    bool              m_active = false;
    float             m_sfxTimer = 0.0f;
    const float       SFX_LOOP = 0.8f;

    std::vector<int>  m_vfxHandles;
    std::unique_ptr<Primitive> m_zonePrimitive;
};