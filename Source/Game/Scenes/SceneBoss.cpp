#include "SceneBoss.h"
#include "System/Graphics.h"
#include "System/Input.h"
#include "WindowManager.h"
#include "Framework.h"
#include <algorithm>
#ifdef NAVI_DEBUG_GUI
#include <imgui.h>
#endif
#include "System/CollisionManager.h"
#include "EnemyManager.h"
#include "ItemManager.h"
#include "Stage.h"
#include <random>
#include "BossPhase02.h"
#include "BossPhase01.h"
#include "HUDRenderer.h"
#include "EffectManager.h"
#include "WindowShatter.h"
#include "AttackParamManager.h"
using namespace DirectX;

// =========================================================
// CONSTRUCTOR / DESTRUCTOR
// =========================================================

SceneBoss::SceneBoss()
{

    // Disable ImGui multi-viewport while in this scene (restored in destructor)
#ifdef NAVI_DEBUG_GUI
    ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_ViewportsEnable;
#endif

    // --- Window Tracking System ---
    m_windowSystem = std::make_unique<WindowTrackingSystem>();
    m_windowSystem->SetPixelToUnitRatio(k_pixelToUnitRatio);
    m_windowSystem->SetFOV(k_fov);

    // --- Camera ---
    // Use a fixed 16:9 aspect for the projection; off-center projection per
    // sub-window is handled by WindowTrackingSystem::UpdateOffCenterProjection.
    const float unifiedHeight = m_windowSystem->GetUnifiedCameraHeight();
    m_mainCamera = std::make_shared<Camera>();
    m_mainCamera->SetPerspectiveFov(XMConvertToRadians(k_fov), 1920.0f / 1080.0f, k_camNear, k_camFar);
    m_mainCamera->SetPosition(m_cameraPosition);
    m_mainCamera->LookAt(m_cameraTarget);

    CameraController::Instance().SetActiveCamera(m_mainCamera);
    CameraController::Instance().SetControlMode(CameraControlMode::FixedStatic);
    CameraController::Instance().SetFixedSetting(XMFLOAT3(0.0f, unifiedHeight, 0.0f));

    // --- PhysX (minimal: no ground plane, gravity = zero) ---
    InitializePhysics();

    // --- Player ---
    m_player = std::make_unique<Player>();
    m_player->InitPhysics(m_controllerManager.get(), m_defaultMaterial.get(),
        PlayerConst::CapsuleHalfHeight);  // Kaki tepat di Y=0, gravity off
    PlayerConfig bossConfig{};
    bossConfig.moveSpeed = 15.0f;         // Fast movement
    bossConfig.dashSpeed = 45.0f;         // Fast dash
    bossConfig.gravityEnabled = false;    // No gravity for Top-Down Boss mode

    m_player->ApplyConfig(bossConfig);

    m_player->SetPosition(0.0f, 0.0f, -8.0f);

    //// --- TAMBAHKAN INISIALISASI MANAGER DI SINI ---
    //auto device = Graphics::Instance().GetDevice();

    // --- Primitive Renderers ---
    ID3D11Device* device = Graphics::Instance().GetDevice();
    auto* context = Graphics::Instance().GetDeviceContext();
    m_primitive2D = std::make_unique<Primitive>(device);
    m_primitive3D = std::make_unique<PrimitiveRenderer>(device);
    m_hud = std::make_unique<HUDRenderer>(device);

    EffectManager::Instance().Initialize(device, context);

    m_stage = std::make_unique<Stage>(device); // Walau kosong, ini mencegah Null Pointer

    //m_enemyManager = std::make_unique<EnemyManager>();
    //m_enemyManager->Initialize(device);

    //m_itemManager = std::make_unique<ItemManager>();
    //m_itemManager->Initialize(device);

    // Jika Anda sudah memiliki inisialisasi Boss, panggil di sini
    // m_boss = std::make_unique<Boss>(); 

    m_collisionManager = std::make_unique<CollisionManager>();

    // Gunakan Overload 2 yang ada Boss-nya
    m_collisionManager->Initialize(m_player.get(), m_stage.get(), m_enemyManager.get(), m_itemManager.get());

    // PENTING: Beri tahu Player siapa wasit (CollisionManager) di scene ini!
    m_player->SetCollisionManager(m_collisionManager.get());

    m_navi = std::make_unique<Boss>();
    m_navi->Initialize(m_windowSystem.get());

    m_navi->ChangePhase(std::make_unique<BossPhase01>(m_player.get()));


    if (m_collisionManager) {
        m_collisionManager->SetBoss(m_navi.get());
        m_playerWindowTransparent = false;
    }

    WindowManager::Instance().SetTopmost(m_topmostEnabled);
    InitializeSubWindows();

    // --- Death Fade Effects ---
    float screenW = static_cast<float>(GetSystemMetrics(SM_CXSCREEN));
    float screenH = static_cast<float>(GetSystemMetrics(SM_CYSCREEN));
    if (auto window = Framework::Instance()->GetMainWindow()) {
        screenW = static_cast<float>(window->GetWidth());
        screenH = static_cast<float>(window->GetHeight());
    }

    m_postProcess = std::make_unique<PostProcessManager>();
    m_postProcess->Initialize(static_cast<int>(screenW), static_cast<int>(screenH));

    m_fadeSprite = std::make_unique<Sprite>(device, "Data/Sprite/Scene Game/Black.png");
    m_whiteSprite = std::make_unique<Sprite>(device, "Data/Sprite/Scene Game/White.png");
    m_uberParams.intensity = FX_BASE_INTENSITY;
    m_uberParams.smoothness = FX_BASE_SMOOTHNESS;

    AddLog("SceneBoss initialized. Windowkill system online.");
}

SceneBoss::~SceneBoss()
{
    // Restore ImGui multi-viewport for other scenes
#ifdef NAVI_DEBUG_GUI
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
#endif
    Shutdown();
}

void SceneBoss::Shutdown()
{
    // CLEAR WINDOWS FIRST (CRITICAL)
    // Destroys sub-windows and unbinds callbacks before the objects they point to (Navi/Player) are deleted.
    if (m_windowSystem) {
        m_windowSystem->ClearAll();
    }

    // STOP ALL SINGLETON LEAKS
    // Singletons outlive the Scene. If we don't clear them, they bleed into SceneTitle/SceneGame.
    AudioManager::Instance().StopMusic();
    EffectManager::Instance().StopAll();
    WindowShatterManager::Instance().Clear();

    // RESTORE MAIN WINDOW OS STATES
    Beyond::Window* mainWindow = WindowManager::Instance().GetWindowByIndex(0);
    if (mainWindow && mainWindow->GetSDLWindow()) {
        SDL_SetWindowAlwaysOnTop(mainWindow->GetSDLWindow(), false);
        mainWindow->SetPriority(50);
        SDL_RaiseWindow(mainWindow->GetSDLWindow());
        WindowManager::Instance().MarkPriorityDirty();
    }
    WindowManager::Instance().SetTopmost(false);

    // RESTORE ENGINE STATES
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
    CameraController::Instance().ClearCamera();

    // EXPLICIT ENTITY DESTRUCTION ORDER
    m_navi.reset();
    m_player.reset();
    m_enemyManager.reset();
    m_itemManager.reset();
    m_collisionManager.reset();
}

// =========================================================
// INITIALIZATION HELPERS
// =========================================================

void SceneBoss::InitializePhysics()
{
    m_foundation.reset(PxCreateFoundation(PX_PHYSICS_VERSION, m_allocator, m_errorCallback));
    assert(m_foundation && "CRITICAL ERROR: PxCreateFoundation failed!");

    m_physics.reset(PxCreatePhysics(PX_PHYSICS_VERSION, *m_foundation, physx::PxTolerancesScale(), true, nullptr));
    assert(m_physics && "CRITICAL ERROR: PxCreatePhysics failed!");

    // Zero gravity: player Y is clamped manually in Update; no ground plane needed.
    physx::PxSceneDesc sceneDesc(m_physics->getTolerancesScale());
    sceneDesc.gravity = physx::PxVec3(0.0f, 0.0f, 0.0f);
    sceneDesc.filterShader = physx::PxDefaultSimulationFilterShader;

    m_dispatcher.reset(physx::PxDefaultCpuDispatcherCreate(2));
    sceneDesc.cpuDispatcher = m_dispatcher.get();

    m_scene.reset(m_physics->createScene(sceneDesc));
    assert(m_scene && "CRITICAL ERROR: createScene failed!");

    m_controllerManager.reset(PxCreateControllerManager(*m_scene));
    assert(m_controllerManager && "CRITICAL ERROR: PxCreateControllerManager failed!");

    m_defaultMaterial.reset(m_physics->createMaterial(0.5f, 0.5f, 0.1f));
    assert(m_defaultMaterial && "CRITICAL ERROR: createMaterial failed!");
}

void SceneBoss::InitializeSubWindows()
{
    if (!m_windowSystem || !m_player) return;

    // =========================================================
    // [FIX MUTLAK] Jangan pernah spawn window "player" jika 
    // bos sedang berada di Fase Windowkill!
    // =========================================================
    bool isWindowkillPhase = false;
    if (m_navi && dynamic_cast<BossPhase02*>(m_navi->GetCurrentPhase())) {
        isWindowkillPhase = true;
    }

    if (!isWindowkillPhase)
    {
        // --- Player-tracking window ---
        m_windowSystem->AddTrackedWindow(
            { "player", "Player", 300, 300, 1 },
            [this]() -> XMFLOAT3 {
                if (!m_player) return XMFLOAT3(0.0f, 0.0f, 0.0f);
                const auto pPos = m_player->GetPosition();
                return XMFLOAT3(
                    pPos.x + m_stretchOffset.x,
                    0.0f,
                    pPos.z + m_stretchOffset.y
                );
            },
            [this]() -> XMFLOAT2 {
                return XMFLOAT2(
                    k_defaultWinSize + m_currentStretch.x,
                    k_defaultWinSize + m_currentStretch.y
                );
            }
        );

        TrackedWindow* playerWin = m_windowSystem->GetTrackedWindow("player");
        if (playerWin && playerWin->window)
        {
            SDL_Window* sdlWin = playerWin->window->GetSDLWindow();
            SDL_SetWindowResizable(sdlWin, true);
            SDL_SetWindowBordered(sdlWin, true);
        }
    }

    // --- Register main window with the tracking system ---
    // (Lanjutkan kode aslimu di bawah sini...)

    // --- Register main window with the tracking system ---
    Beyond::Window* mainWindow = WindowManager::Instance().GetWindowByIndex(0);
    if (mainWindow && mainWindow->GetSDLWindow())
    {
        SDL_ShowWindow(mainWindow->GetSDLWindow());

        if (!m_windowSystem->GetTrackedWindow("main_window"))
            m_windowSystem->RegisterWindow(mainWindow, WindowRole::MAIN_VIEWPORT, m_mainCamera);
    }

    TrackedWindow* mainTracked = m_windowSystem->GetTrackedWindow("main_window");
    if (mainTracked && mainTracked->window && mainTracked->window->GetSDLWindow()) {
        SDL_RaiseWindow(mainTracked->window->GetSDLWindow());
    }

    WindowManager::Instance().EnforceWindowPriorities();
}

// =========================================================
// UPDATE
// =========================================================

