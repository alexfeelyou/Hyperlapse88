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
    m_activeCamera = std::move(camera);
    SyncFromActiveCamera();
}

void CameraController::SetEnabled(bool enabled) noexcept
{
    m_isEnabled = enabled;

    if (enabled)
    {
        m_toggleCursor = true;
        SyncFromActiveCamera();
    }
    else
    {
        // Explicitly release the mouse when the editor camera shuts down
        m_toggleCursor = false;
        Input::Instance().GetMouse().LockCursor(false);
    }
}

void CameraController::SyncFromActiveCamera() noexcept
{
    if (std::shared_ptr<Camera> camera{ m_activeCamera.lock() })
    {
        m_currentAngle = camera->GetRotation();
    }
}

void CameraController::Update(float dt)
{
    // Fast fail if the Editor Camera is not explicitly enabled
    if (!m_isEnabled) return;

    std::shared_ptr<Camera> camera{ m_activeCamera.lock() };
    if (!camera) return;

    // Hold Right Mouse Button to fly/look
    const bool isRightClickDown{ (GetKeyState(VK_RBUTTON) & 0x8000) != 0 };

    // ImGui Window Focus Safety
    if (ImGui::GetCurrentContext() != nullptr)
    {
        const ImGuiIO& io{ ImGui::GetIO() };
        if (io.WantCaptureMouse && !m_isViewportHovered && !m_toggleCursor)
        {
            m_toggleCursor = false;
        }
        else
        {
            m_toggleCursor = isRightClickDown;
        }
    }
    else
    {
        m_toggleCursor = isRightClickDown;
    }

    Input::Instance().GetMouse().LockCursor(m_toggleCursor);

    // Only process look and movement if the cursor is actively locked into the viewport
    if (!m_toggleCursor) return;

    // Track the Right Mouse Button state globally across all frames
    static bool s_wasRightBtnDown{ false };
    static int s_ignoreFrames{ 0 };

    const bool justPressed{ isRightClickDown && !s_wasRightBtnDown };
    s_wasRightBtnDown = isRightClickDown;

    if (justPressed)
    {
        // Absorb the initial click frame and the subsequent OS warp frame
        s_ignoreFrames = 2;
    }

    auto& mouse{ Input::Instance().GetMouse() };
    float deltaX{ static_cast<float>(mouse.GetDeltaX()) };
    float deltaY{ static_cast<float>(mouse.GetDeltaY()) };

    // Discard the massive delta spike while the OS centers the cursor
    if (s_ignoreFrames > 0)
    {
        deltaX = 0.0f;
        deltaY = 0.0f;
        --s_ignoreFrames;
    }

    // Apply rotation purely using the cached angle to prevent matrix read-back drift
    constexpr float sensitivity{ 0.5f };
    const float rotSpeed{ m_rollSpeed * dt };

    m_currentAngle.y += deltaX * sensitivity * rotSpeed;
    m_currentAngle.x += deltaY * sensitivity * rotSpeed;

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