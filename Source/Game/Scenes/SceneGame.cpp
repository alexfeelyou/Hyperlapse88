#include <filesystem>
#include "System/PhysicsManager.h"
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

    // Initialize Editor Camera (Fallback)
    auto& camCtrl{ CameraController::Instance() };
    camCtrl.ClearCamera();

    m_mainCamera = std::make_shared<Camera>();
    m_mainCamera->SetPerspectiveFov(XMConvertToRadians(Config::CAM_FOV), screenW / screenH, Config::CAM_NEAR, Config::CAM_FAR);
    m_mainCamera->SetPosition({ 0.001f, Config::CAM_START_HEIGHT, -14.0f });

    camCtrl.SetActiveCamera(m_mainCamera);

    m_lastEditorMode = EditorManager::Instance().GetEditorMode();
    if (m_lastEditorMode == EditorMode::Edit)
    {
        EditorManager::Instance().LoadUserPreferences(this, m_mainCamera.get());
        camCtrl.SyncFromActiveCamera();
        camCtrl.SetEnabled(true);

        m_bootTimer = 0.0f;
        m_fadeAlpha = 0.0f;
    }
    else
    {
        camCtrl.SetEnabled(false);
    }

    // Initialize Game Systems
    m_respawnTimer = 0.0f;
    PhysicsManager::Instance().Initialize();

    m_player = std::make_unique<Player>();
    m_player->SetPosition(m_playerSpawnPos);
    m_player->InitPhysics(PhysicsManager::Instance().GetControllerManager(), PhysicsManager::Instance().GetDefaultMaterial());
    m_player->SetMaxHP(NORMAL_MAX_HP);

    PlayerConfig gameConfig{};
    gameConfig.moveSpeed = 8.0f;
    gameConfig.dashSpeed = 28.0f;
    m_player->ApplyConfig(gameConfig);
    m_player->GetMovement()->SetRotationY(DirectX::XM_PI);

    auto playerNode{ std::make_unique<GameObject>("Player") };
    playerNode->AddComponent<LegacyCharacterComponent>(m_player.get());
    m_sceneRoot->AddChild(std::move(playerNode));

    m_enemyManager = std::make_unique<EnemyManager>();
    m_enemyManager->Initialize(Graphics::Instance().GetDevice(), m_sceneRoot.get());

    m_navi = std::make_unique<NaviAlly>(Graphics::Instance().GetDevice(), m_player.get(), m_enemyManager.get());

    m_itemManager = std::make_unique<ItemManager>();
    m_itemManager->Initialize(Graphics::Instance().GetDevice(), m_sceneRoot.get());

    // Load Scene Hierarchy (Spawns the real CameraComponent)
    SceneSerializer::Load(GetSceneSavePath(), m_sceneRoot.get());

    m_collisionManager = std::make_unique<CollisionManager>();
    m_collisionManager->Initialize(m_player.get(), m_enemyManager.get(), m_itemManager.get());
    m_collisionManager->SetNavi(m_navi.get());
    m_player->SetCollisionManager(m_collisionManager.get());

    // Pre-Warm Physics
    if (m_lastEditorMode == EditorMode::Play)
    {
        for (int i{ 0 }; i < 300; ++i)
        {
            PhysicsManager::Instance().Simulate(0.0f);
            if (m_player) m_player->Update(0.01666f, nullptr);
            if (m_navi)   m_navi->Update(0.01666f, nullptr);

            if (m_player && m_player->IsGrounded()) break;
        }

        if (m_player) m_playerSpawnPos = m_player->GetPosition();
    }
    else if (m_lastEditorMode == EditorMode::Edit)
    {
        PhysicsManager::Instance().Simulate(0.0f);
        if (m_enemyManager) m_enemyManager->Update(0.0f, m_mainCamera.get(), { 0,0,0 }, true);
        if (m_itemManager) m_itemManager->Update(0.0f, m_mainCamera.get());
        if (m_sceneRoot) m_sceneRoot->Update(0.0f);
    }

    // Initialize Rendering Pipeline
    m_postProcess = std::make_unique<PostProcessManager>();
    m_postProcess->Initialize(static_cast<int>(screenW), static_cast<int>(screenH));
    m_postProcess->SetEnabled(true);
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
}

