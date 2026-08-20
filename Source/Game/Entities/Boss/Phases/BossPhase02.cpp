#include "BossPhase02.h"
#include "Boss.h"
#include "WindowTrackingSystem.h"
#include "System/Graphics.h"
#include "System/Sprite.h"
#include <algorithm>
#include <random>
#include <SceneBoss.h>
#include "Framework.h"
#include "WindowManager.h" 
#include <SDL3/SDL.h>
#include <System/AudioManager.h>
#include "EffectManager.h"
#include "CameraController.h"

using namespace DirectX;

BossPhase02::BossPhase02(Player* player) {
    m_aiTarget = player;
}
// =========================================================
// [MAGIC] MEMORY MANAGEMENT & SPAWNING
// =========================================================
void BossPhase02::Enter(Boss* boss) {
    if (!boss || !boss->GetWindowSystem()) return;

    EffectManager::Instance().StopAll();

    Beyond::Window* mainWindow = WindowManager::Instance().GetWindowByIndex(0);
    if (mainWindow && mainWindow->GetSDLWindow()) {
        SDL_Window* sdlWin = mainWindow->GetSDLWindow();

        // Sembunyikan window utama agar fokus ke window mekanik Windowkill
        SDL_HideWindow(sdlWin);
    }

    auto device = Graphics::Instance().GetDevice();
    auto windowSystem = boss->GetWindowSystem();
    m_pixelToUnit = windowSystem->GetPixelToUnitRatio();

    m_screenW = (float)GetSystemMetrics(SM_CXSCREEN);
    m_screenH = (float)GetSystemMetrics(SM_CYSCREEN);

    // Ciptakan jendela FX SECARA DINAMIS hanya ketika fase ini dimulai!
    TrackedWindowConfig fxCfg = { "navi_fx", "N.A.V.I - FX", (int)m_screenW, (int)m_screenH, 3 };
    fxCfg.role = WindowRole::SUB_VIEWPORT;
    fxCfg.isTransparent = true;

    windowSystem->AddTrackedWindow(fxCfg,
        []() { return DirectX::XMFLOAT3(0, 0, 0); },
        [this]() { return DirectX::XMFLOAT2(m_screenW, m_screenH); }
    );

    auto* fxWin = windowSystem->GetTrackedWindow("navi_fx");
    if (fxWin) {
        m_fxWindow = fxWin->window;
        m_fxCamera = fxWin->camera;
        m_fxWindow->SetBackgroundAlpha(0.0f);
        m_fxWindow->SetClickThrough(true);
        m_fxWindow->SetBorderVisible(false);
        m_fxWindow->SetDraggable(false);
    }

    // Load tekstur sayap ke VRAM hanya di fase ini
    m_wingSprite = std::make_unique<Sprite>(device, "Data/Sprite/Placeholder/[PLACEHOLDER]ErrorAtlas.png");
    m_overdriveSprite = std::make_unique<Sprite>(device, "Data/Sprite/txt_nagaoshi.png");

    // Reset timer dan ciptakan array
    m_glitchTimer = 0.0f;
    m_wingState = WingState::Expanding;
    m_wingStateTimer = 0.0f;
    GenerateButterflyWings();

    boss->SetPosition({ 0.0f, 0.0f, 7.0f });
    boss->GetFaceParams().gridResolution = 16;
    boss->InitializeFaceGrid(Graphics::Instance().GetDevice());

    EffectManager::Instance().PreloadEffect(m_blasterParams.chargeEffectPath);
    EffectManager::Instance().PreloadEffect(m_blasterParams.fireEffectPath);

    m_bossRef = boss; // Simpan referensi boss untuk dipakai saat jendela hancur
    m_bossMaxHP = 4000;
    m_bossHP = m_bossMaxHP;
    m_hitFlashTimer = 0.0f;
    m_aiEnabled = false;

    // =========================================================
    // [BARU] INISIALISASI FSM (OTAK AI)
    // =========================================================
    m_ai = std::make_unique<BossAI_Phase02>(this, m_aiTarget);

    if (windowSystem->GetTrackedWindow("player")) {
        windowSystem->RemoveTrackedWindow("player");
    }

    // =========================================================
    // [BARU] INISIALISASI KANDANG PEMAIN
    // =========================================================
    if (m_aiTarget) {
        m_isPlayerCaged = true;
        m_aiTarget->SetPosition(0, 0, -8.0f);
        m_cageMaxHP = 600; // Sesuaikan dengan total HP yang kamu inginkan
        m_cageHP = m_cageMaxHP;
        m_cagePos = m_aiTarget->GetPosition(); // Kunci posisi kandang di lokasi player saat ini
        m_cageWindowPos = m_cagePos;
        m_cageShakeTimer = 0.0f;
        m_aiTarget->SetShootDelay(0.0f);

        // Hitung ukuran asli di dunia 3D (300 pixel / ratio)
        m_cageSizeWorld = 300.0f / windowSystem->GetPixelToUnitRatio();

        TrackedWindowConfig cageCfg;
        cageCfg.name = m_cageWindowName;
        cageCfg.title = "TRAPPED.exe";
        cageCfg.width = 300;
        cageCfg.height = 300;
        cageCfg.role = WindowRole::TRACKED_ENTITY;
        cageCfg.priority = 100; // Pastikan selalu di atas
        cageCfg.isTransparent = false;

        windowSystem->AddTrackedWindow(cageCfg,
            [this]() { return m_cageWindowPos; }, // Posisinya STATIS, hanya jitter saat kena tembak
            []() { return DirectX::XMFLOAT2(300.0f, 300.0f); }
        );

        if (auto* cageWindow = windowSystem->GetTrackedWindow(m_cageWindowName)) {
            if (cageWindow->window) {
                cageWindow->window->SetBackgroundAlpha(1.0f);
            }
        }
    }

    m_overdriveAlpha = 0.0f;

    // =========================================================
    // [BARU] OPENING DIALOGUE — TRACKING WINDOW
    // =========================================================
    {
        float p2u = windowSystem->GetPixelToUnitRatio();
        float halfW = (m_dialogueWindowW * 0.5f) / p2u;
        m_dialogueWorldPos = { -halfW, 0.0f, 1.5f };
    }

    {
        TrackedWindowConfig diagCfg;
        diagCfg.name = m_dialogueWindowName;
        diagCfg.title = "N.A.V.I";            // Judul window bar atas
        diagCfg.width = (int)m_dialogueWindowW;
        diagCfg.height = (int)m_dialogueWindowH;
        diagCfg.role = WindowRole::TRACKED_ENTITY;
        diagCfg.isTransparent = false;                // Solid — sama seperti kandang & bullet
        diagCfg.priority = 5;

        windowSystem->AddTrackedWindow(diagCfg,
            [this]() { return m_dialogueWorldPos; },
            [this]() { return DirectX::XMFLOAT2(m_dialogueWindowW, m_dialogueWindowH); }
        );

        auto* diagWin = windowSystem->GetTrackedWindow(m_dialogueWindowName);
        if (diagWin && diagWin->window) {
            m_dialogueWindow = diagWin->window;
            m_dialogueCamera = diagWin->camera;
            m_dialogueWindow->SetBackgroundAlpha(1.0f); // Background solid
            m_dialogueWindow->SetClickThrough(false);
            m_dialogueWindow->SetBorderVisible(true);   // Border OS tetap tampil
            m_dialogueWindow->SetDraggable(false);
        }
    }

    m_dialogueBox = std::make_unique<UIDialogueBox>();
    m_dialogueBox->Initialize();
    m_dialogueBox->SetShowBackground(false);  // No background — teks melayang di window transparan

    // Autoadvance ON, strict OFF → player bebas gerak, dialog jalan sendiri
    m_dialogueBox->SetAutoAdvance(false);
    m_dialogueBox->StartDialogue({
        u8"ウィンドウを撃て"
        });

    m_isDialogueActive = true;
    m_aiEnabled = false;
    if (m_aiTarget) {
        m_aiTarget->SetInputEnabled(true); // Player tetap bisa gerak dari awal
    }

    m_cageFirstHitTriggered = false;
    m_overdriveDialogueTriggered = false;

    // =========================================================
    // [BARU] INVISIBLE CLICK BLOCKER WINDOW
    // =========================================================
    // Window ini selebar layar untuk menangkap klik nyasar ke OS
    TrackedWindowConfig blockerCfg;
    blockerCfg.name = "click_blocker";
    blockerCfg.title = "Invisible_Blocker";
    blockerCfg.width = (int)m_screenW;
    blockerCfg.height = (int)m_screenH;
    blockerCfg.role = WindowRole::SUB_VIEWPORT;

    // Priority 99 menjamin window ini berada di z-order tertinggi 
    // (di atas dialog, cage, dan fx) karena WindowManager me-raise priority secara ascending (< 100).
    blockerCfg.priority = 99;
    blockerCfg.isTransparent = true;

    windowSystem->AddTrackedWindow(blockerCfg,
        []() { return DirectX::XMFLOAT3(0, 0, 0); },
        [this]() { return DirectX::XMFLOAT2(m_screenW, m_screenH); }
    );

    if (auto* blockerWin = windowSystem->GetTrackedWindow("click_blocker")) {
        if (blockerWin->window) {
            blockerWin->window->SetBackgroundAlpha(0.0f); // 100% tembus pandang
            blockerWin->window->SetClickThrough(false);   // Tangkap semua klik OS!
            blockerWin->window->SetBorderVisible(false);
            blockerWin->window->SetDraggable(false);
            blockerWin->window->SetRenderScene(false);
        }
    }
}