void SceneBoss::Update(float elapsedTime)
{
    float activeTimeScale = m_timeScale * 1.0f;
    const float scaledDt = elapsedTime * activeTimeScale;

    // =========================================================
    // DEATH SEQUENCE LOGIC
    // =========================================================
    if (m_player && m_player->GetHP() <= 0 && !m_isDying && m_respawnTimer <= 0.0f)
    {
        StartPlayerDeathSequence();
    }

    if (m_isDying)
    {
        m_deathTimer += elapsedTime; 

        if (m_deathTimer < DEATH_DELAY_DURATION)
        {
            m_uberParams.smoothness = FX_BASE_SMOOTHNESS;
            m_uberParams.intensity = FX_BASE_INTENSITY;
            m_fadeAlpha = 0.0f;
        }
        else
        {
            const float fadeTime{ m_deathTimer - DEATH_DELAY_DURATION };
            const float t{ std::clamp(fadeTime / DEATH_FADE_DURATION, 0.0f, 1.0f) };

            m_uberParams.smoothness = FX_BASE_SMOOTHNESS + (FX_BLACK_SMOOTHNESS - FX_BASE_SMOOTHNESS) * t;
            m_uberParams.intensity = FX_BASE_INTENSITY + (FX_BLACK_INTENSITY - FX_BASE_INTENSITY) * t;
            m_fadeAlpha = t;

            if (t >= 1.0f)
            {
                ResetLevel();
                m_isDying = false;
                m_respawnTimer = RESPAWN_FADE_DURATION;

                m_uberParams.smoothness = FX_BLACK_SMOOTHNESS;
                m_uberParams.intensity = FX_BLACK_INTENSITY;
                m_fadeAlpha = 1.0f;
            }
        }
    }
    else if (m_respawnTimer > 0.0f)
    {
        m_respawnTimer -= elapsedTime;

        if (m_player) m_player->SetInputEnabled(false);

        const float linearT{ std::clamp(m_respawnTimer / RESPAWN_FADE_DURATION, 0.0f, 1.0f) };
        const float t{ linearT * linearT }; // Quadratic Ease-Out

        m_uberParams.smoothness = FX_BASE_SMOOTHNESS + (FX_BLACK_SMOOTHNESS - FX_BASE_SMOOTHNESS) * t;
        m_uberParams.intensity = FX_BASE_INTENSITY + (FX_BLACK_INTENSITY - FX_BASE_INTENSITY) * t;
        m_fadeAlpha = t;

        if (m_respawnTimer <= 0.0f && m_player)
        {
            m_player->SetInputEnabled(true);

            if (m_navi)
            {
                if (auto* normalPhase = dynamic_cast<BossPhase01*>(m_navi->GetCurrentPhase())) {
                    normalPhase->SetAIEnabled(true);
                }
                else if (auto* wkPhase = dynamic_cast<BossPhase02*>(m_navi->GetCurrentPhase())) {
                    wkPhase->SetAIEnabled(true);
                }
            }
        }
    }
    else
    {
        m_uberParams.smoothness = FX_BASE_SMOOTHNESS;
        m_uberParams.intensity = FX_BASE_INTENSITY;
        m_fadeAlpha = 0.0f;
    }

    // --- PhysX tick ---
    if (m_scene)
    {
        m_scene->simulate(scaledDt);
        m_scene->fetchResults(true);
    }

    Camera* activeCam = CameraController::Instance().GetActiveCamera().get();

    if (m_navi) {
        auto* wkPhase = dynamic_cast<BossPhase02*>(m_navi->GetCurrentPhase());

        // Trigger the start of the death sequence
        if (wkPhase && wkPhase->IsDead() && !m_isNaviDefeated)
        {
            m_isNaviDefeated = true;
            m_naviDefeatTimer = 0.0f;
            AddLog("Navi defeated. Starting death sequence.");
        }

        // Logic for the Fade-In
        if (m_isNaviDefeated)
        {
            m_naviDefeatTimer += elapsedTime;
            if (m_naviDefeatTimer > NAVI_DEATH_ANIM_DURATION)
            {
                float fadeTime = m_naviDefeatTimer - NAVI_DEATH_ANIM_DURATION;
                m_whiteAlpha = std::clamp(fadeTime / WHITE_FADE_DURATION, 0.0f, 1.0f);
            }
        }
    }

    // Windowkill Phase Logic (Check for Scene Change)
    if (m_navi) {
        auto* wkPhase = dynamic_cast<BossPhase02*>(m_navi->GetCurrentPhase());
        Beyond::Window* mw = WindowManager::Instance().GetWindowByIndex(0);

        if (wkPhase) {
            // Visibility logic
            if (!wkPhase->IsDead()) {
                m_autoSyncMainWindow = false;
                if (mw && mw->GetSDLWindow()) SDL_HideWindow(mw->GetSDLWindow());
            }
            else {
                if (mw && mw->GetSDLWindow()) SDL_ShowWindow(mw->GetSDLWindow());
            }

            // ONLY change scene if the White Fade is complete!
            if (m_whiteAlpha >= 1.0f && !m_isPendingSceneChange) {
                m_isPendingSceneChange = true;
                return; // Now it is safe to return/change scene
            }
        }
    }

    // --- Sync main window size ---
    Beyond::Window* mainWindow = WindowManager::Instance().GetWindowByIndex(0);
    if (mainWindow)
    {
        if (m_autoSyncMainWindow)
        {
#ifdef NAVI_DEBUG_GUI
            int winX, winY;
            SDL_GetWindowPosition(mainWindow->GetSDLWindow(), &winX, &winY);
            SDL_SetWindowSize(mainWindow->GetSDLWindow(),
                static_cast<int>(m_debugPanelSize.x),
                static_cast<int>(m_debugPanelSize.y));
            m_windowSystem->UpdateWindowBounds(0,
                static_cast<int>(m_debugPanelSize.x),
                static_cast<int>(m_debugPanelSize.y));
#endif
        }
        else
        {
            m_windowSystem->UpdateWindowBounds(0, mainWindow->GetWidth(), mainWindow->GetHeight());
        }
    }

    // =========================================================
    // KAMERA STATIS (NO ZOOM)
    // =========================================================
    m_targetZoom = 0.0f;
    m_currentZoom = 0.0f;

    // Kunci Rasio Piksel ke default agar ukuran dunia stabil
    float dynamicPixelRatio = k_pixelToUnitRatio;
    m_windowSystem->SetPixelToUnitRatio(dynamicPixelRatio);

    // =========================================================
    // [FIX] UPDATE KAMERA & CAMERA SHAKE (GABUNGAN)
    // =========================================================
    // Deklarasi hanya dilakukan SATU KALI di sini!
    float newUnifiedHeight = m_windowSystem->GetUnifiedCameraHeight();
    auto& camCtrl = CameraController::Instance();

    // Ambil nilai getaran (Trauma)
    DirectX::XMFLOAT3 shake = camCtrl.GetShakeOffset();

    // Set posisi kamera dengan menggabungkan Zoom (Y) dan Shake (X, Z)
    camCtrl.SetFixedSetting(DirectX::XMFLOAT3(shake.x, newUnifiedHeight + shake.y, shake.z));
    camCtrl.SetTarget({ shake.x, shake.y, shake.z });

    // [PENTING] Update kamera menggunakan waktu murni (elapsedTime) agar tetap bergetar saat Hit Stop!
    camCtrl.Update(elapsedTime);
    // =========================================================

    // --- Player update ---
    if (m_player)
    {
        m_player->Update(scaledDt, activeCam);
    }

    // --- Squash & Stretch ---
    if (m_player)
    {
        const XMFLOAT3 vel = m_player->GetMovement()->GetVelocity();
        const float    currentSpeedSq = vel.x * vel.x + vel.z * vel.z;
        const float    dashThreshold = m_player->GetDashSpeed() * 0.8f;

        XMFLOAT2 targetStretch = { 0.0f, 0.0f };
        XMFLOAT2 targetOffset = { 0.0f, 0.0f };
        constexpr float lerpSpeed = 10.0f;

        if (currentSpeedSq > (dashThreshold * dashThreshold))
        {
            constexpr float kStretchX = 200.0f;
            constexpr float kSquashY = 0.0f;
            constexpr float kStretchZ = 0.0f;
            constexpr float kSquashX = 0.0f;

            const float dashRatio = sqrtf(currentSpeedSq) / m_player->GetDashSpeed();

            if (std::abs(vel.x) > std::abs(vel.z))
            {
                targetStretch.x = dashRatio * kStretchX;
                targetStretch.y = dashRatio * kSquashY;
                const float signX = (vel.x > 0.0f) ? 1.0f : -1.0f;
                // Gunakan dynamicPixelRatio
                targetOffset.x = -signX * (targetStretch.x * 0.5f) / dynamicPixelRatio;
            }
            else
            {
                targetStretch.y = dashRatio * kStretchZ;
                targetStretch.x = dashRatio * kSquashX;
                const float signZ = (vel.z > 0.0f) ? 1.0f : -1.0f;
                // Gunakan dynamicPixelRatio
                targetOffset.y = -signZ * (targetStretch.y * 0.5f) / dynamicPixelRatio;
            }
        }

        m_currentStretch.x += (targetStretch.x - m_currentStretch.x) * lerpSpeed * scaledDt;
        m_currentStretch.y += (targetStretch.y - m_currentStretch.y) * lerpSpeed * scaledDt;
        m_stretchOffset.x += (targetOffset.x - m_stretchOffset.x) * lerpSpeed * scaledDt;
        m_stretchOffset.y += (targetOffset.y - m_stretchOffset.y) * lerpSpeed * scaledDt;
    }

    // --- Entities & Collision Update ---
    if (m_navi) {
        // AI Director にプレイヤーのデータを渡す
        if (auto* normalPhase = dynamic_cast<BossPhase01*>(m_navi->GetCurrentPhase())) {
            normalPhase->SetAITarget(m_player.get());
        }
        // [追加] Windowkill フェーズにもプレイヤーデータを渡す！
        else if (auto* wkPhase = dynamic_cast<BossPhase02*>(m_navi->GetCurrentPhase())) {
            wkPhase->SetAITarget(m_player.get());
        }

        m_navi->Update(scaledDt);

        if (auto* wkPhase = dynamic_cast<BossPhase02*>(m_navi->GetCurrentPhase())) {
            if (!m_isPendingSceneChange && wkPhase->IsReadyToChangeScene()) {
                m_isPendingSceneChange = true;
                //Framework::Instance()->ChangeScene(std::make_unique<SceneTitle>());
                return;
            }
        }

        bool isWindowkillPhase = (dynamic_cast<BossPhase02*>(m_navi->GetCurrentPhase()) != nullptr);

        if (isWindowkillPhase && !m_playerWindowTransparent) {
            // Jika bos baru saja masuk Phase 2, nyalakan transparansi!
            m_playerWindowTransparent = true;
        }
        else if (!isWindowkillPhase && m_playerWindowTransparent) {
            // Jika bos kembali ke Phase 1 (atau respawn/mati), matikan transparansi!
            m_playerWindowTransparent = false;
        }
    }
    if (m_isPendingSceneChange) return;
    if (m_enemyManager) m_enemyManager->Update(scaledDt, activeCam, m_player->GetPosition(), true);
    if (m_itemManager) m_itemManager->Update(scaledDt, activeCam);
    if (m_collisionManager) m_collisionManager->Update(scaledDt);

    EffectManager::Instance().Update(scaledDt);
    WindowShatterManager::Instance().Update(scaledDt);
    // Terapkan posisi m_fixedPos dan Shakes
    //camCtrl.Update(scaledDt);

    // --- Sync sub-window cameras to match main camera ---
    if (m_windowSystem)
    {
        for (auto& tracked : m_windowSystem->GetWindows())
        {
            if (!tracked->isActive) continue;
            if (!tracked->camera || tracked->camera == m_mainCamera) continue;

            tracked->camera->SetPosition(m_mainCamera->GetPosition());
            tracked->camera->SetRotation(m_mainCamera->GetRotation());

            //if (tracked->role != WindowRole::SUB_VIEWPORT && m_player)
            //    tracked->camera->LookAt(m_player->GetPosition());
        }

        m_windowSystem->Update(elapsedTime);
    }

    const int activeWins = m_windowSystem ? static_cast<int>(m_windowSystem->GetWindows().size()) : 0;

    DrawGUI();

    // --- LOGIKA OTOMATISASI OVERDRIVE PLAYER ---
    //if (m_player && m_navi)
    //{
    //    bool shouldUncap = m_forceUncapOverride;

    //    // Cek darah boss jika berada di Fase Normal
    //    if (auto* normalPhase = dynamic_cast<BossPhase01*>(m_navi->GetCurrentPhase()))
    //    {
    //        float bossHpPercent = (static_cast<float>(normalPhase->GetHP()) / 1500.0f) * 100.0f;
    //        if (bossHpPercent <= m_overdriveBossHpTriggerPercent)
    //        {
    //            shouldUncap = true;
    //        }
    //    }

    //    // Picu pelepasan batas kekuatan jika kondisi terpenuhi
    //    if (shouldUncap && !m_player->IsPowerUncapped())
    //    {
    //        m_player->ReleasePowerCap();
    //    }
    //}
}