SceneGame::~SceneGame()
{
    if (EditorManager::Instance().GetEditorMode() == EditorMode::Edit)
    {
        EditorManager::Instance().SaveUserPreferences(this, m_mainCamera.get());
    }

    AudioManager::Instance().StopMusic();
    EffectManager::Instance().StopAll();

    m_player.reset();
    m_enemyManager.reset();
    m_itemManager.reset();
    m_sceneRoot.reset();

    PhysicsManager::Instance().Shutdown();

    std::error_code ec;
    std::filesystem::remove("Data/Scenes/AutoSave_PlayMode.json", ec);
}

void SceneGame::Update(const float elapsedTime)
{
    const EditorMode currentMode{ EditorManager::Instance().GetEditorMode() };

    // Editor State Machine (Strict Decoupling)
    if (m_lastEditorMode != currentMode)
    {
        if (currentMode == EditorMode::Play && m_lastEditorMode == EditorMode::Edit)
        {
            EditorManager::Instance().SaveUserPreferences(this, CameraController::Instance().GetActiveCamera().get());
            SceneSerializer::Save("Data/Scenes/AutoSave_PlayMode.json", m_sceneRoot.get(), false);

            // Relinquish camera control to the Scene Graph
            CameraController::Instance().SetEnabled(false);

            if (m_player)
            {
                for (int i{ 0 }; i < 300; ++i)
                {
                    PhysicsManager::Instance().Simulate(0.01666f);
                    if (m_player) m_player->Update(0.01666f, nullptr);
                    if (m_navi)   m_navi->Update(0.01666f, nullptr);

                    if (m_player->IsGrounded()) break;
                }
                m_playerSpawnPos = m_player->GetPosition();
            }

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

            SceneSerializer::Load("Data/Scenes/AutoSave_PlayMode.json", m_sceneRoot.get(), false);

            if (m_player)
            {
                m_player->SetPosition(m_playerSpawnPos);
                m_player->GetMovement()->SetVelocity({ 0.0f, 0.0f, 0.0f });
                m_player->SetMaxHP(NORMAL_MAX_HP);
                m_player->scale = { 1.0f, 1.0f, 1.0f };
                m_player->GetStateMachine()->ChangeState(m_player.get(), std::make_unique<PlayerIdle>());
                m_player->GetProjectiles().clear();
                m_player->ForceVisualSync();
            }

            if (m_navi)
            {
                m_navi->Reset();
                m_navi->SetPosition({ m_playerSpawnPos.x + 1.0f, m_playerSpawnPos.y + 2.0f, m_playerSpawnPos.z + 0.5f });
                m_navi->ForceVisualSync();
            }

            // Restore Editor Camera Control
            EditorManager::Instance().LoadUserPreferences(this, CameraController::Instance().GetActiveCamera().get());
            CameraController::Instance().SyncFromActiveCamera();
            CameraController::Instance().SetEnabled(true);

            Camera* activeCam{ CameraController::Instance().GetActiveCamera().get() };
            if (m_enemyManager) m_enemyManager->Update(0.0f, activeCam, { 0,0,0 }, true);
            if (m_itemManager)  m_itemManager->Update(0.0f, activeCam);
            Scene::Update(0.0f);

            m_hasCheckpoint = false;
            m_isPaused = false;
            m_isExitingToTitle = false;
            m_exitToTitleTimer = 0.0f;
            m_isDying = false;
            m_respawnTimer = 0.0f;
            m_bootTimer = 0.0f;
            m_hasBGMStarted = false;
            m_hasIntroDialogueTestStarted = false;
            m_hasTriggeredMushroomDialogue = false;

            m_dialogueBox = std::make_unique<UIDialogueBox>();
            m_dialogueBox->Initialize();

            if (m_uiPause) m_uiPause->ResetSelection();
            if (m_player)  m_player->SetInputEnabled(true);

            m_fadeAlpha = 0.0f;
            m_whiteAlpha = 0.0f;
            if (m_postProcess)
            {
                m_postProcess->GetVignette().GetData().smoothness = FX_BASE_SMOOTHNESS;
                m_postProcess->GetVignette().GetData().intensity = FX_BASE_INTENSITY;
            }

            EffectManager::Instance().StopAll();
            AudioManager::Instance().StopMusic();
        }
        else if (currentMode == EditorMode::Pause)
        {
            CameraController::Instance().SetEnabled(true);
        }
        else if (currentMode == EditorMode::Play && m_lastEditorMode == EditorMode::Pause)
        {
            CameraController::Instance().SetEnabled(false);
        }

        m_lastEditorMode = currentMode;
    }

    const bool isPlaying{ currentMode == EditorMode::Play };

    if (isPlaying)
    {
        if (CheckPauseToggleTriggered()) m_isPaused = !m_isPaused;

        if (m_isPaused)
        {
            auto& input{ Input::Instance() };
            if (m_isExitingToTitle)
            {
                m_exitToTitleTimer += elapsedTime;
                const float t{ std::clamp(m_exitToTitleTimer / RESPAWN_FADE_DURATION, 0.0f, 1.0f) };
                m_fadeAlpha = t;
                m_postProcess->GetVignette().GetData().smoothness = FX_BASE_SMOOTHNESS + (FX_BLACK_SMOOTHNESS - FX_BASE_SMOOTHNESS) * t;
                m_postProcess->GetVignette().GetData().intensity = FX_BASE_INTENSITY + (FX_BLACK_INTENSITY - FX_BASE_INTENSITY) * t;

                if (t >= 1.0f) Framework::Instance()->ChangeScene([]() { return std::make_unique<SceneTitle>(); });
                return;
            }

            bool moveUp{ false };
            bool moveDown{ false };

            if (input.GetKeyboard().IsTriggered('W') || input.GetKeyboard().IsTriggered(VK_UP) || (input.GetGamePad().GetButtonDown() & GamePad::BTN_UP) != 0) moveUp = true;
            else if (input.GetKeyboard().IsTriggered('S') || input.GetKeyboard().IsTriggered(VK_DOWN) || (input.GetGamePad().GetButtonDown() & GamePad::BTN_DOWN) != 0) moveDown = true;

            static bool s_analogLatchReset{ true };
            const float ly{ input.GetGamePad().GetAxisLY() };
            if (ly > 0.6f) { if (s_analogLatchReset) { moveUp = true; s_analogLatchReset = false; } }
            else if (ly < -0.6f) { if (s_analogLatchReset) { moveDown = true; s_analogLatchReset = false; } }
            else if (std::abs(ly) < 0.2f) { s_analogLatchReset = true; }

            if (moveUp)   m_uiPause->MoveSelection(-1);
            if (moveDown) m_uiPause->MoveSelection(1);

            if (input.GetKeyboard().IsTriggered(VK_RETURN) || input.GetKeyboard().IsTriggered(VK_SPACE) || (input.GetGamePad().GetButtonDown() & GamePad::BTN_A) != 0)
            {
                if (m_uiPause->GetSelectedOption() == UIPause::PauseOption::Resume)
                {
                    m_isPaused = false;
                    m_uiPause->ResetSelection();
                }
                else if (m_uiPause->GetSelectedOption() == UIPause::PauseOption::Exit)
                {
                    m_isExitingToTitle = true;
                    m_exitToTitleTimer = 0.0f;
                    AudioManager::Instance().FadeOutMusic(RESPAWN_FADE_DURATION);
                    AudioManager::Instance().FadeOutAmbientSFX(RESPAWN_FADE_DURATION);
                }
            }
            return;
        }

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
                m_isNaviDefeatReadyForNextScene = true;
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

            if (m_player) m_player->SetInputEnabled(false);
            if (m_bootTimer <= 0.0f && m_player) m_player->SetInputEnabled(true);
        }
        else if (m_isDying)
        {
            m_deathTimer += elapsedTime;
            if (m_deathTimer < DEATH_DELAY_DURATION)
            {
                m_fadeAlpha = 0.0f;
            }
            else
            {
                const float t{ std::clamp((m_deathTimer - DEATH_DELAY_DURATION) / DEATH_FADE_DURATION, 0.0f, 1.0f) };
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
            const float t{ std::clamp(m_respawnTimer / RESPAWN_FADE_DURATION, 0.0f, 1.0f) };

            m_postProcess->GetVignette().GetData().smoothness = FX_BASE_SMOOTHNESS + (FX_BLACK_SMOOTHNESS - FX_BASE_SMOOTHNESS) * (t * t);
            m_postProcess->GetVignette().GetData().intensity = FX_BASE_INTENSITY + (FX_BLACK_INTENSITY - FX_BASE_INTENSITY) * (t * t);
            m_fadeAlpha = t * t;

            if (m_respawnTimer <= 0.0f && m_player) m_player->SetInputEnabled(true);
        }
        else
        {
            m_fadeAlpha = 0.0f;
            if (!m_hasBGMStarted) { AudioManager::Instance().PlayMusic("Data/Sound/BGM_Game.wav", 0.1f, true); m_hasBGMStarted = true; }
        }

        if (!m_hasIntroDialogueTestStarted && m_bootTimer <= 0.0f && m_respawnTimer <= 0.0f && !m_isDying && !m_isNaviDefeatSequenceActive)
        {
            StartIntroDialogueTest();
        }

        if (m_dialogueBox) m_dialogueBox->Update(elapsedTime);

        PhysicsManager::Instance().Simulate(elapsedTime);

        // Entities update without caring about the camera anymore (decoupled)
        if (m_player) m_player->Update(elapsedTime, nullptr);
        if (m_navi)   m_navi->Update(elapsedTime, nullptr);

        if (m_enemyManager) m_enemyManager->Update(elapsedTime, nullptr, m_player ? m_player->GetPosition() : XMFLOAT3{ 0,0,0 }, m_player && m_player->GetHP() > 0);
        if (m_itemManager) m_itemManager->Update(elapsedTime, nullptr);
        if (m_collisionManager) m_collisionManager->Update(elapsedTime);

        if (m_player && m_enemyManager && m_dialogueBox && !m_dialogueBox->IsActive() && !m_hasTriggeredMushroomDialogue)
        {
            const DirectX::XMFLOAT3 pPos{ m_player->GetPosition() };
            for (const auto& enemy : m_enemyManager->GetEnemies())
            {
                if (!enemy || !enemy->IsActive()) continue;
                const DirectX::XMFLOAT3 ePos{ enemy->GetPosition() };
                const float dx{ pPos.x - ePos.x };
                const float dz{ pPos.z - ePos.z };
                if ((dx * dx) + (dz * dz) < 150.0f && enemy->GetType() == EnemyType::MushroomNone)
                {
                    m_hasTriggeredMushroomDialogue = true;
                    StartMushroomDialogue();
                    break;
                }
            }
        }
    }

    // Engine Core Ticks
    CameraController::Instance().Update(elapsedTime);
    if (m_player) m_postProcess->GetLensDistortion().GetData().glitchStrength = m_player->GetDamageGlitchIntensity();
    Scene::Update(elapsedTime);
    EffectManager::Instance().Update(isPlaying ? elapsedTime : 0.0f);
}

