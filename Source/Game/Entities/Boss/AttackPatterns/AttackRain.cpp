#pragma execution_character_set("utf-8")
#include "AttackRain.h"
#include "Boss.h"
#include "System/Graphics.h"
#include "System/AudioManager.h"
#include "CameraController.h"
#include "EffectManager.h"
#include "Camera.h"
#include <random>
#include <cmath>
#include <windows.h>
#include "WindowTrackingSystem.h"
#include "Player.h"

using namespace DirectX;

AttackRain::AttackRain(const RainParams& params, RainMode mode, bool isPositiveSide, float sweepDir, Player* target)
    : m_params(params), m_mode(mode), m_isPositiveSide(isPositiveSide), m_sweepDir(sweepDir), m_target(target)
{
    // Reservasi handle VFX untuk setiap zona agar tidak bentrok
    int maxDrops = (mode == RainMode::DualPillar) ? 800 : 400;
    m_vfxHandles.assign(params.triggerCount * maxDrops, -1);
}

void AttackRain::StartPooled(Boss* boss, std::vector<std::unique_ptr<Bullet>>* pool) {
    m_pool = pool;
    m_boss = boss;
    m_active = true;

    auto device = Graphics::Instance().GetDevice();
    m_zonePrimitive = std::make_unique<Primitive>(device);

    m_zones.clear();
    // Buat zona sebanyak nilai TriggerCount
    for (int i = 0; i < m_params.triggerCount; i++) {
        RainZone z;
        z.state = 0;
        z.stateTimer = 0.0f;
        z.waitTimer = i * m_params.triggerDelay;
        m_zones.push_back(z);
    }
}

void AttackRain::Update(float dt, Boss* boss) {
    if (!m_active) return;

    bool allFinished = true;
    bool anyActiveOrDissipating = false;
    bool isHit = false;

    m_sfxTimer += dt;

    float halfW = GetActualWidth() * 0.5f;
    float halfD = GetActualDepth() * 0.5f;
    DirectX::XMFLOAT3 pPos = (m_target) ? m_target->GetPosition() : DirectX::XMFLOAT3(0, 0, 0);
    constexpr float PLAYER_RADIUS = 0.3f;

    // Fungsi pengecek hitbox manual (Sama persis seperti aslimu)
    auto isInsideZone = [&](const DirectX::XMFLOAT3& center) {
        float minX = center.x - halfW - PLAYER_RADIUS;
        float maxX = center.x + halfW + PLAYER_RADIUS;
        float minZ = center.z - halfD - PLAYER_RADIUS;
        float maxZ = center.z + halfD + PLAYER_RADIUS;
        return (pPos.x > minX && pPos.x < maxX && pPos.z > minZ && pPos.z < maxZ);
        };

    for (auto& zone : m_zones) {
        if (zone.state == 4) continue;
        allFinished = false;

        if (zone.state == 0) { // Tunggu Delay
            zone.waitTimer -= dt;
            if (zone.waitTimer <= 0.0f) {
                zone.state = 1;
                zone.stateTimer = 0.0f;

                // Hitung posisi saat baru muncul
                if (m_mode == RainMode::DualPillar) {
                    zone.center1 = { -16.0f, 0.0f,  0.0f };
                    zone.center2 = { 16.0f, 0.0f,  0.0f };
                }
                else if (m_mode == RainMode::VerticalSweep) {
                    zone.center1 = { m_isPositiveSide ? 12.5f : -12.5f, 0.0f, 0.0f };
                }
                else if (m_mode == RainMode::HorizontalSweep) {
                    zone.center1 = { 0.0f, 0.0f, m_isPositiveSide ? 7.5f : -7.5f };
                }
                else if (m_mode == RainMode::Targeted) {
                    if (m_target) zone.center1 = { m_target->GetPosition().x, 0.0f, 0.0f };
                    else zone.center1 = { 0.0f, 0.0f, 0.0f };
                }
            }
        }
        else if (zone.state == 1) { // Warning
            zone.stateTimer += dt;
            if (zone.stateTimer >= m_params.warningDuration) {
                zone.state = 2;
                zone.stateTimer = 0.0f;
                CameraController::Instance().AddTrauma(0.5f);
                AudioManager::Instance().PlaySFX("Data/Sound/SE_Boss_Rain_01.wav", 0.07f * m_params.sfxVolume);
                m_sfxTimer = 999.0f; // Paksa masuk ke trigger loop SFX
            }
        }
        else if (zone.state == 2) { // Active
            anyActiveOrDissipating = true;
            zone.stateTimer += dt;

            // Cek Collision
            if (m_mode == RainMode::DualPillar) {
                if (isInsideZone(zone.center1) || isInsideZone(zone.center2)) isHit = true;
            }
            else {
                if (isInsideZone(zone.center1)) isHit = true;
            }

            if (zone.stateTimer >= m_params.activeDuration) {
                zone.state = 3;
                zone.stateTimer = 0.0f;
            }
        }
        else if (zone.state == 3) { // Dissipating
            anyActiveOrDissipating = true;
            zone.stateTimer += dt;

            // Cek Collision
            if (m_mode == RainMode::DualPillar) {
                if (isInsideZone(zone.center1) || isInsideZone(zone.center2)) isHit = true;
            }
            else {
                if (isInsideZone(zone.center1)) isHit = true;
            }

            if (zone.stateTimer >= 1.0f) {
                zone.state = 4;
            }
        }
    }

    // Jika ada hujan yang sedang aktif/menghilang
    if (anyActiveOrDissipating) {
        CameraController::Instance().AddTrauma(1.0f * dt);

        if (m_sfxTimer >= 0.5f) {
            std::string rainSounds[] = {
                "Data/Sound/SE_Boss_Rain_01.wav",
                "Data/Sound/SE_Boss_Rain_02.wav",
                "Data/Sound/SE_Boss_Rain_03.wav"
            };
            for (int i = 0; i < 3; ++i) {
                float delay = 0.5f + ((rand() % 301) / 1000.0f);
                AudioManager::Instance().PlaySFXDelayed(
                    rainSounds[rand() % 3], 0.07f * m_params.sfxVolume, delay);
            }
            m_sfxTimer = 0.0f;
        }

        // Terapkan damage HANYA 1 KALI per update meskipun banyak area hujan menumpuk
        if (isHit && m_target && m_target->GetHP() > 0) {
            m_target->TakeDamage(m_params.damage);
            CameraController::Instance().AddTrauma(0.15f);
        }
    }

    if (allFinished) {
        ClearVFX();
        m_active = false;
    }
}