void BossPhase02::Exit(Boss* boss) {
    m_deathCleanupDone = false;

    // 1. BERSIH-BERSIH WINDOW UTAMA PHASE (Cage, FX, Dialog)
    if (boss && boss->GetWindowSystem()) {
        boss->GetWindowSystem()->RemoveTrackedWindow("navi_fx");
        boss->GetWindowSystem()->RemoveTrackedWindow(m_dialogueWindowName);
        boss->GetWindowSystem()->RemoveTrackedWindow(m_cageWindowName);
    }

    // 2. [BARU] BERSIH-BERSIH ATTACK PATTERNS SECARA OTOMATIS
    for (auto& attack : m_activeAttacks) {
        attack->Stop(boss);
    }
    m_activeAttacks.clear();

    // 3. BERSIH-BERSIH EFEK PARTIKEL (VFX) DEATH SCENE
    if (m_deathVfxHandle != -1) {
        EffectManager::Instance().Stop(m_deathVfxHandle);
        m_deathVfxHandle = -1;
    }

    // 4. RESET POINTER & STATE PHASE
    m_fxWindow = nullptr;
    m_fxCamera.reset();
    m_dialogueWindow = nullptr;
    m_dialogueCamera.reset();
    m_dialogueBox.reset();
    m_isDialogueActive = false;
    m_wingSprite.reset();
    m_leftWingData.clear();
    m_rightWingData.clear();
    m_overdriveSprite.reset();

    // 5. KEMBALIKAN MAIN WINDOW KE STATE NORMAL
    Beyond::Window* mainWindow = WindowManager::Instance().GetWindowByIndex(0);
    if (mainWindow && mainWindow->GetSDLWindow()) {
        SDL_ShowWindow(mainWindow->GetSDLWindow());
        SDL_SetWindowAlwaysOnTop(mainWindow->GetSDLWindow(), false);
        mainWindow->SetPriority(50);
        WindowManager::Instance().MarkPriorityDirty();
    }
    m_deathWindowRaised = false;

    if (boss && boss->GetWindowSystem()) {
        boss->GetWindowSystem()->RemoveTrackedWindow("navi_fx");
        boss->GetWindowSystem()->RemoveTrackedWindow(m_dialogueWindowName);
        boss->GetWindowSystem()->RemoveTrackedWindow(m_cageWindowName);
        boss->GetWindowSystem()->RemoveTrackedWindow("click_blocker"); // <-- [BARU] Hapus Blocker
    }
}

