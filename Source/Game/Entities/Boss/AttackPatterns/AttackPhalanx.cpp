#pragma execution_character_set("utf-8")
#include "AttackPhalanx.h"
#include "Boss.h"
#include "System/AudioManager.h"
#include "System/Graphics.h"
#include "EffectManager.h"
#include "Player.h"
#include <DirectXMath.h>
#include <random>
#include <cmath>

using namespace DirectX;

AttackPhalanx::AttackPhalanx(const PhalanxParams& params, Player* target)
    : m_params(params), m_target(target) {}

void AttackPhalanx::StartPooled(Boss* boss, std::vector<std::unique_ptr<Bullet>>* pool) {
    m_pool = pool;
    m_boss = boss;
    m_state = 1;
    m_timer = 0.0f;
    m_spawned = 0;
    m_fired = 0;
    m_flareTriggered = false;
    m_bullets.clear();

    // Pick a random side to slide to
    std::mt19937 gen(std::random_device{}());
    m_targetPosition.x = (std::uniform_int_distribution<>(0, 1)(gen) == 0) ? -15.0f : 15.0f;
    m_targetPosition.z = 0.0f;
    m_moveLerpSpeed = m_params.attackMoveSpeed;
    m_resetLerpFlag = true;
}

void AttackPhalanx::Update(float dt, Boss* boss) {
    if (m_state == 0 || !m_pool || !boss) return;

    m_timer += dt;
    XMFLOAT3 bPos = boss->GetPosition();

    // Keep un-fired bullets orbiting the boss, aimed at the player
    for (int i = m_fired; i < m_spawned; ++i) {
        if (!m_bullets[i] || !m_bullets[i]->IsActive()) continue;

        XMFLOAT3 pPos = m_target->GetPosition();
        float dx = pPos.x - bPos.x;
        float dz = pPos.z - bPos.z;
        float angleToPlayer = atan2f(dx, dz);

        float totalSpread = XM_PI;
        float startOffset = -totalSpread * 0.5f;
        float step = m_params.count > 1 ? totalSpread / (float)(m_params.count - 1) : 0.0f;
        float bulletAngle = angleToPlayer + startOffset + (i * step);

        XMFLOAT3 hoverPos = bPos;
        hoverPos.x += sinf(bulletAngle) * m_params.hoverRadius;
        hoverPos.z += cosf(bulletAngle) * m_params.hoverRadius;
        hoverPos.y += 1.0f;

        XMFLOAT3 bletPos = m_bullets[i]->GetMovement()->GetPosition();
        float    s = m_params.smoothSpeed;
        bletPos.x += (hoverPos.x - bletPos.x) * s * dt;
        bletPos.y += (hoverPos.y - bletPos.y) * s * dt;
        bletPos.z += (hoverPos.z - bletPos.z) * s * dt;
        m_bullets[i]->GetMovement()->SetPosition(bletPos);
        m_bullets[i]->GetMovement()->SetRotationY(XMConvertToDegrees(angleToPlayer));
    }

    // --- State 1: Moving + Charging ---
    if (m_state == 1) {
        float dx = m_targetPosition.x - bPos.x;
        float dz = m_targetPosition.z - bPos.z;

        if ((dx * dx + dz * dz) > 1.0f) {
            m_timer = 0.0f; // Not at position yet, keep waiting
        }
        else {
            if (m_timer >= m_params.chargeDelay) {
                m_timer -= m_params.chargeDelay;

                for (auto& bullet : *m_pool) {
                    if (!bullet->IsActive()) {
                        bullet->SetActive(true);
                        bullet->ApplyMovement(bPos, { 0, 0, 0 });
                        bullet->SetHomingTarget(nullptr);
                        bullet->SetBossTarget(nullptr);
                        bullet->SetParabolic(false);
                        bullet->SetParryReturn(false);
                        bullet->SetRadius(0.35f);
                        bullet->scale = { 2.0f, 2.0f, 2.0f };
                        bullet->SetTurnSpeed(m_params.turnSpeed);
                        bullet->SetDamage(m_params.damage);
                        bullet->AttachVFX("Data/Effect/VFX_Boss_Phalanx_Smoke.efk", 0.3f);

                        m_bullets.push_back(bullet.get());
                        AudioManager::Instance().PlaySFX("Data/Sound/SE_Boss_Phalanx_Ready.wav", 0.1f * m_params.sfxVolume);
                        m_spawned++;
                        break;
                    }
                }

                if (m_spawned >= m_params.count) {
                    m_state = 2;
                    m_timer = 0.0f;
                }
            }
        }
    }
    // --- State 2: Holding ---
    else if (m_state == 2) {
        // Switch smoke VFX to flare 0.5s before hold ends
        if (m_timer >= (m_params.holdDuration - 0.5f) && !m_flareTriggered) {
            m_flareTriggered = true;
            for (Bullet* b : m_bullets) {
                if (b && b->IsActive()) {
                    b->AttachVFX("Data/Effect/VFX_Boss_Phalanx_Flare.efk", 0.3f);
                }
            }
        }
        if (m_timer >= m_params.holdDuration) {
            m_state = 3;
            m_timer = 0.0f;
        }
    }
    // --- State 3: Firing ---
    else if (m_state == 3) {
        if (m_timer >= m_params.fireDelay) {
            m_timer -= m_params.fireDelay;

            if (m_fired < m_spawned) {
                Bullet* b = m_bullets[m_fired];
                if (b && b->IsActive()) {
                    b->SetHomingTarget(m_target);

                    XMFLOAT3 myPos = b->GetMovement()->GetPosition();
                    XMFLOAT3 pPos = m_target->GetPosition();
                    float dx = pPos.x - myPos.x;
                    float dz = pPos.z - myPos.z;
                    float dist = sqrtf(dx * dx + dz * dz);

                    XMFLOAT3 dir = (dist > 0.001f)
                        ? XMFLOAT3{ dx / dist, 0.0f, dz / dist }
                    : XMFLOAT3{ 0.0f, 0.0f, 1.0f };

                    b->Fire(myPos, dir, m_params.speed);
                }
                AudioManager::Instance().PlaySFX("Data/Sound/SE_Boss_Phalanx_Shoot.wav", 0.1f * m_params.sfxVolume);
                m_fired++;
            }

            if (m_fired >= m_spawned) {
                m_state = 4;
                m_timer = 0.0f;
                m_bullets.clear();
            }
        }
    }
    // --- State 4: Post-fire pause ---
    else if (m_state == 4) {
        if (m_timer >= m_params.postFireDelay) {
            m_state = 5;
            m_targetPosition = { 0.0f, 0.0f, 0.0f };
            m_moveLerpSpeed = m_params.returnMoveSpeed;
            m_resetLerpFlag = true;
        }
    }
    // --- State 5: Returning to center ---
    else if (m_state == 5) {
        XMFLOAT3 pos = boss->GetPosition();
        float dx = m_targetPosition.x - pos.x;
        float dz = m_targetPosition.z - pos.z;
        if ((dx * dx + dz * dz) < 1.0f) {
            m_state = 0;
            m_bullets.clear();
        }
    }
}

void AttackPhalanx::Render(ID3D11DeviceContext* context, Camera* camera, Boss* boss) {
    // Bullets are rendered by the phase's central render loop
}

void AttackPhalanx::Stop(Boss* boss) {
    // Deactivate any bullets still orbiting (not yet fired)
    for (Bullet* b : m_bullets) {
        if (b && b->IsActive()) b->SetActive(false);
    }
    m_bullets.clear();
    m_state = 0;
}

bool AttackPhalanx::IsFinished() const {
    return m_state == 0;
}