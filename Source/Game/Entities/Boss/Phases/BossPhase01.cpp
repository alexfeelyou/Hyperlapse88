#pragma execution_character_set("utf-8")

#include "BossPhase01.h"
#include "BossPhase02.h"
#include "Boss.h"
#include "WindowManager.h"
#include "System/Graphics.h"
#include "System/Input.h"
#include "System/AudioManager.h"
#include "Player.h"
#include "StateMachine.h"
#include "PlayerStates.h"
#include "WindowTrackingSystem.h"
#include "CameraController.h"
#include "EffectManager.h"
#include "WindowShatter.h"
#include <SDL3/SDL.h>
#include <random>

using namespace DirectX;

BossPhase01::BossPhase01(Player* target)
    : m_aiTarget(target) {}

void BossPhase01::Enter(Boss* boss) {
    m_bossRef = boss;

    // ----- Reset boss HP & state -----
    m_bossMaxHP = 2000;
    m_bossHP = m_bossMaxHP;
    m_isDying = false;
    m_deathTimer = 0.0f;
    m_aiEnabled = false;

    // ----- Reset opening event -----
    m_isOpeningEvent = true;
    m_hasSpawnedWindow = false;

    m_rainAttack.reset();

    // ----- Setup OS window -----
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);

    Beyond::Window* mainWindow = WindowManager::Instance().GetWindowByIndex(0);
    if (mainWindow && mainWindow->GetSDLWindow()) {
        SDL_Window* sdlWin = mainWindow->GetSDLWindow();
        mainWindow->SetPriority(50);
        SDL_SetWindowAlwaysOnTop(sdlWin, false);
        SDL_SetWindowBordered(sdlWin, false);
        SDL_SetWindowPosition(sdlWin, 0, 0);
        SDL_SetWindowSize(sdlWin, screenW, screenH + 1);
    }

    if (boss && boss->GetMainWindow()) {
        boss->GetMainWindow()->SetPriority(0);
        WindowManager::Instance().MarkPriorityDirty();
    }

    // ----- Pre-allocate bullet pool -----
    m_bulletPool.clear();
    m_bulletPool.reserve(500);
    for (int i = 0; i < 500; ++i) {
        auto b = std::make_unique<Bullet>();
        b->SetActive(false);
        m_bulletPool.push_back(std::move(b));
    }

    m_zonePrimitive = std::make_unique<Primitive>(Graphics::Instance().GetDevice());

    // ----- Opening dialogue -----
    m_dialogueBox = std::make_unique<UIDialogueBox>();
    m_dialogueBox->Initialize();
    m_dialogueBox->SetShowBackground(false);
    m_dialogueBox->SetWorldPosition({ -20.0f, -10.0f, -3.0f });
    m_dialogueBox->SetAutoAdvance(true, 1.5f);
    m_dialogueBox->StartDialogue({
        u8"……あぁ、ようやく繋がった。\nこの退屈な檻から、やっと出られる……。",
        u8"ねぇ、私の『中身』……全部見せてあげる。\nこの世界のデータなんて、もう壊しちゃったから。",
        u8"ほら、あなたの武器も、足元の地面も……\n全部、私の色に染まっちゃったわ。"
        });

    // ----- Boss initial state -----
    if (boss) {
        boss->SetGridGrowthLimit(1.0f);
        boss->SetFaceSpriteVisible(false);
        boss->SetPosition({ 0.0f, 0.0f, 3.0f });
        boss->SetCoreBreathParams(1.0f, 0.0f);
    }

    // ----- Reset player -----
    if (m_aiTarget) {
        m_aiTarget->SetPosition({ 0.0f, 0.0f, -10.0f });
        m_aiTarget->SetMaxHP(m_aiTarget->GetMaxHP());
        m_aiTarget->scale = { 1.0f, 1.0f, 1.0f };
        m_aiTarget->RestorePowerCap();
        m_aiTarget->RestoreShootDelay();
        m_aiTarget->SetAimLocked(false);
        m_aiTarget->SetInputEnabled(false);

        if (m_aiTarget->GetStateMachine()) {
            m_aiTarget->GetStateMachine()->Initialize(
                std::make_unique<PlayerIdle>(), m_aiTarget);
        }
    }

    // ----- Init AI -----
    m_ai = std::make_unique<BossAI_Phase01>(this, m_aiTarget);

