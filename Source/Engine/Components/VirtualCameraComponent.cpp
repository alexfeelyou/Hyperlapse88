#include <algorithm>
#include <cmath>
#include <imgui.h>
#include "System/Graphics.h"
#include "System/ShapeRenderer.h"
#include "ComponentRegistry.h"
#include "GameObject.h"
#include "CameraController.h"
#include "VirtualCameraComponent.h"

namespace
{
    // Resolves the shortest rotational path to prevent 360-degree unwinding spins
    [[nodiscard]] constexpr float WrapAngle(float angle) noexcept
    {
        while (angle > DirectX::XM_PI)  angle -= DirectX::XM_2PI;
        while (angle < -DirectX::XM_PI) angle += DirectX::XM_2PI;
        return angle;
    }

    // Framerate-independent damping formula
    [[nodiscard]] inline float CalculateDampingBlend(float damping, float dt) noexcept
    {
        constexpr float epsilon{ 0.001f };
        if (damping <= epsilon) return 1.0f;
        return 1.0f - std::exp(-damping * dt);
    }
}

VirtualCameraComponent::VirtualCameraComponent()
{
    s_registry.push_back(this);
}

VirtualCameraComponent::~VirtualCameraComponent()
{
    s_registry.erase(std::remove(s_registry.begin(), s_registry.end(), this), s_registry.end());
}

void VirtualCameraComponent::OnAttach(GameObject* owner) noexcept
{
    IComponent::OnAttach(owner);
    LoadGizmoIcon();
}

void VirtualCameraComponent::LoadGizmoIcon() noexcept
{
    if (m_iconLoaded) return;

    // Instantiate the sprite specifically for 3D billboard rendering
    m_gizmoSprite = std::make_unique<Sprite>(Graphics::Instance().GetDevice(), "Data/Icon/Gizmo/Camera.png");

    m_iconLoaded = true;
}

GameObject* VirtualCameraComponent::FindTargetByName(const std::string& name) const noexcept
{
    if (name.empty() || !m_owner || !m_owner->GetParent()) return nullptr;

    // Traverse up to the Scene Root
    GameObject* root{ m_owner->GetParent() };
    while (root->GetParent() != nullptr)
    {
        root = root->GetParent();
    }

    // Search top-level scene nodes
    for (const auto& child : root->GetChildren())
    {
        if (child->GetName() == name)
        {
            return child.get();
        }
    }
    return nullptr;
}

void VirtualCameraComponent::Update(float dt)
{
    if (!m_owner) return;

    DirectX::XMFLOAT3 currentPos{ m_owner->GetPosition() };
    DirectX::XMFLOAT3 currentRot{ m_owner->GetRotation() };

    // Follow Target Resolution (Position)
    if (GameObject * followTarget{ FindTargetByName(m_followTargetName) })
    {
        const DirectX::XMFLOAT3 targetPos{ followTarget->GetPosition() };

        const DirectX::XMFLOAT3 desiredPos{
            targetPos.x + m_followOffset.x,
            targetPos.y + m_followOffset.y,
            targetPos.z + m_followOffset.z
        };

        const float tPos{ CalculateDampingBlend(m_positionDamping, dt) };
        currentPos.x += (desiredPos.x - currentPos.x) * tPos;
        currentPos.y += (desiredPos.y - currentPos.y) * tPos;
        currentPos.z += (desiredPos.z - currentPos.z) * tPos;
    }

    // Look-At Target Resolution (Rotation)
    if (GameObject * lookTarget{ FindTargetByName(m_lookAtTargetName) })
    {
        const DirectX::XMFLOAT3 targetPos{ lookTarget->GetPosition() };
        const float dx{ targetPos.x - currentPos.x };
        const float dy{ targetPos.y - currentPos.y };
        const float dz{ targetPos.z - currentPos.z };
        const float horizontalDist{ std::sqrt((dx * dx) + (dz * dz)) };

        const float desiredPitch{ std::atan2(-dy, horizontalDist) };
        const float desiredYaw{ std::atan2(dx, dz) };

        const float currentPitchRad{ DirectX::XMConvertToRadians(currentRot.x) };
        const float currentYawRad{ DirectX::XMConvertToRadians(currentRot.y) };

        const float pitchDiff{ WrapAngle(desiredPitch - currentPitchRad) };
        const float yawDiff{ WrapAngle(desiredYaw - currentYawRad) };

        const float tRot{ CalculateDampingBlend(m_rotationDamping, dt) };

        currentRot.x = DirectX::XMConvertToDegrees(currentPitchRad + (pitchDiff * tRot));
        currentRot.y = DirectX::XMConvertToDegrees(currentYawRad + (yawDiff * tRot));
        currentRot.z = 0.0f;
    }

    m_owner->SetPosition(currentPos);
    m_owner->SetRotation(currentRot);
}