// =========================================================
// WING LOGIC (Copas utuh dari versi sebelumnya)
// =========================================================
void BossPhase02::ReplayAnimation() {
    m_wingState = WingState::Expanding;
    m_wingStateTimer = 0.0f;
    m_wingFlickerTimer = 0.0f;
    if (m_fxWindow) m_fxWindow->SetTargetFPS(0.0f);
    GenerateButterflyWings();
}

void BossPhase02::GenerateButterflyWings() {
    m_leftWingData.clear();
    m_rightWingData.clear();
    std::mt19937 gen(m_wingSeed);
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    const int NODES_PER_WING = 120;

    for (int i = 0; i < NODES_PER_WING; ++i) {
        float linearT = (float)i / NODES_PER_WING;
        float t = pow(linearT, 2.0f);
        float angle = t * DirectX::XM_PI;
        float rEdge = (exp(cos(angle)) - 2 * cos(4 * angle) - pow(sin(angle / 12), 5)) * 3.0f;
        float randomFill = pow(dist(gen), 0.3f);
        float r = (i % 5 == 0) ? rEdge : (rEdge * randomFill);
        float scatterX = (dist(gen) - 0.5f) * 1.5f;
        float scatterY = (dist(gen) - 0.5f) * 1.5f;

        WingNode node;
        node.localOffset = { 0.0f, 0.0f };
        node.targetOffset.y = (cos(angle) * r) + scatterY;
        node.flapOffset = t * 2.0f;

        float randomScale = 0.5f + dist(gen);
        float baseSize = 45.0f;
        node.size = { baseSize * randomScale, baseSize * randomScale };

        node.targetOffset.x = -abs(sin(angle) * r) + scatterX;
        m_leftWingData.push_back(node);
        node.targetOffset.x = abs(sin(angle) * r) - scatterX;
        m_rightWingData.push_back(node);
    }

    std::shuffle(m_leftWingData.begin(), m_leftWingData.end(), gen);
    std::shuffle(m_rightWingData.begin(), m_rightWingData.end(), gen);

    for (size_t i = 0; i < m_leftWingData.size(); ++i) {
        float linearDelay = ((float)i / m_leftWingData.size()) * m_spawnDuration;
        m_leftWingData[i].spawnDelay = max(0.0f, linearDelay + (dist(gen) - 0.5f) * m_spawnChaos);
    }
    for (size_t i = 0; i < m_rightWingData.size(); ++i) {
        float linearDelay = ((float)i / m_rightWingData.size()) * m_spawnDuration;
        m_rightWingData[i].spawnDelay = max(0.0f, linearDelay + (dist(gen) - 0.5f) * m_spawnChaos);
    }
}

