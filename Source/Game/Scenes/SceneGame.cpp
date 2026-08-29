#include "EditorManager.h"
#include "SceneGame.h" 

using namespace DirectX;

namespace
{
    size_t NextUtf8Offset(const std::string& text, size_t offset)
    {
        if (offset >= text.size()) return text.size();

        const unsigned char lead = static_cast<unsigned char>(text[offset]);
        size_t length = 1;

        if ((lead & 0x80) == 0x00) length = 1;
        else if ((lead & 0xE0) == 0xC0) length = 2;
        else if ((lead & 0xF0) == 0xE0) length = 3;
        else if ((lead & 0xF8) == 0xF0) length = 4;

        return (std::min)(offset + length, text.size());
    }

    int CountUtf8Characters(const std::string& text)
    {
        int count = 0;
        for (size_t offset = 0; offset < text.size(); offset = NextUtf8Offset(text, offset))
        {
            ++count;
        }
        return count;
    }

    std::string Utf8Prefix(const std::string& text, int characterCount)
    {
        if (characterCount <= 0) return {};

        size_t offset = 0;
        int count = 0;
        while (offset < text.size() && count < characterCount)
        {
            offset = NextUtf8Offset(text, offset);
            ++count;
        }
        return text.substr(0, offset);
    }
}

[[nodiscard]] bool SceneGame::CheckPauseToggleTriggered() const noexcept
{
    // Do not allow pausing during death, respawn, or boot transitions
    // This prevents soft-locks where timers freeze during critical system states
    if (m_isDying || m_isNaviDefeatSequenceActive || m_bootTimer > 0.0f)
    {
        return false;
    }

    auto& input = Input::Instance();

    // Use IsTriggered / GetButtonDown 
    // This guarantees the pause only fires once per physical key press, even if held
    const bool isEscTriggered = input.GetKeyboard().IsTriggered(VK_ESCAPE);
    const bool isStartTriggered = (input.GetGamePad().GetButtonDown() & GamePad::BTN_START) != 0;

    return isEscTriggered || isStartTriggered;
}

SceneGame::SceneGame()
{
    float screenW{ Config::DEFAULT_SCREEN_W };
    float screenH{ Config::DEFAULT_SCREEN_H };

    if (auto window{ Framework::Instance()->GetMainWindow() }) {
        screenW = static_cast<float>(window->GetWidth());
        screenH = static_cast<float>(window->GetHeight());
    }

    auto& camCtrl{ CameraController::Instance() };
    camCtrl.ClearCamera();
    camCtrl.StopSequence();
    camCtrl.SetTargetOffset({ 0.0f, 0.0f, 0.0f });
    camCtrl.SetFixedYawOffset(0.0f);
    camCtrl.SetFixedRollOffset(0.0f);
    camCtrl.SetSplineTension(1.0f);

    m_mainCamera = std::make_shared<Camera>();
    m_mainCamera->SetPerspectiveFov(XMConvertToRadians(Config::CAM_FOV), screenW / screenH, Config::CAM_NEAR, Config::CAM_FAR);

    XMFLOAT3 startPos{ m_cameraPosition };
    startPos.x = 0.0f;
    startPos.z = -14.0f;
    startPos.y = Config::CAM_START_HEIGHT;

    m_mainCamera->SetPosition(startPos);
    m_mainCamera->LookAt(m_cameraTarget);
    camCtrl.SetActiveCamera(m_mainCamera);

    // Sync camera with initial editor mode state
    camCtrl.SetFixedSetting(startPos);
    camCtrl.SetTarget(m_cameraTarget);

    m_lastEditorMode = EditorManager::Instance().GetEditorMode();
    if (m_lastEditorMode == EditorMode::Edit)
    {
        camCtrl.SetControlMode(CameraControlMode::Free);

        // Remove the black screen boot fade
        m_bootTimer = 0.0f;
        m_fadeAlpha = 0.0f;
    }
    else
    {
        camCtrl.SetControlMode(CameraControlMode::FixedFollow);
    }

    m_respawnTimer = 0.0f;

    m_stage = std::make_unique<Stage>(Graphics::Instance().GetDevice());

    auto stageNode{ std::make_unique<GameObject>("Stage") };
    stageNode->AddComponent<StageComponent>(m_stage.get());
    m_sceneRoot->AddChild(std::move(stageNode));

    m_foundation.reset(PxCreateFoundation(PX_PHYSICS_VERSION, m_allocator, m_errorCallback));
    assert(m_foundation != nullptr && "CRITICAL ERROR: PxCreateFoundation failed!");

    m_physics.reset(PxCreatePhysics(PX_PHYSICS_VERSION, *m_foundation, physx::PxTolerancesScale(), true, nullptr));
    assert(m_physics != nullptr && "CRITICAL ERROR: PxCreatePhysics failed!");

    physx::PxSceneDesc sceneDesc(m_physics->getTolerancesScale());
    sceneDesc.gravity = physx::PxVec3(0.0f, Config::GRAVITY, 0.0f);

    m_dispatcher.reset(physx::PxDefaultCpuDispatcherCreate(2));
    sceneDesc.cpuDispatcher = m_dispatcher.get();
    sceneDesc.filterShader = physx::PxDefaultSimulationFilterShader;

    m_scene.reset(m_physics->createScene(sceneDesc));
    assert(m_scene != nullptr && "CRITICAL ERROR: createScene failed!");

    m_controllerManager.reset(PxCreateControllerManager(*m_scene));
    assert(m_controllerManager != nullptr && "CRITICAL ERROR: PxCreateControllerManager failed!");

    m_defaultMaterial.reset(m_physics->createMaterial(0.5f, 0.5f, 0.1f));
    assert(m_defaultMaterial != nullptr && "CRITICAL ERROR: createMaterial failed!");

    m_groundPlane.reset(physx::PxCreatePlane(*m_physics, physx::PxPlane(0, 1, 0, 0), *m_defaultMaterial));
    m_scene->addActor(*m_groundPlane);

    m_player = std::make_unique<Player>();

    m_player->SetPosition(m_playerSpawnPos);
    m_player->InitPhysics(m_controllerManager.get(), m_defaultMaterial.get());
    m_stage->InitPhysics(m_physics.get(), m_scene.get(), m_defaultMaterial.get());

    m_player->SetMaxHP(100);

    PlayerConfig gameConfig{};
    gameConfig.moveSpeed = 8.0f;
    gameConfig.dashSpeed = 28.0f;

    m_player->ApplyConfig(gameConfig);
    m_player->GetMovement()->SetRotationY(DirectX::XM_PI);

    // Create a GameObject node named "Player"
    auto playerNode{ std::make_unique<GameObject>("Player") };
    playerNode->AddComponent<LegacyCharacterComponent>(m_player.get());

    // Add it to the Scene's Root GameObject 
    m_sceneRoot->AddChild(std::move(playerNode));

    m_enemyManager = std::make_unique<EnemyManager>();
    m_enemyManager->Initialize(Graphics::Instance().GetDevice(), m_sceneRoot.get());

    m_navi = std::make_unique<NaviAlly>(Graphics::Instance().GetDevice(), m_player.get(), m_enemyManager.get());

    m_itemManager = std::make_unique<ItemManager>();
    m_itemManager->Initialize(Graphics::Instance().GetDevice(), m_sceneRoot.get());
    
    // Reads the JSON file and pushes the saved data into the managers
    SceneSerializer::Load(GetSceneSavePath(), m_sceneRoot.get(), m_enemyManager.get(), m_itemManager.get());

    m_collisionManager = std::make_unique<CollisionManager>();
    m_collisionManager->Initialize(m_player.get(), m_stage.get(), m_enemyManager.get(), m_itemManager.get());
    m_collisionManager->SetNavi(m_navi.get());

    m_player->SetCollisionManager(m_collisionManager.get());
    m_collisionManager->SetOnEnableLineReachCallback([this](int lineIndex) {
        if (lineIndex == 0 && !m_bossCinematicTriggered)
        {
            if (AreTrackingEnemiesDead())
            {
                StartBossCinematic();
            }
        }
        });

    m_collisionManager->SetOnCheckpointReachCallback([this](DirectX::XMFLOAT3 pos) {
        m_currentCheckpointPos = pos;
        m_hasCheckpoint = true;
    });

    if (m_lastEditorMode == EditorMode::Play)
    {
        for (int i = 0; i < 60; ++i)
        {
            if (m_scene)
            {
                m_scene->simulate(0.01666f);
                m_scene->fetchResults(true);
            }
            if (m_player) m_player->Update(0.01666f, nullptr);
            if (m_navi) m_navi->Update(0.01666f, nullptr);
        }
        if (m_player)
        {
            CameraController::Instance().SetTarget(m_player->GetPosition());
            CameraController::Instance().Update(1.0f);
        }
    }

    m_postProcess = std::make_unique<PostProcessManager>();
    m_postProcess->Initialize(static_cast<int>(screenW), static_cast<int>(screenH));
    m_postProcess->SetEnabled(true);

    // Automatically load this scene's unique post-process profile on boot
    m_postProcess->LoadConfig(GetPostProcessProfilePath());

    if (m_lastEditorMode == EditorMode::Edit)
    {
        m_postProcess->GetVignette().GetData().smoothness = FX_BASE_SMOOTHNESS;
        m_postProcess->GetVignette().GetData().intensity = FX_BASE_INTENSITY;
    }

    m_dialogueBox = std::make_unique<UIDialogueBox>();
    m_dialogueBox->Initialize();

    m_uiPause = std::make_unique<UIPause>();
    m_uiPause->Initialize();

    m_fadeSprite = std::make_unique<Sprite>(Graphics::Instance().GetDevice(), "Data/Sprite/Scene Game/Black.png");
    m_whiteSprite = std::make_unique<Sprite>(Graphics::Instance().GetDevice(), "Data/Sprite/Scene Game/White.png");
    EffectManager::Instance().PreloadEffect("Data/Effect/Hit.efk");
    EffectManager::Instance().PreloadEffect("Data/Effect/FakeBossPoison.efk");
}