void VirtualCameraComponent::DrawInspector()
{
    ImGui::DragInt("Priority", &m_priority, 1, 0, 999);
    ImGui::Separator();

    static char s_followBuf[64];
    strncpy_s(s_followBuf, sizeof(s_followBuf), m_followTargetName.c_str(), _TRUNCATE);
    if (ImGui::InputText("Follow Target", s_followBuf, sizeof(s_followBuf)))
    {
        m_followTargetName = s_followBuf;
    }

    ImGui::DragFloat3("Follow Offset", &m_followOffset.x, 0.1f);
    ImGui::DragFloat("Position Damping", &m_positionDamping, 0.1f, 0.0f, 50.0f);

    ImGui::Separator();

    static char s_lookBuf[64];
    strncpy_s(s_lookBuf, sizeof(s_lookBuf), m_lookAtTargetName.c_str(), _TRUNCATE);
    if (ImGui::InputText("Look-At Target", s_lookBuf, sizeof(s_lookBuf)))
    {
        m_lookAtTargetName = s_lookBuf;
    }
    ImGui::DragFloat("Rotation Damping", &m_rotationDamping, 0.1f, 0.0f, 50.0f);

    ImGui::Separator();

    ImGui::DragFloat("Field of View", &m_fovDegrees, 0.1f, 1.0f, 179.0f);
    ImGui::DragFloat("Near Clip", &m_nearZ, 0.01f, 0.01f, m_farZ - 0.01f);
    ImGui::DragFloat("Far Clip", &m_farZ, 1.0f, m_nearZ + 0.01f, 100000.0f);

    ImGui::Separator();
    ImGui::DragFloat("Gizmo Draw Distance", &m_gizmoDrawDistance, 0.1f, 0.5f, 100.0f);
}

void VirtualCameraComponent::DrawGizmo(ShapeRenderer* shapeRenderer) noexcept
{
    if (!shapeRenderer || !m_owner) return;

    const DirectX::XMFLOAT3 pos{ m_owner->GetPosition() };
    const DirectX::XMFLOAT3 rot{ m_owner->GetRotation() };

    const DirectX::XMFLOAT3 rotRad{
        DirectX::XMConvertToRadians(rot.x),
        DirectX::XMConvertToRadians(rot.y),
        DirectX::XMConvertToRadians(rot.z)
    };

    // Virtual Camera Color: Cyan
    constexpr float r{ 0.2f }, g{ 0.8f }, b{ 1.0f }, a{ 1.0f };
    constexpr DirectX::XMFLOAT4 gizmoColor{ r, g, b, a };

    // Draw Frustum Wireframe
    shapeRenderer->DrawFrustum(pos, rotRad, DirectX::XMConvertToRadians(m_fovDegrees), 16.0f / 9.0f, m_nearZ, m_farZ, gizmoColor, m_gizmoDrawDistance);

    // Draw 3D Billboard Icon
    if (m_gizmoSprite)
    {
        Camera* activeCam{ CameraController::Instance().GetActiveCamera().get() };
        if (activeCam && activeCam->CheckSphere(pos.x, pos.y, pos.z, 0.5f))
        {
            auto dc{ Graphics::Instance().GetDeviceContext() };
            const DirectX::XMFLOAT3 activeCamRot{ activeCam->GetRotation() };

            m_gizmoSprite->Render(
                dc, activeCam,
                pos.x, pos.y, pos.z,
                0.5f, 0.5f,
                activeCamRot.x, activeCamRot.y, activeCamRot.z,
                r, g, b, a   // Tints the icon Cyan
            );
        }
    }
}

void VirtualCameraComponent::Serialize(nlohmann::json& outJson) const
{
    outJson["Priority"] = m_priority;
    outJson["FollowTarget"] = m_followTargetName;
    outJson["LookAtTarget"] = m_lookAtTargetName;
    outJson["FollowOffsetX"] = m_followOffset.x;
    outJson["FollowOffsetY"] = m_followOffset.y;
    outJson["FollowOffsetZ"] = m_followOffset.z;
    outJson["PositionDamping"] = m_positionDamping;
    outJson["RotationDamping"] = m_rotationDamping;
    outJson["FovDegrees"] = m_fovDegrees;
    outJson["NearZ"] = m_nearZ;
    outJson["FarZ"] = m_farZ;
}

void VirtualCameraComponent::Deserialize(const nlohmann::json& inJson)
{
    m_priority = inJson.value("Priority", m_priority);
    m_followTargetName = inJson.value("FollowTarget", m_followTargetName);
    m_lookAtTargetName = inJson.value("LookAtTarget", m_lookAtTargetName);
    m_followOffset.x = inJson.value("FollowOffsetX", m_followOffset.x);
    m_followOffset.y = inJson.value("FollowOffsetY", m_followOffset.y);
    m_followOffset.z = inJson.value("FollowOffsetZ", m_followOffset.z);
    m_positionDamping = inJson.value("PositionDamping", m_positionDamping);
    m_rotationDamping = inJson.value("RotationDamping", m_rotationDamping);
    m_fovDegrees = inJson.value("FovDegrees", m_fovDegrees);
    m_nearZ = inJson.value("NearZ", m_nearZ);
    m_farZ = inJson.value("FarZ", m_farZ);
}

REGISTER_COMPONENT(VirtualCameraComponent)