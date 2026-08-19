#pragma once
#include <DirectXMath.h>
#include <memory>
#include <vector>
#include <string>
#include "BeyondWindow.h"
#include "Camera.h"
#include "INaviPhase.h"

class WindowTrackingSystem;
class Sprite;

// ============================================================
// FaceParams - Controls the glitch matrix animation on the boss face
// ============================================================
struct FaceParams {
    // Tile swap timing
    float minInterval = 1.0f;
    float maxInterval = 2.0f;

    // Visual layout
    float faceTotalSize = 5.0f;
    int   gridResolution = 8;

    // Large tile chance (0–100)
    float chance2x2 = 30.0f;

    // Flicker: chance per-frame a tile swaps instantly (0–10%)
    float flickerChance = 1.5f;

    // Color glitch: chance of RGB shift on interval tick (0–100%)
    float colorGlitchChance = 25.0f;

    bool  enableGlitch = true;
};

// ============================================================
// Boss - Top-level boss entity. Delegates gameplay logic
//            to INaviPhase implementations (State Machine).
// ============================================================
class Boss {
public:
    Boss();
    ~Boss();

    // ----- Lifecycle -----
    void Initialize(WindowTrackingSystem* windowSystem);
    void SpawnHeadWindow(); // Call when the opening dialogue reaches line 2
    void Update(float dt);
    void Render(ID3D11DeviceContext* context, Camera* currentCamera);

    // ----- State Machine -----
    void ChangePhase(std::unique_ptr<INaviPhase> newPhase);
    INaviPhase* GetCurrentPhase() const { return m_currentPhase.get(); }

    // ----- Transform -----
    DirectX::XMFLOAT3 GetPosition() const { return m_position; }
    void              SetPosition(const DirectX::XMFLOAT3& pos) { m_position = pos; }

    // ----- System Access -----
    WindowTrackingSystem* GetWindowSystem() const { return m_windowSystem; }
    Beyond::Window* GetMainWindow()   const { return m_naviWindow; }

    // ----- Stats -----
    float GetHP() const { return m_hp; }
    void  TakeDamage(float damage) { m_hp -= damage; }

    // ----- Breathing Animation -----
    void  SetCoreBreathParams(float speed, float intensity) { m_breathSpeed = speed; m_breathIntensity = intensity; }
    float GetCoreBreathSpeed()     const { return m_breathSpeed; }
    float GetCoreBreathIntensity() const { return m_breathIntensity; }

    // ----- Window / Size -----
    void               SetBaseWindowSize(float w, float h) { m_baseWindowSize = { w, h }; }
    void               SetWindowSize(float w, float h) { m_windowSize = { w, h }; }
    void               SetWindowTitle(const std::string& newTitle);
    const std::string& GetWindowTitle() const { return m_currentTitle; }

    // ----- Face Grid -----
    void       InitializeFaceGrid(ID3D11Device* device);
    void       UpdateFaceGlitch(float dt);
    void       RenderFaceGrid(ID3D11DeviceContext* context, Camera* currentCamera);
    void       SetGridResolution(int res);
    FaceParams& GetFaceParams() { return m_faceParams; }

    // ----- Visibility -----
    void  SetFaceSpriteVisible(bool visible) { m_isFaceSpriteVisible = visible; }
    void  SetGridGrowthLimit(float limit) { m_currentGridLimit = limit; }
    float GetGridGrowthLimit() const { return m_currentGridLimit; }

private:
    // ============================================================
    // Face Grid Tile - data for one cell in the glitch matrix
    // ============================================================
    struct FaceTile {
        int   texIdx = 0;
        float timer = 0.0f;
        float interval = 0.1f; // How fast this tile swaps
        int   size = 1;     // 1 = 1x1, 2 = 2x2 merged tile
        DirectX::XMFLOAT3 color = { 1.0f, 1.0f, 1.0f };
    };

    void RandomizeFaceGrid();

    // ----- System References -----
    WindowTrackingSystem* m_windowSystem = nullptr;

    // ----- State Machine -----
    std::unique_ptr<INaviPhase> m_currentPhase;

    // ----- OS Window Handles -----
    Beyond::Window* m_naviWindow = nullptr;
    std::shared_ptr<Camera>  m_naviCamera;
    HWND                     m_hHeadWindow = nullptr;
    std::string              m_currentTitle = "mat_grass.png";
    std::vector<std::string> m_glitchTitles;

    // ----- Core Visuals -----
    std::unique_ptr<Sprite> m_faceSprite;
    bool m_isFaceSpriteVisible = true;
    bool m_isOSWindowVisible = true;

    // ----- Transform & Size -----
    DirectX::XMFLOAT3 m_position = { 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT2 m_baseWindowSize = { 200.0f, 200.0f };
    DirectX::XMFLOAT2 m_windowSize = { 400.0f, 400.0f };

    // ----- Timers & Animation -----
    float m_hp = 100.0f;
    float m_breathSpeed = 2.0f;
    float m_breathIntensity = 14.0f;
    float m_glitchTimer = 0.0f;
    float m_breathTimer = 0.0f;
    float m_pixelToUnit = 40.0f;

    // ----- Face Grid Data -----
    // NOTE: Only one declaration of m_faceGrid — as a dynamic 2D vector.
    //       The grid is resized via SetGridResolution().
    std::vector<std::unique_ptr<Sprite>>        m_faceTextures;
    std::vector<std::vector<FaceTile>>          m_faceGrid;
    FaceParams                                  m_faceParams;
    float                                       m_currentGridLimit = 1.0f;
};