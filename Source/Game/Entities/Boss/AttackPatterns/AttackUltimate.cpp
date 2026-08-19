#pragma execution_character_set("utf-8")
#include "AttackUltimate.h"
#include "Boss.h"
#include "Player.h"
#include "System/Graphics.h"
#include "System/AudioManager.h"
#include "CameraController.h"
#include "EffectManager.h"
#include <DirectXMath.h>
#include <random>
#include <cmath>

using namespace DirectX;

AttackUltimate::AttackUltimate(const UltimateParams& params, Player* target)
    : m_params(params), m_target(target) {}

void AttackUltimate::StartPooled(Boss* boss, std::vector<std::unique_ptr<Bullet>>* pool) {
    m_pool = pool;
    m_state = State::Moving;
    m_chargeTimer = 0.0f;
    m_ball = nullptr;
    m_chargeEffectHandle = -1;

    // Boss slides to top center to charge
    m_targetPosition = { 0.0f, 0.0f, 10.0f };
    m_moveLerpSpeed = m_params.attackMoveSpeed;
    m_resetLerpFlag = true;

    // Claim a bullet from the pool for the ball
    for (auto& bullet : *m_pool) {
        if (!bullet->IsActive()) {
            bullet->SetActive(true);
            bullet->ApplyMovement({ 0.0f, -1000.0f, 0.0f }, { 0.0f, 0.0f, 0.0f });
            bullet->SetHomingTarget(nullptr);
            bullet->SetBossTarget(nullptr);
            bullet->SetParabolic(false);
            bullet->SetParryReturn(false);
            bullet->SetTurnSpeed(8.0f);

            float baseHitbox = m_params.baseHitbox;
            float baseVisual = baseHitbox * m_params.visualMultiplier;
            bullet->SetRadius(baseHitbox);
            bullet->scale = { baseVisual, baseVisual, baseVisual };
            bullet->SetDamage(m_params.ballDamage);

            m_ball = bullet.get();
            break;
        }
    }
}

void AttackUltimate::Update(float dt, Boss* boss) {
    if (m_state == State::Done || !m_pool || !boss) return;

    XMFLOAT3 bPos = boss->GetPosition();

    // --- Moving: wait for boss to reach charge position ---
    if (m_state == State::Moving) {
        float dx = m_targetPosition.x - bPos.x;
        float dz = m_targetPosition.z - bPos.z;
        if ((dx * dx + dz * dz) <= 1.0f) {
            m_state = State::Charging;
            m_chargeTimer = 0.0f;
            if (m_ball) m_ball->SetActive(true); // [FIX] Pastikan bola menyala saat mulai charge!
        }
        else {
            // [FIX] Jangan pernah menonaktifkan (SetActive(false)) bola di sini!
            return;
        }
    }

    // --- Charging ---
    if (m_state == State::Charging) {
        if (m_chargeTimer == 0.0f) {
            AudioManager::Instance().PlaySFX(
                "Data/Sound/SE_Boss_Bijuudama_Charge.wav", 0.1f * m_params.sfxVolume);
        }

        // Spawn charge VFX once
        if (m_chargeEffectHandle == -1 && m_ball) {
            XMFLOAT3 spawnPos = m_ball->GetMovement()->GetPosition();
            m_chargeEffectHandle = EffectManager::Instance().Play(
                "Data/Effect/VFX_Boss_Bijuudama_Charge.efk", spawnPos, 1.0f);
            m_ball->AttachVFX("Data/Effect/VFX_Boss_Fireball.efk", m_ball->scale.x * 0.3f);
        }

        m_chargeTimer += dt;

        // Position and grow ball while stationary
        if (m_ball) { // [FIX] Syarat IsActive() DIHAPUS agar kode di bawahnya bisa berjalan!
            if (!m_ball->IsActive()) m_ball->SetActive(true); // Nyalakan jika terlanjur mati

            XMFLOAT3 vel = m_ball->GetVelocity();
            if ((vel.x * vel.x + vel.z * vel.z) < 0.01f) {
                XMFLOAT3 offsetPos = bPos;
                offsetPos.z -= m_params.spawnOffsetZ;
                m_ball->GetMovement()->SetPosition(offsetPos);

                float progress = min(1.0f, m_chargeTimer / m_params.laserDuration);
                float currentHitbox = m_params.baseHitbox + (m_params.maxHitboxGrow * progress);
                m_ball->SetRadius(currentHitbox);
                float visualScale = currentHitbox * m_params.visualMultiplier;
                m_ball->scale = { visualScale, visualScale, visualScale };

                // Sync charge VFX scale to ball
                if (m_chargeEffectHandle != -1 &&
                    EffectManager::Instance().IsPlaying(m_chargeEffectHandle))
                {
                    EffectManager::Instance().SetPosition(m_chargeEffectHandle, offsetPos);
                    float vfxScale = visualScale * 0.5f;
                    EffectManager::Instance().SetScale(
                        m_chargeEffectHandle, { vfxScale, vfxScale, vfxScale });
                }
            }
        }

        if (m_chargeTimer >= m_params.laserDuration) {
            LaunchBall(boss);
        }
    }

    // --- Recovering ---
    if (m_state == State::Recovering) {
        m_recoveryTimer += dt;
        if (m_recoveryTimer >= m_params.postFireDelay) {
            m_state = State::Done;
            m_targetPosition = { 0.0f, 0.0f, 0.0f };
            m_moveLerpSpeed = m_params.returnMoveSpeed;
            m_resetLerpFlag = true;
        }
    }
}

