#include "AttackBlasters.h"
#include "Boss.h"
#include "WindowTrackingSystem.h"
#include "System/Graphics.h"
#include "System/AudioManager.h"
#include "CameraController.h"
#include "EffectManager.h"
#include <SDL3/SDL.h>

using namespace DirectX;

AttackBlasters::AttackBlasters(const BlasterParams& params, bool isTargeted, float targetX)
    : m_params(params), m_isTargeted(isTargeted), m_targetX(targetX) {}

void AttackBlasters::Start(Boss* boss) {
    auto device = Graphics::Instance().GetDevice();
    m_solidRenderer = std::make_unique<Primitive>(device);
    m_placeholderModel = std::make_shared<Model>(device, "Data/Model/Character/PLACEHOLDER_mdl_Ball.glb");

    m_isSpawning = true;
    m_spawnedCount = 0;
    m_spawnTimer = m_params.spawnDelay;
}

void AttackBlasters::Update(float dt, Boss* boss) {
    if (!boss || !boss->GetWindowSystem()) return;

    // 1. Spawning Sequence
    if (m_isSpawning) {
        m_spawnTimer += dt;
        while (m_isSpawning && m_spawnTimer >= m_params.spawnDelay) {
            m_spawnTimer = (m_params.spawnDelay > 0.0f) ? m_spawnTimer - m_params.spawnDelay : 1.0f;

            auto blaster = std::make_shared<OrbitalBlaster>();
            float startX = m_isTargeted ? m_targetX : -m_params.spawnSpreadX * 0.5f;
            float stepX = (m_params.spawnCount > 1 && !m_isTargeted) ? (m_params.spawnSpreadX / (m_params.spawnCount - 1)) : 0.0f;

            blaster->baseX = m_isTargeted ? m_targetX : startX + (stepX * m_spawnedCount);
            blaster->pos = { blaster->baseX, 1.0f, 25.0f }; // Starts high
            blaster->targetPos = { blaster->baseX, 1.0f, 10.0f }; // Drops down

            int uniqueID = rand() % 100000;
            blaster->beamWindowName = "blaster_beam_" + std::to_string(uniqueID);
            blaster->windowName = "blaster_cannon_" + std::to_string(uniqueID);

            // Beam Window Config
            TrackedWindowConfig beamCfg = { blaster->beamWindowName, "!!! BEAM REACHING !!!", 1, 1, 1 };
            beamCfg.isTransparent = false;
            boss->GetWindowSystem()->AddTrackedWindow(beamCfg,
                [ptr = blaster.get()]() { return (ptr->state < 2) ? DirectX::XMFLOAT3(ptr->pos.x, ptr->pos.y, -10000.0f) : ptr->pos; },
                [ptr = blaster.get(), boss]() {
                    if (ptr->state < 2) return DirectX::XMFLOAT2(1.0f, 1.0f);
                    float p2u = boss->GetWindowSystem()->GetPixelToUnitRatio();
                    float w = max(50.0f, (ptr->beamScaleX + 1.5f) * p2u);
                    float h = max(1.0f, ptr->beamCurrentLength * p2u);
                    return DirectX::XMFLOAT2(w, h);
                }
            );

            // Cannon Window Config
            TrackedWindowConfig cfg = { blaster->windowName, "DANGER: CANNON", (int)m_params.cannonWindowSize, (int)m_params.cannonWindowSize, 10 };
            cfg.isTransparent = false;
            boss->GetWindowSystem()->AddTrackedWindow(cfg,
                [ptr = blaster.get()]() { return ptr->pos; },
                [this]() { return DirectX::XMFLOAT2(m_params.cannonWindowSize, m_params.cannonWindowSize); }
            );

            m_blasters.push_back(blaster);
            m_spawnedCount++;
            if (m_spawnedCount >= m_params.spawnCount) m_isSpawning = false;
        }
    }

    // 2. Logic Update per Blaster
    for (auto it = m_blasters.begin(); it != m_blasters.end(); ) {
        auto& b = *it;
        if (!b->active) {
            it = m_blasters.erase(it);
            continue;
        }

        b->timer += dt;
        DirectX::XMFLOAT3 vfxPos = {
            b->pos.x + m_params.effectOffset.x,
            b->pos.y + m_params.effectOffset.y,
            b->pos.z + m_params.effectOffset.z
        };

        if (b->state == 1) { // DROP IN
            b->pos.z += (b->targetPos.z - b->pos.z) * 12.0f * dt;
            if (b->timer >= m_params.dropInDuration) {
                b->state = 2; b->timer = 0.0f;
                AudioManager::Instance().PlaySFX("Data/Sound/SE_Boss_Laser_Charge.wav", 0.1f);
                b->chargeEffectHandle = EffectManager::Instance().Play(m_params.chargeEffectPath, vfxPos, m_params.chargeEffectScale);
                EffectManager::Instance().SetRotation(b->chargeEffectHandle, { XMConvertToRadians(m_params.effectPitchDegrees), 0, 0 });
            }
        }
        else if (b->state == 2) { // CHARGE
            b->beamScaleX = 0.2f; // Thin warning line
            b->beamCurrentLength += (m_params.beamMaxLength - b->beamCurrentLength) * m_params.beamSlideSpeed * dt;
            EffectManager::Instance().SetPosition(b->chargeEffectHandle, vfxPos);

            if (b->timer >= m_params.chargeDelay) {
                b->state = 3; b->timer = 0.0f;
                CameraController::Instance().AddTrauma(0.6f);
                AudioManager::Instance().PlaySFX("Data/Sound/SE_Boss_Laser_Shoot.wav", 0.2f);
                EffectManager::Instance().Stop(b->chargeEffectHandle);

                b->fireEffectHandle = EffectManager::Instance().Play(m_params.fireEffectPath, vfxPos, m_params.fireEffectScale);
                EffectManager::Instance().SetRotation(b->fireEffectHandle, { XMConvertToRadians(m_params.effectPitchDegrees), 0, 0 });
            }
        }
        else if (b->state == 3) { // FIRE
            b->pos.x = b->baseX; // Lock X against shake drift
            b->beamScaleX += (m_params.beamVisualWidth - b->beamScaleX) * m_params.beamGrowSpeed * dt;
            CameraController::Instance().AddTrauma(0.1f);
            EffectManager::Instance().SetPosition(b->fireEffectHandle, vfxPos);

            if (b->timer >= m_params.fireDuration) {
                b->state = 4; b->timer = 0.0f;
                EffectManager::Instance().Stop(b->fireEffectHandle);
            }
        }
        else if (b->state == 4) { // RETREAT
            b->beamScaleX -= dt * m_params.windowFadeSpeed;
            if (b->beamScaleX <= 0.1f && boss->GetWindowSystem()->GetTrackedWindow(b->beamWindowName)) {
                b->pos.z = -10000.0f;
                boss->GetWindowSystem()->RemoveTrackedWindow(b->beamWindowName);
            }

            b->pos.z += m_params.retreatSpeed * dt;
            if (b->timer >= 0.3f) {
                b->active = false;
                boss->GetWindowSystem()->RemoveTrackedWindow(b->windowName);
            }
        }
        ++it;
    }
}

