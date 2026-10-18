#pragma once

#include <DirectXMath.h>
#include <memory>

class Camera;

// CameraController (Editor Free-Fly Camera)
// Strictly handles the developer/editor flying camera. 

class CameraController
{
public:
    [[nodiscard]] static CameraController& Instance() noexcept;

    // Sets the developer/editor free-fly camera (controlled by WASDEQ + RMB)
    void SetActiveCamera(std::weak_ptr<Camera> camera) noexcept;
    void SetEditorCamera(std::weak_ptr<Camera> camera) noexcept { SetActiveCamera(std::move(camera)); }
    [[nodiscard]] std::shared_ptr<Camera> GetEditorCamera() const noexcept;

    // Sets the runtime gameplay camera (driven by CameraComponent / VirtualCamera)
    void SetGameCamera(std::weak_ptr<Camera> camera) noexcept { m_gameCamera = std::move(camera); }
    void ClearGameCamera(const std::shared_ptr<Camera>& camera) noexcept;
    [[nodiscard]] std::shared_ptr<Camera> GetGameCamera() const noexcept { return m_gameCamera.lock(); }

    // Returns the active camera for rendering & post-processing:
    // Prioritizes editor camera when free-fly is enabled (Edit/Pause), gameplay camera in Play mode.
    [[nodiscard]] std::shared_ptr<Camera> GetActiveCamera() const noexcept;

    void Update(float dt);

    // Tells the controller if the mouse is safely inside the 3D viewport
    void SetViewportHovered(bool hovered) noexcept { m_isViewportHovered = hovered; }
    [[nodiscard]] bool IsViewportHovered() const noexcept { return m_isViewportHovered; }

    // Syncs the controller's internal Euler angles to the physical camera
    void SyncFromActiveCamera() noexcept;

    // Engages or disengages the free-fly camera
    void SetEnabled(bool enabled) noexcept;
    [[nodiscard]] bool IsEnabled() const noexcept { return m_isEnabled; }

    void ClearCamera() noexcept;

private:
    CameraController() = default;
    ~CameraController() = default;

    CameraController(const CameraController&) = delete;
    CameraController& operator=(const CameraController&) = delete;

    std::weak_ptr<Camera> m_editorCamera{};
    std::weak_ptr<Camera> m_gameCamera{};

    long m_lockedCursorX{ 0 };
    long m_lockedCursorY{ 0 };
    bool m_isEnabled{ false };
    bool m_isViewportHovered{ false };
    bool m_toggleCursor{ true };

    DirectX::XMFLOAT3 m_currentAngle{ 0.0f, 0.0f, 0.0f };
    float m_moveSpeed{ 15.0f };
    float m_rollSpeed{ DirectX::XMConvertToRadians(90.0f) };
};