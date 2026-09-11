#include "VirtualCameraComponent.h"
#include <algorithm>
#include <cmath>
#include <imgui.h>
#include "System/Graphics.h"
#include "System/ShapeRenderer.h"
#include "ComponentRegistry.h"
#include "GameObject.h"
#include "CameraController.h"

namespace
{
    [[nodiscard]] constexpr float WrapAngle(float angle) noexcept
    {
        while (angle > DirectX::XM_PI)  angle -= DirectX::XM_2PI;
        while (angle < -DirectX::XM_PI) angle += DirectX::XM_2PI;
        return angle;
    }

    [[nodiscard]] inline float CalculateDampingBlend(float damping, float dt) noexcept
    {
        constexpr float epsilon{ 0.001f };
        if (damping <= epsilon) return 1.0f;
        return 1.0f - std::exp(-damping * dt);
    }

    [[nodiscard]] inline bool IsFloat3Equal(const DirectX::XMFLOAT3& a, const DirectX::XMFLOAT3& b, float ep = 0.0001f) noexcept
    {
        return (std::abs(a.x - b.x) <= ep) && (std::abs(a.y - b.y) <= ep) && (std::abs(a.z - b.z) <= ep);
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
    EnsureSharedGizmoLoaded();
    ResolveTargets();

    if (owner)
    {
        m_cachedPos = owner->GetPosition();
        m_cachedRot = owner->GetRotation();
    }
}

void VirtualCameraComponent::EnsureSharedGizmoLoaded() noexcept
{
    if (!s_sharedGizmoSprite)
    {
        s_sharedGizmoSprite = std::make_shared<Sprite>(Graphics::Instance().GetDevice(), "Data/Icon/Gizmo/Camera.png");
    }
}

GameObject* VirtualCameraComponent::FindTargetByName(const std::string& name) const noexcept
{
    if (name.empty() || !m_owner || !m_owner->GetParent()) return nullptr;

    GameObject* root{ m_owner->GetParent() };
    while (root->GetParent() != nullptr)
    {
        root = root->GetParent();
    }

    for (const auto& child : root->GetChildren())
    {
        if (child->GetName() == name) return child.get();
    }
    return nullptr;
}

void VirtualCameraComponent::ResolveTargets() noexcept
{
    m_followTarget = FindTargetByName(m_followTargetName);
    m_lookAtTarget = FindTargetByName(m_lookAtTargetName);
}

void VirtualCameraComponent::Update(float dt)
{
    if (!m_owner) return;

    // Fast O(1) pointer validation instead of string search
    if (m_followTarget && m_followTarget->IsDestroyed()) m_followTarget = nullptr;
    if (m_lookAtTarget && m_lookAtTarget->IsDestroyed()) m_lookAtTarget = nullptr;

    if (!m_followTarget && !m_lookAtTarget) return;

    DirectX::XMFLOAT3 currentPos{ m_owner->GetPosition() };
    DirectX::XMFLOAT3 currentRot{ m_owner->GetRotation() };

    if (m_followTarget)
    {
        const DirectX::XMFLOAT3 targetPos{ m_followTarget->GetPosition() };
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

    if (m_lookAtTarget)
    {
        const DirectX::XMFLOAT3 targetPos{ m_lookAtTarget->GetPosition() };
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

    if (!IsFloat3Equal(currentPos, m_cachedPos))
    {
        m_owner->SetPosition(currentPos);
        m_cachedPos = currentPos;
    }
    if (!IsFloat3Equal(currentRot, m_cachedRot))
    {
        m_owner->SetRotation(currentRot);
        m_cachedRot = currentRot;
    }
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
        ResolveTargets(); // Re-cache pointer on text change
    }

    ImGui::DragFloat3("Follow Offset", &m_followOffset.x, 0.1f);
    ImGui::DragFloat("Position Damping", &m_positionDamping, 0.1f, 0.0f, 50.0f);

    ImGui::Separator();

    static char s_lookBuf[64];
    strncpy_s(s_lookBuf, sizeof(s_lookBuf), m_lookAtTargetName.c_str(), _TRUNCATE);
    if (ImGui::InputText("Look-At Target", s_lookBuf, sizeof(s_lookBuf)))
    {
        m_lookAtTargetName = s_lookBuf;
        ResolveTargets(); // Re-cache pointer on text change
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

    shapeRenderer->DrawFrustum(pos, rotRad, DirectX::XMConvertToRadians(m_fovDegrees), 16.0f / 9.0f, m_nearZ, m_farZ, { 0.2f, 0.8f, 1.0f, 1.0f }, m_gizmoDrawDistance);

    // Queue to CPU batch array 
    Camera* activeCam{ CameraController::Instance().GetActiveCamera().get() };
    if (activeCam && activeCam->CheckSphere(pos.x, pos.y, pos.z, 0.5f))
    {
        const DirectX::XMFLOAT3 activeCamRot{ activeCam->GetRotation() };

        s_gizmoBatchData.push_back({
            pos.x, pos.y, pos.z,
            0.5f, 0.5f,
            0.0f, 0.0f, 0.0f, 0.0f, 
            activeCamRot.x, activeCamRot.y, activeCamRot.z,
            0.2f, 0.8f, 1.0f, 1.0f  // Cyan
            });
    }
}

void VirtualCameraComponent::FlushGizmos(ID3D11DeviceContext* dc, const Camera* activeCam) noexcept
{
    if (s_gizmoBatchData.empty()) return;

    if (s_sharedGizmoSprite)
    {
        s_sharedGizmoSprite->Render3DBatch(dc, activeCam, s_gizmoBatchData);
    }

    s_gizmoBatchData.clear();
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

    ResolveTargets(); // Resolve immediately upon load
}

REGISTER_COMPONENT(VirtualCameraComponent)