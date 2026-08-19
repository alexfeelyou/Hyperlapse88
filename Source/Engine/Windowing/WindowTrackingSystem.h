#pragma once

#include <vector>
#include <string>
#include <memory>
#include <functional>
#include <unordered_map>
#include <DirectXMath.h>
#include "Camera.h"
#include "BeyondWindow.h"

// =========================================================
// DATA STRUCTURES
// =========================================================
enum class WindowRole {
    MAIN_VIEWPORT,
    TRACKED_ENTITY,
    SUB_VIEWPORT
};

struct WindowState
{
    int actualX = 0;
    int actualY = 0;
    float targetX = 0.0f;
    float targetY = 0.0f;

    int actualW = 0;
    int actualH = 0;
    float targetW = 0.0f;
    float targetH = 0.0f;
};

struct TrackedWindowConfig
{
    std::string name;
    std::string title;
    int width = 300;
    int height = 300;
    int priority = 0;
    DirectX::XMFLOAT3 trackingOffset = { 0.0f, 0.0f, 0.0f };
    float fpsLimit = 0.0f;
    bool isTransparent = false;

    bool isAlwaysOnTop = false; // <--- TAMBAHKAN INI (Default: false)

    WindowRole role = WindowRole::TRACKED_ENTITY;
};

struct TrackedWindow
{
    std::string name;
    Beyond::Window* window = nullptr;
    std::shared_ptr<Camera> camera;
    WindowState state;
    DirectX::XMFLOAT3 trackingOffset = { 0.0f, 0.0f, 0.0f };
    std::function<DirectX::XMFLOAT3()> getTargetPositionFunc;
    std::function<DirectX::XMFLOAT2()> getTargetSizeFunc = nullptr;

    WindowRole role = WindowRole::TRACKED_ENTITY;

    bool isActive = true;
    bool isTransparent = false;
};

// =========================================================
// WINDOW TRACKING SYSTEM
// =========================================================
class WindowTrackingSystem
{
public:
    WindowTrackingSystem();
    ~WindowTrackingSystem();

    // Core Logic
    void Update(float dt);

    // Management
    void RegisterWindow(Beyond::Window* window, WindowRole role, std::shared_ptr<Camera> camera = nullptr); // <-- BARU
    void UpdateWindowBounds(int windowIndex, int width, int height);

    // Management
    bool AddTrackedWindow(
        const TrackedWindowConfig& config,
        std::function<DirectX::XMFLOAT3()> getTargetPos,
        std::function<DirectX::XMFLOAT2()> getTargetSize = nullptr // Default null
    );
    void RemoveTrackedWindow(const std::string& name);
    TrackedWindow* GetTrackedWindow(const std::string& name);
    void ClearAll();

    // Configuration
    void SetFollowSpeed(float speed) { m_followSpeed = speed; }
    void SetPixelToUnitRatio(float ratio) { m_pixelToUnitRatio = ratio; }
    void SetFOV(float fov) { m_fov = fov; }
    float GetPixelToUnitRatio() const { return m_pixelToUnitRatio; }

    // Accessors for Rendering (misal untuk menggambar overlay shatter)
    const std::vector<std::unique_ptr<TrackedWindow>>& GetWindows() const { return m_trackedWindows; }

    // Helpers (Public karena mungkin Scene butuh untuk debug/rendering overlay)
    void WorldToScreenPos(const DirectX::XMFLOAT3& worldPos, float& outScreenX, float& outScreenY);
    float GetUnifiedCameraHeight();

    // Jembatan untuk Pool System
    std::unique_ptr<TrackedWindow> ExtractForPool(const std::string& name);
    void RestoreFromPool(std::unique_ptr<TrackedWindow> window);

private:
    void UpdateSingleWindow(float dt, TrackedWindow& tracked);
    void UpdateOffCenterProjection(Camera* targetCam, int winX, int winY, int winW, int winH, float camHeight);    void GetScreenDimensions(int& outWidth, int& outHeight);

private:
    std::vector<std::unique_ptr<TrackedWindow>> m_trackedWindows;
    std::unordered_map<std::string, TrackedWindow*> m_windowLookup;

    // Cache Screen Metrics
    int m_cachedScreenWidth = 0;
    int m_cachedScreenHeight = 0;
    float m_cacheUpdateTimer = 0.0f;

    // Settings
    float m_followSpeed = 100.0f;
    float m_pixelToUnitRatio = 40.0f;
    float m_fov = 60.0f;
};