SceneGame::~SceneGame()
{
    AudioManager::Instance().StopMusic();
    EffectManager::Instance().StopAll();

    m_player.reset();
    m_stage.reset();
    m_enemyManager.reset();
    m_itemManager.reset();
}

void SceneGame::Update(const float elapsedTime)
{
    EditorMode currentMode = EditorManager::Instance().GetEditorMode();

    // Editor State Machine
    if (m_lastEditorMode != currentMode)
    {
        if (currentMode == EditorMode::Play && m_lastEditorMode == EditorMode::Edit)
        {
            // STOP -> PLAY: Backup the scene layout so we can revert it later
            SceneSerializer::Save("Data/Scenes/AutoSave_PlayMode.json", m_sceneRoot.get(), m_enemyManager.get(), m_itemManager.get());

            // Cache editor camera
            if (Camera* activeCam = CameraController::Instance().GetActiveCamera().get())
            {
                m_cachedEditorCamPos = activeCam->GetPosition();
                m_cachedEditorCamRot = activeCam->GetRotation();
            }

            CameraController::Instance().SetControlMode(CameraControlMode::FixedFollow);

            // Physics Pre Warming
            for (int i = 0; i < 60; ++i)
            {
                if (m_scene)
                {
                    m_scene->simulate(0.01666f);
                    m_scene->fetchResults(true);
                }

                if (m_player) m_player->Update(0.01666f, CameraController::Instance().GetActiveCamera().get());
                if (m_navi) m_navi->Update(0.01666f, CameraController::Instance().GetActiveCamera().get());
            }

            // Snap camera directly to the settled, grounded player position
            if (m_player)
            {
                CameraController::Instance().SetTarget(m_player->GetPosition());
                CameraController::Instance().Update(1.0f);
            }

            // Start the game boot sequence
            m_bootTimer = 1.1f;

            m_respawnTimer = 0.0f;
            m_isDying = false;

            m_fadeAlpha = 1.0f;
            if (m_postProcess)
            {
                m_postProcess->GetVignette().GetData().smoothness = FX_BLACK_SMOOTHNESS;
                m_postProcess->GetVignette().GetData().intensity = FX_BLACK_INTENSITY;
            }
            if (m_player) m_player->SetInputEnabled(false);
        }
        else if (currentMode == EditorMode::Edit)
        {
            EditorManager::Instance().ClearSelection();

            if (m_sceneRoot)
            {
                for (const auto& child : m_sceneRoot->GetChildren())
                {
                    if (child->GetName() != "Player" && child->GetName() != "Stage")
                    {
                        child->Destroy();
                    }
                }
                m_sceneRoot->Update(0.0f);
            }

            SceneSerializer::Load("Data/Scenes/AutoSave_PlayMode.json", m_sceneRoot.get(), m_enemyManager.get(), m_itemManager.get());

            Camera* activeCam{ CameraController::Instance().GetActiveCamera().get() };
            if (m_enemyManager) m_enemyManager->Update(0.0f, activeCam, m_cameraTarget, false);
            if (m_itemManager) m_itemManager->Update(0.0f, activeCam);
            Scene::Update(0.0f);

            ResetLevel();

            // UI & State Reset
            m_isPaused = false;
            m_isExitingToTitle = false;
            m_exitToTitleTimer = 0.0f;

            m_isDying = false;
            m_respawnTimer = 0.0f;
            m_bootTimer = 0.0f;

            // Prevent access violations from the camera tracking a deleted enemy
            m_cachedClosestEnemy = nullptr;
            m_targetZoom = 0.0f;

            m_bossCinematicTriggered = false;
            m_isBossCinematicActive = false;
            m_bossDialogueStarted = false;
            m_bossEffectTriggered = false;
            m_isPoisonDialogueActive = false;

            m_hasBGMStarted = false;
            m_hasIntroDialogueTestStarted = false;
            m_hasTriggeredMushroomDialogue = false;
            m_hasTriggeredTrackingDialogue = false;
            m_hasTriggeredPoisonDialogue = false;

            // Forcibly destroy and recreate the UI to clear active typewriter text
            m_dialogueBox = std::make_unique<UIDialogueBox>();
            m_dialogueBox->Initialize();

            if (m_uiPause) m_uiPause->ResetSelection();
            if (m_player) m_player->SetInputEnabled(true);

            // Visual & Audio Cleanup
            m_fadeAlpha = 0.0f;
            m_whiteAlpha = 0.0f;
            if (m_postProcess)
            {
                m_postProcess->GetVignette().GetData().smoothness = FX_BASE_SMOOTHNESS;
                m_postProcess->GetVignette().GetData().intensity = FX_BASE_INTENSITY;
            }

            EffectManager::Instance().StopAll();
            AudioManager::Instance().StopMusic();

            CameraController::Instance().SetControlMode(CameraControlMode::Free);
        }
        else if (currentMode == EditorMode::Pause)
        {
            // PLAY -> PAUSE: Switch to Free camera so designer can inspect the frozen combat frame
            CameraController::Instance().SetControlMode(CameraControlMode::Free);
        }
        else if (currentMode == EditorMode::Play && m_lastEditorMode == EditorMode::Pause)
        {
            // PAUSE -> PLAY: Snap back to gameplay camera
            CameraController::Instance().SetControlMode(CameraControlMode::FixedFollow);
        }

        m_lastEditorMode = currentMode;
    }

    const bool isPlaying{ currentMode == EditorMode::Play };

    // In game pause mode
    if (isPlaying)
    {
        if (CheckPauseToggleTriggered())
        {
            m_isPaused = !m_isPaused;
        }

        if (m_isPaused)
        {
            auto& input = Input::Instance();
            auto& keyboard = input.GetKeyboard();
            auto& gamepad = input.GetGamePad();

            if (m_isExitingToTitle)
            {
                m_exitToTitleTimer += elapsedTime;
                const float t{ std::clamp(m_exitToTitleTimer / RESPAWN_FADE_DURATION, 0.0f, 1.0f) };
                m_fadeAlpha = t;
                m_postProcess->GetVignette().GetData().smoothness = FX_BASE_SMOOTHNESS + (FX_BLACK_SMOOTHNESS - FX_BASE_SMOOTHNESS) * t;
                m_postProcess->GetVignette().GetData().intensity = FX_BASE_INTENSITY + (FX_BLACK_INTENSITY - FX_BASE_INTENSITY) * t;

                if (t >= 1.0f)
                {
                    Framework::Instance()->ChangeScene([]() { return std::make_unique<SceneTitle>(); });
                }
                return;
            }

            bool moveUp{ false };
            bool moveDown{ false };

            if (keyboard.IsTriggered('W') || keyboard.IsTriggered(VK_UP) || (gamepad.GetButtonDown() & GamePad::BTN_UP) != 0) moveUp = true;
            else if (keyboard.IsTriggered('S') || keyboard.IsTriggered(VK_DOWN) || (gamepad.GetButtonDown() & GamePad::BTN_DOWN) != 0) moveDown = true;

            static bool s_analogLatchReset{ true };
            const float ly{ gamepad.GetAxisLY() };
            constexpr float analogThreshold{ 0.6f };
            constexpr float deadzoneThreshold{ 0.2f };

            if (ly > analogThreshold) { if (s_analogLatchReset) { moveUp = true; s_analogLatchReset = false; } }
            else if (ly < -analogThreshold) { if (s_analogLatchReset) { moveDown = true; s_analogLatchReset = false; } }
            else if (std::abs(ly) < deadzoneThreshold) { s_analogLatchReset = true; }

            if (moveUp)   m_uiPause->MoveSelection(-1);
            if (moveDown) m_uiPause->MoveSelection(1);

            if (keyboard.IsTriggered(VK_RETURN) || keyboard.IsTriggered(VK_SPACE) || (gamepad.GetButtonDown() & GamePad::BTN_A) != 0)
            {
                const auto selected = m_uiPause->GetSelectedOption();
                if (selected == UIPause::PauseOption::Resume)
                {
                    m_isPaused = false;
                    m_uiPause->ResetSelection();
                }
                else if (selected == UIPause::PauseOption::Exit)
                {
                    m_isExitingToTitle = true;
                    m_exitToTitleTimer = 0.0f;
                    AudioManager::Instance().FadeOutMusic(RESPAWN_FADE_DURATION);
                    AudioManager::Instance().FadeOutAmbientSFX(RESPAWN_FADE_DURATION);
                }
            }

            return; // Halt ALL further updates while the in-game menu is paused
        }
    }

    // Gameplay Simulation (Only runs in Play Mode)
    if (isPlaying)
    {
        m_globalTime += elapsedTime;
        if (m_globalTime > Config::TIME_LOOP_MAX) m_globalTime -= Config::TIME_LOOP_MAX;

        if (m_navi && !m_navi->IsAlive() && !m_isNaviDefeatSequenceActive) StartNaviDefeatSequence();
        if (m_player && m_player->GetHP() <= 0 && !m_isDying && !m_isNaviDefeatSequenceActive && m_respawnTimer <= 0.0f) StartPlayerDeathSequence();

        if (m_isNaviDefeatSequenceActive)
        {
            m_naviDefeatTimer += elapsedTime;
            const float linearT{ std::clamp(m_naviDefeatTimer / NAVI_DEFEAT_FADE_DURATION, 0.0f, 1.0f) };
            const float t{ linearT * linearT * (3.0f - 2.0f * linearT) };

            m_postProcess->GetVignette().GetData().smoothness = FX_BASE_SMOOTHNESS + (FX_BLACK_SMOOTHNESS - FX_BASE_SMOOTHNESS) * t;
            m_postProcess->GetVignette().GetData().intensity = FX_BASE_INTENSITY + (FX_BLACK_INTENSITY - FX_BASE_INTENSITY) * t;
            m_fadeAlpha = t;

            if (linearT >= 1.0f)
            {
                m_postProcess->GetVignette().GetData().smoothness = FX_BLACK_SMOOTHNESS;
                m_postProcess->GetVignette().GetData().intensity = FX_BLACK_INTENSITY;
                m_fadeAlpha = 1.0f;
                m_isNaviDefeatReadyForNextScene = true;
                m_player.reset(); m_navi.reset(); m_enemyManager.reset(); m_itemManager.reset(); m_stage.reset(); m_collisionManager.reset();

                m_groundPlane.reset(); m_defaultMaterial.reset(); m_controllerManager.reset(); m_scene.reset(); m_dispatcher.reset(); m_physics.reset(); m_foundation.reset();

                CameraController::Instance().ClearCamera();
                Framework::Instance()->ChangeScene([]() { return std::make_unique<SceneTitle>(); });
                return;
            }
        }
        else if (m_bootTimer > 0.0f)
        {
            m_bootTimer -= elapsedTime;

            const float t{ std::clamp(m_bootTimer / 1.1f, 0.0f, 1.0f) };
            m_fadeAlpha = t;

            m_postProcess->GetVignette().GetData().smoothness = FX_BASE_SMOOTHNESS + (FX_BLACK_SMOOTHNESS - FX_BASE_SMOOTHNESS) * t;
            m_postProcess->GetVignette().GetData().intensity = FX_BASE_INTENSITY + (FX_BLACK_INTENSITY - FX_BASE_INTENSITY) * t;

            if (m_player)
            {
                m_player->SetInputEnabled(false);
                CameraController::Instance().SetTarget(m_player->GetPosition());
                CameraController::Instance().Update(0.0f);
            }

            if (m_bootTimer <= 0.0f && m_player) m_player->SetInputEnabled(true);
        }
        else if (m_isDying)
        {
            m_deathTimer += elapsedTime;
            if (m_deathTimer < DEATH_DELAY_DURATION)
            {
                m_postProcess->GetVignette().GetData().smoothness = FX_BASE_SMOOTHNESS;
                m_postProcess->GetVignette().GetData().intensity = FX_BASE_INTENSITY;
                m_fadeAlpha = 0.0f;
            }
            else
            {
                const float fadeTime{ m_deathTimer - DEATH_DELAY_DURATION };
                const float t{ std::clamp(fadeTime / DEATH_FADE_DURATION, 0.0f, 1.0f) };
                m_postProcess->GetVignette().GetData().smoothness = FX_BASE_SMOOTHNESS + (FX_BLACK_SMOOTHNESS - FX_BASE_SMOOTHNESS) * t;
                m_postProcess->GetVignette().GetData().intensity = FX_BASE_INTENSITY + (FX_BLACK_INTENSITY - FX_BASE_INTENSITY) * t;
                m_fadeAlpha = t;

                if (t >= 1.0f)
                {
                    ResetLevel();
                    m_isDying = false;
                    m_respawnTimer = RESPAWN_FADE_DURATION;
                    m_postProcess->GetVignette().GetData().smoothness = FX_BLACK_SMOOTHNESS;
                    m_postProcess->GetVignette().GetData().intensity = FX_BLACK_INTENSITY;
                    m_fadeAlpha = 1.0f;
                }
            }
        }
        else if (m_respawnTimer > 0.0f)
        {
            m_respawnTimer -= elapsedTime;
            if (m_player) m_player->SetInputEnabled(false);
            const float linearT{ std::clamp(m_respawnTimer / RESPAWN_FADE_DURATION, 0.0f, 1.0f) };
            const float t{ linearT * linearT };
            m_postProcess->GetVignette().GetData().smoothness = FX_BASE_SMOOTHNESS + (FX_BLACK_SMOOTHNESS - FX_BASE_SMOOTHNESS) * t;
            m_postProcess->GetVignette().GetData().intensity = FX_BASE_INTENSITY + (FX_BLACK_INTENSITY - FX_BASE_INTENSITY) * t;
            m_fadeAlpha = t;

            if (m_respawnTimer <= 0.0f && m_player) m_player->SetInputEnabled(true);
        }
        else
        {
            m_postProcess->GetVignette().GetData().smoothness = FX_BASE_SMOOTHNESS;
            m_fadeAlpha = 0.0f;
            if (!m_hasBGMStarted) { AudioManager::Instance().PlayMusic("Data/Sound/BGM_Game.wav", 0.1f, true); m_hasBGMStarted = true; }
        }

        if (!m_hasIntroDialogueTestStarted && m_bootTimer <= 0.0f && m_respawnTimer <= 0.0f && !m_isDying && !m_isNaviDefeatSequenceActive)
        {
            StartIntroDialogueTest();
        }

        if (m_dialogueBox) m_dialogueBox->Update(elapsedTime);

        // Run Physics
        if (m_scene) {
            m_scene->simulate(elapsedTime);
            m_scene->fetchResults(true);
        }

        Camera* activeCam{ CameraController::Instance().GetActiveCamera().get() };

        // Run Entities
        if (m_player) {
            m_player->Update(elapsedTime, activeCam);
            if (m_navi) m_navi->Update(elapsedTime, activeCam);
        }

        if (m_enemyManager) {
            XMFLOAT3 targetPos{ 0.0f, 0.0f, 0.0f };
            if (m_player) targetPos = m_player->GetPosition();
            bool canAttack = (m_player && m_player->GetHP() > 0);
            m_enemyManager->Update(elapsedTime, activeCam, targetPos, canAttack);
        }

        if (m_itemManager) m_itemManager->Update(elapsedTime, activeCam);
        if (m_collisionManager) m_collisionManager->Update(elapsedTime);

        // Boss Cinematic Logic
        if (m_isBossCinematicActive)
        {
            float t = std::clamp(m_bossCinematicTimer / BOSS_CINEMATIC_DURATION, 0.0f, 1.0f);
            float smoothT = t * t * (3.0f - 2.0f * t);

            DirectX::XMFLOAT3 currentTarget = {
                m_cinematicStartTarget.x + (m_cinematicEndTarget.x - m_cinematicStartTarget.x) * smoothT,
                m_cinematicStartTarget.y + (m_cinematicEndTarget.y - m_cinematicStartTarget.y) * smoothT,
                m_cinematicStartTarget.z + (m_cinematicEndTarget.z - m_cinematicStartTarget.z) * smoothT
            };

            CameraController::Instance().SetDynamicZoomOffset(0.0f);
            CameraController::Instance().SetTarget(currentTarget);

            if (m_bossCinematicTimer < BOSS_CINEMATIC_DURATION)
            {
                m_bossCinematicTimer += elapsedTime;
            }
            else if (!m_bossDialogueStarted)
            {
                m_bossDialogueStarted = true;
                std::vector<std::string> dialogPages = {
                    u8"えっ...？ 何あのキノコ...。\n他のやつらより、ずっと大きい...？",
                    u8"ちょっと待って、様子がおかしいわ。\nなんか...膨らんでない！？",
                    u8"きゃあああああっ！？\n毒ガス！？ ごほっ、げほっ...！"
                };
                m_dialogueBox->StartDialogue(dialogPages);
            }
            else if (m_bossDialogueStarted)
            {
                int currentLine = m_dialogueBox->GetCurrentDialogueIndex();
                if (currentLine == 2 && !m_bossEffectTriggered)
                {
                    m_bossEffectTriggered = true;
                    static const std::string POISON_SFX{ "Data/Sound/SE_FakeBoss_Poison.wav" };
                    AudioManager::Instance().PlayAmbientSFX(POISON_SFX, 1.0f, 0.5f);

                    if (Enemy* fakeBoss = GetFakeBoss())
                    {
                        DirectX::XMFLOAT3 spawnPos = fakeBoss->GetPosition();
                        spawnPos.x += m_fakeBossEffectOffset.x; spawnPos.y += m_fakeBossEffectOffset.y; spawnPos.z += m_fakeBossEffectOffset.z;

                        m_poisonEffectHandle = EffectManager::Instance().Play("Data/Effect/FakeBossPoison.efk", spawnPos, m_fakeBossEffectScale);

                        if (m_poisonEffectHandle >= 0) {
                            DirectX::XMFLOAT3 rotRad{ DirectX::XMConvertToRadians(m_fakeBossEffectRotation.x), DirectX::XMConvertToRadians(m_fakeBossEffectRotation.y), DirectX::XMConvertToRadians(m_fakeBossEffectRotation.z) };
                            EffectManager::Instance().SetRotation(m_poisonEffectHandle, rotRad);
                        }
                    }
                }

                if (m_bossEffectTriggered && !m_dialogueBox->IsActive())
                {
                    AudioManager::Instance().FadeOutAmbientSFX(1.5f);
                    m_bossCinematicTimer += elapsedTime;
                    float timeInFade = m_bossCinematicTimer - BOSS_CINEMATIC_DURATION;

                    if (timeInFade < WHITEOUT_FADE_DURATION)
                    {
                        float linearT = std::clamp(timeInFade / WHITEOUT_FADE_DURATION, 0.0f, 1.0f);
                        m_whiteAlpha = linearT * linearT * (3.0f - 2.0f * linearT);
                    }
                    else if (timeInFade < WHITEOUT_FADE_DURATION + WHITEOUT_HOLD_DURATION)
                    {
                        m_whiteAlpha = 1.0f;
                        if (m_poisonEffectHandle >= 0) { EffectManager::Instance().Stop(m_poisonEffectHandle); m_poisonEffectHandle = -1; }
                        if (!m_hasHealedForBoss) { if (m_player) m_player->SetMaxHP(BOSS_MAX_HP); m_hasHealedForBoss = true; }
                    }
                    else
                    {
                        float fadeOutTime = timeInFade - (WHITEOUT_FADE_DURATION + WHITEOUT_HOLD_DURATION);
                        m_whiteAlpha = 1.0f - std::clamp(fadeOutTime / FADE_BACK_DURATION, 0.0f, 1.0f);

                        if (m_navi) { m_navi->SetPotionedState(true); m_navi->StartAttackDelay(999.0f); }

                        if (m_whiteAlpha <= 0.0f)
                        {
                            m_whiteAlpha = 0.0f;
                            m_isBossCinematicActive = false;
                            if (!m_hasTriggeredPoisonDialogue) StartPoisonDialogue();
                        }
                    }
                }
            }
        }
        else // Normal gameplay camera behavior
        {
            if (m_player) CameraController::Instance().SetTarget(m_player->GetPosition());

            if (m_zoomFrameCounter++ % 10 == 0)
            {
                if (m_enemyManager && m_player)
                {
                    float closestDistSq{ 999999.0f };
                    const DirectX::XMFLOAT3 pPos{ m_player->GetPosition() };
                    const Enemy* currentClosest{ nullptr };

                    for (const auto& enemy : m_enemyManager->GetEnemies())
                    {
                        if (!enemy || !enemy->IsActive()) continue;
                        const DirectX::XMFLOAT3 ePos{ enemy->GetPosition() };
                        const float dx{ pPos.x - ePos.x }; const float dz{ pPos.z - ePos.z };
                        const float distSq{ (dx * dx) + (dz * dz) };

                        if (distSq < closestDistSq) { closestDistSq = distSq; currentClosest = enemy.get(); }
                    }
                    m_cachedClosestEnemy = currentClosest;

                    if (m_cachedClosestEnemy)
                    {
                        constexpr float combatRadius{ 25.0f }; constexpr float maxZoomIn{ -8.0f };
                        const float dist{ std::sqrt(closestDistSq) };
                        const float intensity{ std::clamp(1.0f - (dist / combatRadius), 0.0f, 1.0f) };
                        m_targetZoom = maxZoomIn * intensity;
                    }
                    else m_targetZoom = 0.0f;
                }
            }

            if (m_cachedClosestEnemy && !m_cachedClosestEnemy->IsActive()) { m_cachedClosestEnemy = nullptr; m_targetZoom = 0.0f; }
            CameraController::Instance().SetDynamicZoomOffset(m_targetZoom);
        }

        // Dialogue Checkers
        if (m_player && m_enemyManager && m_dialogueBox && !m_dialogueBox->IsActive())
        {
            if (!m_hasTriggeredMushroomDialogue || !m_hasTriggeredTrackingDialogue)
            {
                const DirectX::XMFLOAT3 pPos = m_player->GetPosition();
                for (const auto& enemy : m_enemyManager->GetEnemies())
                {
                    if (!enemy || !enemy->IsActive()) continue;
                    const DirectX::XMFLOAT3 ePos = enemy->GetPosition();
                    const float dx = pPos.x - ePos.x; const float dz = pPos.z - ePos.z;
                    if ((dx * dx) + (dz * dz) < 150.0f)
                    {
                        if (!m_hasTriggeredMushroomDialogue && enemy->GetType() == EnemyType::MushroomNone) { m_hasTriggeredMushroomDialogue = true; StartMushroomDialogue(); break; }
                        else if (!m_hasTriggeredTrackingDialogue && enemy->GetAttackType() == AttackType::Tracking) { m_hasTriggeredTrackingDialogue = true; StartTrackingDialogue(); break; }
                    }
                }
            }
        }

        if (m_isPoisonDialogueActive && m_dialogueBox && !m_dialogueBox->IsActive())
        {
            m_isPoisonDialogueActive = false;
            if (m_player) { m_player->SetInputEnabled(true); m_player->SetAimLocked(false); }
            if (m_navi) { m_navi->StartAttackDelay(0.5f); }
        }
    }

    // Engine Core & Visuals (Always runs so Editor functions)
    CameraController::Instance().Update(elapsedTime);

    if (m_player)
    {
        m_postProcess->GetLensDistortion().GetData().glitchStrength = m_player->GetDamageGlitchIntensity();
    }

    // Tick the Scene Graph / GameObjects to sync transforms to ImGuizmo
    Scene::Update(elapsedTime);

    // Freeze effects when not playing so explosions don't animate during Pause/Edit
    EffectManager::Instance().Update(isPlaying ? elapsedTime : 0.0f);
}

