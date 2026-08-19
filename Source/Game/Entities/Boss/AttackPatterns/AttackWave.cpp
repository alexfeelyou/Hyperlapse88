#pragma execution_character_set("utf-8")
#include "AttackWave.h"
#include "Boss.h"
#include "System/AudioManager.h"
#include <DirectXMath.h>

using namespace DirectX;

AttackWave::AttackWave(const WaveParams& params)
    : m_params(params) {}

void AttackWave::StartPooled(Boss* boss, std::vector<std::unique_ptr<Bullet>>* pool) {
    m_pool = pool;
    m_boss = boss;
    m_active = true;
    m_wavesFired = 0;
    m_waveTimer = m_params.waveDelay; // Set penuh agar langsung nembak di frame pertama
    m_nextIsRight = true;             // Gelombang pertama dari kanan
}

void AttackWave::Update(float dt, Boss* boss) {
    if (!m_active || !m_pool) return;

    m_waveTimer += dt;
    if (m_waveTimer >= m_params.waveDelay) {
        m_waveTimer -= m_params.waveDelay;

        FireWave(m_nextIsRight);

        m_nextIsRight = !m_nextIsRight; // Balikkan arah untuk gelombang berikutnya

        if (++m_wavesFired >= m_params.waves) {
            m_active = false;
        }
    }
}

void AttackWave::Render(ID3D11DeviceContext* context, Camera* camera, Boss* boss) {
    // Peluru di-render oleh BossPhase01 secara terpusat
}

void AttackWave::Stop(Boss* boss) {
    m_active = false;
}

bool AttackWave::IsFinished() const {
    return !m_active;
}

// ============================================================
// Internal Logic
// ============================================================
void AttackWave::FireWave(bool fromRight) {
    if (!m_pool) return;

    int fired = 0;
    // Kanan = Track Ganjil (1, 3, 5...), Kiri = Track Genap (0, 2, 4...)
    int startIndex = fromRight ? 1 : 0;

    for (auto& bullet : *m_pool) {
        if (!bullet->IsActive()) {
            int trackIndex = startIndex + (fired * 2);

            // Cegah peluru spawn jika melewati batas total track
            if (trackIndex >= m_params.trackCount) break;

            // Kalkulasi posisi spawn di grid/track
            float zPos = m_params.startZ + (trackIndex * m_params.trackSpacing);
            float xPos = fromRight ? m_params.spawnX : -m_params.spawnX;

            DirectX::XMFLOAT3 spawnPos = { xPos, 1.0f, zPos };
            DirectX::XMFLOAT3 direction = { fromRight ? -1.0f : 1.0f, 0.0f, 0.0f };

            // Setup bullet
            bullet->SetRadius(0.35f);
            bullet->scale = { 1.5f, 1.5f, 1.5f };
            bullet->SetHomingTarget(nullptr);
            bullet->SetBossTarget(nullptr);
            bullet->SetParabolic(false);
            bullet->SetParryReturn(false);
            bullet->SetTurnSpeed(8.0f);
            bullet->SetDamage(m_params.damage);

            bullet->Fire(spawnPos, direction, m_params.speed);

            if (++fired >= m_params.bulletsPerWave) break;
        }
    }

    AudioManager::Instance().PlaySFX("Data/Sound/SE_Boss_Shoot.wav", 0.2f * m_params.sfxVolume);
}