void BossPhase02::AddAttack(std::unique_ptr<IBossAttackPattern> attack) {
    if (attack) {
        attack->Start(m_bossRef);
        m_activeAttacks.push_back(std::move(attack));
    }
}

void BossPhase02::Update(float dt, Boss* boss) {
    m_glitchTimer += dt;
    
    if (boss && boss->GetWindowSystem()) {
        if (auto* blockerWin = boss->GetWindowSystem()->GetTrackedWindow("click_blocker")) {
            if (blockerWin->window && blockerWin->window->GetSDLWindow()) {
                SDL_RaiseWindow(blockerWin->window->GetSDLWindow());
            }
        }
    }

    if (m_aiTarget && m_aiTarget->GetHP() <= 0) {
        // Hentikan semua BGM yang sedang berjalan
        AudioManager::Instance().StopMusic();

        // Sembunyikan main window boss jika perlu agar transisi bersih
        if (boss && boss->GetMainWindow()) {
            SDL_HideWindow(boss->GetMainWindow()->GetSDLWindow());
        }

        // Return segera agar logika boss/AI di bawahnya tidak dieksekusi
        return;
    }

    // =========================================================
    // [DEATH SEQUENCE] Boss Mati
    // =========================================================
    if (m_bossHP <= 0) {
        if (!m_isDying) {
            m_isDying = true;
            m_deathTimer = 0.0f;
            m_aiEnabled = false;

            DirectX::XMFLOAT3 deathVfxPos = boss->GetPosition();
            deathVfxPos.y += 5.0f;
            m_deathVfxHandle = EffectManager::Instance().Play("Data/Effect/VFX_Boss_Death.efk", deathVfxPos, 2.0f);
            if (m_deathVfxHandle != -1) {
                float rotX = DirectX::XMConvertToRadians(90.0f);
                EffectManager::Instance().SetRotation(m_deathVfxHandle, { rotX, 0.0f, 0.0f });
            }
        }

        m_deathTimer += dt;

        if (m_deathVfxHandle != -1 && EffectManager::Instance().IsPlaying(m_deathVfxHandle)) {
            DirectX::XMFLOAT3 trackPos = boss->GetPosition();
            trackPos.y += 5.0f;
            EffectManager::Instance().SetPosition(m_deathVfxHandle, trackPos);
        }

        if (m_deathTimer >= 7.0f) {
            if (!m_deathCleanupDone) {
                m_deathCleanupDone = true;
                if (m_deathVfxHandle != -1) {
                    EffectManager::Instance().Stop(m_deathVfxHandle);
                    m_deathVfxHandle = -1;
                }
                AudioManager::Instance().StopMusic();

                if (boss && boss->GetWindowSystem() && boss->GetWindowSystem()->GetTrackedWindow("navi_head")) {
                    boss->GetWindowSystem()->RemoveTrackedWindow("navi_head");
                }

                if (boss && boss->GetMainWindow() && boss->GetMainWindow()->GetSDLWindow()) {
                    SDL_HideWindow(boss->GetMainWindow()->GetSDLWindow());
                }
            }
            return;
        }

        // Main window ke depan
        int screenW = GetSystemMetrics(SM_CXSCREEN);
        int screenH = GetSystemMetrics(SM_CYSCREEN);

        Beyond::Window* mainWindow = WindowManager::Instance().GetWindowByIndex(0);
        if (mainWindow && mainWindow->GetSDLWindow()) {
            SDL_Window* sdlWin = mainWindow->GetSDLWindow();
            SDL_ShowWindow(sdlWin);
            SDL_SetWindowBordered(sdlWin, false);
            SDL_SetWindowResizable(sdlWin, false);
            SDL_SetWindowPosition(sdlWin, 0, 0);
            SDL_SetWindowSize(sdlWin, screenW, screenH + 1);
            SDL_SetWindowAlwaysOnTop(sdlWin, true);

            mainWindow->SetPriority(0);
            WindowManager::Instance().MarkPriorityDirty();
        }

        if (m_fxWindow) {
            m_fxWindow->SetPriority(10);
            WindowManager::Instance().MarkPriorityDirty();
        }

        m_deathWindowRaised = true;
    }

    if (m_hitFlashTimer > 0.0f) {
        m_hitFlashTimer -= dt;
        if (m_hitFlashTimer < 0.0f) m_hitFlashTimer = 0.0f;
    }

    if (m_isDialogueActive && m_dialogueBox) {
        m_dialogueBox->Update(dt);
        if (!m_isPlayerCaged || m_overdriveAlpha > 0.0f) {
            m_isDialogueActive = false;
            if (boss && boss->GetWindowSystem()) {
                boss->GetWindowSystem()->RemoveTrackedWindow(m_dialogueWindowName);
            }
            m_dialogueWindow = nullptr;
            m_dialogueCamera.reset();
            m_dialogueBox.reset();
            m_aiEnabled = (m_aiTarget != nullptr);
        }
    }

    if (m_bossHP <= 0) {
        m_aiEnabled = false;
    }

    m_cageWindowPos = m_cagePos;
    if (m_isPlayerCaged && m_cageShakeTimer > 0.0f) {
        m_cageShakeTimer -= dt;
        float t = (m_cageShakeDuration > 0.0f) ? (m_cageShakeTimer / m_cageShakeDuration) : 0.0f;
        float strength = max(0.0f, t) * m_cageShakeIntensity;
        float offsetX = (((rand() % 200) / 100.0f) - 1.0f) * strength;
        float offsetZ = (((rand() % 200) / 100.0f) - 1.0f) * strength;
        m_cageWindowPos = { m_cagePos.x + offsetX, m_cagePos.y, m_cagePos.z + offsetZ };
    }

    if (m_aiTarget && m_aiTarget->IsPowerUncapped() && m_isPlayerCaged) {
        m_overdriveAlpha += m_overdriveFadeSpeed * dt;
        if (m_overdriveAlpha > 1.0f) m_overdriveAlpha = 1.0f;

        if (!m_overdriveDialogueTriggered) {
            m_overdriveDialogueTriggered = true;
            if (m_isDialogueActive && boss && boss->GetWindowSystem()) {
                boss->GetWindowSystem()->RemoveTrackedWindow(m_dialogueWindowName);
                m_dialogueWindow = nullptr;
                m_dialogueCamera.reset();
                m_isDialogueActive = false;
            }
        }
    }
    else {
        m_overdriveAlpha = 0.0f;
    }

    if (m_isPlayerCaged && m_aiTarget) {
        DirectX::XMFLOAT3 pPos = m_aiTarget->GetPosition();
        float halfSize = (m_cageSizeWorld * 0.5f) - 0.5f;
        bool isClamped = false;

        if (pPos.x > m_cagePos.x + halfSize) { pPos.x = m_cagePos.x + halfSize; isClamped = true; }
        if (pPos.x < m_cagePos.x - halfSize) { pPos.x = m_cagePos.x - halfSize; isClamped = true; }
        if (pPos.z > m_cagePos.z + halfSize) { pPos.z = m_cagePos.z + halfSize; isClamped = true; }
        if (pPos.z < m_cagePos.z - halfSize) { pPos.z = m_cagePos.z - halfSize; isClamped = true; }

        if (isClamped) {
            m_aiTarget->GetMovement()->SetPosition(pPos);
            m_aiTarget->SetPosition(pPos);
        }
    }

    if (boss) {
        float centerX = 0.0f;
        float centerZ = 7.0f;
        float rangeX = 2.0f;
        float rangeZ = 1.0f;
        float newX = centerX + sinf(m_glitchTimer * 0.6f) * rangeX;
        float newZ = centerZ + cosf(m_glitchTimer * 0.4f) * rangeZ;
        boss->SetPosition({ newX, 0.0f, newZ });
    }

    // UPDATE WINGS ANIMATION
    if (m_wingState == WingState::Expanding) {
        m_wingStateTimer += dt;
        if (m_wingStateTimer >= WING_EXPAND_DURATION) {
            m_wingState = WingState::Idle;
            if (m_fxWindow) m_fxWindow->SetTargetFPS(0.0f);
        }
    }
    else {
        m_wingFlickerTimer += dt;
        if (m_wingFlickerTimer >= m_nextFlickerTarget) {
            m_wingFlickerTimer = 0.0f;
            float randomFactor = (rand() % 100) / 100.0f;
            m_nextFlickerTarget = 0.05f + (1.0f - min(1.0f, m_spawnChaos)) * 0.8f * randomFactor;
            int windowsToClose = 1 + (rand() % (int)(max(1.0f, m_spawnChaos * 5.0f)));

            for (int w = 0; w < windowsToClose; ++w) {
                std::vector<WingNode>& targetWing = (rand() % 2 == 0) ? m_leftWingData : m_rightWingData;
                if (targetWing.size() > 20) {
                    targetWing[rand() % (targetWing.size() - 10)].isClosing = true;
                }
            }
        }
    }

    auto updateNodeAnimations = [&](std::vector<WingNode>& wingData) {
        int nodeToMoveToTop = -1;
        for (int i = 0; i < wingData.size(); ++i) {
            auto& node = wingData[i];
            if (m_wingState == WingState::Expanding) {
                if (m_wingStateTimer >= node.spawnDelay) {
                    node.animScale = min(1.0f, node.animScale + dt / m_popDuration);
                }
            }
            else {
                if (node.isClosing) {
                    node.animScale -= dt / m_popDuration;
                    if (node.animScale <= 0.0f) {
                        node.animScale = 0.0f;
                        node.isClosing = false;
                        nodeToMoveToTop = i;
                    }
                }
                else if (node.animScale < 1.0f) {
                    float individualSpeed = m_popDuration * (0.8f + ((rand() % 40) / 100.0f));
                    node.animScale = min(1.0f, node.animScale + dt / individualSpeed);
                }
            }
        }
        if (nodeToMoveToTop != -1) {
            WingNode temp = wingData[nodeToMoveToTop];
            wingData.erase(wingData.begin() + nodeToMoveToTop);
            wingData.push_back(temp);
        }
        };

    updateNodeAnimations(m_leftWingData);
    updateNodeAnimations(m_rightWingData);

    for (auto& node : m_leftWingData) {
        node.localOffset.x += (node.targetOffset.x - node.localOffset.x) * dt * 2.0f;
        node.localOffset.y += (node.targetOffset.y - node.localOffset.y) * dt * 2.0f;
        node.localOffset.x -= sinf(m_glitchTimer * m_wingFlapSpeed + node.flapOffset) * m_wingFlapIntensity;
    }

    for (auto& node : m_rightWingData) {
        node.localOffset.x += (node.targetOffset.x - node.localOffset.x) * dt * 2.0f;
        node.localOffset.y += (node.targetOffset.y - node.localOffset.y) * dt * 2.0f;
        node.localOffset.x += sinf(m_glitchTimer * m_wingFlapSpeed + node.flapOffset) * m_wingFlapIntensity;
    }

    // =========================================================
    // [BARU] UPDATE AI DECISIONS (THE BRAIN)
    // =========================================================
    if (m_ai) {
        m_ai->SetEnabled(m_aiEnabled);
        m_ai->Update(dt, boss);
    }

    // =========================================================
    // [BARU] UPDATE ACTIVE ATTACK PATTERNS (THE MUSCLE)
    // =========================================================
    for (auto it = m_activeAttacks.begin(); it != m_activeAttacks.end(); ) {
        (*it)->Update(dt, boss);

        if ((*it)->IsFinished()) {
            (*it)->Stop(boss); // Cleanup resources via interface
            it = m_activeAttacks.erase(it);
        }
        else {
            ++it;
        }
    }
}