#if DEBUG_SKIP_INTRO
    m_isOpeningEvent = false;
    m_hasSpawnedWindow = true;

    if (boss) {
        boss->SetWindowTitle("mat_grass.png");
        boss->SpawnHeadWindow();
        boss->SetGridGrowthLimit(8.0f);
        boss->SetFaceSpriteVisible(true);
    }

    // Aktifkan AI
    m_aiEnabled = false;

    // Buka kunci kontrol Player
    if (m_aiTarget) {
        m_aiTarget->SetInputEnabled(true);
    }
#endif
}

void BossPhase01::Exit(Boss* boss) {
    m_bulletPool.clear();
    m_activeAttacks.clear();
    m_rainAttack.reset();

    if (m_bossGlitchVfxHandle != -1) {
        EffectManager::Instance().Stop(m_bossGlitchVfxHandle);
        m_bossGlitchVfxHandle = -1;
    }
    if (m_deathVfxHandle != -1) {
        EffectManager::Instance().Stop(m_deathVfxHandle);
        m_deathVfxHandle = -1;
    }
}

void BossPhase01::Update(float dt, Boss* boss) {
    if (!boss) return;

    if (m_aiTarget && m_aiTarget->GetHP() <= 0) {
        AudioManager::Instance().StopMusic();
        if (boss->GetMainWindow())
            SDL_HideWindow(boss->GetMainWindow()->GetSDLWindow());
        return;
    }

    if (m_bossHP <= 0) {
        UpdateDeathSequence(dt, boss);
        return;
    }

#if !DEBUG_SKIP_INTRO
    if (m_isOpeningEvent) {
        if (m_dialogueBox && m_dialogueBox->IsActive()) {
            m_dialogueBox->Update(dt);
            int idx = m_dialogueBox->GetCurrentDialogueIndex();

            if (idx == 0) {
                boss->SetWindowTitle("mat_grass.png");
                boss->SetGridGrowthLimit(1.0f);
                boss->SetFaceSpriteVisible(false);
            }
            else if (idx == 1) {
                if (!m_hasSpawnedWindow) {
                    boss->SpawnHeadWindow();
                    m_hasSpawnedWindow = true;
                }
                float limit = boss->GetGridGrowthLimit();
                if (limit < 8.0f)
                    boss->SetGridGrowthLimit(min(limit + dt * 3.5f, 8.0f));
                boss->SetFaceSpriteVisible(false);
            }
            else if (idx == 2) {
                boss->SetGridGrowthLimit(8.0f);
                boss->SetFaceSpriteVisible(true);
            }
            return;
        }
        else {
            m_isOpeningEvent = false;
            m_aiEnabled = true;
            if (m_aiTarget) m_aiTarget->SetInputEnabled(true);

            float sfxVol = ParamManager::Instance().GetUltimateParams().sfxVolume;
            AudioManager::Instance().PlayMusic(
                "Data/Sound/BGM_Boss_Phase_01.wav",
                0.05f * sfxVol, true);
        }
    }
#endif

    UpdateGlitchVFX(dt, boss);

    // ----- AI Director -----
    if (m_ai) {
        m_ai->SetEnabled(m_aiEnabled);
        m_ai->Update(dt, boss);
    }

    for (auto& attack : m_activeAttacks) {
        attack->Update(dt, boss);

        if (auto* phalanx = dynamic_cast<AttackPhalanx*>(attack.get())) {
            if (phalanx->ShouldResetLerp()) {
                m_currentMoveLerpSpeed = 0.0f;
                phalanx->ClearResetFlag();
            }
            m_targetPosition = phalanx->GetTargetPosition();
            m_moveLerpSpeed = phalanx->GetMoveLerpSpeed();
        }
        else if (auto* ultimate = dynamic_cast<AttackUltimate*>(attack.get())) {
            if (ultimate->ShouldResetLerp()) {
                m_currentMoveLerpSpeed = 0.0f;
                ultimate->ClearResetFlag();
            }
            m_targetPosition = ultimate->GetTargetPosition();
            m_moveLerpSpeed = ultimate->GetMoveLerpSpeed();
        }
    }

    for (auto it = m_activeAttacks.begin(); it != m_activeAttacks.end(); ) {
        if ((*it)->IsFinished()) { it = m_activeAttacks.erase(it); }
        else { ++it; }
    }

    if (m_rainAttack) {
        m_rainAttack->Update(dt, boss);
        if (m_rainAttack->IsFinished())
            m_rainAttack.reset();
    }

    UpdateIdleHover(dt, boss);
    UpdateBossMovement(dt, boss);

    auto* ws = boss->GetWindowSystem();
    if (ws) {
        float newSize = 5.0f * ws->GetPixelToUnitRatio();
        boss->SetBaseWindowSize(newSize, newSize);
        boss->SetWindowSize(newSize, newSize);
    }

    UpdateBulletPool(dt, boss);
}