void SceneGame::StartPlayerDeathSequence()
{
    if (m_isDying) return;
    m_isDying = true;
    m_deathTimer = 0.0f;

    //AudioManager::Instance().PlaySFX("Data/Sound/SE_Explosion.wav", 0.4f);

    // Redundancy: Ensure the player is fully hidden and disabled
    if (m_player)
    {
        m_player->SetInputEnabled(false);
        m_player->scale = { 0.0f, 0.0f, 0.0f };
    }
}

void SceneGame::StartNaviDefeatSequence()
{
    if (m_isNaviDefeatSequenceActive) return;

    m_isNaviDefeatSequenceActive = true;
    m_naviDefeatTimer = 0.0f;
    m_isNaviDefeatReadyForNextScene = false;

    AudioManager::Instance().FadeOutMusic(NAVI_DEFEAT_FADE_DURATION);

    if (m_player)
    {
        m_player->SetInputEnabled(false);
        m_player->SetAimLocked(true);
        m_player->GetMovement()->SetVelocity({ 0.0f, 0.0f, 0.0f });
        m_player->GetProjectiles().clear();
    }
}

void SceneGame::StartIntroDialogueTest()
{
    m_hasIntroDialogueTestStarted = true;

    if (m_dialogueBox)
    {
		// Dialogue pages for the intro tutorial
        std::vector<std::string> dialogPages = {
            u8"目を覚まして。戦いの時間が来たわ。\n{ATK}で攻撃よ。遠くの敵は撃ち抜き、\n近づけばその刃で斬り裂くの。",
            u8"そして、よく覚えておいて。\nいずれそのキーは、敵の牙を弾き返す\n「Parry」の要にもなるわ。魂に刻み込んで。",
            u8"次は{DASH}を試して。\n風のように「Dash」して、敵の弾幕をすり抜けるのよ。\n\nさあ、あなたの力を見せて。"
        };

        m_dialogueBox->SetPosition(536.0f, 750.0f);
        m_dialogueBox->StartDialogue(dialogPages);
    }
}

