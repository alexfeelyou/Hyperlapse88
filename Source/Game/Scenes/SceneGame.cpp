#include <filesystem>
#include "System/PhysicsManager.h"
#include "System/RespawnManager.h"
#include "System/TimeManager.h"
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
    if (m_isDying || m_bootTimer > 0.0f)
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

    m_respawnTimer = 0.0f;
    PhysicsManager::Instance().Initialize();

    // Data-Driven Instantiation 
    // Load the scene exactly as authored in the JSON editor
    SceneSerializer::Load(GetSceneSavePath(), m_sceneRoot.get());

    RespawnManager::Instance().ResetRuntimeState();
    m_playerCtrl = nullptr;
    for (const auto& child : m_sceneRoot->GetChildren())
    {
        if (auto* ctrl = child->GetComponent<PlayerControllerComponent>())
        {
            m_playerCtrl = ctrl;
            RespawnManager::Instance().SetFallbackSpawn(child->GetPosition(), child->GetRotation().y);
            break;
        }
    }

    if (m_lastEditorMode == EditorMode::Edit)
    {
        PhysicsManager::Instance().Simulate(0.0f);
        if (m_sceneRoot)
        {
            m_sceneRoot->Update(0.0f);
            m_sceneRoot->LateUpdate(0.0f);
        }
    }

    m_postProcess = std::make_unique<PostProcessManager>();
    m_postProcess->Initialize(static_cast<int>(screenW), static_cast<int>(screenH));
    m_postProcess->SetEnabled(true);
    m_postProcess->LoadConfig(GetPostProcessProfilePath());

    // Preserve the profile's authored Vignette settings across boot and Play/Edit transitions
    const auto& loadedVignette{ m_postProcess->GetVignette().GetData() };
    m_editVignetteEnabled = loadedVignette.enabled;
    m_editVignetteIntensity = loadedVignette.intensity;
    m_editVignetteSmoothness = loadedVignette.smoothness;
    m_playVignetteEnabled = loadedVignette.enabled;
    m_playVignetteIntensity = loadedVignette.intensity;
    m_playVignetteSmoothness = loadedVignette.smoothness;

    m_dialogueBox = std::make_unique<UIDialogueBox>();
    m_dialogueBox->Initialize();

    m_uiPause = std::make_unique<UIPause>();
    m_uiPause->Initialize();

    m_fadeSprite = std::make_unique<Sprite>(Graphics::Instance().GetDevice(), "Data/Sprite/Scene Game/Black.png");
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

    m_sceneRoot.reset();

    PhysicsManager::Instance().Shutdown();

    std::error_code ec;
    std::filesystem::remove("Data/Scenes/AutoSave_PlayMode.json", ec);
}