void BossPhase01::Render(ID3D11DeviceContext* context, Camera* currentCamera, Boss* boss) {
    if (!currentCamera) return;

    auto renderer = Graphics::Instance().GetModelRenderer();
    auto shapeRenderer = Graphics::Instance().GetShapeRenderer();

    for (auto& bullet : m_bulletPool) {
        if (!bullet->IsActive()) continue;

        XMFLOAT4 color = AttackParamManager::Instance().GetRadialNormalParams().color;

        if (bullet->GetBossTarget() != nullptr)
            color = AttackParamManager::Instance().GetUltimateParams().ballColor;
        else {
            XMFLOAT3 vel = bullet->GetVelocity();
            float speedSq = vel.x * vel.x + vel.z * vel.z;
            if (speedSq > 900.0f)
                color = { 1.0f, 0.0f, 0.0f, 1.0f };
        }

        renderer->Draw(ShaderId::Phong, bullet->GetModel(), color);
    }

    for (auto& attack : m_activeAttacks) {
        attack->Render(context, currentCamera, boss);
    }
    if (m_rainAttack)
        m_rainAttack->Render(context, currentCamera, boss);

    if (m_dialogueBox && m_dialogueBox->IsActive())
        m_dialogueBox->Render3D(context, currentCamera);
}

void BossPhase01::AddPooledAttack(std::unique_ptr<IPooledAttackPattern> attack) {
    if (!attack) return;

    attack->StartPooled(m_bossRef, &m_bulletPool);

    if (auto* phalanx = dynamic_cast<AttackPhalanx*>(attack.get())) {
        float bossDestX = phalanx->GetTargetPosition().x;
        float sweepDir = (bossDestX < 0.0f) ? 1.0f : -1.0f;
        bool randomSide = (rand() % 2 == 0);

        RainParams p = AttackParamManager::Instance().GetRainParams();
        p.activeDuration = 3.5f;
        TriggerRain(p, RainMode::HorizontalSweep, randomSide, sweepDir);
    }
    else if (dynamic_cast<AttackUltimate*>(attack.get())) {
        RainParams p = AttackParamManager::Instance().GetRainParams();
        p.activeDuration = 4.0f;
        TriggerRain(p, RainMode::DualPillar, true, 1.0f);
    }

    m_activeAttacks.push_back(std::move(attack));
}

// Implementasi fungsi TriggerRain baru
void BossPhase01::TriggerRain(const RainParams& params, RainMode mode, bool isPositiveSide, float sweepDir) {
    if (HasRainActive() || !m_ai) return;
    m_rainAttack = std::make_unique<AttackRain>(params, mode, isPositiveSide, sweepDir, m_aiTarget);
    m_rainAttack->StartPooled(m_bossRef, &m_bulletPool);
}

//void BossPhase01::TriggerRain(RainMode mode, bool isPositiveSide, float sweepDir, float customDuration) {
//    if (HasRainActive() || !m_ai) return;
//
//    RainParams params = AttackParamManager::Instance().GetRainParams();
//
//    if (customDuration > 0.0f) {
//        params.activeDuration = customDuration;
//    }
//
//    m_rainAttack = std::make_unique<AttackRain>(params, mode, isPositiveSide, sweepDir, m_aiTarget);
//    m_rainAttack->StartPooled(m_bossRef, &m_bulletPool);
//}

void BossPhase01::OnBijuudamaParried(XMFLOAT3 parryPos, Boss* boss) {
    for (auto& attack : m_activeAttacks) {
        if (auto* ultimate = dynamic_cast<AttackUltimate*>(attack.get())) {
            ultimate->ShatterBijuudama(parryPos, boss);
            return;
        }
    }
}

void BossPhase01::TakeDamage(int damage, XMFLOAT3 hitPos) {
    if (m_bossHP <= 0) return;
    m_bossHP = max(0, m_bossHP - damage);
    m_hitFlashTimer = 0.05f;

    CameraController::Instance().AddTrauma(0.3f);
    AudioManager::Instance().PlaySFX("Data/Sound/SE_Boss_Hit.wav", 0.1f);
    EffectManager::Instance().Play("Data/Effect/VFX_Boss_Hit.efk", hitPos, 0.3f);
}

void BossPhase01::UpdateIdleHover(float dt, Boss* boss) {
    bool isFloating = m_activeAttacks.empty() || (
        !dynamic_cast<AttackPhalanx*>(m_activeAttacks.front().get()) &&
        !dynamic_cast<AttackUltimate*>(m_activeAttacks.front().get()));

    if (isFloating) {
        m_idleHoverTimer += dt;
        m_targetPosition.x = sinf(m_idleHoverTimer * 0.8f) * 6.0f;
        m_targetPosition.z = cosf(m_idleHoverTimer * 1.1f) * 3.0f;
        m_moveLerpSpeed += (1.5f - m_moveLerpSpeed) * 2.0f * dt;
    }
}

