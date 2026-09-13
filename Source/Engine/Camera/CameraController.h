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

    void SetActiveCamera(std::weak_ptr<Camera> camera) noexcept;
    [[nodiscard]] std::shared_ptr<Camera> GetActiveCamera() const noexcept { return m_activeCamera.lock(); }

    void Update(float dt);

    // Tells the controller if the mouse is safely inside the 3D viewport
    void SetViewportHovered(bool hovered) noexcept { m_isViewportHovered = hovered; }

    // Syncs the controller's internal Euler angles to the physical camera
    void SyncFromActiveCamera() noexcept;

    // Engages or disengages the free-fly camera
    void SetEnabled(bool enabled) noexcept;
    [[nodiscard]] bool IsEnabled() const noexcept { return m_isEnabled; }

    void ClearCamera() noexcept { m_activeCamera.reset(); }

private:
    CameraController() = default;
    ~CameraController() = default;

    CameraController(const CameraController&) = delete;
    CameraController& operator=(const CameraController&) = delete;

    std::weak_ptr<Camera> m_activeCamera{};

    long m_lockedCursorX{ 0 };
    long m_lockedCursorY{ 0 };
    bool m_isEnabled{ false };
    bool m_isViewportHovered{ false };
    bool m_toggleCursor{ true };

    DirectX::XMFLOAT3 m_currentAngle{ 0.0f, 0.0f, 0.0f };
    float m_moveSpeed{ 15.0f };
    float m_rollSpeed{ DirectX::XMConvertToRadians(90.0f) };
};