void BossPhase02::Render(ID3D11DeviceContext* context, Camera* currentCamera, Boss* boss) {
    if (!currentCamera || !m_wingSprite || !boss) return;

    bool isFXCam = (currentCamera == m_fxCamera.get());
    bool isDialogueCam = m_dialogueCamera && (currentCamera == m_dialogueCamera.get());
    bool isMainCam = !isFXCam && !isDialogueCam;
    bool isDeathMainCam = m_isDying && isMainCam;

    // 1. RENDER DIALOGUE
    if (isDialogueCam) {
        if (m_isDialogueActive && m_dialogueBox && m_dialogueBox->IsActive()) {
            m_dialogueBox->RenderToWindow(context, m_dialogueWindowW, m_dialogueWindowH);
        }
        return;
    }

    // =========================================================
    // 1. RENDER SAYAP
    // Normalnya hanya di FX cam, tapi saat mati juga di main cam
    // =========================================================
    if (isFXCam || isDeathMainCam) {
        std::vector<Sprite::Sprite3DBatchData> batchData;
        DirectX::XMFLOAT3 bossPos = boss->GetPosition();

        float leftWingX = bossPos.x - m_wingXOffset;
        for (const auto& node : m_leftWingData) {
            if (node.animScale <= 0.0f) continue;
            float unitW = (node.size.x / m_pixelToUnit) * m_wingGlobalScale * node.animScale;
            float unitH = (node.size.y / m_pixelToUnit) * m_wingGlobalScale * node.animScale;
            batchData.push_back({
                leftWingX + node.localOffset.x, bossPos.y - 0.1f, bossPos.z + m_wingZOffset + node.localOffset.y,
                unitW, unitH, 0.0f, 0.0f, 0.0f, 0.0f,
                DirectX::XMConvertToRadians(90.0f), 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f
                });
        }
        float rightWingX = bossPos.x + m_wingXOffset;
        for (const auto& node : m_rightWingData) {
            if (node.animScale <= 0.0f) continue;
            float unitW = (node.size.x / m_pixelToUnit) * m_wingGlobalScale * node.animScale;
            float unitH = (node.size.y / m_pixelToUnit) * m_wingGlobalScale * node.animScale;
            batchData.push_back({
                rightWingX + node.localOffset.x, bossPos.y - 0.1f, bossPos.z + m_wingZOffset + node.localOffset.y,
                unitW, unitH, 0.0f, 0.0f, 0.0f, 0.0f,
                DirectX::XMConvertToRadians(90.0f), 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f
                });
        }

        if (!batchData.empty()) {
            m_wingSprite->Render3DBatch(context, currentCamera, batchData);
        }
    }

    // 3. RENDER OVERDRIVE FLOOR DECAL
    if (m_aiTarget && m_aiTarget->IsPowerUncapped() && m_isPlayerCaged && m_overdriveAlpha > 0.0f && m_overdriveSprite) {
        std::vector<Sprite::Sprite3DBatchData> overdriveBatch;

        float baseWidth = 592.0f;
        float baseHeight = 193.0f;
        float finalWidth = baseWidth * m_overdriveSpriteScale;
        float finalHeight = baseHeight * m_overdriveSpriteScale;

        DirectX::XMFLOAT3 pos = { 0.0f, 0.0f, -7.5f };
        float scale = 0.4f;

        overdriveBatch.push_back({
            pos.x, pos.y, pos.z,
            finalWidth * scale, finalHeight * scale,
            0.0f, 0.0f, 0.0f, 0.0f,
            DirectX::XMConvertToRadians(90.0f), 0.0f, 0.0f,
            1.0f, 1.0f, 1.0f, m_overdriveAlpha * 0.5f
            });

        m_overdriveSprite->Render3DBatch(context, currentCamera, overdriveBatch);
    }

    // =========================================================
    // [BARU] DELEGASI RENDER KE ATTACK PATTERNS
    // =========================================================
    for (auto& attack : m_activeAttacks) {
        attack->Render(context, currentCamera, boss);
    }
}


