#include <algorithm>
#include <imgui.h>
#include "System/Input.h" 
#include "Camera.h"
#include "CameraController.h"

// Define WIN32_LEAN_AND_MEAN to strip out heavy Windows.h bloat
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

CameraController& CameraController::Instance() noexcept
{
    static CameraController s_instance{};
    return s_instance;
}

void CameraController::SetActiveCamera(std::weak_ptr<Camera> camera) noexcept
{
    m_editorCamera = std::move(camera);
    SyncFromActiveCamera();
}

std::shared_ptr<Camera> CameraController::GetActiveCamera() const noexcept
{
    // Prioritize editor camera when free-fly mode is active (Edit / Pause mode)
    if (m_isEnabled)
    {
        if (auto editorCam{ m_editorCamera.lock() })
        {
            return editorCam;
        }
    }

    // In Play mode, prioritize the gameplay camera registered by CameraComponent
    if (auto gameCam{ m_gameCamera.lock() })
    {
        return gameCam;
    }

    return m_editorCamera.lock();
}

std::shared_ptr<Camera> CameraController::GetEditorCamera() const noexcept
{
    return m_editorCamera.lock();
}

void CameraController::ClearGameCamera(const std::shared_ptr<Camera>& camera) noexcept
{
    if (m_gameCamera.lock() == camera)
    {
        m_gameCamera.reset();
    }
}

void CameraController::ClearCamera() noexcept
{
    m_editorCamera.reset();
}

void CameraController::SetEnabled(bool enabled) noexcept
{
    m_isEnabled = enabled;

    if (enabled)
    {
        // Do not force the cursor to lock on boot. Let the right-click logic handle it.
        m_toggleCursor = false;
        SyncFromActiveCamera();
    }
    else
    {
        // If the scene changes or the editor shuts down while we were flying, 
        // we must release the trap and make the cursor visible again.
        if (m_toggleCursor)
        {
            SetCursorPos(m_lockedCursorX, m_lockedCursorY);
            ShowCursor(TRUE);
        }

        m_toggleCursor = false;
        Input::Instance().GetMouse().LockCursor(false);
    }
}

void CameraController::SyncFromActiveCamera() noexcept
{
    if (std::shared_ptr<Camera> camera{ m_editorCamera.lock() })
    {
        m_currentAngle = camera->GetRotation();
    }
}

void CameraController::Update(float dt)
{
    // Fast fail if the Editor Camera is not explicitly enabled
    if (!m_isEnabled) return;

    std::shared_ptr<Camera> camera{ m_editorCamera.lock() };
    if (!camera) return;

    // Hold Right Mouse Button to fly/look
    // ImGui Window Focus Safety
    ImGuiIO& io = ImGui::GetIO();
    const bool isRightClickDown = io.MouseDown[ImGuiMouseButton_Right];

    static bool s_wasRightBtnDown{ false };
    const bool justPressed{ isRightClickDown && !s_wasRightBtnDown };
    const bool justReleased{ !isRightClickDown && s_wasRightBtnDown };
    s_wasRightBtnDown = isRightClickDown;

    if (justPressed)
    {
        // Only engage if we aren't clicking on an ImGui menu, unless we are already hovering the 3D viewport
        if (!io.WantCaptureMouse || m_isViewportHovered)
        {
            m_toggleCursor = true;

            // Anchor the cursor using a local POINT struct
            POINT p;
            GetCursorPos(&p);
            m_lockedCursorX = p.x;
            m_lockedCursorY = p.y;

            ShowCursor(FALSE);
        }
    }
    else if (justReleased)
    {
        if (m_toggleCursor)
        {
            m_toggleCursor = false;
            // Restore the cursor
            SetCursorPos(m_lockedCursorX, m_lockedCursorY);
            ShowCursor(TRUE);
        }
    }

    if (!m_toggleCursor) return;

    // Calculate deltas manually
    POINT currentCursorPos;
    GetCursorPos(&currentCursorPos);

    float deltaX = static_cast<float>(currentCursorPos.x - m_lockedCursorX);
    float deltaY = static_cast<float>(currentCursorPos.y - m_lockedCursorY);

    // Trap the cursor
    SetCursorPos(m_lockedCursorX, m_lockedCursorY);

    // Invalidate the very first frame's delta to prevent an initial jump
    if (justPressed)
    {
        deltaX = 0.0f;
        deltaY = 0.0f;
    }

    // Apply rotation
    // Mouse displacement is an instantaneous pointer delta, not a velocity: do not multiply by dt
    constexpr float sensitivity{ 0.003f };

    m_currentAngle.y += deltaX * sensitivity;
    m_currentAngle.x += deltaY * sensitivity;

    // Ensure Pitch does not exceed vertical bounds to prevent screen flip
    constexpr float PITCH_LIMIT{ DirectX::XM_PIDIV2 - 0.01f };
    m_currentAngle.x = std::clamp(m_currentAngle.x, -PITCH_LIMIT, PITCH_LIMIT);

    camera->SetRotation(m_currentAngle);

    // Translation (WASDQE)
    const float moveAmount{ m_moveSpeed * dt };
    DirectX::XMFLOAT3 moveDir{ 0.0f, 0.0f, 0.0f };

    if (GetKeyState('W') & 0x8000) moveDir.z += moveAmount;
    if (GetKeyState('S') & 0x8000) moveDir.z -= moveAmount;
    if (GetKeyState('A') & 0x8000) moveDir.x -= moveAmount;
    if (GetKeyState('D') & 0x8000) moveDir.x += moveAmount;
    if (GetKeyState('E') & 0x8000) moveDir.y += moveAmount; // Go Up
    if (GetKeyState('Q') & 0x8000) moveDir.y -= moveAmount; // Go Down

    camera->Translate(moveDir);
}