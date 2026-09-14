#pragma once

#include <algorithm>
#include <cstdio>
#include <DirectXMath.h>
#include <imgui.h>
#include <memory>
#include <string>
#include <vector>
#include <PxPhysicsAPI.h> 
#include <SDL3/SDL.h>
#include <wrl/client.h> 

#include "Scene.h"
#include "System/AudioManager.h"
#include "System/CollisionManager.h"
#include "System/Graphics.h"
#include "System/Sprite.h"

// Component Includes
#include "AnimationComponent.h"
#include "CameraController.h"
#include "CameraComponent.h"
#include "CapsuleColliderComponent.h"
#include "CharacterMovementComponent.h"
#include "MeshComponent.h"
#include "PlayerControllerComponent.h"
#include "VirtualCameraComponent.h"

// System Includes
#include "EffectManager.h"
#include "Framework.h"
#include "GameObject.h"             
#include "Light.h"
#include "PostProcessManager.h"
#include "Primitive.h"
#include "SceneSerializer.h"

// UI Includes
#include "UIDialogueBox.h"
#include "UIPause.h"

// Forward Declarations
class Camera;
class CollisionManager;
class GameBreakerGUI;
class PlayerControllerComponent;
class PostProcessManager;
class UIPause;
class UIDialogueBox;
class NaviAlly;

enum class EditorMode : std::uint8_t;

class SceneGame final : public Scene
{
    friend class GameBreakerGUI;

public:
    SceneGame();
    ~SceneGame() override;

    SceneGame(const SceneGame&) = delete;
    SceneGame& operator=(const SceneGame&) = delete;
    SceneGame(SceneGame&&) = delete;
    SceneGame& operator=(SceneGame&&) = delete;

    void Update(float elapsedTime) override;
    void Render(float elapsedTime, Camera* camera = nullptr) override;
    void OnResize(int width, int height) override;

    [[nodiscard]] Camera* GetMainCamera() const noexcept { return m_mainCamera.get(); }
    [[nodiscard]] PostProcessManager* GetPostProcessManager() const noexcept override { return m_postProcess.get(); }

    [[nodiscard]] std::string_view GetSceneSavePath() const noexcept override { return "Data/Scenes/Scene_Game.json"; }
    [[nodiscard]] std::string_view GetPostProcessProfilePath() const noexcept override { return "Data/Config/PostProcess_Game.json"; }
    [[nodiscard]] std::string_view GetSceneName() const noexcept override { return "Scene_Game"; }

private:
    struct Config {
        static constexpr float DEFAULT_SCREEN_W{ 1920.0f };
        static constexpr float DEFAULT_SCREEN_H{ 1080.0f };
        static constexpr float TIME_LOOP_MAX{ 1000.0f };
        static constexpr float GRAVITY{ -9.81f };
        static constexpr float CAM_FOV{ 45.0f };
        static constexpr float CAM_NEAR{ 0.1f };
        static constexpr float CAM_FAR{ 1000.0f };
        static constexpr float CAM_START_HEIGHT{ 20.0f };
    };

    void RenderScene(float elapsedTime, Camera* camera);

    PlayerControllerComponent* m_playerCtrl{ nullptr };

    std::shared_ptr<Camera> m_mainCamera{};

    std::unique_ptr<UIDialogueBox> m_dialogueBox{};
    std::unique_ptr<UIPause> m_uiPause{};
    std::unique_ptr<PostProcessManager> m_postProcess{};

    std::unique_ptr<Sprite> m_fadeSprite{};
    float m_fadeAlpha{ 1.0f };
    std::unique_ptr<Sprite> m_whiteSprite{};
    float m_whiteAlpha{ 0.0f };

    float m_globalTime{ 0.0f };

    EditorMode m_lastEditorMode{};

    bool m_isPaused{ false };
    bool m_isExitingToTitle{ false };
    float m_exitToTitleTimer{ 0.0f };

    [[nodiscard]] bool CheckPauseToggleTriggered() const noexcept;

    // Death & Respawn
    bool m_isDying{ false };
    float m_deathTimer{ 0.0f };
    float m_bootTimer{ 1.1f };
    float m_respawnTimer{ RESPAWN_FADE_DURATION };
    bool m_hasBGMStarted{ false };
    bool m_isNaviDefeatSequenceActive{ false };
    float m_naviDefeatTimer{ 0.0f };
    bool m_isNaviDefeatReadyForNextScene{ false };
    DirectX::XMFLOAT3 m_currentCheckpointPos{ 0.0f, 0.0f, 0.0f };
    bool m_hasCheckpoint{ false };

    bool m_hasIntroDialogueTestStarted{ false };
    bool m_hasTriggeredMushroomDialogue{ false };

    static constexpr float DEATH_DELAY_DURATION{ 0.5f };
    static constexpr float DEATH_FADE_DURATION{ 3.0f };
    static constexpr float NAVI_DEFEAT_FADE_DURATION{ 3.0f };
    static constexpr float RESPAWN_FADE_DURATION{ 3.0f };

    static constexpr float FX_BASE_SMOOTHNESS{ 0.2f };
    static constexpr float FX_BASE_INTENSITY{ 0.38f };
    static constexpr float FX_BLACK_SMOOTHNESS{ 7.0f };
    static constexpr float FX_BLACK_INTENSITY{ 5.0f };

    DirectX::XMFLOAT3 m_playerSpawnPos{ 0.0f, 0.0f, 0.0f };

    void StartNaviDefeatSequence();
    void StartIntroDialogueTest();
    void StartMushroomDialogue();
};