void AttackBlasters::Render(ID3D11DeviceContext* context, Camera* camera, Boss* boss) {
    if (!m_placeholderModel || !m_solidRenderer) return;
    auto modelRenderer = Graphics::Instance().GetModelRenderer();
    float p2u = boss->GetWindowSystem()->GetPixelToUnitRatio();

    for (auto& b : m_blasters) {
        if (!b->active) continue;

        // Render Cannon Head
        if (!m_placeholderModel->GetNodes().empty()) {
            auto& rootNode = m_placeholderModel->GetNodes().at(0);
            DirectX::XMFLOAT4X4 identity;
            XMStoreFloat4x4(&identity, XMMatrixIdentity());
            rootNode.position = b->pos;
            rootNode.scale = { m_params.cannonVisualScale, m_params.cannonVisualScale, m_params.cannonVisualScale };
            m_placeholderModel->UpdateTransform(identity);
            modelRenderer->Draw(ShaderId::Phong, m_placeholderModel, { 0.2f, 0.5f, 0.5f, 1.0f });
        }

        // Render Beam Rect (Solid 2D)
        if (b->beamScaleX > 0.0f) {
            float startX, startY, endY, dummyX;
            boss->GetWindowSystem()->WorldToScreenPos(b->pos, startX, startY);

            DirectX::XMFLOAT3 endPos = b->pos;
            endPos.z -= b->beamCurrentLength;
            boss->GetWindowSystem()->WorldToScreenPos(endPos, dummyX, endY);

            DirectX::XMFLOAT3 camPos = camera->GetPosition();
            startX -= (camPos.x * p2u);
            startY += (camPos.z * p2u);
            endY += (camPos.z * p2u);

            // Correct projection for shaking windows
            for (auto& tw : boss->GetWindowSystem()->GetWindows()) {
                if (tw->camera.get() == camera) {
                    startX -= (float)tw->state.actualX;
                    startY -= (float)tw->state.actualY;
                    endY -= (float)tw->state.actualY;
                    break;
                }
            }

            float pixelWidth = b->beamScaleX * p2u;
            float pixelHeight = endY - startY;
            DirectX::XMFLOAT4 color = (b->state == 2) ? DirectX::XMFLOAT4{ 1,0,0,0.5f } : DirectX::XMFLOAT4{ 0,1,1,0.9f };

            m_solidRenderer->Rect(startX, startY, pixelWidth, pixelHeight, pixelWidth * 0.5f, 0.0f, 0.0f, color.x, color.y, color.z, color.w);
            m_solidRenderer->Render(context);
        }
    }
}

void AttackBlasters::Stop(Boss* boss) {
    for (auto& b : m_blasters) {
        EffectManager::Instance().Stop(b->chargeEffectHandle);
        EffectManager::Instance().Stop(b->fireEffectHandle);
        if (boss && boss->GetWindowSystem()) {
            boss->GetWindowSystem()->RemoveTrackedWindow(b->beamWindowName);
            boss->GetWindowSystem()->RemoveTrackedWindow(b->windowName);
        }
    }
    m_blasters.clear();
    m_isSpawning = false;
}

bool AttackBlasters::IsFinished() const {
    return !m_isSpawning && m_blasters.empty();
}

std::vector<Bullet*> AttackBlasters::GetActiveProjectiles() const {
    // Note: The beam logic currently uses AABB overlap rather than Bullet instances in your original code. 
    // If you plan to spawn actual bullets for the beam hitboxes later, return them here.
    return {};
}