void SceneGame::StartMushroomDialogue()
{
    if (m_dialogueBox)
    {
        std::vector<std::string> dialogPages = {
            u8"あのキノコを見て。今は大人しく見えるけれど…\n気を抜かないで。",
            u8"この森の奥は奇妙な薬液で汚染されているわ。\n凶暴化した個体もいるはずよ。"
        };

        m_dialogueBox->StartDialogue(dialogPages);
    }
}

void SceneGame::StartTrackingDialogue()
{
    if (m_dialogueBox)
    {
        std::vector<std::string> dialogPages = {
            u8"危ない！あのキノコは他と違うわ！\nあなたを狙って自爆する気よ！",
            u8"近づかれる前に早く撃ち落として！"
        };

        m_dialogueBox->StartDialogue(dialogPages);
    }
}

void SceneGame::StartPoisonDialogue()
{
    if (m_dialogueBox)
    {
        std::vector<std::string> dialogPages = {
            u8"あ....あ、ぁ....",
            u8"あつい....からだが...とける....",
            u8"にげて...わたし、もう.....",
            u8"あはッ......アはハハハハハハハッ！！！！"
        };

        m_hasTriggeredPoisonDialogue = true;
        m_isPoisonDialogueActive = true;

        m_dialogueBox->StartDialogue(dialogPages);
    }
}