// =========================================================
// RENDER
// =========================================================

void SceneBoss::Render(float elapsedTime, Camera* camera)
{
    Camera* targetCam = camera ? camera : m_mainCamera.get();
    auto dc = Graphics::Instance().GetDeviceContext();
    auto rs = Graphics::Instance().GetRenderState();

    // Detect whether this render call is targeting a transparent sub-window
    bool isTransparentWindow = false;
    if (m_windowSystem)
    {
        for (const auto& tracked : m_windowSystem->GetWindows())
        {
            if (!tracked->isActive) continue;

            if (tracked->camera.get() == camera &&
                tracked->window &&
                tracked->window->IsTransparent())
            {
                isTransparentWindow = true;
                break;
            }
        }
    }

    // =========================================================
    // [NEW] OVERRIDE CLEAR COLOR UNTUK JENDELA NON-TRANSPARAN
    // =========================================================
    if (!isTransparentWindow)
    {
        ID3D11RenderTargetView* currentRTV = nullptr;
        ID3D11DepthStencilView* currentDSV = nullptr;

        // Ambil Render Target dan Depth Stencil yang saat ini sedang diikat oleh WindowManager
        dc->OMGetRenderTargets(1, &currentRTV, &currentDSV);

        if (currentRTV)
        {
            // Bersihkan ulang menggunakan warna kustom dari SceneBoss
            float clearColor[4] = { m_clearColor.x, m_clearColor.y, m_clearColor.z, m_clearColor.w };
            dc->ClearRenderTargetView(currentRTV, clearColor);

            // Wajib di-release karena OMGetRenderTargets menaikkan tingkat referensi (COM AddRef)
            currentRTV->Release();
        }

        if (currentDSV)
        {
            // Bersihkan ulang depth buffer agar kalkulasi depth 3D tidak rusak
            dc->ClearDepthStencilView(currentDSV, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
            currentDSV->Release();
        }
    }

    // Transparent windows need TransparentWindow blend state so the alpha
    // channel is preserved for UpdateLayeredWindow. Normal windows use
    // standard alpha blending.
    dc->OMSetBlendState(
        rs->GetBlendState(isTransparentWindow
            ? BlendState::TransparentWindow
            : BlendState::Transparency),
        nullptr, 0xFFFFFFFF);
    dc->OMSetDepthStencilState(rs->GetDepthStencilState(DepthState::TestAndWrite), 0);
    dc->RSSetState(rs->GetRasterizerState(RasterizerState::SolidCullBack));

    // =========================================================
    // POST-PROCESS VIGNETTE (Only applied to Main Window)
    // =========================================================
    auto* wkPhase = m_navi ? dynamic_cast<BossPhase02*>(m_navi->GetCurrentPhase()) : nullptr;

    // 2. Determine if Windowkill is active AND the boss is NOT dead
    bool isWindowkillAndAlive = (wkPhase != nullptr && !wkPhase->IsDead());

    // 3. Use the updated boolean to gate post-processing
    bool usePostProcess = (!isTransparentWindow &&
        targetCam == m_mainCamera.get() &&
        m_postProcess &&
        isWindowkillAndAlive);

    if (usePostProcess)
    {
        m_postProcess->SetEnabled(true);
        UberShader::UberData& activeData = m_postProcess->GetData();
        activeData = m_uberParams;

        // Turn off unneeded filters just to be safe
        activeData.glitchStrength = 0.0f;
        activeData.distortion = 0.0f;
        activeData.chromaticAberration = 0.0f;
        activeData.scanlineStrength = 0.0f;
        activeData.bloomIntensity = 0.0f;
        activeData.psxEnabled = false;

        m_postProcess->BeginCapture();
    }

    RenderScene(elapsedTime, targetCam, isTransparentWindow);

    if (usePostProcess)
    {
        m_postProcess->EndCapture(elapsedTime);
    }

    if (m_showGrid && m_primitive3D)
    {
        m_primitive3D->DrawGrid(25, 1.0f);
        m_primitive3D->Render(dc,
            targetCam->GetView(),
            targetCam->GetProjection(),
            D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
    }

    auto shapeRenderer = Graphics::Instance().GetShapeRenderer();
    if (m_showHitboxes)
    {
        // 1. Gambar Hurtbox Player (Lingkaran Hijau)
        // Kita kunci di y=1.0f agar terlihat menonjol di badan player
        if (m_player && m_player->GetHP() > 0) {
            DirectX::XMFLOAT3 pPos = m_player->GetPosition();
            pPos.y = 1.0f;
            shapeRenderer->DrawSphere(pPos, 0.3f, { 0.0f, 1.0f, 0.0f, 1.0f });
        }

        // 2. Gambar Hitbox Peluru Navi (Lingkaran Merah / Hijau)
        if (m_navi) {
            if (auto* normalPhase = dynamic_cast<BossPhase01*>(m_navi->GetCurrentPhase())) {
                for (auto& bullet : normalPhase->GetProjectiles()) {
                    if (bullet->IsActive()) {
                        DirectX::XMFLOAT3 bPos = bullet->GetMovement()->GetPosition();
                        shapeRenderer->DrawSphere(bPos, bullet->GetRadius(), { 1.0f, 0.0f, 0.0f, 1.0f });
                    }
                }
            }
            // =========================================================
            // [FIX MUTLAK] LOGIKA WINDOWKILL DITEMPATKAN DI SINI!
            // =========================================================
            else if (auto* wkPhase = dynamic_cast<BossPhase02*>(m_navi->GetCurrentPhase())) {
                for (Bullet* bullet : wkPhase->GetProjectiles()) {
                    if (bullet && bullet->IsActive()) {
                        DirectX::XMFLOAT3 bPos = bullet->GetPosition();
                        // Warna Hijau Lime agar kontras untuk peluru mantul
                        shapeRenderer->DrawSphere(bPos, bullet->GetRadius(), { 0.0f, 1.0f, 0.0f, 1.0f });
                    }
                }
            }
        }
    }

    Graphics::Instance().GetShapeRenderer()->Render(
        dc, targetCam->GetView(), targetCam->GetProjection());

    // =========================================================
    // FADE SPRITE OVERLAY 
    // =========================================================
    if (m_fadeAlpha > 0.001f && m_fadeSprite)
    {
        float screenW = static_cast<float>(GetSystemMetrics(SM_CXSCREEN));
        float screenH = static_cast<float>(GetSystemMetrics(SM_CYSCREEN));
        if (auto window = Framework::Instance()->GetMainWindow()) {
            screenW = static_cast<float>(window->GetWidth());
            screenH = static_cast<float>(window->GetHeight());
        }

        dc->OMSetBlendState(rs->GetBlendState(BlendState::Transparency), nullptr, 0xFFFFFFFF);
        dc->OMSetDepthStencilState(rs->GetDepthStencilState(DepthState::NoTestNoWrite), 0);

        m_fadeSprite->Render(
            dc,
            0.0f, 0.0f, 0.0f,
            screenW, screenH,
            0.0f, 0.0f,
            1920.0f, 1080.0f,
            0.0f,
            0.0f, 0.0f, 0.0f, m_fadeAlpha
        );
    }

    if (m_whiteAlpha > 0.001f && m_whiteSprite)
    {
        float screenW = static_cast<float>(GetSystemMetrics(SM_CXSCREEN));
        float screenH = static_cast<float>(GetSystemMetrics(SM_CYSCREEN));
        if (auto window = Framework::Instance()->GetMainWindow()) {
            screenW = static_cast<float>(window->GetWidth());
            screenH = static_cast<float>(window->GetHeight());
        }

        dc->OMSetBlendState(rs->GetBlendState(BlendState::Transparency), nullptr, 0xFFFFFFFF);
        dc->OMSetDepthStencilState(rs->GetDepthStencilState(DepthState::NoTestNoWrite), 0);

        m_whiteSprite->Render(
            dc,
            0.0f, 0.0f, 0.0f,
            screenW, screenH,
            0.0f, 0.0f,
            1920.0f, 1080.0f,
            0.0f,
            1.0f, 1.0f, 1.0f, m_whiteAlpha 
        );
    }
}

void SceneBoss::StartPlayerDeathSequence()
{
    if (m_isDying) return;

    m_isDying = true;
    m_deathTimer = 0.0f;

    if (m_player)
    {
        m_player->SetInputEnabled(false);
        m_player->scale = { 0.0f, 0.0f, 0.0f }; // Hide player

        // Stop movement sliding
        if (m_player->GetMovement()) {
            m_player->GetMovement()->SetVelocity({ 0.0f, 0.0f, 0.0f });
        }
    }

    if (m_navi)
    {
        if (auto* normalPhase = dynamic_cast<BossPhase01*>(m_navi->GetCurrentPhase())) {
            normalPhase->SetAIEnabled(false);
        }
        else if (auto* wkPhase = dynamic_cast<BossPhase02*>(m_navi->GetCurrentPhase())) {
            wkPhase->SetAIEnabled(false);
        }
    }
}

void SceneBoss::RenderScene(float elapsedTime, Camera* camera, bool isTransparentWindow)
{

    if (!camera) return;

    auto dc = Graphics::Instance().GetDeviceContext();
    auto modelRenderer = Graphics::Instance().GetModelRenderer();

    RenderContext rc{ dc, Graphics::Instance().GetRenderState(), camera, nullptr };
    rc.isTransparentWindow = isTransparentWindow;
    // --- 1. DETEKSI KAMERA SAYAP (CAMERA FILTERING) ---
    bool isWingCamera = false;
    if (m_navi) {
        if (auto* wkPhase = dynamic_cast<BossPhase02*>(m_navi->GetCurrentPhase())) {
            isWingCamera = (camera == wkPhase->GetFXCamera());
        }
    }

    // A. RENDER PELURU (Selalu di semua jendela agar terlihat menembus layar)
    if (m_player) {
        m_player->RenderProjectiles(modelRenderer);
    }

    // B. RENDER TUBUH PEMAIN (Kondisional)
    if (m_player) {
        // Logika bawaan:
        bool shouldRenderHere = m_playerWindowTransparent ? isWingCamera : !isWingCamera;

        // [FIX MUTLAK] PAKSA RENDER DI SEMUA KAMERA SAAT WINDOWKILL
        if (m_navi && dynamic_cast<BossPhase02*>(m_navi->GetCurrentPhase())) {
            shouldRenderHere = true;
        }

        if (shouldRenderHere) {
            const XMFLOAT3 pPos = m_player->GetPosition();

            // [MODIFIKASI] Bypass pengecekan Sphere jika sedang dikurung!
            bool isInView = camera->CheckSphere(pPos.x, pPos.y, pPos.z, 1.5f);

            auto* wkPhase = dynamic_cast<BossPhase02*>(m_navi->GetCurrentPhase());
            if (wkPhase && wkPhase->IsPlayerCaged()) {
                isInView = true; // Selalu render player selama dia di dalam kandang!
            }

            if (isInView) {
                m_player->Render(modelRenderer);
            }
        }
    }

    // C. RENDER MUSUH & ITEM — tetap skip di wing camera
    if (!isWingCamera) {
        if (m_enemyManager) m_enemyManager->Render(modelRenderer);
        if (m_itemManager) m_itemManager->Render(modelRenderer);
    }

    // --- 3. RENDER NAVI (termasuk wajah & background) ---
    // Saat mati, paksa render di main cam meski isWingCamera false sudah benar
    if (m_navi) m_navi->Render(dc, camera);

    modelRenderer->Render(rc);
    EffectManager::Instance().Render(camera);

}


// =========================================================
// GUI
// =========================================================

    void SceneBoss::DrawGUI()
    {
/*        return;*/ // Add this — skips all ImGui rendering for release

        if (m_isPendingSceneChange) return;
        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
        ImGui::SetNextWindowSize(m_debugPanelSize, ImGuiCond_FirstUseEver);

    ImGui::Begin("WINDOWKILL MASTER CONTROL", nullptr,
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove);

    m_debugPanelSize = ImGui::GetWindowSize();

        if (ImGui::BeginTabBar("MasterControlTabs"))
        {
            // ---------------------------------------------------------
            // TAB 1: SYSTEM & ENGINE
            // ---------------------------------------------------------
            if (ImGui::BeginTabItem("System & Engine"))
            {
                int activeWins = 0;
                int sleepingWins = 0;
                for (const auto& tw : m_windowSystem->GetWindows()) {
                    if (tw->isActive) activeWins++;
                    else sleepingWins++;
                }

            ImGui::Separator();
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Active OS Windows: %d", activeWins);
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Pooled (Sleeping) Windows: %d", sleepingWins);

            if (ImGui::CollapsingHeader("System Metrics & Time", ImGuiTreeNodeFlags_DefaultOpen))
            {
                const float fps = ImGui::GetIO().Framerate;
                ImVec4 fpsColor = (fps < 40.0f) ? ImVec4(1, 0, 0, 1) : (fps < 50.0f) ? ImVec4(1, 1, 0, 1) : ImVec4(0, 1, 0, 1);
                ImGui::TextColored(fpsColor, "FPS: %.1f (%.2f ms) [cap: 60]", fps, 1000.0f / fps);

                static float s_frametimes[90] = {};
                static int   s_offset = 0;
                s_frametimes[s_offset] = 1000.0f / fps;
                s_offset = (s_offset + 1) % IM_ARRAYSIZE(s_frametimes);
                ImGui::PlotLines("Frametime", s_frametimes, IM_ARRAYSIZE(s_frametimes), s_offset, nullptr, 0.0f, 33.0f, ImVec2(0, 50));

                ImGui::Separator();
                ImGui::SliderFloat("Time Scale", &m_timeScale, 0.1f, 3.0f, "%.1fx");
                if (ImGui::Button("Reset Time (1.0x)")) m_timeScale = 1.0f;
            }

            if (ImGui::CollapsingHeader("Window Tracking Config"))
            {
                if (ImGui::Checkbox("[All] Toggle Topmost (triggers reset)", &m_topmostEnabled)) {
                    WindowManager::Instance().SetTopmost(m_topmostEnabled);
                    ResetEverything();
                }

                if (m_navi) {
                    if (auto* wkPhase = dynamic_cast<BossPhase02*>(m_navi->GetCurrentPhase())) {
                        bool fxClickthrough = wkPhase->IsClickThrough();
                        if (ImGui::Checkbox("[ALL] Toggle Clickthrough", &fxClickthrough)) {
                            wkPhase->SetClickThrough(fxClickthrough);
                            AddLog(fxClickthrough ? "FX Window: Click-through Enabled" : "FX Window: Click-through Disabled");
                        }
                    }
                }

                if (ImGui::Checkbox("[Player] Toggle Transparent", &m_playerWindowTransparent)) {
                    if (m_playerWindowTransparent) {
                        m_windowSystem->RemoveTrackedWindow("player");
                        AddLog("Player Window: Removed (Rendering to SFX Layer)");
                    }
                    else {
                        InitializeSubWindows();
                        AddLog("Player Window: Restored");
                    }
                    WindowManager::Instance().EnforceWindowPriorities();
                }

                ImGui::Checkbox("[ImGui] Sync size to main window", &m_autoSyncMainWindow);
                ImGui::Separator();
                ImGui::Text("Active Windows: %zu", m_windowSystem->GetWindows().size());

                if (ImGui::Button("Spawn Dummy Window", ImVec2(-1.0f, 30.0f))) SpawnDebugWindow();
                if (ImGui::Button("Spawn Transparent (Hollow)", ImVec2(-1.0f, 30.0f))) SpawnTransparentWindow(0.0f, "Hollow");
                if (ImGui::Button("Spawn Transparent (Solid)", ImVec2(-1.0f, 30.0f))) SpawnTransparentWindow(1.0f / 255.0f, "Solid");

                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.1f, 0.1f, 1.0f));
                if (ImGui::Button("Close All Sub Windows", ImVec2(-1.0f, 30.0f))) {
                    m_windowSystem->ClearAll();
                    InitializeSubWindows();
                    m_spawnCount = 0;
                }
                if (ImGui::Button("HARD RESET", ImVec2(-1.0f, 40.0f))) ResetEverything();
                ImGui::PopStyleColor();
            }

            if (ImGui::CollapsingHeader("World & Camera"))
            {
                ImGui::Checkbox("Show 3D Grid", &m_showGrid);
                ImGui::Checkbox("Show Hitboxes/Hurtboxes", &m_showHitboxes);

                if (m_player) {
                    ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "[Player]");
                    ImGui::Text("Loc: X:%.2f, Y:%.2f, Z:%.2f", m_player->GetPosition().x, m_player->GetPosition().y, m_player->GetPosition().z);
                }

                ImGui::Separator();
                ImGui::SliderFloat("Combat Radius", &m_combatRadius, 5.0f, 50.0f);
                ImGui::SliderFloat("Max Zoom Amount", &m_maxZoomIn, -15.0f, 0.0f);
            }
            ImGui::EndTabItem();
        }

            // ---------------------------------------------------------
            // TAB 2: PHASE & VISUALS
            // ---------------------------------------------------------
            if (m_navi && ImGui::BeginTabItem("Phase & Visuals"))
            {
                if (ImGui::Button("TEST VFX")) {
                    auto handle = EffectManager::Instance().Play("Data/Effect/LASER.efk", { 0.0f, 0.0f, 0.0f }, 1.0f);
                    float rotX = DirectX::XMConvertToRadians(90.0f);
                    EffectManager::Instance().SetRotation(handle, { rotX, 0.0f, 0.0f });
                }

            if (ImGui::CollapsingHeader("Core Face Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
                float speed = m_navi->GetCoreBreathSpeed();
                float intensity = m_navi->GetCoreBreathIntensity();
                bool changed = false;
                changed |= ImGui::SliderFloat("Breath Speed", &speed, 0.1f, 20.0f);
                changed |= ImGui::SliderFloat("Breath Intensity", &intensity, 0.0f, 200.0f);
                if (changed) m_navi->SetCoreBreathParams(speed, intensity);
            }

            if (auto* normalPhase = dynamic_cast<BossPhase01*>(m_navi->GetCurrentPhase()))
            {
                ImGui::Separator();
                ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "CURRENT PHASE: 1 (NORMAL MODE)");

                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.1f, 0.1f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 0.2f, 0.2f, 1.0f));
                if (ImGui::Button("TRIGGER PHASE 2 (WINDOWKILL) !!!", ImVec2(-1.0f, 50.0f))) {
                    m_navi->ChangePhase(std::make_unique<BossPhase02>(m_player.get()));
                    m_playerWindowTransparent = true;
                    AddLog("Transitioning to Windowkill Phase...");
                }
                ImGui::PopStyleColor(2);
            }
            else if (auto* wkPhase = dynamic_cast<BossPhase02*>(m_navi->GetCurrentPhase()))
            {
                ImGui::Separator();
                ImGui::TextColored(ImVec4(0.0f, 1.0f, 1.0f, 1.0f), "CURRENT PHASE: 2 (WINDOWKILL)");

                if (ImGui::CollapsingHeader("Wing Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
                    float wSpeed = wkPhase->GetWingFlapSpeed();
                    float wIntensity = wkPhase->GetWingFlapIntensity();
                    float wOffsetX = wkPhase->GetWingOffsetX();
                    float wOffsetZ = wkPhase->GetWingOffsetZ();

                    if (ImGui::SliderFloat("Flap Speed", &wSpeed, 0.1f, 10.0f)) wkPhase->SetWingFlapParams(wSpeed, wIntensity);
                    if (ImGui::SliderFloat("Flap Intensity", &wIntensity, 0.0f, 2.0f)) wkPhase->SetWingFlapParams(wSpeed, wIntensity);

                    bool offsetChanged = false;
                    offsetChanged |= ImGui::SliderFloat("Spacing (X)", &wOffsetX, 0.0f, 20.0f);
                    offsetChanged |= ImGui::SliderFloat("Vertical (Z)", &wOffsetZ, -20.0f, 20.0f);
                    if (offsetChanged) wkPhase->SetWingOffsets(wOffsetX, wOffsetZ);
                }

                if (ImGui::CollapsingHeader("Wing Animation & Render")) {
                    float p2u = wkPhase->GetPixelToUnit();
                    float gScale = wkPhase->GetWingGlobalScale();
                    if (ImGui::SliderFloat("Pixel to Unit Ratio", &p2u, 1.0f, 100.0f)) wkPhase->SetScalingParams(p2u, gScale);
                    if (ImGui::SliderFloat("Global Wing Scale", &gScale, 0.1f, 5.0f)) wkPhase->SetScalingParams(p2u, gScale);

                    int currentSeed = static_cast<int>(wkPhase->GetWingSeed());
                    if (ImGui::InputInt("Wing Seed", &currentSeed)) wkPhase->SetWingSeed(static_cast<unsigned int>(currentSeed));
                    if (ImGui::Button("Randomize Seed (Gacha!)", ImVec2(-1.0f, 30.0f))) {
                        std::random_device rd; wkPhase->SetWingSeed(rd());
                    }

                    float pDur = wkPhase->GetPopDuration();
                    float sDur = wkPhase->GetSpawnDuration();
                    float sChaos = wkPhase->GetSpawnChaos();
                    bool animChanged = false;
                    animChanged |= ImGui::SliderFloat("Pop Duration", &pDur, 0.01f, 1.0f);
                    animChanged |= ImGui::SliderFloat("Spawn Duration", &sDur, 0.1f, 5.0f);
                    animChanged |= ImGui::SliderFloat("Spawn Chaos", &sChaos, 0.0f, 2.0f);
                    if (animChanged) wkPhase->SetSpawnParams(pDur, sDur, sChaos);
                    if (ImGui::Button("Re-play Expand Animation", ImVec2(-1.0f, 30.0f))) wkPhase->ReplayAnimation();
                }
            }

            ImGui::PushID("GlitchMatrixFaceSection");
            if (ImGui::CollapsingHeader("Glitch Matrix Face Configuration"))
            {
                auto& fp = m_navi->GetFaceParams();
                ImGui::Checkbox("Enable Matrix Animation (Glitch)", &fp.enableGlitch);
                ImGui::SliderFloat("Total Render Size", &fp.faceTotalSize, 1.0f, 15.0f, "%.1f units");
                ImGui::Separator();
                ImGui::TextColored(ImVec4(0.0f, 1.0f, 1.0f, 1.0f), "[ Refresh Rate Speeds ]");
                ImGui::SliderFloat("Min Swap Delay", &fp.minInterval, 0.01f, 1.0f, "%.2f sec");
                ImGui::SliderFloat("Max Swap Delay", &fp.maxInterval, 0.01f, 2.0f, "%.2f sec");
                if (fp.minInterval > fp.maxInterval) fp.minInterval = fp.maxInterval;
                ImGui::Separator();
                ImGui::TextColored(ImVec4(1.0f, 0.5f, 1.0f, 1.0f), "[ Artifact Mutation ]");
                ImGui::SliderFloat("2x2 Mega Chunk Chance", &fp.chance2x2, 0.0f, 100.0f, "%.1f %%");
                ImGui::SliderFloat("Instant Flicker Chance", &fp.flickerChance, 0.0f, 15.0f, "%.2f %%");
                ImGui::SliderFloat("Color Glitch Chance", &fp.colorGlitchChance, 0.0f, 100.0f, "%.1f %%");
            }
            ImGui::PopID();
            ImGui::EndTabItem();
        }

            // ---------------------------------------------------------
                        // TAB 3: BOSS CONFIG
                        // ---------------------------------------------------------
            if (m_navi && ImGui::BeginTabItem("Boss Config"))
            {
                ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "=== BOSS MASTER CONTROLS ===");

                // 1. Health Bar Player
                float pHP = m_player->GetHP();
                float pMaxHP = m_player->GetMaxHP();
                float pHpProgress = (pMaxHP > 0.0f) ? (pHP / pMaxHP) : 0.0f;
                ImGui::Text("Player HP: %.1f / %.1f", pHP, pMaxHP);
                ImVec4 pBarColor = { (1.0f - pHpProgress), pHpProgress, 0.0f, 1.0f };
                ImGui::PushStyleColor(ImGuiCol_PlotHistogram, pBarColor);
                ImGui::ProgressBar(pHpProgress, ImVec2(-1.0f, 18.0f));
                ImGui::PopStyleColor();

                // 2. Health Bar Boss
                if (m_navi) {
                    if (auto* normalPhase = dynamic_cast<BossPhase01*>(m_navi->GetCurrentPhase())) {
                        int bHP = normalPhase->GetHP();
                        int bMaxHP = normalPhase->GetMaxHP();
                        float bHpProgress = (bMaxHP > 0) ? (float)bHP / bMaxHP : 0.0f;
                        ImGui::Text("Boss HP (Tracked): %d / %d", bHP, bMaxHP);
                        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.8f, 0.1f, 0.1f, 1.0f));
                        ImGui::ProgressBar(bHpProgress, ImVec2(-1.0f, 18.0f));
                        ImGui::PopStyleColor();
                    }
                    else {
                        if (auto* wkPhase = dynamic_cast<BossPhase02*>(m_navi->GetCurrentPhase())) {
                            int bHP = wkPhase->GetHP();
                            int bMaxHP = wkPhase->GetMaxHP();
                            float bHpProgress = (bMaxHP > 0) ? (float)bHP / bMaxHP : 0.0f;
                            ImGui::Text("Boss HP (Tracked): %d / %d", bHP, bMaxHP);
                            ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.8f, 0.1f, 0.1f, 1.0f));
                            ImGui::ProgressBar(bHpProgress, ImVec2(-1.0f, 18.0f));
                            ImGui::PopStyleColor();
                        }
                        else {
                            ImGui::Text("Boss HP (Tracked): [Windowkill Phase Active]");
                        }
                    }
                }
                ImGui::Separator();

                // --- TOMBOL HEAL & RESPAWN BOSS ---
                if (ImGui::Button("Heal Boss to Full", ImVec2(180.0f, 30.0f))) {
                    if (auto* normalPhase = dynamic_cast<BossPhase01*>(m_navi->GetCurrentPhase())) {
                        normalPhase->SetHP(normalPhase->GetMaxHP());
                        AddLog("Boss healed to full HP.");
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("Respawn Boss", ImVec2(180.0f, 30.0f))) {
                    if (auto* normalPhase = dynamic_cast<BossPhase01*>(m_navi->GetCurrentPhase())) {
                        // Hapus logika savedParams, cukup buat ulang Phase 1 yang bersih
                        m_navi->ChangePhase(std::make_unique<BossPhase01>(m_player.get()));
                        AddLog("Boss respawned (Phase 1 Normal).");
                    }
                    else {
                        m_navi->ChangePhase(std::make_unique<BossPhase01>(m_player.get()));
                        m_playerWindowTransparent = false;
                        if (m_player) {
                            m_player->RestoreShootDelay();
                        }
                        AddLog("Boss respawned (Phase 1 Normal).");
                    }
                }
                ImGui::Separator();

                if (auto* normalPhase = dynamic_cast<BossPhase01*>(m_navi->GetCurrentPhase()))
                {
                    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "=== BATTLE STATUS ===");

                    bool aiActive = normalPhase->IsAIEnabled();
                    if (!aiActive) {
                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.1f, 0.6f, 0.1f, 1.0f));
                        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.2f, 0.8f, 0.2f, 1.0f));
                        if (ImGui::Button("START BOSS FIGHT (ENABLE AI)", ImVec2(-1.0f, 50.0f))) {
                            normalPhase->SetAIEnabled(true);
                            // [FIX] Mengambil sfxVolume melalui GetAI()
                            AudioManager::Instance().PlayMusic("Data/Sound/BGM_Boss_Phase_01.wav", 0.05f * AttackParamManager::Instance().GetUltimateParams().sfxVolume, true);
                        }
                        ImGui::PopStyleColor(2);
                    }
                    else {
                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.1f, 0.1f, 1.0f));
                        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 0.2f, 0.2f, 1.0f));
                        if (ImGui::Button("STOP BOSS FIGHT (DISABLE AI)", ImVec2(-1.0f, 50.0f))) {
                            normalPhase->SetAIEnabled(false);
                        }
                        ImGui::PopStyleColor(2);
                    }

                    // Health Bar Boss
                    if (normalPhase->GetHP() > 0) {
                        float hpProgress = (float)normalPhase->GetHP() / normalPhase->GetMaxHP();
                        ImGui::Text("Navi HP: %d / %d", normalPhase->GetHP(), normalPhase->GetMaxHP());
                        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.8f, 0.1f, 0.1f, 1.0f));
                        ImGui::ProgressBar(hpProgress, ImVec2(-1.0f, 20.0f));
                        ImGui::PopStyleColor();
                    }
                    else {
                        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "[ NAVI DEFEATED ]");
                    }

                    ImGui::Separator();

                    if (ImGui::CollapsingHeader("Manual Attack Triggers", ImGuiTreeNodeFlags_DefaultOpen))
                    {
                        if (ImGui::Button("RADIAL BURST", ImVec2(-1.0f, 35.0f))) {
                            normalPhase->AddPooledAttack(std::make_unique<AttackRadial>(AttackParamManager::Instance().GetRadialNormalParams()));
                        }

                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.4f, 0.0f, 1.0f));
                        if (ImGui::Button("RADIAL STREAM (CONTINUOUS)", ImVec2(-1.0f, 35.0f))) {
                            normalPhase->AddPooledAttack(std::make_unique<AttackRadial>(AttackParamManager::Instance().GetRadialContinuousParams()));
                        }
                        ImGui::PopStyleColor();

                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.1f, 0.6f, 0.1f, 1.0f));
                        if (ImGui::Button("TARGETED FAN BURST", ImVec2(-1.0f, 35.0f))) {
                            if (m_player) {
                                DirectX::XMFLOAT3 pPos = m_player->GetPosition();
                                DirectX::XMFLOAT3 bPos = m_navi->GetPosition();
                                float lockedAngle = static_cast<float>(std::atan2(pPos.x - bPos.x, pPos.z - bPos.z));

                                normalPhase->AddPooledAttack(std::make_unique<AttackFan>(AttackParamManager::Instance().GetFanNormalParams(), lockedAngle));
                            }
                        }
                        ImGui::PopStyleColor();

                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.8f, 0.4f, 1.0f));
                        if (ImGui::Button("FAN TRIPLE (TRACKING)", ImVec2(-1.0f, 35.0f))) {
                            if (m_player) {
                                DirectX::XMFLOAT3 pPos = m_player->GetPosition();
                                DirectX::XMFLOAT3 bPos = m_navi->GetPosition();
                                float lockedAngle = static_cast<float>(std::atan2(pPos.x - bPos.x, pPos.z - bPos.z));

                                normalPhase->AddPooledAttack(std::make_unique<AttackFan>(
                                    AttackParamManager::Instance().GetFanContinuousParams(),
                                    lockedAngle,
                                    m_player.get()
                                ));
                            }
                        }
                        ImGui::PopStyleColor();

                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.7f, 0.7f, 1.0f));
                        if (ImGui::Button("GRID WAVE ATTACK", ImVec2(-1.0f, 35.0f))) {
                            normalPhase->AddPooledAttack(std::make_unique<AttackWave>(AttackParamManager::Instance().GetWaveParams()));
                        }
                        ImGui::PopStyleColor();

                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.1f, 0.5f, 0.8f, 1.0f));
                        if (ImGui::Button("GLINTSTONE PHALANX", ImVec2(-1.0f, 35.0f))) {
                            if (m_player) normalPhase->AddPooledAttack(std::make_unique<AttackPhalanx>(AttackParamManager::Instance().GetPhalanxParams(), m_player.get()));
                        }
                        ImGui::PopStyleColor();

                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.1f, 0.6f, 1.0f));
                        if (ImGui::Button("BIJUUDAMA (ULTIMATE)", ImVec2(-1.0f, 35.0f))) {
                            if (m_player) normalPhase->AddPooledAttack(std::make_unique<AttackUltimate>(AttackParamManager::Instance().GetUltimateParams(), m_player.get()));
                        }
                        ImGui::PopStyleColor();

                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
                        if (ImGui::Button("METEOR STRIKE", ImVec2(-1.0f, 35.0f))) {
                            normalPhase->AddPooledAttack(std::make_unique<AttackMeteor>(AttackParamManager::Instance().GetMeteorParams()));
                        }
                        ImGui::PopStyleColor();

                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.5f, 0.5f, 0.0f, 1.0f));
                        if (ImGui::Button("DIRECT TRACKING STREAM", ImVec2(-1.0f, 35.0f))) {
                            if (m_player) {
                                normalPhase->AddPooledAttack(std::make_unique<AttackDirect>(AttackParamManager::Instance().GetDirectParams(), m_player.get()));
                            }
                        }
                        ImGui::PopStyleColor();

                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1.0f, 0.3f, 0.0f, 1.0f));
                        if (ImGui::Button("RAIN (LEFT)", ImVec2(105.0f, 30.0f))) normalPhase->TriggerRain(AttackParamManager::Instance().GetRainParams(), RainMode::VerticalSweep, false); ImGui::SameLine();
                        if (ImGui::Button("RAIN (RIGHT)", ImVec2(105.0f, 30.0f))) normalPhase->TriggerRain(AttackParamManager::Instance().GetRainParams(), RainMode::VerticalSweep, true);

                        // Tambahkan Tombol Baru:
                        if (ImGui::Button("RAIN TARGETED", ImVec2(-1.0f, 30.0f))) normalPhase->TriggerRain(AttackParamManager::Instance().GetRainTargetedParams(), RainMode::Targeted, true);                        
                        ImGui::PopStyleColor();


                    }

                    if (ImGui::CollapsingHeader("Direct Tracking Config")) {
                        auto& p = AttackParamManager::Instance().GetDirectParams();
                        ImGui::SliderInt("Count", &p.count, 1, 50);
                        ImGui::SliderFloat("Spawn Delay", &p.spawnDelay, 0.05f, 1.0f);
                        ImGui::SliderFloat("Speed", &p.speed, 10.0f, 100.0f);
                    }

                    if (ImGui::CollapsingHeader("Radial Burst (Triple)")) {
                        auto& p = AttackParamManager::Instance().GetRadialNormalParams();
                        ImGui::ColorEdit4("Color", (float*)&p.color);
                        ImGui::SliderFloat("Speed", &p.speed, 1.0f, 100.0f);
                        ImGui::SliderInt("Count", &p.count, 4, 128);
                        ImGui::SliderFloat("Delay", &p.burstDelay, 0.01f, 1.0f);
                        ImGui::SliderInt("Burst Count", &p.burstCount, 1, 20);
                        ImGui::SliderInt("Damage", &p.damage, 1, 100);
                    }

                    if (ImGui::CollapsingHeader("Targeted Fan Burst")) {
                        auto& p = AttackParamManager::Instance().GetFanNormalParams();
                        ImGui::SliderFloat("Speed", &p.speed, 1.0f, 100.0f);
                        ImGui::SliderInt("Lines", &p.rows, 1, 10);
                        ImGui::SliderInt("Waves", &p.waves, 1, 10);
                        ImGui::SliderFloat("Spread", &p.spreadAngle, 0.05f, 0.5f);
                        ImGui::SliderInt("Damage", &p.damage, 1, 100);
                    }

                    if (ImGui::CollapsingHeader("Glintstone Phalanx")) {
                        auto& p = AttackParamManager::Instance().GetPhalanxParams();
                        ImGui::SliderInt("Count", &p.count, 3, 10);
                        ImGui::SliderFloat("Flight Speed", &p.speed, 10.0f, 80.0f);
                        ImGui::SliderInt("Damage", &p.damage, 1, 150);
                    }

                    if (ImGui::CollapsingHeader("Grid Wave Attack Config")) {
                        auto& p = AttackParamManager::Instance().GetWaveParams();
                        ImGui::SliderInt("Total Waves", &p.waves, 1, 20);
                        ImGui::SliderFloat("Wave Delay", &p.waveDelay, 0.1f, 3.0f);
                        ImGui::SliderFloat("Bullet Speed", &p.speed, 5.0f, 60.0f);
                        ImGui::SliderFloat("Track Spacing", &p.trackSpacing, 1.0f, 10.0f);
                        ImGui::SliderFloat("Start Z (Bottom)", &p.startZ, -30.0f, 0.0f);
                    }

                    if (ImGui::CollapsingHeader("Bijuudama (Ultimate)")) {
                        auto& p = AttackParamManager::Instance().GetUltimateParams();
                        ImGui::ColorEdit4("Color", (float*)&p.ballColor);
                        ImGui::SliderFloat("Laser Duration", &p.laserDuration, 0.5f, 4.0f);
                        ImGui::SliderFloat("Shoot Speed", &p.shootSpeed, 10.0f, 120.0f);
                    }

                    if (ImGui::CollapsingHeader("Meteor Strike Config")) {
                        auto& p = AttackParamManager::Instance().GetMeteorParams();
                        ImGui::SliderInt("Count", &p.count, 1, 20);
                        ImGui::SliderFloat("Spawn Delay", &p.spawnDelay, 0.05f, 2.0f);
                        ImGui::SliderFloat("Base Speed", &p.speed, 10.0f, 80.0f);
                        ImGui::SliderFloat("Speed Variance (+/-)", &p.speedVariance, 0.0f, 40.0f); //                        
                        ImGui::SliderFloat("Visual Scale", &p.visualScale, 0.5f, 10.0f);
                        ImGui::SliderFloat("Spread Offset", &p.spreadOffset, 0.0f, 10.0f);

                        ImGui::Separator();
                        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "[ Anchor Path ]");
                        ImGui::SliderFloat("Start X", &p.startX, -50.0f, 50.0f);
                        ImGui::SliderFloat("Start Z", &p.startZ, -50.0f, 50.0f);
                        ImGui::SliderFloat("Target X", &p.targetX, -50.0f, 50.0f);
                        ImGui::SliderFloat("Target Z", &p.targetZ, -50.0f, 50.0f);
                    }

                    if (ImGui::CollapsingHeader("Asgore Rain (Area Denial)")) {
                        auto& p = AttackParamManager::Instance().GetRainParams();
                        ImGui::SliderFloat("Min Fall Speed", &p.minSpeed, 10.0f, 150.0f);
                        ImGui::SliderFloat("Max Fall Speed", &p.maxSpeed, 10.0f, 150.0f);
                        ImGui::SliderFloat("Active Duration", &p.activeDuration, 0.5f, 10.0f);
                        ImGui::SliderFloat("Damage", &p.damage, 0.0f, 50.0f);
                    }

                    if (ImGui::CollapsingHeader("Targeted Rain (Player Tracking)")) {
                        auto& p = AttackParamManager::Instance().GetRainTargetedParams();

                        ImGui::TextColored(ImVec4(0.0f, 1.0f, 1.0f, 1.0f), "[ Drop Pattern ]");
                        ImGui::SliderFloat("Targeted Min Speed", &p.minSpeed, 10.0f, 150.0f);
                        ImGui::SliderFloat("Targeted Max Speed", &p.maxSpeed, 10.0f, 150.0f);

                        ImGui::Separator();
                        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "[ Zone Size & Timing ]");
                        ImGui::SliderFloat("Warning Duration", &p.warningDuration, 0.1f, 3.0f);
                        ImGui::SliderFloat("Active Duration", &p.activeDuration, 0.1f, 5.0f);
                        ImGui::SliderFloat("Zone Width (X)", &p.width, 2.0f, 50.0f);
                        ImGui::SliderFloat("Zone Depth (Z)", &p.depth, 10.0f, 80.0f); // 45.0 adalah full screen

                        ImGui::Separator();
                        ImGui::TextColored(ImVec4(1.0f, 0.2f, 0.8f, 1.0f), "[ Burst Logic ]");
                        ImGui::SliderInt("Trigger Count", &p.triggerCount, 1, 10);
                        ImGui::SliderFloat("Trigger Delay", &p.triggerDelay, 0.1f, 3.0f);
                        ImGui::SliderFloat("Targeted Damage", &p.damage, 0.0f, 50.0f);
                    }
                }

                else if (auto* wkPhase = dynamic_cast<BossPhase02*>(m_navi->GetCurrentPhase()))
                {
                    ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "--- PHASE 2: WINDOWKILL ATTACKS ---");                ImGui::Separator();

                if (ImGui::CollapsingHeader("Windowkill AI & Damage", ImGuiTreeNodeFlags_DefaultOpen))
                {
                    bool aiActive = wkPhase->IsAIEnabled();
                    ImGui::Text("AI Status: %s", aiActive ? "ENABLED" : "DISABLED");
                    if (aiActive) {
                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.1f, 0.1f, 1.0f));
                        if (ImGui::Button("DISABLE WINDOWKILL AI", ImVec2(-1.0f, 34.0f))) {
                            wkPhase->SetAIEnabled(false);
                            AddLog("Windowkill AI disabled.");
                        }
                        ImGui::PopStyleColor();
                    }
                    else {
                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.1f, 0.6f, 0.1f, 1.0f));
                        if (ImGui::Button("ENABLE WINDOWKILL AI", ImVec2(-1.0f, 34.0f))) {
                            wkPhase->SetAIEnabled(true);
                            AddLog("Windowkill AI enabled.");
                        }
                        ImGui::PopStyleColor();
                    }

                    if (wkPhase->IsPlayerCaged()) {
                        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "AI will stay gated until the cage breaks.");
                    }

                        ImGui::Separator();
                        ImGui::PushID("WindowkillDamage");

                        // [FIX] Mengambil parameter dari struct baru
                        auto& bp = wkPhase->GetBlasterParams();
                        auto& bounce = wkPhase->GetBouncingParams();
                        auto& boom = wkPhase->GetBoomerangParams();
                        auto& spear = wkPhase->GetUndyneParams();

                        ImGui::SliderInt("Orbital Laser Damage", &bp.beamDamage, 1, 200);
                        ImGui::SliderInt("Targeted Laser Damage", &bp.beamDamage, 1, 200); // [FIX] Menggunakan bp karena digabung
                        ImGui::SliderInt("Bouncing Window Damage", &bounce.damage, 1, 200);
                        ImGui::SliderInt("Boomerang Damage", &boom.damage, 1, 200);
                        ImGui::SliderInt("Undyne Spear Damage", &spear.damage, 1, 500);
                        ImGui::PopID();
                    }

                if (ImGui::CollapsingHeader("System Metrics & Time", ImGuiTreeNodeFlags_DefaultOpen))
                {
                    const float fps = ImGui::GetIO().Framerate;
                    ImVec4 fpsColor = (fps < 40.0f) ? ImVec4(1, 0, 0, 1) : (fps < 50.0f) ? ImVec4(1, 1, 0, 1) : ImVec4(0, 1, 0, 1);
                    ImGui::TextColored(fpsColor, "FPS: %.1f (%.2f ms) [cap: 60]", fps, 1000.0f / fps);

                    static float s_frametimes[90] = {};
                    static int   s_offset = 0;
                    s_frametimes[s_offset] = 1000.0f / fps;
                    s_offset = (s_offset + 1) % IM_ARRAYSIZE(s_frametimes);
                    ImGui::PlotLines("Frametime", s_frametimes, IM_ARRAYSIZE(s_frametimes), s_offset, nullptr, 0.0f, 33.0f, ImVec2(0, 50));

                    int activeWins = 0;
                    int sleepingWins = 0;
                    for (const auto& tw : m_windowSystem->GetWindows()) {
                        if (tw->isActive) activeWins++;
                        else sleepingWins++;
                    }

                    ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Active OS Windows: %d", activeWins);
                    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Pooled (Sleeping) Windows: %d", sleepingWins);

                        ImGui::Separator();
                        ImGui::SliderFloat("Time Scale", &m_timeScale, 0.1f, 3.0f, "%.1fx");
                        if (ImGui::Button("Reset Time (1.0x)")) m_timeScale = 1.0f;
                    }

                    ImGui::PushID("OrbitalLaserBlock");
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.1f, 0.1f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 0.2f, 0.2f, 1.0f));

                    // [FIX MUTLAK] MENGGUNAKAN ADDATTACK DARI FSM
                    if (ImGui::Button("FIRE ORBITAL LASER (RANDOM)", ImVec2(-1.0f, 40.0f))) {
                        wkPhase->AddAttack(std::make_unique<AttackBlasters>(wkPhase->GetBlasterParams(), false));
                    }

                    if (ImGui::Button("FIRE ORBITAL LASER (TARGETED)", ImVec2(-1.0f, 40.0f))) {
                        float playerX = m_player ? m_player->GetPosition().x : 0.0f;
                        wkPhase->AddAttack(std::make_unique<AttackBlasters>(wkPhase->GetBlasterParams(), true, playerX));
                        AddLog("Targeted Orbital Blaster Triggered!");
                    }
                    ImGui::PopStyleColor(2);

                    if (ImGui::CollapsingHeader("Orbital Laser Configuration"))
                    {
                        auto& bp = wkPhase->GetBlasterParams();
                        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "[ Cannon Head ]");
                        ImGui::SliderFloat("OS Window Size", &bp.cannonWindowSize, 100.0f, 800.0f);
                        ImGui::SliderFloat("Visual Scale 3D", &bp.cannonVisualScale, 0.1f, 10.0f);
                        // [FIX] cannonHitboxRadius dan cannonShakeIntensity dihapus karena sdh tidak ada di struct baru

                        ImGui::Separator();
                        ImGui::TextColored(ImVec4(0.0f, 1.0f, 1.0f, 1.0f), "[ Laser Beam (2D) ]");
                        ImGui::SliderFloat("Visual Width", &bp.beamVisualWidth, 1.0f, 20.0f);
                        // [FIX] beamHitboxWidth dihapus
                        ImGui::SliderFloat("Max Length", &bp.beamMaxLength, 50.0f, 500.0f);
                        ImGui::SliderFloat("Grow Speed", &bp.beamGrowSpeed, 1.0f, 100.0f);
                        ImGui::SliderFloat("Slide Speed", &bp.beamSlideSpeed, 1.0f, 50.0f);
                        ImGui::SliderInt("Damage per Tick", &bp.beamDamage, 1, 100);

                        ImGui::Separator();
                        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "[ Attack Timing & Pattern ]");
                        ImGui::SliderInt("Spawn Count", &bp.spawnCount, 1, 15);
                        ImGui::SliderFloat("Spawn Delay", &bp.spawnDelay, 0.0f, 1.0f, "%.2f sec");
                        ImGui::SliderFloat("Spawn Spread (Width)", &bp.spawnSpreadX, 10.0f, 100.0f);
                        ImGui::SliderFloat("Charge Delay (Telegraph)", &bp.chargeDelay, 0.1f, 3.0f, "%.2f sec");
                        ImGui::SliderFloat("Fire Duration", &bp.fireDuration, 0.1f, 3.0f, "%.2f sec");
                    }

                    if (ImGui::CollapsingHeader("Targeted Blaster Config", ImGuiTreeNodeFlags_DefaultOpen)) {
                        auto& tParams = wkPhase->GetBlasterParams(); // [FIX] Disambungkan ke BlasterParams krn struct-nya sama
                        ImGui::SliderInt("Targeted Count", &tParams.spawnCount, 1, 20);
                        ImGui::DragFloat("Targeted Spawn Delay", &tParams.spawnDelay, 0.05f, 0.05f, 2.0f, "%.2f sec");
                        ImGui::DragFloat("Targeted Drop In", &tParams.dropInDuration, 0.05f, 0.1f, 2.0f, "%.2f sec");
                        ImGui::DragFloat("Targeted Charge", &tParams.chargeDelay, 0.05f, 0.1f, 2.0f, "%.2f sec");
                        ImGui::DragFloat("Targeted Fire Dur", &tParams.fireDuration, 0.05f, 0.1f, 3.0f, "%.2f sec");
                        // [FIX] Parameter Z fixed dan Hitbox Width dihapus
                        ImGui::SliderInt("Targeted Damage per Tick", &tParams.beamDamage, 1, 100);
                    }
                    ImGui::PopID();

                ImGui::Separator();

                    ImGui::PushID("BouncingWindowsBlock");
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.1f, 0.1f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 0.2f, 0.2f, 1.0f));

                    // [FIX MUTLAK] MENGGUNAKAN ADDATTACK DARI FSM
                    if (ImGui::Button("FIRE BOUNCING WINDOWS", ImVec2(-1.0f, 40.0f))) {
                        wkPhase->AddAttack(std::make_unique<AttackBouncing>(wkPhase->GetBouncingParams()));
                    }
                    ImGui::PopStyleColor(2);

                if (ImGui::CollapsingHeader("Bouncing Windows Configuration"))
                {
                    auto& bnp = wkPhase->GetBouncingParams();
                    ImGui::TextColored(ImVec4(1.0f, 0.5f, 1.0f, 1.0f), "[ Spawn & Behavior ]");
                    ImGui::SliderInt("Spawn Count", &bnp.spawnCount, 1, 10);
                    ImGui::SliderFloat("Spawn Delay", &bnp.spawnDelay, 0.0f, 1.0f, "%.2f sec");
                    ImGui::SliderFloat("Movement Speed", &bnp.speed, 5.0f, 100.0f);
                    ImGui::SliderInt("Max Bounces", &bnp.maxBounces, 1, 30);
                    ImGui::SliderInt("Impact Damage", &bnp.damage, 1, 50);
                    ImGui::Separator();
                    ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "[ Window & Hitbox ]");
                    ImGui::DragFloat2("OS Window Size (W/H)", (float*)&bnp.windowWidth, 1.0f, 100.0f, 1000.0f);
                    ImGui::SliderFloat("Visual Scale 3D", &bnp.visualScale, 0.1f, 5.0f);
                    ImGui::SliderFloat("Hitbox Radius", &bnp.hitboxRadius, 0.1f, 10.0f);
                }
                ImGui::PopID();

                ImGui::Separator();

                    ImGui::PushID("BoomerangBlock");
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.1f, 0.1f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 0.2f, 0.2f, 1.0f));

                    // [FIX MUTLAK] MENGGUNAKAN ADDATTACK DARI FSM
                    if (ImGui::Button("FIRE BOOMERANG WINDOW", ImVec2(-1.0f, 40.0f))) {
                        wkPhase->AddAttack(std::make_unique<AttackBoomerangs>(wkPhase->GetBoomerangParams()));
                    }
                    ImGui::PopStyleColor(2);

                if (ImGui::CollapsingHeader("Boomerang Configuration"))
                {
                    auto& bmp = wkPhase->GetBoomerangParams();
                    ImGui::TextColored(ImVec4(1.0f, 0.5f, 1.0f, 1.0f), "[ Spawn Pattern ]");
                    ImGui::SliderInt("Spawn Count", &bmp.spawnCount, 1, 20);
                    ImGui::SliderFloat("Spawn Delay", &bmp.spawnDelay, 0.0f, 2.0f, "%.2f sec");
                    ImGui::Checkbox("Spawn Bottom Half Only", &bmp.spawnBottomHalfOnly);
                    ImGui::Separator();
                    ImGui::TextColored(ImVec4(0.0f, 1.0f, 1.0f, 1.0f), "[ Physics & Lerp ]");
                    ImGui::SliderFloat("Boomerang Speed", &bmp.speed, 10.0f, 100.0f);
                    ImGui::SliderFloat("Max Travel Distance", &bmp.maxTravelDistance, 10.0f, 120.0f, "%.1f units");
                    ImGui::SliderFloat("Turn Smoothness", &bmp.turnSpeed, 1.0f, 20.0f, "%.1f");
                    ImGui::SliderInt("Impact Damage", &bmp.damage, 1, 200);
                    ImGui::Separator();
                    ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "[ Window & Hitbox ]");
                    ImGui::SliderFloat("Window Size", &bmp.windowSize, 100.0f, 500.0f);
                    ImGui::SliderFloat("Boomerang Scale", &bmp.visualScale, 0.1f, 20.0f);
                    ImGui::SliderFloat("Hitbox Radius", &bmp.hitboxRadius, 0.1f, 10.0f);
                }
                ImGui::PopID();

                    // [FIX MUTLAK] MENGGUNAKAN ADDATTACK DARI FSM
                    if (ImGui::Button("Trigger Undyne Spear", ImVec2(180.0f, 30.0f))) {
                        wkPhase->AddAttack(std::make_unique<AttackSpears>(wkPhase->GetUndyneParams(), m_player.get()));
                        AddLog("Undyne Spear Attack Triggered!");
                    }

                    if (ImGui::CollapsingHeader("Undyne Spear Config", ImGuiTreeNodeFlags_DefaultOpen)) {
                        auto& params = wkPhase->GetUndyneParams();
                        ImGui::SliderInt("Spear Count", &params.count, 1, 20);
                        ImGui::DragFloat("Spawn Delay", &params.spawnDelay, 0.05f, 0.05f, 2.0f, "%.2f sec");
                        ImGui::DragFloat("Aiming Time", &params.hoverDuration, 0.05f, 0.1f, 3.0f, "%.2f sec");
                        // [FIX] telegraphDuration diganti menjadi hoverDuration di dalam arsitektur baru
                        ImGui::DragFloat("Max Speed", &params.maxSpeed, 1.0f, 10.0f, 300.0f, "%.1f");
                        ImGui::DragFloat("Arc Radius", &params.arcRadius, 0.5f, 5.0f, 100.0f, "%.1f");
                        ImGui::DragFloat("Arc Center X", &params.arcCenterX, 0.5f, -50.0f, 50.0f, "%.1f");
                        ImGui::DragFloat("Arc Center Z", &params.arcCenterZ, 0.5f, -50.0f, 50.0f, "%.1f");
                        ImGui::DragFloat("Arc Min Angle", &params.arcMinAngle, 1.0f, 0.0f, 360.0f, "%.0f deg");
                        ImGui::DragFloat("Arc Max Angle", &params.arcMaxAngle, 1.0f, 0.0f, 360.0f, "%.0f deg");
                        ImGui::SliderInt("Spear Damage", &params.damage, 1, 500);
                    }
                }

                ImGui::EndTabItem(); // <--- Menutup tab Boss Config dengan sempurna
            }

            // ---------------------------------------------------------
            // TAB 4: PLAYER CONFIG
            // ---------------------------------------------------------
            if (m_player && ImGui::BeginTabItem("Player Config"))
            {
                // --- 2 HEALTHBAR TRACKER ---
                ImGui::TextColored(ImVec4(0.0f, 1.0f, 1.0f, 1.0f), "=== MASTER HEALTH STATUS ===");

            // 1. Health Bar Player
                float pHP = m_player->GetHP();
                float pMaxHP = m_player->GetMaxHP();
                float pHpProgress = (pMaxHP > 0.0f) ? (pHP / pMaxHP) : 0.0f;
                ImVec4 pBarColor = { (1.0f - pHpProgress), pHpProgress, 0.0f, 1.0f };
                ImGui::Text("Player HP: %.1f / %.1f", pHP, pMaxHP);
                ImGui::PushStyleColor(ImGuiCol_PlotHistogram, pBarColor);
                ImGui::ProgressBar(pHpProgress, ImVec2(-1.0f, 18.0f));
                ImGui::PopStyleColor();
                // 2. Health Bar Boss
                if (m_navi) {
                    if (auto* normalPhase = dynamic_cast<BossPhase01*>(m_navi->GetCurrentPhase())) {
                        int bHP = normalPhase->GetHP();
                        int bMaxHP = normalPhase->GetMaxHP();
                        float bHpProgress = (bMaxHP > 0) ? (float)bHP / bMaxHP : 0.0f;
                        ImGui::Text("Boss HP (Tracked): %d / %d", bHP, bMaxHP);
                        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.8f, 0.1f, 0.1f, 1.0f));
                        ImGui::ProgressBar(bHpProgress, ImVec2(-1.0f, 18.0f));
                        ImGui::PopStyleColor();
                    }
                    else {
                        if (auto* wkPhase = dynamic_cast<BossPhase02*>(m_navi->GetCurrentPhase())) {
                            int bHP = wkPhase->GetHP();
                            int bMaxHP = wkPhase->GetMaxHP();
                            float bHpProgress = (bMaxHP > 0) ? (float)bHP / bMaxHP : 0.0f;
                            ImGui::Text("Boss HP (Tracked): %d / %d", bHP, bMaxHP);
                            ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.8f, 0.1f, 0.1f, 1.0f));
                            ImGui::ProgressBar(bHpProgress, ImVec2(-1.0f, 18.0f));
                            ImGui::PopStyleColor();
                        }
                        else {
                            ImGui::Text("Boss HP (Tracked): [Windowkill Phase Active]");
                        }
                    }
                }
                ImGui::Separator();

            // --- TOMBOL HEAL & RESPAWN PLAYER ---
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "[ Quick Actions ]");
            if (ImGui::Button("Heal Player to Full", ImVec2(180.0f, 30.0f))) {
                m_player->SetMaxHP(m_player->GetMaxHP());
                AddLog("Player healed to full HP.");
            }
            ImGui::SameLine();
            if (ImGui::Button("Respawn Player", ImVec2(180.0f, 30.0f))) {
                m_player->SetMaxHP(m_player->GetMaxHP());
                m_player->scale = { 1.0f, 1.0f, 1.0f };
                m_player->SetPosition(0.0f, 0.0f, -8.0f);

                    m_player->SetInputEnabled(true);
                    m_player->SetAimLocked(false);
                    if (m_player->GetStateMachine()) {
                        m_player->GetStateMachine()->Initialize(std::make_unique<PlayerIdle>(), m_player.get());
                    }

                AddLog("Player resurrected, input unlocked, and state reset to Idle.");
            }
            ImGui::Separator();

                m_player->DrawDebugGUI();

            ImGui::EndTabItem();
        }

        // ---------------------------------------------------------
        // TAB 5: TERMINAL
        // ---------------------------------------------------------
        if (ImGui::BeginTabItem("Terminal"))
        {
            ImGui::BeginChild("LogRegion", ImVec2(0.0f, 0.0f), true, ImGuiWindowFlags_AlwaysVerticalScrollbar);
            for (const auto& log : m_debugLogs) ImGui::TextUnformatted(log.c_str());
            if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) ImGui::SetScrollHereY(1.0f);
            ImGui::EndChild();
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::End();

}

