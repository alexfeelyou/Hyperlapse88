#include "AttackSpears.h"
#include "Boss.h"
#include "WindowTrackingSystem.h"
#include "WindowManager.h"
#include "Player.h"
#include <algorithm>
#include <random>

using namespace DirectX;

AttackSpears::AttackSpears(const UndyneSpearParams& params, Player* target)
    : m_params(params), m_playerTarget(target) {}

void AttackSpears::Start(Boss* boss) {
    m_isSpawning = true;
    m_spawnedCount = 0;
    m_spawnTimer = 0.0f;

    // Prepare randomized spawn indices
    m_spawnIndices.clear();
    for (int i = 0; i < m_params.count; ++i) {
        m_spawnIndices.push_back(i);
    }
    static std::mt19937 gen(std::random_device{}());
    std::shuffle(m_spawnIndices.begin(), m_spawnIndices.end(), gen);
}

void AttackSpears::Update(float dt, Boss* boss) {
    if (!boss || !boss->GetWindowSystem()) return;

    // 1. Spawning Rainbow Arc
    if (m_isSpawning) {
        m_spawnTimer -= dt;
        if (m_spawnTimer <= 0.0f) {
            m_spawnTimer = m_params.spawnDelay;

            float angleRange = m_params.arcMaxAngle - m_params.arcMinAngle;
            float step = (m_params.count > 1) ? (angleRange / (m_params.count - 1)) : 0.0f;

            int randomPositionIndex = m_spawnIndices[m_spawnedCount];
            float angleDeg = m_params.arcMinAngle + (randomPositionIndex * step);
            float rad = XMConvertToRadians(angleDeg);

            DirectX::XMFLOAT3 spawnPos = {
                m_params.arcCenterX + cosf(rad) * m_params.arcRadius,
                0.0f,
                m_params.arcCenterZ + sinf(rad) * m_params.arcRadius
            };

            UndyneSpearWindow spear;
            spear.bullet = std::make_unique<Bullet>();
            spear.bullet->SetActive(true);
            spear.bullet->ApplyMovement(spawnPos, { 0, 0, 0 });
            spear.bullet->SetDamage(m_params.damage);
            spear.bullet->SetRadius(0.8f);

            spear.windowName = "Spear_" + std::to_string(rand() % 100000) + "_" + std::to_string(m_spawnedCount);

            TrackedWindowConfig config = { spear.windowName, "Spear.exe", 100, 100, 10 };
            boss->GetWindowSystem()->AddTrackedWindow(config, [raw = spear.bullet.get()]() {
                return raw->GetMovement()->GetPosition();
                });

            m_spears.push_back(std::move(spear));

            if (++m_spawnedCount >= m_params.count) {
                m_isSpawning = false;
            }
        }
    }

    // 2. Spear Hover & Homing Update
    for (auto& spear : m_spears) {
        if (spear.isPreparedForDestroy) continue;

        spear.timer += dt;
        DirectX::XMFLOAT3 bPos = spear.bullet->GetMovement()->GetPosition();

        if (spear.state == 0 && m_playerTarget) {
            DirectX::XMFLOAT3 pPos = m_playerTarget->GetPosition();
            float dx = pPos.x - bPos.x;
            float dz = pPos.z - bPos.z;
            float dist = sqrtf(dx * dx + dz * dz);

            if (dist > 0.001f) {
                spear.lockDir = { dx / dist, 0.0f, dz / dist };
                spear.bullet->GetMovement()->SetRotationY(XMConvertToDegrees(atan2f(dx, dz)));
            }

            if (spear.timer >= m_params.hoverDuration) {
                spear.state = 2; // Launch!
                spear.timer = 0.0f;
                spear.currentSpeed = m_params.startSpeed;
            }
        }
        else if (spear.state == 2) {
            spear.currentSpeed += m_params.acceleration * dt;
            if (spear.currentSpeed > m_params.maxSpeed) spear.currentSpeed = m_params.maxSpeed;

            spear.bullet->ApplyMovement(bPos, {
                spear.lockDir.x * spear.currentSpeed,
                0.0f,
                spear.lockDir.z * spear.currentSpeed
                });
            spear.bullet->Update(dt, nullptr);

            if (bPos.x < -40.0f || bPos.x > 40.0f || bPos.z < -40.0f || bPos.z > 40.0f) {
                spear.isPreparedForDestroy = true;
                spear.bullet->SetActive(false);

                auto extracted = boss->GetWindowSystem()->ExtractForPool(spear.windowName);
                if (extracted && extracted->window) {
                    WindowManager::Instance().DestroyWindow(extracted->window);
                }
            }
        }
    }
}

void AttackSpears::Render(ID3D11DeviceContext* context, Camera* camera, Boss* boss) {
    auto modelRenderer = Graphics::Instance().GetModelRenderer();
    for (auto& spear : m_spears) {
        if (!spear.isPreparedForDestroy && spear.bullet && spear.bullet->IsActive()) {
            modelRenderer->Draw(ShaderId::Phong, spear.bullet->GetModel(), { 0.0f, 0.8f, 1.0f, 1.0f });
        }
    }
}

void AttackSpears::Stop(Boss* boss) {
    if (boss && boss->GetWindowSystem()) {
        for (auto& spear : m_spears) {
            if (!spear.isPreparedForDestroy) {
                boss->GetWindowSystem()->RemoveTrackedWindow(spear.windowName);
            }
        }
    }
    m_spears.clear();
    m_isSpawning = false;
}

bool AttackSpears::IsFinished() const {
    if (m_isSpawning) return false;
    for (const auto& spear : m_spears) {
        if (!spear.isPreparedForDestroy) return false;
    }
    return true;
}

std::vector<Bullet*> AttackSpears::GetActiveProjectiles() const {
    std::vector<Bullet*> activeBullets;
    for (const auto& spear : m_spears) {
        if (!spear.isPreparedForDestroy && spear.bullet && spear.bullet->IsActive() && spear.state == 2) {
            activeBullets.push_back(spear.bullet.get());
        }
    }
    return activeBullets;
}