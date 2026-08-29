#pragma once

#include <algorithm>
#include <cstdio>
#include <DirectXMath.h>
#include <imgui.h>
#include <memory>
#include <string>
#include <PxPhysicsAPI.h> 
#include <SDL3/SDL.h>
#include <wrl/client.h> 
#include <vector>
#include "Scene.h"
#include "System/AudioManager.h"
#include "System/CollisionManager.h"
#include "System/Graphics.h"
#include "System/Light.h"
#include "System/Sprite.h"
#include "CameraController.h"
#include "EffectManager.h"
#include "Enemy.h"
#include "EnemyManager.h"
#include "Framework.h"
#include "GameObject.h"             
#include "ItemManager.h"
#include "LegacyCharacterComponent.h"
#include "PhysXUtils.h"
#include "PostProcessManager.h"
#include "Primitive.h"
#include "NaviAlly.h"
#include "Player.h"
#include "PlayerStates.h"
#include "SceneSerializer.h"
#include "Stage.h"
#include "StageComponent.h"
#include "UIDialogueBox.h"
#include "UIPause.h"

// Forward Declarations
class Camera;
class CollisionManager;
class Enemy;
class EnemyManager;
class GameBreakerGUI;
class ItemManager;
class NaviAlly;
class Player;
class PostProcessManager;
class Primitive;
class Stage;
class UIPause;

enum class EditorMode : std::uint8_t;

class SceneGame : public Scene
{
    friend class GameBreakerGUI;

public:
    SceneGame();
    ~SceneGame() override;

    SceneGame(const SceneGame&) = delete;
    SceneGame& operator=(const SceneGame&) = delete;

    void Update(float elapsedTime) override;
    void Render(float elapsedTime, Camera* camera = nullptr) override;
    void OnResize(int width, int height) override;

    Camera* GetMainCamera() const { return m_mainCamera.get(); }
    [[nodiscard]] PostProcessManager* GetPostProcessManager() const noexcept override { return m_postProcess.get(); }

	// Assign unique JSON save path for the Game Screen
    [[nodiscard]] std::string_view GetSceneSavePath() const noexcept override
    {
        return "Data/Scenes/Scene_Game.json"; // or "Data/Scenes/Stage_01.json"
    }

    // Assign unique JSON profile for the Game Screen
    [[nodiscard]] std::string_view GetPostProcessProfilePath() const noexcept override
    {
        return "Data/Config/PostProcess_Game.json";
    }