void SceneBoss::OnResize(int /*width*/, int /*height*/)
{
    // Intentionally empty: off-center projection is recalculated per sub-window
    // every frame inside WindowTrackingSystem::UpdateOffCenterProjection.
}

// =========================================================
// DEBUG / SYSTEM HELPERS
// =========================================================

void SceneBoss::AddLog(const std::string& message)
{
    m_debugLogs.push_back(message);
    if (m_debugLogs.size() > 50)
        m_debugLogs.erase(m_debugLogs.begin());
}

    void SceneBoss::ResetLevel()
    {
        // Reset Player State (Safe check prevents crashes)
        if (m_player)
        {
            m_player->SetPosition(0.0f, 0.0f, -8.0f);
            m_player->GetMovement()->SetVelocity({ 0.0f, 0.0f, 0.0f });
            m_player->SetMaxHP(m_player->GetMaxHP());
            m_player->SetInputEnabled(false);
            m_player->scale = { 1.0f, 1.0f, 1.0f };

            if (m_player->GetStateMachine()) {
                m_player->GetStateMachine()->ChangeState(m_player.get(), std::make_unique<PlayerIdle>());
            }

            m_player->GetProjectiles().clear();
            m_player->RestoreShootDelay();
            m_player->SetAimLocked(false);
        }

        // Reset Boss 
        if (m_navi)
        {
            if (auto* normalPhase = dynamic_cast<BossPhase01*>(m_navi->GetCurrentPhase()))
            {
                normalPhase->SetHP(normalPhase->GetMaxHP());
                m_playerWindowTransparent = false; // Normal mode = solid player
            }
            else if (auto* wkPhase = dynamic_cast<BossPhase02*>(m_navi->GetCurrentPhase()))
            {
                wkPhase->SetHP(wkPhase->GetMaxHP());
                m_playerWindowTransparent = true;  // Windowkill mode = transparent player
            }
        }

        // Clean up the Windowkill environment
        m_timeScale = 1.0f;
        WindowShatterManager::Instance().Clear();

        // Smart Camera Reset (Instantly snaps during the black screen)
        CameraController::Instance().SetDynamicZoomOffset(0.0f);
        float unifiedHeight = m_windowSystem ? m_windowSystem->GetUnifiedCameraHeight() : 18.0f;

        CameraController::Instance().SetFixedSetting(DirectX::XMFLOAT3(0.0f, unifiedHeight, 0.0f));
        CameraController::Instance().SetTarget({ 0.0f, 0.0f, 0.0f });

        // Force the camera math to finish instantly
        for (int i = 0; i < 60; ++i)
        {
            CameraController::Instance().Update(0.016f);
        }
    }

    void SceneBoss::ResetEverything()
    {
        CameraController::Instance().ClearCamera();

    // 1. Destroy everything (Navi harus hancur sebelum WindowSystem)
    m_navi.reset();
    m_collisionManager.reset();
    m_enemyManager.reset();
    m_itemManager.reset();
    m_stage.reset();
    WindowShatterManager::Instance().Clear();
    m_player->RestoreShootDelay();


    if (m_windowSystem) m_windowSystem->ClearAll();
    m_player.reset();

    // 2. Rebuild PhysX
    m_defaultMaterial.reset();
    m_controllerManager.reset();
    m_scene.reset();
    m_dispatcher.reset();
    m_physics.reset();
    m_foundation.reset();
    InitializePhysics();

    // 3. Rebuild Window System & Camera
    m_windowSystem = std::make_unique<WindowTrackingSystem>();
    m_windowSystem->SetPixelToUnitRatio(k_pixelToUnitRatio);
    m_windowSystem->SetFOV(k_fov);

    const float unifiedHeight = m_windowSystem->GetUnifiedCameraHeight();
    m_mainCamera = std::make_shared<Camera>();
    m_mainCamera->SetPerspectiveFov(XMConvertToRadians(k_fov), 1920.0f / 1080.0f, k_camNear, k_camFar);
    m_mainCamera->SetPosition(0.0f, unifiedHeight, 0.0f);
    m_mainCamera->LookAt({ 0.0f, 0.0f, 0.0f });

    CameraController::Instance().SetActiveCamera(m_mainCamera);
    CameraController::Instance().SetControlMode(CameraControlMode::FixedStatic);
    CameraController::Instance().SetFixedSetting(XMFLOAT3(0.0f, unifiedHeight, 0.0f));

    // 4. Rebuild Player & Managers
    ID3D11Device* device = Graphics::Instance().GetDevice();

    m_player = std::make_unique<Player>();
    m_player->InitPhysics(m_controllerManager.get(), m_defaultMaterial.get(), PlayerConst::CapsuleHalfHeight);
    PlayerConfig bossConfig{};
    bossConfig.moveSpeed = 20.0f;
    bossConfig.dashSpeed = 60.0f;         // Kembalikan nilai dash
    bossConfig.gravityEnabled = false;    // Pastikan gravity mati saat reset!

    m_player->ApplyConfig(bossConfig);

    m_player->SetPosition(0.0f, 0.0f, -8.0f);

    m_stage = std::make_unique<Stage>(device);

    m_collisionManager = std::make_unique<CollisionManager>();
    m_collisionManager->Initialize(m_player.get(), m_stage.get(), m_enemyManager.get(), m_itemManager.get());
    m_player->SetCollisionManager(m_collisionManager.get());

    // =========================================================
    // [FIX] INISIALISASI NAVI BOSS & SET FASE AWAL!
    // =========================================================
    m_navi = std::make_unique<Boss>();
    m_navi->Initialize(m_windowSystem.get());

    // Beri otak ke Navi agar masuk ke Mode Layar Penuh!
    m_navi->ChangePhase(std::make_unique<BossPhase01>());
    // =========================================================

    m_player->SetMaxHP(m_player->GetMaxHP());
    m_player->scale = { 1.0f, 1.0f, 1.0f }; // Kembalikan badan player jika tadi mati

    if (m_collisionManager) m_collisionManager->SetBoss(m_navi.get());

    // 5. Finalize
    m_timeScale = 1.0f;
    m_spawnCount = 0;
    m_currentStretch = { 0.0f, 0.0f };
    m_stretchOffset = { 0.0f, 0.0f };
    m_showGrid = false;
    m_autoSyncMainWindow = false; // Tetap false agar tidak merusak Fullscreen Fase 1

    //m_topmostEnabled = true;
    m_playerWindowTransparent = true;
    m_debugLogs.clear();

    WindowManager::Instance().SetTopmost(m_topmostEnabled);
    InitializeSubWindows();

    AddLog("HARD RESET: All systems successfully restored.");
}