void AttackUltimate::Render(ID3D11DeviceContext* context, Camera* camera, Boss* boss) {
    if (m_state != State::Charging || !m_target) return;

    auto shapeRenderer = Graphics::Instance().GetShapeRenderer();
    XMFLOAT3 pPos = m_target->GetPosition();
    pPos.y += 1.0f;

    float t = min(1.0f, m_chargeTimer / m_params.laserDuration);
    float ringRadius = m_params.laserStartRadius +
        (m_params.laserTargetRadius - m_params.laserStartRadius) * t;

    XMFLOAT4 ringColor = { 1.0f, 0.0f, 0.0f, 1.0f };
    if (IsInParryWindow())
        ringColor = { 1.0f, 1.0f, 1.0f, 1.0f };

    shapeRenderer->DrawSphere(pPos, ringRadius, ringColor);
}

void AttackUltimate::Stop(Boss* boss) {
    CancelCharge();
    if (m_ball && m_ball->IsActive()) {
        m_ball->SetActive(false);
    }
    m_ball = nullptr;
    m_state = State::Done;
}

bool AttackUltimate::IsFinished() const {
    return m_state == State::Done;
}

bool AttackUltimate::IsInParryWindow() const {
    return fabsf(m_chargeTimer - m_params.laserDuration) <= m_params.parryWindow;
}

// ============================================================
// ShatterBijuudama — Called externally on parry
// ============================================================

void AttackUltimate::ShatterBijuudama(XMFLOAT3 parryPos, Boss* boss) {
    if (!m_pool || !boss) return;

    if (m_ball) {
        m_ball->SetActive(false);
        m_ball = nullptr; 
    }

    CameraController::Instance().AddTrauma(0.8f);
    AudioManager::Instance().PlaySFX("Data/Sound/SE_Player_Parry_Boss.wav", 0.6f);

    std::mt19937 gen(std::random_device{}());
    std::uniform_int_distribution<>   distCount(m_params.shatterMinFragments, m_params.shatterMaxFragments);
    std::uniform_real_distribution<float> distSize(m_params.shatterMinRadius, m_params.shatterMaxRadius);
    std::uniform_real_distribution<float> distDur(m_params.shatterMinDuration, m_params.shatterMaxDuration);
    std::uniform_real_distribution<float> distAngle(-XM_PIDIV2, XM_PIDIV2);

    int fragments = distCount(gen);
    int spawned = 0;

    XMFLOAT3 bossPos = boss->GetPosition();
    float baseAngle = atan2f(bossPos.x - parryPos.x, bossPos.z - parryPos.z);

    for (auto& bullet : *m_pool) {
        if (!bullet->IsActive()) {
            bullet->SetActive(true);
            bullet->ApplyMovement(parryPos, { 0, 0, 0 });
            bullet->SetBossTarget(boss);
            bullet->SetParabolic(true);
            bullet->SetParryReturn(false);
            bullet->SetTurnSpeed(8.0f);

            float r = distSize(gen);
            bullet->SetRadius(r);
            bullet->scale = { r * 3.0f, r * 3.0f, r * 3.0f };

            float spreadAngle = baseAngle + distAngle(gen);
            XMFLOAT3 ctrlPoint = {
                parryPos.x + sinf(spreadAngle) * m_params.shatterCurveOffset,
                parryPos.y,
                parryPos.z + cosf(spreadAngle) * m_params.shatterCurveOffset
            };
            bullet->SetParabolaParams(parryPos, ctrlPoint, distDur(gen));
            AudioManager::Instance().PlaySFX(
                "Data/Sound/SE_Player_Parry_Boss.wav", 0.015f * m_params.sfxVolume);

            if (++spawned >= fragments) break;
        }
    }

    CancelCharge();
    m_ball = nullptr;
    m_state = State::Recovering;
    m_recoveryTimer = 0.0f;
}

// ============================================================
// Internal
// ============================================================

void AttackUltimate::LaunchBall(Boss* boss) {
    CancelCharge();

    m_state = State::Recovering;
    m_recoveryTimer = 0.0f;

    if (m_ball && m_ball->IsActive()) {
        XMFLOAT3 vel = m_ball->GetVelocity();
        if ((vel.x * vel.x + vel.z * vel.z) < 0.01f) {
            XMFLOAT3 bPos = boss->GetPosition();
            XMFLOAT3 pPos = m_target->GetPosition();
            float dx = pPos.x - bPos.x;
            float dz = pPos.z - bPos.z;
            float dist = sqrtf(dx * dx + dz * dz);

            if (dist > 0.001f) {
                float spd = m_params.shootSpeed;
                XMFLOAT3 shootVel = { (dx / dist) * spd, 0.0f, (dz / dist) * spd };
                XMFLOAT3 ballPos = m_ball->GetMovement()->GetPosition();
                m_ball->ApplyMovement(ballPos, shootVel);
                AudioManager::Instance().PlaySFX(
                    "Data/Sound/SE_Boss_Bijuudama_Shoot.wav", 0.2f * m_params.sfxVolume);
            }
        }
    }
    m_ball = nullptr;
}

void AttackUltimate::CancelCharge() {
    if (m_chargeEffectHandle != -1) {
        EffectManager::Instance().Stop(m_chargeEffectHandle);
        m_chargeEffectHandle = -1;
    }
    m_chargeTimer = 0.0f;
}