void SceneGame::StartPlayerDeathSequence()
{
    if (m_isDying) return;
    m_isDying = true;
    m_deathTimer = 0.0f;
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
        m_dialogueBox->SetPosition(536.0f, 750.0f);
        m_dialogueBox->StartDialogue({
            u8"目を覚まして。戦いの時間が来たわ。\n{ATK}で攻撃よ。遠くの敵は撃ち抜き、\n近づけばその刃で斬り裂くの。",
            u8"そして、よく覚えておいて。\nいずれそのキーは、敵の牙を弾き返す\n「Parry」の要にもなるわ。魂に刻み込んで。",
            u8"次は{DASH}を試して。\n風のように「Dash」して、敵の弾幕をすり抜けるのよ。\n\nさあ、あなたの力を見せて。"
            });
    }
}

void SceneGame::StartMushroomDialogue()
{
    if (m_dialogueBox)
    {
        m_dialogueBox->StartDialogue({
            u8"あのキノコを見て。今は大人しく見えるけれど…\n気を抜かないで。",
            u8"この森の奥は奇妙な薬液で汚染されているわ。\n凶暴化した個体もいるはずよ。"
            });
    }
}

void SceneGame::ResetLevel()
{
    DirectX::XMFLOAT3 respawnPos{ m_playerSpawnPos };
    if (m_hasCheckpoint) respawnPos = { m_currentCheckpointPos.x, m_playerSpawnPos.y, m_currentCheckpointPos.z };

    if (m_player)
    {
        m_player->SetPosition(respawnPos);
        m_player->GetMovement()->SetVelocity({ 0.0f, 0.0f, 0.0f });
        m_player->SetMaxHP(NORMAL_MAX_HP);
        m_player->SetInputEnabled(false);
        m_player->scale = { 1.0f, 1.0f, 1.0f };
        m_player->GetStateMachine()->ChangeState(m_player.get(), std::make_unique<PlayerIdle>());
        m_player->GetProjectiles().clear();
    }

    if (m_navi)
    {
        m_navi->Reset();
        m_navi->SetPosition({ respawnPos.x + 1.0f, respawnPos.y + 2.0f, respawnPos.z + 0.5f });
    }

    if (EditorManager::Instance().GetEditorMode() == EditorMode::Play)
    {
        for (int i = 0; i < 60; ++i)
        {
            PhysicsManager::Instance().Simulate(0.01666f);
            if (m_player) m_player->Update(0.01666f, nullptr);
            if (m_navi) m_navi->Update(0.01666f, nullptr);
        }
    }
}