void SceneBoss::SpawnDebugWindow()
{
    m_spawnCount++;

    TrackedWindowConfig config;
    config.name = "debug_win_" + std::to_string(m_spawnCount);
    config.title = "D" + std::to_string(m_spawnCount) + " (drag/stretch me!)";
    config.width = 300;
    config.height = 300;
    config.role = WindowRole::SUB_VIEWPORT;

    m_windowSystem->AddTrackedWindow(config,
        []() { return XMFLOAT3(0.0f, 0.0f, 0.0f); });

    TrackedWindow* tracked = m_windowSystem->GetTrackedWindow(config.name);
    if (tracked && tracked->window)
    {
        SDL_SetWindowResizable(tracked->window->GetSDLWindow(), true);
        SDL_SetWindowBordered(tracked->window->GetSDLWindow(), true);
    }

    AddLog("Spawned portal: " + config.name);
    WindowManager::Instance().EnforceWindowPriorities();
}

void SceneBoss::SpawnTransparentWindow(float bgAlpha, const std::string& typeSuffix)
{
    m_spawnCount++;

    TrackedWindowConfig config;
    config.name = "trans_" + typeSuffix + "_" + std::to_string(m_spawnCount);
    config.title = "T-" + typeSuffix + " " + std::to_string(m_spawnCount);
    config.width = 300;
    config.height = 300;
    config.role = WindowRole::SUB_VIEWPORT;
    config.isTransparent = true;

    m_windowSystem->AddTrackedWindow(config,
        []() { return XMFLOAT3(0.0f, 0.0f, 0.0f); });

    TrackedWindow* tracked = m_windowSystem->GetTrackedWindow(config.name);
    if (tracked && tracked->window)
    {
        tracked->window->SetBackgroundAlpha(bgAlpha);
        tracked->window->SetDraggable(true);
        tracked->window->SetBorderVisible(true);
    }

    AddLog("Spawned " + typeSuffix + " window (alpha: " + std::to_string(bgAlpha) + ")");
}

void SceneBoss::CloseSubWindowBySDLID(Uint32 sdlWindowID)
{
    if (!m_windowSystem) return;

    for (const auto& tracked : m_windowSystem->GetWindows())
    {
        if (tracked->window &&
            SDL_GetWindowID(tracked->window->GetSDLWindow()) == sdlWindowID)
        {
            const std::string name = tracked->name;
            m_windowSystem->RemoveTrackedWindow(name);
            AddLog("Window closed: " + name);
            return;
        }
    }
}