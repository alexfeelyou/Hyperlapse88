#pragma execution_character_set("utf-8")
#include "AttackRadial.h"
#include "Boss.h"
#include "System/AudioManager.h"
#include <DirectXMath.h>

using namespace DirectX;

AttackRadial::AttackRadial(const RadialParams& params)
    : m_params(params) {}

void AttackRadial::StartPooled(Boss* boss, std::vector<std::unique_ptr<Bullet>>* pool) {
    m_pool = pool;
    m_boss = boss;
    m_active = true;
    m_burstsFired = 0;
    m_burstTimer = 0.0f;
    m_lifeTimer = 0.0f;

    // Fire first burst immediately on start
    FireBurst(boss, 0.0f);
    m_burstsFired++;
}

void AttackRadial::Update(float dt, Boss* boss) {
    if (!m_active || !m_pool) return;

    // ========================================================
    // LOGIKA PENGHENTIAN SERANGAN (DURATION vs BURST COUNT)
    // ========================================================
    if (m_params.activeDuration > 0.0f) {
        // Mode Stream: Berhenti jika waktu durasi sudah habis
        m_lifeTimer += dt;
        if (m_lifeTimer >= m_params.activeDuration) {
            m_active = false;
            return;
        }
    }
    else {
        // Mode Normal: Berhenti jika jumlah tembakan sudah mencapai batas
        if (m_burstsFired >= m_params.burstCount) {
            m_active = false;
            return;
        }
    }

    // ========================================================
    // LOGIKA PENEMBAKAN
    // ========================================================
    m_burstTimer += dt;
    if (m_burstTimer >= m_params.burstDelay) {
        m_burstTimer -= m_params.burstDelay;

        // Alternate half-step offsets to create an interlocking pattern
        float step = XM_2PI / (float)m_params.count;
        float offset = (m_burstsFired % 2 == 1) ? (step * 0.5f) : 0.0f;

        FireBurst(boss, offset);
        m_burstsFired++;
    }
}

void AttackRadial::Render(ID3D11DeviceContext* context, Camera* camera, Boss* boss) {
    // Bullets are rendered by the phase's central render loop
}

void AttackRadial::Stop(Boss* boss) {
    m_active = false;
}

bool AttackRadial::IsFinished() const {
    return !m_active;
}

// ============================================================
// Internal
// ============================================================

void AttackRadial::FireBurst(Boss* boss, float angleOffset) {
    if (!m_pool || !boss) return;

    int   fired = 0;
    float angleStep = XM_2PI / (float)m_params.count;

    for (auto& bullet : *m_pool) {
        if (!bullet->IsActive()) {
            bullet->SetRadius(0.25f);
            bullet->scale = { 1.0f, 1.0f, 1.0f };
            bullet->SetHomingTarget(nullptr);
            bullet->SetBossTarget(nullptr);
            bullet->SetParabolic(false);
            bullet->SetParryReturn(false);
            bullet->SetTurnSpeed(8.0f);
            bullet->SetDamage(m_params.damage);

            float angle = (fired * angleStep) + angleOffset;
            bullet->Fire(boss->GetPosition(), { sinf(angle), 0.0f, cosf(angle) }, m_params.speed);

            if (++fired >= m_params.count) break;
        }
    }

    AudioManager::Instance().PlaySFX("Data/Sound/SE_Boss_Shoot.wav", 0.2f * m_params.sfxVolume);
}