void SceneGame::ResetLevel()
{
    const bool isBossStage = m_bossCinematicTriggered;

    // Calculate Respawn Position
    DirectX::XMFLOAT3 respawnPos = m_playerSpawnPos;

    if (isBossStage)
    {
        // If Navi is poisoned / Boss stage is active, respawn exactly where you died
        if (m_player)
        {
            respawnPos = m_player->GetPosition(); 
            respawnPos.y = m_playerSpawnPos.y; 
        }
    }
    else if (m_hasCheckpoint)
    {
        // Normal stage progression still uses your checkpoint lines
        respawnPos = { m_currentCheckpointPos.x, m_playerSpawnPos.y, m_currentCheckpointPos.z };
    }

    // Reset Player State
    if (m_player)
    {
        m_player->SetPosition(respawnPos);
        m_player->GetMovement()->SetVelocity({ 0.0f, 0.0f, 0.0f });
        m_player->SetMaxHP(isBossStage ? BOSS_MAX_HP : NORMAL_MAX_HP);
        m_player->SetInputEnabled(false);
        m_player->scale = { 1.0f, 1.0f, 1.0f };
        m_player->GetStateMachine()->ChangeState(m_player.get(), std::make_unique<PlayerIdle>());
        m_player->GetProjectiles().clear();
    }

    // 3. Reset Enemies & Items
    if (!isBossStage)
    {
        if (m_enemyManager)
        {
           // Revive Kamikazes that successfully hit and killed the player.
            m_enemyManager->ReviveKamikazes();
        }
    }

    // Reset Navi Ally
    if (m_navi)
    {
        m_navi->Reset();
        m_navi->SetPotionedState(isBossStage);

        // Position Navi near Player
        m_navi->SetPosition({ respawnPos.x + 1.0f, respawnPos.y + 2.0f, respawnPos.z + 0.5f });

        if (isBossStage)
        {
            m_navi->StartAttackDelay(3.0f);
        }
    }

    if (EditorManager::Instance().GetEditorMode() == EditorMode::Play)
    {
        for (int i = 0; i < 60; ++i)
        {
            if (m_scene)
            {
                m_scene->simulate(0.01666f);
                m_scene->fetchResults(true);
            }
            if (m_player) m_player->Update(0.01666f, nullptr);
            if (m_navi) m_navi->Update(0.01666f, nullptr);
        }

        CameraController::Instance().SetDynamicZoomOffset(0.0f);
        CameraController::Instance().SetTarget(m_player ? m_player->GetPosition() : respawnPos);
        CameraController::Instance().Update(1.0f);
    }
}