void AttackRain::Render(ID3D11DeviceContext* context, Camera* camera, Boss* boss) {
    if (!m_active || !boss || !boss->GetWindowSystem()) return;

    auto shapeRenderer = Graphics::Instance().GetShapeRenderer();
    float actualW = GetActualWidth();
    float actualD = GetActualDepth();
    float halfW = actualW * 0.5f;
    float halfD = actualD * 0.5f;

    float p2u = boss->GetWindowSystem()->GetPixelToUnitRatio();
    int   screenW = GetSystemMetrics(SM_CXSCREEN);
    int   screenH = GetSystemMetrics(SM_CYSCREEN);
    XMFLOAT3 camPos = camera->GetPosition();

    float width2D = actualW * p2u;
    float height2D = actualD * p2u;

    auto drawRect = [&](XMFLOAT3 center, float customAlpha) {
        float sx = (center.x - camPos.x) * p2u + (screenW * 0.5f);
        float sy = -(center.z - camPos.z) * p2u + (screenH * 0.5f);
        m_zonePrimitive->Rect(sx, sy, width2D, height2D,
            width2D * 0.5f, height2D * 0.5f,
            0.0f, 1.0f, 0.0f, 0.0f, customAlpha);
        };

    int zoneIndex = 0;
    int maxDrops = (m_mode == RainMode::DualPillar) ? 800 : 400;

    for (const auto& zone : m_zones) {
        if (zone.state == 1 || zone.state == 2 || zone.state == 3) {

            // 1. GAMBAR KOTAK MERAH 2D (Render Primitive Original)
            float alpha = 0.0f;
            if (zone.state == 1) {
                float blink = (sinf(zone.stateTimer * 20.0f) + 1.0f) * 0.5f;
                alpha = 0.2f + (blink * 0.4f);
            }
            else if (zone.state == 2) {
                alpha = 0.6f;
            }
            else if (zone.state == 3) {
                alpha = 0.6f * (1.0f - (zone.stateTimer / 1.0f));
            }

            drawRect(zone.center1, alpha);
            if (m_mode == RainMode::DualPillar) drawRect(zone.center2, alpha);
            m_zonePrimitive->Render(context);

            // 2. GAMBAR HUJAN VFX 3D
            if (zone.state == 2 || zone.state == 3) {
                std::mt19937 gen(1337 + zoneIndex); // Seed berbeda tiap zona agar tidak numpuk
                std::uniform_real_distribution<float> distSpeed(m_params.minSpeed, m_params.maxSpeed);
                std::uniform_real_distribution<float> distSpawn(0.0f, m_params.activeDuration);

                float globalTime = (zone.state == 2) ? zone.stateTimer : (m_params.activeDuration + zone.stateTimer);
                int handleOffset = zoneIndex * maxDrops;

                for (int i = 0; i < maxDrops; ++i) {
                    float speed = distSpeed(gen);
                    float spawnTime = distSpawn(gen);
                    float localTime = globalTime - spawnTime;
                    if (localTime < 0.0f) continue;

                    XMFLOAT3 dropPos = {};
                    bool     isActive = false;
                    float    yawAngle = 0.0f;

                    if (m_mode == RainMode::DualPillar) {
                        XMFLOAT3 activeCenter = (i % 2 == 0) ? zone.center1 : zone.center2;
                        std::uniform_real_distribution<float> distX(activeCenter.x - halfW, activeCenter.x + halfW);
                        float rx = distX(gen);
                        float topEdge = activeCenter.z + halfD + 5.0f;
                        float bottomEdge = activeCenter.z - halfD;
                        float z = topEdge - (localTime * speed);
                        if (z >= bottomEdge) {
                            dropPos = { rx, 1.0f, z };
                            isActive = true;
                            yawAngle = XM_PI;
                        }
                    }
                    else if (m_mode == RainMode::VerticalSweep || m_mode == RainMode::Targeted) {
                        std::uniform_real_distribution<float> distX(zone.center1.x - halfW, zone.center1.x + halfW);
                        float rx = distX(gen);
                        float topEdge = zone.center1.z + halfD + 5.0f;
                        float bottomEdge = zone.center1.z - halfD;
                        float z = topEdge - (localTime * speed);
                        if (z >= bottomEdge) {
                            dropPos = { rx, 1.0f, z };
                            isActive = true;
                            yawAngle = XM_PI;
                        }
                    }
                    else { // HorizontalSweep
                        std::uniform_real_distribution<float> distZ(zone.center1.z - halfD, zone.center1.z + halfD);
                        float rz = distZ(gen);
                        float dir = m_sweepDir;
                        float startX = (dir > 0) ? (zone.center1.x - halfW - 5.0f) : (zone.center1.x + halfW + 5.0f);
                        float endX = (dir > 0) ? (zone.center1.x + halfW) : (zone.center1.x - halfW);
                        float x = startX + (localTime * speed * dir);
                        bool inside = (dir > 0) ? (x <= endX) : (x >= endX);
                        if (inside) {
                            dropPos = { x, 1.0f, rz };
                            isActive = true;
                            yawAngle = (dir > 0) ? XM_PIDIV2 : -XM_PIDIV2;
                        }
                    }

                    int hIdx = handleOffset + i;
                    if (isActive) {
                        shapeRenderer->DrawSphere(dropPos, 0.4f, { 1.0f, 0.4f, 0.0f, 1.0f });

                        if (m_vfxHandles[hIdx] == -1) {
                            m_vfxHandles[hIdx] = EffectManager::Instance().Play(
                                "Data/Effect/VFX_Boss_Asgore_Rain.efk", dropPos, 0.4f);
                            float rotX = XMConvertToRadians(90.0f);
                            EffectManager::Instance().SetRotation(
                                m_vfxHandles[hIdx], { rotX, yawAngle, 0.0f });
                        }
                        else {
                            EffectManager::Instance().SetPosition(m_vfxHandles[hIdx], dropPos);
                        }
                    }
                    else {
                        if (m_vfxHandles[hIdx] != -1) {
                            EffectManager::Instance().Stop(m_vfxHandles[hIdx]);
                            m_vfxHandles[hIdx] = -1;
                        }
                    }
                }
            }
        }
        zoneIndex++;
    }
}

void AttackRain::Stop(Boss* boss) {
    ClearVFX();
    m_active = false;
}

bool AttackRain::IsFinished() const {
    return !m_active;
}

float AttackRain::GetActualWidth() const {
    if (m_mode == RainMode::DualPillar)    return 18.0f;
    if (m_mode == RainMode::VerticalSweep) return 25.0f;
    if (m_mode == RainMode::Targeted)      return m_params.width;
    return 80.0f;
}

float AttackRain::GetActualDepth() const {
    if (m_mode == RainMode::VerticalSweep || m_mode == RainMode::DualPillar) return 45.0f;
    if (m_mode == RainMode::Targeted) return m_params.depth;
    return 15.0f;
}

void AttackRain::ClearVFX() {
    for (int& h : m_vfxHandles) {
        if (h != -1) { EffectManager::Instance().Stop(h); h = -1; }
    }
}