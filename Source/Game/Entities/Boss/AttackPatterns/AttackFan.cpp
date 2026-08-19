#pragma execution_character_set("utf-8")
#include "AttackFan.h"
#include "Boss.h"
#include "Player.h"
#include "System/AudioManager.h"
#include <DirectXMath.h>
#include <cmath>

using namespace DirectX;

AttackFan::AttackFan(const FanParams& params, float lockedBaseAngle, Player* target)
    : m_params(params), m_lockedBaseAngle(lockedBaseAngle), m_target(target) {}

void AttackFan::StartPooled(Boss* boss, std::vector<std::unique_ptr<Bullet>>* pool) {
    m_pool = pool;
    m_boss = boss;
    m_active = true;
    m_wavesFired = 0;
    m_currentTrigger = 0;
    m_isTriggerWaiting = false;
    m_triggerTimer = 0.0f;

    // Set timer to full delay so first wave fires immediately on first Update()
    m_waveTimer = m_params.waveDelay;
}

void AttackFan::Update(float dt, Boss* boss) {
    if (!m_active || !m_pool) return;

    // 1. Jika sedang dalam jeda antar trigger (nunggu untuk trigger berikutnya)
    if (m_isTriggerWaiting) {
        m_triggerTimer += dt;
        if (m_triggerTimer >= m_params.triggerDelay) {
            m_isTriggerWaiting = false;
            m_wavesFired = 0; // Reset wave untuk trigger baru
            m_waveTimer = m_params.waveDelay;
        }
        return;
    }

    // 2. Logika tembakan normal
    m_waveTimer += dt;
    if (m_waveTimer >= m_params.waveDelay) {
        m_waveTimer -= m_params.waveDelay;

        if (m_target && boss) {
            DirectX::XMFLOAT3 pPos = m_target->GetPosition();
            DirectX::XMFLOAT3 bPos = boss->GetPosition();
            m_lockedBaseAngle = std::atan2f(pPos.x - bPos.x, pPos.z - bPos.z);
        }

        FireWave(boss);

        // Jika satu set tembakan (semua wave) sudah selesai
        if (++m_wavesFired >= m_params.waves) {
            m_currentTrigger++; // Hitung trigger selesai

            if (m_currentTrigger >= m_params.triggerCount) {
                m_active = false; // Serangan benar-benar selesai
            }
            else {
                // Masuk ke mode tunggu untuk trigger berikutnya
                m_isTriggerWaiting = true;
                m_triggerTimer = 0.0f;
            }
        }
    }
}

void AttackFan::Render(ID3D11DeviceContext* context, Camera* camera, Boss* boss) {
    // Bullets are rendered by the phase's central render loop
}

void AttackFan::Stop(Boss* boss) {
    m_active = false;
}

bool AttackFan::IsFinished() const {
    return !m_active;
}

// ============================================================
// Internal
// ============================================================

void AttackFan::FireWave(Boss* boss) {
    if (!m_pool || !boss) return;

    int   fired = 0;
    // --- UBAH m_params.lines JADI m_params.rows ---
    float startAngle = m_lockedBaseAngle - ((m_params.rows - 1) * m_params.spreadAngle * 0.5f);

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

            float angle = startAngle + (fired * m_params.spreadAngle);
            bullet->Fire(boss->GetPosition(), { sinf(angle), 0.0f, cosf(angle) }, m_params.speed);

            // --- UBAH m_params.lines JADI m_params.rows ---
            if (++fired >= m_params.rows) break;
        }
    }

    AudioManager::Instance().PlaySFX("Data/Sound/SE_Boss_Shoot.wav", 0.2f * m_params.sfxVolume);
}