void SceneGame::Update(const float elapsedTime)
{
    const EditorMode currentMode{ EditorManager::Instance().GetEditorMode() };

    if (m_lastEditorMode != currentMode)
    {
        if (currentMode == EditorMode::Play && m_lastEditorMode == EditorMode::Edit)
        {
            Engine::Time::TimeManager::Instance().ResetGameplayDilation();
            EditorManager::Instance().SaveUserPreferences(this, CameraController::Instance().GetActiveCamera().get());
            SceneSerializer::Save("Data/Scenes/AutoSave_PlayMode.json", m_sceneRoot.get(), false);

            CameraController::Instance().SetEnabled(false);
            RespawnManager::Instance().ResetRuntimeState();
            if (m_playerCtrl && m_playerCtrl->GetOwner())
            {
                RespawnManager::Instance().SetFallbackSpawn(
                    m_playerCtrl->GetOwner()->GetPosition(),
                    m_playerCtrl->GetOwner()->GetRotation().y
                );
            }
            m_bootTimer = 1.1f;
            m_respawnTimer = 0.0f;
            m_isDying = false;
            m_fadeAlpha = 1.0f;

            if (m_postProcess)
            {
                // Snapshot live Edit-mode Vignette state before Play-mode boot fade overrides it
                const auto& currentVignette{ m_postProcess->GetVignette().GetData() };
                m_editVignetteEnabled = currentVignette.enabled;
                m_editVignetteIntensity = currentVignette.intensity;
                m_editVignetteSmoothness = currentVignette.smoothness;
                m_playVignetteEnabled = currentVignette.enabled;
                m_playVignetteIntensity = currentVignette.intensity;
                m_playVignetteSmoothness = currentVignette.smoothness;
                m_isVignetteFading = false;
                ApplyVignetteFade(1.0f);
            }

            if (m_playerCtrl)
            {
                m_playerCtrl->SetInputEnabled(false);

                // PREVENTIVE BUG: Manually wipe stale Editor blackboard state 
                // since the live memory blocks are not destroyed on Play.
                auto& bb{ m_playerCtrl->getAnimBlackboard() };
                bb.setFlag(Engine::Animation::AnimFlag::is_combat_active, false);
                bb.actionIndex = 0;

                if (auto* sm{ m_playerCtrl->GetStateMachine() })
                {
                    sm->ChangeState(m_playerCtrl, m_playerCtrl->GetState(PlayerStateType::Locomotion));
                }
            }
        }
        else if (currentMode == EditorMode::Edit)
        {
            Engine::Time::TimeManager::Instance().ResetGameplayDilation();
            Engine::Time::TimeManager::Instance().ResetDebugTimeScale();
            EditorManager::Instance().ClearSelection();

            if (m_sceneRoot)
            {
                m_sceneRoot->ClearChildren();
            }

            SceneSerializer::Load("Data/Scenes/AutoSave_PlayMode.json", m_sceneRoot.get(), false);

            m_playerCtrl = nullptr;
            for (const auto& child : m_sceneRoot->GetChildren())
            {
                if (auto* ctrl = child->GetComponent<PlayerControllerComponent>())
                {
                    m_playerCtrl = ctrl;
                    break;
                }
            }

            EditorManager::Instance().LoadUserPreferences(this, CameraController::Instance().GetActiveCamera().get());
            CameraController::Instance().SyncFromActiveCamera();
            CameraController::Instance().SetEnabled(true);

            Camera* activeCam{ CameraController::Instance().GetActiveCamera().get() };
            Scene::Update(0.0f);
            Scene::LateUpdate(0.0f);

            m_isPaused = false;
            m_isExitingToTitle = false;
            m_exitToTitleTimer = 0.0f;
            RespawnManager::Instance().ResetRuntimeState();
            m_isDying = false;
            m_respawnTimer = 0.0f;
            m_bootTimer = 0.0f;
            m_hasBGMStarted = false;
            m_hasIntroDialogueTestStarted = false;

            m_dialogueBox = std::make_unique<UIDialogueBox>();
            m_dialogueBox->Initialize();

            if (m_uiPause) m_uiPause->ResetSelection();

            m_fadeAlpha = 0.0f;
            m_isVignetteFading = false;
            RestoreEditVignette();

            EffectManager::Instance().StopAll();
            AudioManager::Instance().StopMusic();
        }
        else if (currentMode == EditorMode::Pause)
        {
            if (m_lastEditorMode == EditorMode::Play)
            {
                if (m_sceneRoot)
                {
                    for (const auto& child : m_sceneRoot->GetChildren())
                    {
                        if (auto* brain = child->GetComponent<CameraComponent>())
                        {
                            auto brainCam = brain->GetCamera();
                            m_mainCamera->SetPosition(brainCam->GetPosition());
                            m_mainCamera->SetRotation(brainCam->GetRotation());
                            m_mainCamera->SetPerspectiveFov(
                                brainCam->GetFovY(),
                                brainCam->GetAspectRatio(),
                                brainCam->GetNearZ(),
                                brainCam->GetFarZ()
                            );
                            break;
                        }
                    }
                }
                CameraController::Instance().SyncFromActiveCamera();
            }
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
                ApplyVignetteFade(t);

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

        auto& timeMgr{ Engine::Time::TimeManager::Instance() };
        timeMgr.Update(elapsedTime);
        const float worldDt{ timeMgr.GetWorldDeltaTime(elapsedTime) };

        m_globalTime += worldDt;
        if (m_globalTime > Config::TIME_LOOP_MAX) m_globalTime -= Config::TIME_LOOP_MAX;

        if (m_bootTimer > 0.0f)
        {
            m_bootTimer -= elapsedTime;
            const float t{ std::clamp(m_bootTimer / 1.1f, 0.0f, 1.0f) };
            m_fadeAlpha = t;
            ApplyVignetteFade(t);

            if (m_bootTimer <= 0.0f && m_playerCtrl)
            {
                m_playerCtrl->SetInputEnabled(true);
            }
        }
        else if (m_isDying)
        {
            m_deathTimer += elapsedTime;
            if (m_deathTimer < DEATH_DELAY_DURATION)
            {
                m_fadeAlpha = 0.0f;
                EndVignetteFade();
            }
            else
            {
                const float t{ std::clamp((m_deathTimer - DEATH_DELAY_DURATION) / DEATH_FADE_DURATION, 0.0f, 1.0f) };
                m_fadeAlpha = t;
                ApplyVignetteFade(t);

                if (t >= 1.0f)
                {
                    m_isDying = false;
                    m_respawnTimer = RESPAWN_FADE_DURATION;
                    m_fadeAlpha = 1.0f;
                    ApplyVignetteFade(1.0f);
                }
            }
        }
        else if (m_respawnTimer > 0.0f)
        {
            m_respawnTimer -= elapsedTime;
            const float t{ std::clamp(m_respawnTimer / RESPAWN_FADE_DURATION, 0.0f, 1.0f) };
            m_fadeAlpha = t * t;
            ApplyVignetteFade(m_fadeAlpha);
        }
        else if (RespawnManager::Instance().IsRespawning())
        {
            const float fade{ RespawnManager::Instance().GetFadeAlpha() };
            m_fadeAlpha = fade;
            ApplyVignetteFade(fade);
        }
        else
        {
            m_fadeAlpha = 0.0f;
            EndVignetteFade();
            if (!m_hasBGMStarted) { AudioManager::Instance().PlayMusic("Data/Sound/BGM_Game.wav", 0.1f, true); m_hasBGMStarted = true; }
        }

        if (!m_hasIntroDialogueTestStarted && m_bootTimer <= 0.0f && m_respawnTimer <= 0.0f && !m_isDying)
        {
            StartIntroDialogueTest();
        }

        if (m_dialogueBox) m_dialogueBox->Update(elapsedTime);

        if (worldDt > 0.00001f)
        {
            PhysicsManager::Instance().Simulate(worldDt);
        }

        RespawnManager::Instance().Update(elapsedTime);
    }

    CameraController::Instance().Update(elapsedTime);

    const float sceneDt{ isPlaying ? Engine::Time::TimeManager::Instance().GetWorldDeltaTime(elapsedTime) : 0.0f };
    Scene::Update(sceneDt);
    Scene::LateUpdate(sceneDt);

    EffectManager::Instance().Update(sceneDt);
}

void SceneGame::StartIntroDialogueTest()
{
    m_hasIntroDialogueTestStarted = true;
    if (m_dialogueBox)
    {
        m_dialogueBox->SetPosition(536.0f, 750.0f);
        m_dialogueBox->StartDialogue(std::vector<std::string>{
            reinterpret_cast<const char*>(u8"目を覚まして。戦いの時間が来たわ。\n{ATK}で攻撃よ。遠くの敵は撃ち抜き、\n近づけばその刃で斬り裂くの。"),
                reinterpret_cast<const char*>(u8"そして、よく覚えておいて。\nいずれそのキーは、敵の牙を弾き返す\n「Parry」の要にもなるわ。魂に刻み込んで。"),
                reinterpret_cast<const char*>(u8"次は{DASH}を試して。\n風のように「Dash」して、敵の弾幕をすり抜けるのよ。\n\nさあ、あなたの力を見せて。")
        });
    }
}

void SceneGame::Render(float elapsedTime, Camera* camera)
{
    const float renderTime{ m_isPaused ? 0.0f : Engine::Time::TimeManager::Instance().GetWorldDeltaTime(elapsedTime) };

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

    // Gizmo Render Pass
    const bool isEditMode = EditorManager::Instance().GetEditorMode() != EditorMode::Play;
    const bool shouldDrawGizmos = EditorManager::Instance().GetShowGizmos();

    if (shouldDrawGizmos)
    {
        auto shapeRenderer{ Graphics::Instance().GetShapeRenderer() };
        auto primRenderer{ Graphics::Instance().GetPrimitiveRenderer() };

        // Pack the context payload with the bitmask from the Editor
        GizmoContext ctx{ 
            shapeRenderer, 
            primRenderer, 
            targetCam, 
            EditorManager::Instance().GetGizmoMask() 
        };
        
        // Broadcast to all active components in the hierarchy
        if (m_sceneRoot) m_sceneRoot->DrawGizmo(ctx);

        VirtualCameraComponent::FlushGizmos(dc, targetCam);
        shapeRenderer->Render(dc, targetCam->GetView(), targetCam->GetProjection());
        primRenderer->Render(dc, targetCam->GetView(), targetCam->GetProjection(), D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
    }
    else
    {
        // Safely discard accumulated queue data if gizmos are toggled off
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

    modelRenderer->Render(rc);
    EffectManager::Instance().Render(camera);
}

void SceneGame::OnResize(int width, int height)
{
    if (height <= 0) height = 1;

    // Only update Aspect Ratio so we don't destroy synced FOV settings from Play Mode
    if (m_mainCamera)
    {
        m_mainCamera->SetAspectRatio(static_cast<float>(width) / static_cast<float>(height));
    }

    if (m_postProcess) m_postProcess->OnResize(width, height);
}

void SceneGame::CapturePlayVignetteBaseline() noexcept
{
    if (!m_postProcess) return;

    const auto& vig{ m_postProcess->GetVignette().GetData() };
    m_playVignetteEnabled = vig.enabled;
    m_playVignetteIntensity = vig.intensity;
    m_playVignetteSmoothness = vig.smoothness;
}

void SceneGame::ApplyVignetteFade(float fadeAlpha) noexcept
{
    if (!m_postProcess) return;

    const float clampedFade{ std::clamp(fadeAlpha, 0.0f, 1.0f) };
    if (clampedFade <= 0.001f)
    {
        EndVignetteFade();
        return;
    }

    // Snapshot the player's current Play-mode Vignette settings right when a fade begins
    if (!m_isVignetteFading)
    {
        CapturePlayVignetteBaseline();
        m_isVignetteFading = true;
    }

    // When Vignette is turned off, let m_fadeSprite drive the transition cleanly without forcing Vignette on
    if (!m_playVignetteEnabled)
    {
        return;
    }

    auto& vig{ m_postProcess->GetVignette().GetData() };
    vig.smoothness = m_playVignetteSmoothness + (FX_BLACK_SMOOTHNESS - m_playVignetteSmoothness) * clampedFade;
    vig.intensity = m_playVignetteIntensity + (FX_BLACK_INTENSITY - m_playVignetteIntensity) * clampedFade;
}

void SceneGame::EndVignetteFade() noexcept
{
    if (!m_isVignetteFading) return;

    m_isVignetteFading = false;
    if (!m_postProcess) return;

    auto& vig{ m_postProcess->GetVignette().GetData() };
    vig.enabled = m_playVignetteEnabled;
    vig.intensity = m_playVignetteIntensity;
    vig.smoothness = m_playVignetteSmoothness;
}

void SceneGame::RestoreEditVignette() noexcept
{
    if (!m_postProcess) return;

    auto& vig{ m_postProcess->GetVignette().GetData() };
    vig.enabled = m_editVignetteEnabled;
    vig.intensity = m_editVignetteIntensity;
    vig.smoothness = m_editVignetteSmoothness;
}