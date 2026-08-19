#pragma execution_character_set("utf-8")
#include "AttackMeteor.h"
#include "Boss.h"
#include "System/AudioManager.h"
#include <DirectXMath.h>
#include <cmath>

using namespace DirectX;

AttackMeteor::AttackMeteor(const MeteorParams& params)
    : m_params(params) {}

void AttackMeteor::StartPooled(Boss* boss, std::vector<std::unique_ptr<Bullet>>* pool) {
    m_pool = pool;
    m_boss = boss;
    m_active = true;
    m_spawnedCount = 0;
    m_currentTrigger = 0;
    m_isTriggerWaiting = false;
    m_triggerTimer = 0.0f;

    // Set agar meteor pertama langsung meluncur
    m_spawnTimer = m_params.spawnDelay;
}

void AttackMeteor::Update(float dt, Boss* boss) {
    if (!m_active || !m_pool) return;

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

        FireMeteor();

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

void AttackMeteor::Render(ID3D11DeviceContext* context, Camera* camera, Boss* boss) {
    // Peluru dirender terpusat oleh Phase
}

void AttackMeteor::Stop(Boss* boss) {
    m_active = false;
}

bool AttackMeteor::IsFinished() const {
    return !m_active;
}

void AttackMeteor::FireMeteor() {
    if (!m_pool) return;

    for (auto& bullet : *m_pool) {
        if (!bullet->IsActive()) {

            // 1. Hitung arah lintasan utama (Dari Start ke Target Anchor)
            float dx = m_params.targetX - m_params.startX;
            float dz = m_params.targetZ - m_params.startZ;
            float dist = sqrtf((dx * dx) + (dz * dz));

            float dirX = 0.0f;
            float dirZ = 1.0f; // Fallback jika jarak 0
            if (dist > 0.0001f) {
                dirX = dx / dist;
                dirZ = dz / dist;
            }

            // 2. Cari vektor tegak lurus (Perpendicular) untuk garis sebar
            float perpX = -dirZ;
            float perpZ = dirX;

            // ========================================================
            // PERUBAHAN 1: TIDAK URUT (SCATTER INDEX)
            // ========================================================
            // Gunakan Prime Jump agar urutan jatuhnya melompat-lompat
            // Contoh untuk count=5: urutan yang jatuh adalah indeks 0, 2, 4, 1, 3
            int primeJump = 5;
            int scatterIndex = (m_spawnedCount * primeJump) % m_params.count;

            // 3. Kalkulasi jarak offset menggunakan scatterIndex
            float centerIndex = (m_params.count - 1) / 2.0f;
            float currentOffset = (scatterIndex - centerIndex) * m_params.spreadOffset;

            // ========================================================
            // PERUBAHAN 2: RANDOM OFFSET X (JITTER)
            // ========================================================
            // Menghasilkan angka acak antara -2.5f hingga 2.5f
            float randomOffsetX = ((rand() % 100) / 50.0f - 1.0f) * 8.0f;

            // 4. Set Titik Spawn final untuk meteor ini
            DirectX::XMFLOAT3 spawnPos = {
                m_params.startX + (perpX * currentOffset) + randomOffsetX,  // <--- Ditambah jitter X
                1.0f,
                m_params.startZ + (perpZ * currentOffset)
            };

            DirectX::XMFLOAT3 direction = { dirX, 0.0f, dirZ };
            // ========================================================
            // PERUBAHAN 3: RANDOM SPEED OFFSET (VARIANCE)
            // ========================================================
            // Menghasilkan angka acak antara -1.0 hingga 1.0, lalu dikali batas variance
            float randomSpeedOffset = ((rand() % 100) / 50.0f - 1.0f) * m_params.speedVariance;
            float finalSpeed = m_params.speed + randomSpeedOffset;

            // Pastikan kecepatan tidak pernah 0 atau negatif (minimal 5.0f)
            if (finalSpeed < 5.0f) finalSpeed = 5.0f;

            bullet->SetRadius(m_params.radius);
            bullet->scale = { m_params.visualScale, m_params.visualScale, m_params.visualScale };
            bullet->SetHomingTarget(nullptr);
            bullet->SetBossTarget(nullptr);
            bullet->SetParabolic(false);
            bullet->SetParryReturn(false);
            bullet->SetTurnSpeed(8.0f);
            bullet->SetDamage(m_params.damage);

            // [PERBAIKAN] Gunakan finalSpeed, bukan m_params.speed
            bullet->Fire(spawnPos, direction, finalSpeed);
            break;
        }
    }

    AudioManager::Instance().PlaySFX("Data/Sound/SE_Boss_Shoot.wav", 0.2f * m_params.sfxVolume);
}