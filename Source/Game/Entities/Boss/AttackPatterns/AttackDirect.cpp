#pragma execution_character_set("utf-8")
#include "AttackDirect.h"
#include "Boss.h"
#include "Player.h"
#include "System/AudioManager.h"
#include <DirectXMath.h>
#include <cmath>

using namespace DirectX;

AttackDirect::AttackDirect(const DirectParams& params, Player* target)
    : m_params(params), m_target(target) {}

void AttackDirect::StartPooled(Boss* boss, std::vector<std::unique_ptr<Bullet>>* pool) {
    m_pool = pool;
    m_boss = boss;
    m_active = true;
    m_spawnedCount = 0;
    m_currentTrigger = 0;
    m_isTriggerWaiting = false;
    m_triggerTimer = 0.0f;

    // Set agar peluru pertama langsung meluncur
    m_spawnTimer = m_params.spawnDelay;
}

void AttackDirect::Update(float dt, Boss* boss) {
    if (!m_active || !m_pool || !m_boss || !m_target) return;

    if (m_isTriggerWaiting) {
        m_triggerTimer += dt;
        if (m_triggerTimer >= m_params.triggerDelay) {
            m_isTriggerWaiting = false;
            m_spawnedCount = 0;
            m_spawnTimer = m_params.spawnDelay;
        }
        return;
    }

    m_spawnTimer += dt;
    if (m_spawnTimer >= m_params.spawnDelay) {
        m_spawnTimer -= m_params.spawnDelay;

        FireBullet();

        if (++m_spawnedCount >= m_params.count) {
            m_currentTrigger++;
            if (m_currentTrigger >= m_params.triggerCount) {
                m_active = false;
            }
            else {
                m_isTriggerWaiting = true;
                m_triggerTimer = 0.0f;
            }
        }
    }
}

void AttackDirect::Render(ID3D11DeviceContext* context, Camera* camera, Boss* boss) {
    // Render ditangani terpusat
}

void AttackDirect::Stop(Boss* boss) {
    m_active = false;
}

bool AttackDirect::IsFinished() const {
    return !m_active;
}

void AttackDirect::FireBullet() {
    if (!m_pool || !m_boss || !m_target) return;

    for (auto& bullet : *m_pool) {
        if (!bullet->IsActive()) {

            // ========================================================
            // LOGIKA TRACKING REAL-TIME
            // ========================================================
            XMFLOAT3 bPos = m_boss->GetPosition();
            XMFLOAT3 pPos = m_target->GetPosition();

            float dx = pPos.x - bPos.x;
            float dz = pPos.z - bPos.z;
            float dist = sqrtf((dx * dx) + (dz * dz));

            XMFLOAT3 direction = { 0.0f, 0.0f, 1.0f }; // Default jika jarak 0
            if (dist > 0.001f) {
                direction = { dx / dist, 0.0f, dz / dist };
            }

            bullet->SetRadius(m_params.radius);
            bullet->scale = { m_params.visualScale, m_params.visualScale, m_params.visualScale };
            bullet->SetHomingTarget(nullptr);
            bullet->SetBossTarget(nullptr);
            bullet->SetParabolic(false);
            bullet->SetParryReturn(false);
            bullet->SetTurnSpeed(8.0f);
            bullet->SetDamage(m_params.damage);

            bullet->Fire(bPos, direction, m_params.speed);
            break;
        }
    }

    AudioManager::Instance().PlaySFX("Data/Sound/SE_Boss_Shoot.wav", 0.2f * m_params.sfxVolume);
}