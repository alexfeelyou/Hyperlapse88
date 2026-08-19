#include "AttackBoomerangs.h"
#include "Boss.h"
#include "WindowTrackingSystem.h"
#include "CameraController.h"
#include <SDL3/SDL.h>

using namespace DirectX;

AttackBoomerangs::AttackBoomerangs(const BoomerangParams& params)
    : m_params(params) {}

void AttackBoomerangs::Start(Boss* boss) {
    m_isSpawning = true;
    m_spawnedCount = 0;
    m_spawnTimer = m_params.spawnDelay;
}

void AttackBoomerangs::Update(float dt, Boss* boss) {
    if (!boss || !boss->GetWindowSystem()) return;

    float p2u = boss->GetWindowSystem()->GetPixelToUnitRatio();
    float limitX = (GetSystemMetrics(SM_CXSCREEN) / 2.0f) / p2u;
    float limitZ = (GetSystemMetrics(SM_CYSCREEN) / 2.0f) / p2u;

    // 1. Spawning Sequence
    if (m_isSpawning) {
        m_spawnTimer += dt;
        while (m_isSpawning && m_spawnTimer >= m_params.spawnDelay) {
            m_spawnTimer = (m_params.spawnDelay > 0.0f) ? m_spawnTimer - m_params.spawnDelay : 1.0f;

            BoomerangWindowBullet bw;
            bw.bullet = std::make_unique<Bullet>();
            bw.bullet->SetRadius(m_params.hitboxRadius);
            bw.bullet->SetDamage(m_params.damage);
            bw.bullet->scale = { m_params.visualScale, m_params.visualScale, m_params.visualScale };

            bw.spawnSide = (rand() % 2 == 0) ? 1 : -1;
            bw.startX = bw.spawnSide * (limitX + 8.0f);
            bw.targetX = bw.startX - (bw.spawnSide * m_params.maxTravelDistance);

            float randomZ = m_params.spawnBottomHalfOnly
                ? -((rand() % 100) / 100.0f) * (limitZ - 3.0f)
                : ((rand() % 200) / 100.0f - 1.0f) * (limitZ - 3.0f);

            DirectX::XMFLOAT3 startPos = { bw.startX, 1.0f, randomZ };
            bw.targetVelX = -1.0f * bw.spawnSide * m_params.speed;

            DirectX::XMFLOAT3 startDir = { bw.targetVelX > 0 ? 1.0f : -1.0f, 0.0f, 0.0f };
            bw.bullet->Fire(startPos, startDir, m_params.speed);
            bw.windowName = "boomerang_win_" + std::to_string(rand() % 100000);

            TrackedWindowConfig cfg = { bw.windowName, "BOOMERANG WARNING", (int)m_params.windowSize, (int)m_params.windowSize, 8 };
            cfg.isTransparent = false;
            boss->GetWindowSystem()->AddTrackedWindow(cfg,
                [ptr = bw.bullet.get()]() { return ptr->GetMovement()->GetPosition(); },
                [this]() { return DirectX::XMFLOAT2(m_params.windowSize, m_params.windowSize); }
            );

            m_bullets.push_back(std::move(bw));
            m_spawnedCount++;
            CameraController::Instance().AddTrauma(0.1f);

            if (m_spawnedCount >= m_params.spawnCount) m_isSpawning = false;
        }
    }

    // 2. Process Return Logic
    for (auto it = m_bullets.begin(); it != m_bullets.end(); ) {
        auto& bw = *it;
        if (!bw.bullet->IsActive()) {
            boss->GetWindowSystem()->RemoveTrackedWindow(bw.windowName);
            it = m_bullets.erase(it);
            continue;
        }

        DirectX::XMFLOAT3 vel = bw.bullet->GetVelocity();
        // Smooth Velocity Lerp
        vel.x += (bw.targetVelX - vel.x) * m_params.turnSpeed * dt;

        bw.bullet->ApplyMovement(bw.bullet->GetMovement()->GetPosition(), vel);
        bw.bullet->Update(dt, nullptr);

        DirectX::XMFLOAT3 pos = bw.bullet->GetMovement()->GetPosition();

        if (bw.state == 0) { // MODE MASUK
            if ((bw.spawnSide == 1 && pos.x <= bw.targetX) || (bw.spawnSide == -1 && pos.x >= bw.targetX)) {
                bw.state = 1;
                bw.targetVelX = bw.spawnSide * m_params.speed; // Switch direction
            }
        }
        else if (bw.state == 1) { // MODE KELUAR
            if ((bw.spawnSide == 1 && pos.x >= bw.startX) || (bw.spawnSide == -1 && pos.x <= bw.startX)) {
                boss->GetWindowSystem()->RemoveTrackedWindow(bw.windowName);
                it = m_bullets.erase(it);
                continue;
            }
        }
        ++it;
    }
}

void AttackBoomerangs::Render(ID3D11DeviceContext* context, Camera* camera, Boss* boss) {
    auto modelRenderer = Graphics::Instance().GetModelRenderer();
    for (auto& bw : m_bullets) {
        if (bw.bullet && bw.bullet->IsActive()) {
            modelRenderer->Draw(ShaderId::Phong, bw.bullet->GetModel(), { 0.6f, 0.0f, 0.8f, 1.0f });
        }
    }
}

void AttackBoomerangs::Stop(Boss* boss) {
    if (boss && boss->GetWindowSystem()) {
        for (auto& bw : m_bullets) {
            boss->GetWindowSystem()->RemoveTrackedWindow(bw.windowName);
        }
    }
    m_bullets.clear();
    m_isSpawning = false;
}

bool AttackBoomerangs::IsFinished() const {
    return !m_isSpawning && m_bullets.empty();
}

std::vector<Bullet*> AttackBoomerangs::GetActiveProjectiles() const {
    std::vector<Bullet*> activeBullets;
    for (const auto& bw : m_bullets) {
        if (bw.bullet && bw.bullet->IsActive()) {
            activeBullets.push_back(bw.bullet.get());
        }
    }
    return activeBullets;
}