    [[nodiscard]] EnemyManager* GetEnemyManager() const noexcept { return m_enemyManager.get(); }
    [[nodiscard]] ItemManager* GetItemManager() const noexcept { return m_itemManager.get(); }

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
        static constexpr float FX_CRT_BASE_STRENGTH{ 0.2f };
        static constexpr float FX_CRT_ROTATION_TARGET{ 0.45f };
        static constexpr float FX_TRANSITION_WINDOW{ 0.2f };
        static constexpr float FX_GLITCH_FACTOR{ 0.7f };
    };

    void RenderScene(float elapsedTime, Camera* camera);

    std::unique_ptr<Player> m_player{};
    std::unique_ptr<NaviAlly> m_navi{};
    std::unique_ptr<CollisionManager> m_collisionManager{};
    std::unique_ptr<EnemyManager> m_enemyManager{};
    std::unique_ptr<ItemManager> m_itemManager{};
    std::unique_ptr<Stage> m_stage{};
    std::shared_ptr<Camera> m_mainCamera{};
    std::unique_ptr<UIDialogueBox> m_dialogueBox{};
    std::unique_ptr<UIPause> m_uiPause{};

    DirectX::XMFLOAT3 m_cameraPosition{ 0.0f, 18.0f, 0.0f };
    DirectX::XMFLOAT3 m_cameraTarget{ 0.0f, 0.0f, 0.0f };
    LightManager m_lightManager{};
    std::unique_ptr<PostProcessManager> m_postProcess{};

    std::unique_ptr<Sprite> m_fadeSprite{};
    float m_fadeAlpha{ 1.0f };
    DirectX::XMFLOAT4 m_bgSpriteColor{ 1.0f, 1.0f, 1.0f, 1.0f };
    std::unique_ptr<Sprite> m_whiteSprite{};
    float m_whiteAlpha{ 0.0f };

    float m_globalTime{ 0.0f };
    float m_configFineDensity{ 30.0f };
    float m_configZoomDensity{ 0.0f };

    physx::PxDefaultAllocator m_allocator{};
    physx::PxDefaultErrorCallback m_errorCallback{};
    std::unique_ptr<physx::PxFoundation, PhysXDeleter> m_foundation{};
    std::unique_ptr<physx::PxPhysics, PhysXDeleter> m_physics{};
    std::unique_ptr<physx::PxDefaultCpuDispatcher, PhysXDeleter> m_dispatcher{};
    std::unique_ptr<physx::PxScene, PhysXDeleter> m_scene{};
    std::unique_ptr<physx::PxControllerManager, PhysXDeleter> m_controllerManager{};
    std::unique_ptr<physx::PxMaterial, PhysXDeleter> m_defaultMaterial{};
    std::unique_ptr<physx::PxRigidStatic, PhysXDeleter> m_groundPlane{};

    // Editor state tracking
    EditorMode m_lastEditorMode{};
    DirectX::XMFLOAT3 m_cachedEditorCamPos{ 0.0f, 18.0f, -14.0f };
    DirectX::XMFLOAT3 m_cachedEditorCamRot{ 0.0f, 0.0f, 0.0f };

	// Pause & Exit to Title
    bool m_isPaused{ false };
    bool m_isExitingToTitle{ false };
    float m_exitToTitleTimer{ 0.0f };

    [[nodiscard]] bool CheckPauseToggleTriggered() const noexcept;

	// Health & Damage
    static constexpr float BOSS_MAX_HP{ 150.0f };
    static constexpr float NORMAL_MAX_HP{ 100.0f };

    bool m_hasHealedForBoss{ false };

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
    bool m_hasTriggeredTrackingDialogue{ false };
    bool m_bossDialogueStarted{ false };
    bool m_hasTriggeredPoisonDialogue{ false };
    bool m_isPoisonDialogueActive{ false };

    static constexpr float DEATH_DELAY_DURATION{ 0.5f };
    static constexpr float DEATH_FADE_DURATION{ 3.0f };
    static constexpr float NAVI_DEFEAT_FADE_DURATION{ 3.0f };
    static constexpr float RESPAWN_FADE_DURATION{ 3.0f };
    static constexpr float WHITEOUT_HOLD_DURATION{ 7.0f };
    static constexpr float FADE_BACK_DURATION{ 2.0f };
    static constexpr float DIALOG_CHARACTERS_PER_SECOND{ 18.0f };

    // Post-Process Values for Fading to Black
    static constexpr float FX_BASE_SMOOTHNESS{ 0.2f };
    static constexpr float FX_BASE_INTENSITY{ 0.38f };
    static constexpr float FX_BLACK_SMOOTHNESS{ 7.0f };
    static constexpr float FX_BLACK_INTENSITY{ 5.0f };

    const DirectX::XMFLOAT3 m_playerSpawnPos{ 0.0f, 2.0f, 0.0f };

    void StartPlayerDeathSequence();
    void StartNaviDefeatSequence();
    void StartIntroDialogueTest();
    void UpdateDialogue(float elapsedTime);
    void RenderDialogue();
    void StartMushroomDialogue();
    void StartTrackingDialogue();
    void StartPoisonDialogue();
    void ResetLevel();

    // Cinematic States
    bool AreTrackingEnemiesDead() const;
    class Enemy* GetFakeBoss() const;
    void StartBossCinematic();

    bool m_bossCinematicTriggered{ false };
    bool m_isBossCinematicActive{ false };
    float m_bossCinematicTimer{ 0.0f };
    bool m_bossEffectTriggered{ false };
    int m_poisonEffectHandle{ -1 };

	// Cinematic Constants
    static constexpr float BOSS_CINEMATIC_DURATION{ 4.0f };
    static constexpr float BOSS_CINEMATIC_HOLD_DURATION{ 3.0f };
    static constexpr float BOSS_EFFECT_WHITEOUT_DELAY{ 4.5f }; 
    static constexpr float WHITEOUT_FADE_DURATION{ 3.0f };
    float m_fakeBossEffectScale{ 3.000f };
    DirectX::XMFLOAT3 m_fakeBossEffectOffset{ 9.690f, 0.000f, 24.600f };
    DirectX::XMFLOAT3 m_fakeBossEffectRotation{ 0.000f, 25.000f, 0.000f };
    DirectX::XMFLOAT3 m_cinematicStartTarget{ 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT3 m_cinematicEndTarget{ 0.0f, 0.0f, 0.0f };

    float m_targetZoom{ 0.0f };
    int m_zoomFrameCounter{ 0 };
    const Enemy* m_cachedClosestEnemy{ nullptr };
};