void BossPhase02::DamageCage(int dmg) {
    if (!m_isPlayerCaged) return;

    bool isFirstHit = !m_cageFirstHitTriggered;
    if (isFirstHit) {
        m_cageFirstHitTriggered = true;
        TriggerCageFirstHitDialogue(m_bossRef);
    }

    m_cageHP -= dmg;
    if (m_cageHP < 0) m_cageHP = 0;

    m_cageShakeTimer = m_cageShakeDuration;

    AudioManager::Instance().PlaySFX("Data/Sound/SE_Hit.wav", 0.2f);

    if (m_bossRef && m_bossRef->GetWindowSystem()) {
        if (auto* cageWindow = m_bossRef->GetWindowSystem()->GetTrackedWindow(m_cageWindowName)) {
            if (cageWindow->window) {
                float hpRatio = (m_cageMaxHP > 0) ? ((float)m_cageHP / (float)m_cageMaxHP) : 0.0f;
                cageWindow->window->SetBackgroundAlpha(max(0.15f, hpRatio));
            }
        }
    }

    // =========================================================
    // PICU PLAYER OVERDRIVE / UNCAPPED (HP <= 50%)
    // =========================================================
    if (m_cageHP <= 300 && m_aiTarget) {
        if (!m_aiTarget->IsPowerUncapped()) {
            m_aiTarget->ReleasePowerCap();
            m_aiTarget->SetShootDelay(0.5f);

            CameraController::Instance().AddTrauma(0.5f);
            AudioManager::Instance().PlaySFX("Data/Sound/SE_Boss_Laser_Charge.wav", 0.3f);
        }
    }

    // =========================================================
    // JIKA KANDANG HANCUR (HP <= 0)
    // =========================================================
    if (m_cageHP <= 0) {
        m_isPlayerCaged = false;
        m_aiEnabled = true;

        CameraController::Instance().AddTrauma(0.6f);
        AudioManager::Instance().PlaySFX("Data/Sound/SE_GlassShatter.wav", 0.4f);

        if (m_bossRef && m_bossRef->GetWindowSystem()) {
            auto extracted = m_bossRef->GetWindowSystem()->ExtractForPool(m_cageWindowName);
            if (extracted && extracted->window) {
                WindowManager::Instance().DestroyWindow(extracted->window);
            }
        }
    }
}

void BossPhase02::TriggerCageFirstHitDialogue(Boss* boss)
{
    // Dialog tidak dibutuhkan — fungsi dikosongkan dengan sengaja.
}

void BossPhase02::TakeDamage(int damage, DirectX::XMFLOAT3 hitPos) {
    if (m_bossHP <= 0) return;

    m_bossHP = max(0, m_bossHP - damage);
    m_hitFlashTimer = 0.05f;

    CameraController::Instance().AddTrauma(0.3f);
    AudioManager::Instance().PlaySFX("Data/Sound/SE_Boss_Hit.wav", 0.1f);
    EffectManager::Instance().Play("Data/Effect/VFX_Boss_Hit.efk", hitPos, 0.3f);
}

std::vector<Bullet*> BossPhase02::GetProjectiles() {
    std::vector<Bullet*> allBullets;

    // 実行中のすべてのアタックパターンから、アクティブな弾を動的に集める
    for (auto& attack : m_activeAttacks) {
        std::vector<Bullet*> attackBullets = attack->GetActiveProjectiles();
        allBullets.insert(allBullets.end(), attackBullets.begin(), attackBullets.end());
    }

    return allBullets;
}