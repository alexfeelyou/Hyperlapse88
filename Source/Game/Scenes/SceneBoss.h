#pragma once

#include <memory>
#include <vector>
#include <string>
// =========================================================
// Untuk mengaktifkan kembali ImGui debug panel, uncomment baris ini:
// #define NAVI_DEBUG_GUI
// =========================================================

#include <imgui.h>

#include <PxPhysicsAPI.h>

#include "Scene.h"
#include "Camera.h"
#include "CameraController.h"
#include "Player.h"
#include "PostProcessManager.h"
#include "Primitive.h"
#include "WindowTrackingSystem.h"
#include "System/PrimitiveRenderer.h"
#include "System/Sprite.h"
#include "UberShader.h"
#include "BeyondWindow.h"
#include "PhysXUtils.h"
#include "Boss.h"

// =========================================================
// SCENE BOSS - WINDOWKILL MODE
// =========================================================

class CollisionManager;
class EnemyManager;
class ItemManager;
class Stage;

class SceneBoss : public Scene
{
public:
    SceneBoss();
    ~SceneBoss() override;

    void Update(float elapsedTime) override;
    void Shutdown();
    void Render(float elapsedTime, Camera* camera = nullptr) override;
    void DrawGUI() override;
    void OnResize(int width, int height) override;

    [[nodiscard]] Camera* GetMainCamera() const { return m_mainCamera.get(); }
    [[nodiscard]] Player* GetPlayer()     const { return m_player.get(); }
    bool IsPendingSceneChange() const { return m_isPendingSceneChange; }

    void CloseSubWindowBySDLID(Uint32 sdlWindowID);

private:
    // --- Render Pipeline ---
    void RenderScene(float elapsedTime, Camera* camera, bool isTransparentWindow);

    // --- Initialization ---
    void InitializePhysics();
    void InitializeSubWindows();

    // --- Debug & System Helpers ---
    void ResetEverything();
    void SpawnDebugWindow();
    void SpawnTransparentWindow(float bgAlpha, const std::string& typeSuffix);
    void AddLog(const std::string& message);

private:
    // =========================================================
    // LOCAL CONSTANTS
    // =========================================================
    static constexpr float k_fov = 60.0f;
    static constexpr float k_pixelToUnitRatio = 40.0f;
    static constexpr float k_camNear = 0.1f;
    static constexpr float k_camFar = 1000.0f;
    static constexpr float k_defaultWinSize = 200.0f;
    //static constexpr float k_screenLimitX = 23.0f;
    //static constexpr float k_screenLimitZ = 12.5f;

    // =========================================================
    // CORE COMPONENTS
    // =========================================================
    std::shared_ptr<Camera>               m_mainCamera;
    std::unique_ptr<Player>               m_player;
    std::unique_ptr<WindowTrackingSystem> m_windowSystem;
    std::unique_ptr<Primitive>            m_primitive2D;
    std::unique_ptr<PrimitiveRenderer>    m_primitive3D;

    DirectX::XMFLOAT3 m_cameraPosition{ 0.0f, 18.0f, 0.0f }; // Posisi default (Top View)
    DirectX::XMFLOAT3 m_cameraTarget{ 0.0f, 0.0f, 0.0f };   // Titik yang dilihat kamera

    std::unique_ptr<EnemyManager>         m_enemyManager;
    std::unique_ptr<ItemManager>          m_itemManager;
    std::unique_ptr<Stage>                m_stage;
    std::unique_ptr<CollisionManager>     m_collisionManager;
    std::unique_ptr<Boss>             m_navi;

    // =========================================================
    // PHYSX (minimal — no ground plane, no gravity)
    // =========================================================
    physx::PxDefaultAllocator                                    m_allocator;
    physx::PxDefaultErrorCallback                                m_errorCallback;
    std::unique_ptr<physx::PxFoundation, PhysXDeleter>           m_foundation;
    std::unique_ptr<physx::PxPhysics, PhysXDeleter>              m_physics;
    std::unique_ptr<physx::PxDefaultCpuDispatcher, PhysXDeleter> m_dispatcher;
    std::unique_ptr<physx::PxScene, PhysXDeleter>                m_scene;
    std::unique_ptr<physx::PxControllerManager, PhysXDeleter>    m_controllerManager;
    std::unique_ptr<physx::PxMaterial, PhysXDeleter>             m_defaultMaterial;

    // =========================================================
    // SQUASH & STRETCH STATE
    // =========================================================
    DirectX::XMFLOAT2 m_currentStretch = { 0.0f, 0.0f };
    DirectX::XMFLOAT2 m_stretchOffset = { 0.0f, 0.0f };

    // =========================================================
    // GUI / DEBUG STATE
    // =========================================================
    bool      m_showGrid = false;
    float     m_timeScale = 1.0f;
    int       m_spawnCount = 0;
    bool      m_autoSyncMainWindow = false;
    bool      m_topmostEnabled = false;
    bool      m_playerWindowTransparent = false;

    ImVec2    m_debugPanelSize = { 450.0f, 750.0f };

    bool      m_showHitboxes = true; // <--- TAMBAHKAN INI (Default True)

    // =========================================================
    // CAMERA COMBAT ZOOM (Furi Style)
    // =========================================================
    float     m_targetZoom = 0.0f;
    float     m_currentZoom = 0.0f;
    float     m_combatRadius = 25.0f; // Radius deteksi
    float     m_maxZoomIn = -8.0f;    // Seberapa jauh kamera turun (Y)

    // Terminal log (capped at 50 lines)
    std::vector<std::string> m_debugLogs;

    DirectX::XMFLOAT4 m_clearColor = { 0.0f, 0.0f, 0.0f, 1.0f }; // Default: Abu-abu Gelap (R, G, B, A)

private:
    // =========================================================
    // DEATH & RESPAWN SEQUENCE
    // =========================================================
    bool m_isDying{ false };
    float m_deathTimer{ 0.0f };
    float m_respawnTimer{ 0.0f };
    float m_fadeAlpha{ 0.0f };

    std::unique_ptr<PostProcessManager> m_postProcess{};
    UberShader::UberData m_uberParams{};
    std::unique_ptr<Sprite> m_fadeSprite{};

    static constexpr float DEATH_DELAY_DURATION{ 0.5f };
    static constexpr float DEATH_FADE_DURATION{ 3.0f };
    static constexpr float RESPAWN_FADE_DURATION{ 3.0f };

    // Post-Process Values for Fading to Black
    static constexpr float FX_BASE_SMOOTHNESS{ 0.2f };
    static constexpr float FX_BASE_INTENSITY{ 0.0f }; 
    static constexpr float FX_BLACK_SMOOTHNESS{ 7.0f };
    static constexpr float FX_BLACK_INTENSITY{ 5.0f };

    void StartPlayerDeathSequence();
    void ResetLevel();

private:
    bool m_isPendingSceneChange{ false };
    std::unique_ptr<Sprite> m_whiteSprite{};
    float m_whiteAlpha{ 0.0f };
    bool  m_isNaviDefeated{ false };
    float m_naviDefeatTimer{ 0.0f };

    static constexpr float NAVI_DEATH_ANIM_DURATION{ 4.0f };
    static constexpr float WHITE_FADE_DURATION{ 2.0f };
private:
    // --- Pengaturan Desain Keseimbangan Game (Tuning) ---
    float m_overdriveBossHpTriggerPercent = 30.0f; // Default: Uncap aktif saat HP Boss di bawah 30%
    bool  m_forceUncapOverride = false;            // Kontrol manual untuk memaksa mode Uncap
    bool  m_lastUncapState = false;                // Menyimpan status state frame sebelumnya
};