void SceneGame::Render(float elapsedTime, Camera* camera)
{
    const float renderTime{ m_isPaused ? 0.0f : elapsedTime };

    // Dynamic Camera Resolution
    // Default to the Editor Free Camera (m_mainCamera)
    Camera* targetCam{ camera ? camera : m_mainCamera.get() };

    // In Play Mode, overwrite with the active scene Camera Brain
    if (EditorManager::Instance().GetEditorMode() == EditorMode::Play && m_sceneRoot)
    {
        for (const auto& child : m_sceneRoot->GetChildren())
        {
            if (auto* brain = child->GetComponent<CameraComponent>())
            {
                targetCam = brain->GetCamera().get();
                break; // Found the Camera Brain!
            }
        }
    }

    auto dc{ Graphics::Instance().GetDeviceContext() };
    auto rs{ Graphics::Instance().GetRenderState() };

    float screenW{ Config::DEFAULT_SCREEN_W };
    float screenH{ Config::DEFAULT_SCREEN_H };

    if (const auto* window{ Framework::Instance()->GetMainWindow() })
    {
        screenW = static_cast<float>(window->GetWidth());
        screenH = static_cast<float>(window->GetHeight());
    }

    static std::uint32_t s_taaFrameIndex{ 0 };
    targetCam->SetJitterEnabled(m_postProcess->IsEnabled() && m_postProcess->GetTemporalAA().IsEnabled());
    targetCam->AdvanceJitter(++s_taaFrameIndex, screenW, screenH);

    if (m_postProcess->IsEnabled()) m_postProcess->BeginCapture();
    else
    {
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

    // Render Editor Gizmos only if we are looking through the Editor Free Camera
    if (targetCam == m_mainCamera.get() && EditorManager::Instance().GetEditorMode() != EditorMode::Play)
    {
        auto shapeRenderer{ Graphics::Instance().GetShapeRenderer() };
        auto primRenderer{ Graphics::Instance().GetPrimitiveRenderer() };

        if (m_itemManager) m_itemManager->RenderDebug(shapeRenderer);
        if (m_enemyManager) m_enemyManager->RenderDebug(shapeRenderer);
        if (m_sceneRoot) m_sceneRoot->DrawGizmo(shapeRenderer);

        VirtualCameraComponent::FlushGizmos(dc, targetCam);
        shapeRenderer->Render(dc, targetCam->GetView(), targetCam->GetProjection());
        primRenderer->Render(dc, targetCam->GetView(), targetCam->GetProjection(), D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
    }
    else
    {
        // Even if we don't render gizmos in play mode, we must safely discard accumulated queue data
        VirtualCameraComponent::FlushGizmos(dc, targetCam);
    }

    if (m_postProcess->IsEnabled()) m_postProcess->EndCapture(renderTime);

    if (m_dialogueBox) m_dialogueBox->Render(dc, screenW, screenH);

    if (m_fadeAlpha > 0.001f && m_fadeSprite)
    {
        dc->OMSetBlendState(rs->GetBlendState(BlendState::Transparency), nullptr, 0xFFFFFFFF);
        dc->OMSetDepthStencilState(rs->GetDepthStencilState(DepthState::NoTestNoWrite), 0);
        const auto fade{ UI::GetScaled(0.0f, 0.0f, 1920.0f, 1080.0f, screenW, screenH) };
        m_fadeSprite->Render(dc, fade.x, fade.y, 0.0f, fade.w, fade.h, 0.0f, 0.0f, 1920.0f, 1080.0f, 0.0f, 0.0f, 0.0f, 0.0f, m_fadeAlpha);
    }

    if (m_isPaused && m_fadeSprite)
    {
        dc->OMSetBlendState(rs->GetBlendState(BlendState::Transparency), nullptr, 0xFFFFFFFF);
        dc->OMSetDepthStencilState(rs->GetDepthStencilState(DepthState::NoTestNoWrite), 0);
        const auto pauseFade{ UI::GetScaled(0.0f, 0.0f, 1920.0f, 1080.0f, screenW, screenH) };
        m_fadeSprite->Render(dc, pauseFade.x, pauseFade.y, 0.0f, pauseFade.w, pauseFade.h, 0.0f, 0.0f, 1920.0f, 1080.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.6f);

        if (m_uiPause) m_uiPause->Render(dc, screenW, screenH, m_isExitingToTitle ? (1.0f - m_fadeAlpha) : 1.0f);
    }

    targetCam->CachePreviousViewProjection();
}

void SceneGame::RenderScene(const float elapsedTime, Camera* camera)
{
    if (!camera) return;
    auto dc{ Graphics::Instance().GetDeviceContext() };
    if (Graphics::Instance().GetLightManager().HasSkybox())
    {
        Graphics::Instance().GetSkyboxRenderer()->Render(dc, *camera, Graphics::Instance().GetLightManager().GetSkyboxSRV());
    }

    auto modelRenderer{ Graphics::Instance().GetModelRenderer() };
    RenderContext rc{ dc, Graphics::Instance().GetRenderState(), camera, &Graphics::Instance().GetLightManager() };

    rc.velocityRenderTargetView = m_postProcess->IsEnabled() && m_postProcess->GetTemporalAA().IsEnabled() ? m_postProcess->GetVelocityRTV() : nullptr;
    rc.psxEnabled = (m_postProcess->IsEnabled() && m_postProcess->GetPSX().GetData().enabled);
    rc.psxResWidth = m_postProcess->GetPSX().GetData().resWidth;
    rc.psxResHeight = m_postProcess->GetPSX().GetData().resHeight;

    if (m_sceneRoot) m_sceneRoot->Render(modelRenderer);

    if (m_player && (!m_player->GetOwnerNode() || m_player->GetOwnerNode()->IsActive()))
    {
        m_player->RenderWeapon(modelRenderer);
        m_player->RenderProjectiles(modelRenderer);
    }
    if (m_navi && (!m_navi->GetOwnerNode() || m_navi->GetOwnerNode()->IsActive())) m_navi->RenderProjectiles(modelRenderer);
    if (m_enemyManager)
    {
        for (auto& enemy : m_enemyManager->GetEnemies())
        {
            if (enemy->GetOwnerNode() && !enemy->GetOwnerNode()->IsActive()) continue;
            enemy->RenderProjectiles(modelRenderer);
        }
    }

    modelRenderer->Render(rc);
    EffectManager::Instance().Render(camera);
}

void SceneGame::OnResize(int width, int height)
{
    if (height <= 0) height = 1;
    if (m_mainCamera) m_mainCamera->SetPerspectiveFov(DirectX::XMConvertToRadians(Config::CAM_FOV), static_cast<float>(width) / static_cast<float>(height), Config::CAM_NEAR, Config::CAM_FAR);
    if (m_postProcess) m_postProcess->OnResize(width, height);
}
