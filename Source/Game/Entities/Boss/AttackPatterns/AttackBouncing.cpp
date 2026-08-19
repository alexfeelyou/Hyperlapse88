#include "AttackBouncing.h"
#include "Boss.h"
#include "WindowTrackingSystem.h"
#include "System/AudioManager.h"
#include "CameraController.h"
#include <SDL3/SDL.h>

using namespace DirectX;

AttackBouncing::AttackBouncing(const BouncingBulletParams& params)
    : m_params(params) {}

void AttackBouncing::Start(Boss* boss) {
    m_isSpawning = true;
    m_spawnedCount = 0;
    m_spawnTimer = m_params.spawnDelay; // Force immediate first spawn

    // Cache screen limits based on pixel-to-unit ratio
    if (boss && boss->GetWindowSystem()) {
        float p2u = boss->GetWindowSystem()->GetPixelToUnitRatio();
        m_screenLimitX = (GetSystemMetrics(SM_CXSCREEN) / 2.0f) / p2u;
        m_screenLimitZ = (GetSystemMetrics(SM_CYSCREEN) / 2.0f) / p2u;
    }
}

void AttackBouncing::Update(float dt, Boss* boss) {
    if (!boss || !boss->GetWindowSystem()) return;

    // 1. Handle Spawning Sequence
    if (m_isSpawning) {
        m_spawnTimer += dt;
        while (m_isSpawning && m_spawnTimer >= m_params.spawnDelay) {
            m_spawnTimer = (m_params.spawnDelay > 0.0f) ? m_spawnTimer - m_params.spawnDelay : 1.0f;

            // Asymmetric pattern: Right, Top-Right, Top-Left
            float patternAngles[3] = {
                XMConvertToRadians(35.0f),
                XMConvertToRadians(75.0f),
                XMConvertToRadians(145.0f)
            };

            float angle = patternAngles[m_spawnedCount % 3];
            if (m_spawnedCount >= 3) {
                angle += XMConvertToRadians((float)((rand() % 10) - 5)); // Add chaos
            }

            BouncingWindowBullet bwb;
            bwb.bullet = std::make_unique<Bullet>();
            bwb.bullet->SetRadius(m_params.hitboxRadius);
            bwb.bullet->SetDamage(m_params.damage);
            bwb.bullet->scale = { m_params.visualScale, m_params.visualScale, m_params.visualScale };
            bwb.maxBounces = m_params.maxBounces;

            DirectX::XMFLOAT3 dir = { cosf(angle), 0, sinf(angle) };
            bwb.bullet->Fire(boss->GetPosition(), dir, m_params.speed);
            bwb.windowName = "bouncing_win_" + std::to_string(rand() % 100000);

            // Register Window tracking
            TrackedWindowConfig cfg;
            cfg.name = bwb.windowName;
            cfg.title = "PROJECTILE";
            cfg.width = (int)m_params.windowWidth;
            cfg.height = (int)m_params.windowHeight;
            cfg.isTransparent = false;

            boss->GetWindowSystem()->AddTrackedWindow(cfg,
                [ptr = bwb.bullet.get()]() { return ptr->GetMovement()->GetPosition(); },
                [this]() { return DirectX::XMFLOAT2(m_params.windowWidth, m_params.windowHeight); }
            );

            m_bullets.push_back(std::move(bwb));
            m_spawnedCount++;

            CameraController::Instance().AddTrauma(0.2f);
            AudioManager::Instance().PlaySFX("Data/Sound/SE_Boss_Bouncing_Shoot.wav", 0.1f);

            if (m_spawnedCount >= m_params.spawnCount) m_isSpawning = false;
        }
    }

    // 2. Process Physics & Screen Bouncing
    bool anyBouncedThisFrame = false;
    for (auto it = m_bullets.begin(); it != m_bullets.end(); ) {
        auto& bwb = *it;

        if (!bwb.bullet->IsActive()) {
            boss->GetWindowSystem()->RemoveTrackedWindow(bwb.windowName);
            it = m_bullets.erase(it);
            continue;
        }

        bwb.bullet->Update(dt, nullptr);

        DirectX::XMFLOAT3 pos = bwb.bullet->GetMovement()->GetPosition();
        DirectX::XMFLOAT3 vel = bwb.bullet->GetVelocity();
        float radius = bwb.bullet->GetRadius();

        if (bwb.bounceCount < bwb.maxBounces) {
            bool bounced = false;
            // X-Axis bounds
            if (pos.x > m_screenLimitX - radius) { pos.x = m_screenLimitX - radius; vel.x *= -1.0f; bounced = true; }
            else if (pos.x < -m_screenLimitX + radius) { pos.x = -m_screenLimitX + radius; vel.x *= -1.0f; bounced = true; }

            // Z-Axis bounds
            if (pos.z > m_screenLimitZ - radius) { pos.z = m_screenLimitZ - radius; vel.z *= -1.0f; bounced = true; }
            else if (pos.z < -m_screenLimitZ + radius) { pos.z = -m_screenLimitZ + radius; vel.z *= -1.0f; bounced = true; }

            if (bounced) {
                bwb.bounceCount++;
                bwb.bullet->ApplyMovement(pos, vel);
                anyBouncedThisFrame = true;
            }
        }
        else {
            // Destroy if out of bounds after max bounces
            if (abs(pos.x) > m_screenLimitX + 15.0f || abs(pos.z) > m_screenLimitZ + 15.0f) {
                boss->GetWindowSystem()->RemoveTrackedWindow(bwb.windowName);
                it = m_bullets.erase(it);
                continue;
            }
        }
        ++it;
    }

    if (anyBouncedThisFrame) {
        CameraController::Instance().AddTrauma(0.3f);
        std::string dashSounds[] = {
            "Data/Sound/SE_Boss_Bouncing_Thud_01.wav",
            "Data/Sound/SE_Boss_Bouncing_Thud_02.wav",
            "Data/Sound/SE_Boss_Bouncing_Thud_03.wav"
        };
        AudioManager::Instance().PlaySFX(dashSounds[rand() % 3], 0.2f);
    }
}

void AttackBouncing::Render(ID3D11DeviceContext* context, Camera* camera, Boss* boss) {
    auto modelRenderer = Graphics::Instance().GetModelRenderer();
    for (auto& bwb : m_bullets) {
        if (bwb.bullet && bwb.bullet->IsActive()) {
            modelRenderer->Draw(ShaderId::Phong, bwb.bullet->GetModel(), { 1.0f, 0.4f, 0.0f, 1.0f });
        }
    }
}

void AttackBouncing::Stop(Boss* boss) {
    if (boss && boss->GetWindowSystem()) {
        for (auto& bwb : m_bullets) {
            boss->GetWindowSystem()->RemoveTrackedWindow(bwb.windowName);
        }
    }
    m_bullets.clear();
    m_isSpawning = false;
}

bool AttackBouncing::IsFinished() const {
    return !m_isSpawning && m_bullets.empty();
}

std::vector<Bullet*> AttackBouncing::GetActiveProjectiles() const {
    std::vector<Bullet*> activeBullets;
    for (const auto& bwb : m_bullets) {
        if (bwb.bullet && bwb.bullet->IsActive()) {
            activeBullets.push_back(bwb.bullet.get());
        }
    }
    return activeBullets;
}