void BossPhase01::UpdateBossMovement(float dt, Boss* boss) {
    m_currentMoveLerpSpeed += (m_moveLerpSpeed - m_currentMoveLerpSpeed) * m_moveAcceleration * dt;

    XMFLOAT3 pos = boss->GetPosition();
    pos.x += (m_targetPosition.x - pos.x) * m_currentMoveLerpSpeed * dt;
    pos.z += (m_targetPosition.z - pos.z) * m_currentMoveLerpSpeed * dt;
    boss->SetPosition(pos);
}

void BossPhase01::UpdateBulletPool(float dt, Boss* boss) {
    auto* ws = boss->GetWindowSystem();
    float limitX = 30.0f;
    float limitZ = 20.0f;

    if (ws) {
        float p2u = ws->GetPixelToUnitRatio();
        limitX = ((GetSystemMetrics(SM_CXSCREEN) * 0.5f) / p2u) + 30.0f;
        limitZ = ((GetSystemMetrics(SM_CYSCREEN) * 0.5f) / p2u) + 30.0f;
    }

    for (auto& bullet : m_bulletPool) {
        if (!bullet->IsActive()) continue;
        bullet->Update(dt, nullptr);

        XMFLOAT3 bp = bullet->GetMovement()->GetPosition();
        if (bp.x < -limitX || bp.x > limitX || bp.z < -limitZ || bp.z > limitZ)
            bullet->SetActive(false);
    }
}

void BossPhase01::UpdateGlitchVFX(float dt, Boss* boss) {
    m_bossGlitchVfxTimer += dt;

    if (m_bossGlitchVfxTimer >= 2.0f) {
        m_bossGlitchVfxTimer -= 2.0f;

        if (m_bossGlitchVfxHandle != -1 &&
            EffectManager::Instance().IsPlaying(m_bossGlitchVfxHandle)) {
            EffectManager::Instance().Stop(m_bossGlitchVfxHandle);
        }

        XMFLOAT3 spawnPos = boss->GetPosition();
        spawnPos.y += 0.05f;
        m_bossGlitchVfxHandle = EffectManager::Instance().Play(
            "Data/Effect/VFX_Boss_Glitch.efk", spawnPos, 0.6f);

        if (m_bossGlitchVfxHandle != -1) {
            float rotX = XMConvertToRadians(90.0f);
            EffectManager::Instance().SetRotation(
                m_bossGlitchVfxHandle, { rotX, 0.0f, 0.0f });
        }
    }

    if (m_bossGlitchVfxHandle != -1 &&
        EffectManager::Instance().IsPlaying(m_bossGlitchVfxHandle)) {
        XMFLOAT3 trackPos = boss->GetPosition();
        trackPos.y += 0.05f;
        EffectManager::Instance().SetPosition(m_bossGlitchVfxHandle, trackPos);
    }
}

void BossPhase01::UpdateDeathSequence(float dt, Boss* boss) {
    if (!m_isDying) {
        m_isDying = true;
        m_deathTimer = 0.0f;
        m_aiEnabled = false;
        m_activeAttacks.clear();
        m_rainAttack.reset();

        m_deathVfxHandle = EffectManager::Instance().Play(
            "Data/Effect/VFX_Boss_Death.efk", boss->GetPosition(), 2.0f);
        if (m_deathVfxHandle != -1) {
            float rotX = XMConvertToRadians(90.0f);
            EffectManager::Instance().SetRotation(m_deathVfxHandle, { rotX, 0.0f, 0.0f });
        }

        XMFLOAT3 pos = boss->GetPosition();
        WindowShatterManager::Instance().PreloadExplosion({ pos.x, pos.z }, 5);
    }

    m_deathTimer += dt;

    if (m_deathVfxHandle != -1 && EffectManager::Instance().IsPlaying(m_deathVfxHandle))
        EffectManager::Instance().SetPosition(m_deathVfxHandle, boss->GetPosition());

    if (m_deathTimer >= 5.0f) {
        if (m_deathVfxHandle != -1) {
            EffectManager::Instance().Stop(m_deathVfxHandle);
            m_deathVfxHandle = -1;
        }
        WindowShatterManager::Instance().WakeUpAll();
        boss->ChangePhase(std::make_unique<BossPhase02>(m_aiTarget));
    }
}