void SceneGame::Render(float elapsedTime, Camera* camera)
{
    const float renderTime = m_isPaused ? 0.0f : elapsedTime;
    Camera* targetCam{ camera ? camera : m_mainCamera.get() };
    auto dc{ Graphics::Instance().GetDeviceContext() };
    auto rs{ Graphics::Instance().GetRenderState() };

    float screenW{ Config::DEFAULT_SCREEN_W };
    float screenH{ Config::DEFAULT_SCREEN_H };

    if (const auto* window{ Framework::Instance()->GetMainWindow() })
    {
        screenW = static_cast<float>(window->GetWidth());
        screenH = static_cast<float>(window->GetHeight());
    }

    if (m_postProcess->IsEnabled())
    {
        m_postProcess->BeginCapture();
    }
    else {
        // Fallback clear if post-process is bypassed
        ID3D11RenderTargetView* originalRTV{ nullptr };
        ID3D11DepthStencilView* originalDSV{ nullptr };
        dc->OMGetRenderTargets(1, &originalRTV, &originalDSV);
        if (originalRTV) {
            float clearColor[4]{ 0.0f, 0.0f, 0.0f, 1.0f }; 
            dc->ClearRenderTargetView(originalRTV, clearColor);
            originalRTV->Release();
        }
        if (originalDSV) {
            dc->ClearDepthStencilView(originalDSV, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
            originalDSV->Release();
        }
    }

    dc->OMSetBlendState(rs->GetBlendState(BlendState::Opaque), nullptr, 0xFFFFFFFF);
    dc->OMSetDepthStencilState(rs->GetDepthStencilState(DepthState::TestAndWrite), 0);
    dc->RSSetState(rs->GetRasterizerState(RasterizerState::SolidCullBack));

    RenderScene(renderTime, targetCam);

    if (targetCam == m_mainCamera.get()) {
        auto shapeRenderer{ Graphics::Instance().GetShapeRenderer() };
        auto primRenderer{ Graphics::Instance().GetPrimitiveRenderer() };

        //primRenderer->DrawGrid(50, 1.0f);

        if (m_itemManager) m_itemManager->RenderDebug(shapeRenderer);
        if (m_stage) m_stage->RenderDebug(shapeRenderer, primRenderer);
        if (m_enemyManager) m_enemyManager->RenderDebug(shapeRenderer);

        // Player hitbox (green), Enemy hitboxes (red)
        //if (m_player)
        //{
        //    DirectX::XMFLOAT3 pPos = m_player->GetMovement()->GetPosition();
        //    constexpr float PLAYER_RADIUS = 0.25f;
        //    shapeRenderer->DrawSphere(pPos, PLAYER_RADIUS, { 0.0f, 1.0f, 0.0f, 1.0f });
        //}
        //if (m_enemyManager && m_collisionManager)
        //{
        //    for (const auto& enemy : m_enemyManager->GetEnemies())
        //    {
        //        if (!enemy || !enemy->IsActive()) continue;

        //        DirectX::XMFLOAT3 ePos = enemy->GetPosition();

        //        // Ask the collision manager how big this specific enemy's hitbox is
        //        float radius = m_collisionManager->GetEnemyPushRadius(enemy.get());

        //        shapeRenderer->DrawSphere(ePos, radius, { 1.0f, 0.0f, 0.0f, 0.5f });
        //    }
        //}

        // Navi hitboxes (blue)
        //if (m_navi) m_navi->RenderDebug(shapeRenderer);

        shapeRenderer->Render(dc, targetCam->GetView(), targetCam->GetProjection());
        primRenderer->Render(dc, targetCam->GetView(), targetCam->GetProjection(), D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
    }

    if (m_postProcess->IsEnabled())
    {
        m_postProcess->EndCapture(renderTime);
    }

    if (m_dialogueBox)
    {
        m_dialogueBox->Render(dc, screenW, screenH);
    }

    if (m_fadeAlpha > 0.001f && m_fadeSprite)
    {
        // Enable 2D Transparency
        dc->OMSetBlendState(rs->GetBlendState(BlendState::Transparency), nullptr, 0xFFFFFFFF);
        dc->OMSetDepthStencilState(rs->GetDepthStencilState(DepthState::NoTestNoWrite), 0);

        const auto fade = UI::GetScaled(0.0f, 0.0f, 1920.0f, 1080.0f, screenW, screenH);

        // Draw the sprite over the whole screen using the scaled coordinates
        m_fadeSprite->Render(
            dc,
            fade.x, fade.y, 0.0f,
            fade.w, fade.h,
            0.0f, 0.0f,
            1920.0f, 1080.0f,
            0.0f,
            0.0f, 0.0f, 0.0f, m_fadeAlpha 
        );
    }

    if (m_whiteAlpha > 0.001f && m_whiteSprite)
    {
        // Enable 2D Transparency
        dc->OMSetBlendState(rs->GetBlendState(BlendState::Transparency), nullptr, 0xFFFFFFFF);
        dc->OMSetDepthStencilState(rs->GetDepthStencilState(DepthState::NoTestNoWrite), 0);

        const auto white = UI::GetScaled(0.0f, 0.0f, 1920.0f, 1080.0f, screenW, screenH);

        // Draw the white sprite over the whole screen
        m_whiteSprite->Render(
            dc,
            white.x, white.y, 0.0f,
            white.w, white.h,
            0.0f, 0.0f,
            1920.0f, 1080.0f,
            0.0f,
            1.0f, 1.0f, 1.0f, m_whiteAlpha
        );
    }

    if (m_isPaused && m_fadeSprite)
    {
        // Enable 2D Transparency 
        dc->OMSetBlendState(rs->GetBlendState(BlendState::Transparency), nullptr, 0xFFFFFFFF);
        dc->OMSetDepthStencilState(rs->GetDepthStencilState(DepthState::NoTestNoWrite), 0);

        const auto pauseFade = UI::GetScaled(0.0f, 0.0f, 1920.0f, 1080.0f, screenW, screenH);

        // Render the black sprite over the whole screen with 60% opacity 
        m_fadeSprite->Render(
            dc,
            pauseFade.x, pauseFade.y, 0.0f,
            pauseFade.w, pauseFade.h,
            0.0f, 0.0f,
            1920.0f, 1080.0f,
            0.0f,
            0.0f, 0.0f, 0.0f, 0.6f
        );

        // Render the pause menu UI on top of the darkened screen
        if (m_uiPause)
        {
            // If we are exiting, fade the UI out. Otherwise, alpha is 1.0f.
            const float uiAlpha = m_isExitingToTitle ? (1.0f - m_fadeAlpha) : 1.0f;
            m_uiPause->Render(dc, screenW, screenH, uiAlpha);
        }
    }
}


void SceneGame::RenderScene(const float elapsedTime, Camera* camera)
{
    if (!camera) return;
    auto dc{ Graphics::Instance().GetDeviceContext() };
    auto modelRenderer{ Graphics::Instance().GetModelRenderer() };
    RenderContext rc{ dc, Graphics::Instance().GetRenderState(), camera, &m_lightManager };

    const auto& psxData = m_postProcess->GetPSX().GetData();
    rc.psxEnabled = (m_postProcess->IsEnabled() && psxData.enabled);
    rc.psxResWidth = psxData.resWidth;
    rc.psxResHeight = psxData.resHeight;

    if (m_player)
    {
        modelRenderer->Draw(ShaderId::Phong, m_player->GetModel(), m_player->color);
        m_player->RenderWeapon(modelRenderer);
        m_player->RenderProjectiles(modelRenderer);
    }
    if (m_navi) {
        m_navi->Render(modelRenderer);
        m_navi->RenderProjectiles(modelRenderer);
    }
    if (m_enemyManager) m_enemyManager->Render(modelRenderer);
    if (m_itemManager) m_itemManager->Render(modelRenderer);
    if (m_stage)
    {
        m_stage->UpdateTransform();
        m_stage->Render(modelRenderer);
    }

    modelRenderer->Render(rc);

    EffectManager::Instance().Render(camera);
}

void SceneGame::OnResize(int width, int height)
{
    if (height <= 0) height = 1;
    if (m_mainCamera) {
        m_mainCamera->SetPerspectiveFov(DirectX::XMConvertToRadians(Config::CAM_FOV), static_cast<float>(width) / static_cast<float>(height), Config::CAM_NEAR, Config::CAM_FAR);
    }
    if (m_postProcess) m_postProcess->OnResize(width, height);
}

bool SceneGame::AreTrackingEnemiesDead() const
{
    if (!m_enemyManager) return false;

    for (const auto& enemy : m_enemyManager->GetEnemies())
    {
        // If we find even one active tracking enemy, abort
        if (enemy && enemy->IsActive() && enemy->GetAttackType() == AttackType::Tracking)
        {
            return false;
        }
    }
    return true;
}

Enemy* SceneGame::GetFakeBoss() const
{
    if (!m_enemyManager) return nullptr;

    for (const auto& enemy : m_enemyManager->GetEnemies())
    {
        if (enemy && enemy->IsActive() && enemy->GetType() == EnemyType::FakeBoss)
        {
            return enemy.get();
        }
    }
    return nullptr;
}

void SceneGame::StartBossCinematic()
{
    if (m_bossCinematicTriggered) return;

    Enemy* fakeBoss = GetFakeBoss();

    if (!fakeBoss || !m_player) return;

    m_bossCinematicTriggered = true;
    m_isBossCinematicActive = true;
    m_bossCinematicTimer = 0.0f;
    m_bossDialogueStarted = false;

    // Lock the Player 
    m_player->SetInputEnabled(false);
    m_player->GetMovement()->SetVelocity({ 0.0f, 0.0f, 0.0f });
    m_player->GetStateMachine()->ChangeState(m_player.get(), std::make_unique<PlayerIdle>());
    m_player->SetAimLocked(true);
    m_player->ForceAimTarget(fakeBoss->GetPosition());

    // Set lerp anchors
    m_cinematicStartTarget = m_player->GetPosition();
    m_cinematicEndTarget = fakeBoss->GetPosition();
}
