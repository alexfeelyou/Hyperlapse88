#include <imgui.h>
#include "System/Input.h"
#include "CameraController.h"
#include "CameraInput.h"
#include "ComponentRegistry.h"
#include "EditorManager.h"
#include "GameObject.h"
#include "OrbitCameraDriverComponent.h"
#include "VirtualCameraComponent.h"

void OrbitCameraDriverComponent::OnAttach(GameObject* owner) noexcept
{
    IComponent::OnAttach(owner);
    s_activeInstance = this;

    if (m_owner)
    {
        m_orbitCamera = m_owner->GetComponent<VirtualCameraComponent>();
    }
}

OrbitCameraDriverComponent::~OrbitCameraDriverComponent()
{
    if (s_activeInstance == this)
    {
        s_activeInstance = nullptr;
    }
}

float OrbitCameraDriverComponent::GetActiveYawRadians() noexcept
{
    if (!s_activeInstance || !s_activeInstance->m_orbitCamera) return 0.0f;

    GameObject* cameraObject{ s_activeInstance->m_orbitCamera->GetOwner() };
    if (!cameraObject) return 0.0f;

    // Reads the VCam's already-damped GameObject rotation
    return DirectX::XMConvertToRadians(cameraObject->GetRotation().y);
}

bool OrbitCameraDriverComponent::IsMouseCaptured() noexcept
{
    // State poll: If the camera doesn't exist, the mouse isn't captured.
    return s_activeInstance ? s_activeInstance->m_isCaptured : false;
}

void OrbitCameraDriverComponent::Update(float dt)
{
    // Fast fail outside Play: reset capture state so it automatically captures next time we hit Play.
    if (EditorManager::Instance().GetEditorMode() != EditorMode::Play)
    {
        Input::Instance().GetMouse().LockCursor(false);
        m_isCaptured = true;
        return;
    }

    if (!m_orbitCamera)
    {
        m_orbitCamera = m_owner->GetComponent<VirtualCameraComponent>();
        if (!m_orbitCamera) return;
    }

    // Shift + F1 to release the cursor back to the Editor
    const bool isShiftDown = ImGui::IsKeyDown(ImGuiKey_LeftShift) || ImGui::IsKeyDown(ImGuiKey_RightShift);
    if (m_isCaptured && isShiftDown && ImGui::IsKeyPressed(ImGuiKey_F1, false))
    {
        m_isCaptured = false;
    }
    // Left click inside the 3D Viewport to re-capture the mouse
    else if (!m_isCaptured && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && CameraController::Instance().IsViewportHovered())
    {
        m_isCaptured = true;
    }

    m_orbitCamera->SetOrbitEnabled(true);

    if (m_isCaptured)
    {
        Input::Instance().GetMouse().LockCursor(true);
        const DirectX::XMFLOAT2 delta{ CameraInput::ResolveOrbitDelta(dt) };
        m_orbitCamera->AddOrbitYaw(delta.x);
        m_orbitCamera->AddOrbitPitch(delta.y);
    }
    else
    {
        Input::Instance().GetMouse().LockCursor(false);
    }
}

void OrbitCameraDriverComponent::OnDisable() noexcept
{
    Input::Instance().GetMouse().LockCursor(false);
}

void OrbitCameraDriverComponent::DrawInspector()
{
    ImGui::TextDisabled("Third-Person Orbit Camera Driver");
    ImGui::Separator();

    if (!m_orbitCamera)
    {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "No sibling VirtualCameraComponent found.");
        return;
    }

    // Read-only live view
    ImGui::BeginDisabled();
    float yawDeg{ DirectX::XMConvertToDegrees(m_orbitCamera->GetOrbitYaw()) };
    float pitchDeg{ DirectX::XMConvertToDegrees(m_orbitCamera->GetOrbitPitch()) };
    float distance{ m_orbitCamera->GetOrbitDistance() };
    ImGui::DragFloat("Yaw (deg)", &yawDeg);
    ImGui::DragFloat("Pitch (deg)", &pitchDeg);
    ImGui::DragFloat("Distance", &distance);
    ImGui::EndDisabled();
}

REGISTER_COMPONENT(OrbitCameraDriverComponent)