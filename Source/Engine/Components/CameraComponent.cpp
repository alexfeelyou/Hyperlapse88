#include "CameraComponent.h"
#include <algorithm>
#include <cmath>
#include <utility>
#include <imgui.h>
#include "ComponentRegistry.h"
#include "GameObject.h"
#include "System/ShapeRenderer.h"
#include "System/Graphics.h"
#include "CameraController.h"
#include "VirtualCameraComponent.h"

namespace
{
    [[nodiscard]] inline bool IsFloat3Equal(const DirectX::XMFLOAT3& a, const DirectX::XMFLOAT3& b, float ep = 0.0001f) noexcept
    {
        return (std::abs(a.x - b.x) <= ep) && (std::abs(a.y - b.y) <= ep) && (std::abs(a.z - b.z) <= ep);
    }

    [[nodiscard]] DirectX::XMFLOAT3 ExtractEulerFromQuaternion(const DirectX::XMVECTOR& quat) noexcept
    {
        DirectX::XMFLOAT4X4 mRot{};
        DirectX::XMStoreFloat4x4(&mRot, DirectX::XMMatrixRotationQuaternion(quat));

        const float pitch{ std::asinf(std::clamp(-mRot._32, -1.0f, 1.0f)) };
        float yaw{ 0.0f }, roll{ 0.0f };

        constexpr float epsilon{ 0.0001f };
        if (std::cos(pitch) > epsilon)
        {
            yaw = std::atan2(mRot._31, mRot._33);
            roll = std::atan2(mRot._12, mRot._22);
        }
        else
        {
            yaw = std::atan2(-mRot._13, mRot._11);
        }
        return { pitch, yaw, roll };
    }

    [[nodiscard]] std::pair<DirectX::XMFLOAT3, DirectX::XMFLOAT3> ExtractWorldTransform(const DirectX::XMFLOAT4X4& worldMatrix) noexcept
    {
        const DirectX::XMMATRIX matWorld{ DirectX::XMLoadFloat4x4(&worldMatrix) };
        DirectX::XMVECTOR vScale{}, vRotQuat{}, vTrans{};

        if (DirectX::XMMatrixDecompose(&vScale, &vRotQuat, &vTrans, matWorld))
        {
            DirectX::XMFLOAT3 pos{};
            DirectX::XMStoreFloat3(&pos, vTrans);
            return { pos, ExtractEulerFromQuaternion(vRotQuat) };
        }
        return { {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f} };
    }
}

CameraComponent::CameraComponent()
{
    ApplyProjectionSettings();
}

void CameraComponent::OnAttach(GameObject* owner) noexcept
{
    IComponent::OnAttach(owner);
    VirtualCameraComponent::EnsureSharedGizmoLoaded();
}

void CameraComponent::Update(float dt)
{
    if (!GetOwner()) return;

    VirtualCameraComponent* bestVCam{ nullptr };
    int highestPriority{ -1 };

    for (VirtualCameraComponent* vcam : VirtualCameraComponent::GetRegistry())
    {
        if (vcam->GetOwner() && vcam->GetOwner()->IsActive())
        {
            if (vcam->GetPriority() > highestPriority)
            {
                highestPriority = vcam->GetPriority();
                bestVCam = vcam;
            }
        }
    }

    if (bestVCam != m_activeVirtualCamera)
    {
        m_blendTimer = 0.0f;
        m_blendStartPos = m_camera->GetPosition();
        m_blendStartRot = m_camera->GetRotation();

        if (!m_activeVirtualCamera) m_blendTimer = m_blendDuration;
        m_activeVirtualCamera = bestVCam;
    }

    DirectX::XMFLOAT3 targetPos{};
    DirectX::XMFLOAT3 targetRot{};

    if (m_activeVirtualCamera)
    {
        const auto& [vPos, vRot] = ExtractWorldTransform(m_activeVirtualCamera->GetOwner()->transform.GetWorldMatrix());

        if (m_blendTimer < m_blendDuration)
        {
            m_blendTimer += dt;
            const float t{ std::clamp(m_blendTimer / m_blendDuration, 0.0f, 1.0f) };
            const float smoothT{ t * t * (3.0f - 2.0f * t) };

            const DirectX::XMVECTOR vStartPos{ DirectX::XMLoadFloat3(&m_blendStartPos) };
            const DirectX::XMVECTOR vEndPos{ DirectX::XMLoadFloat3(&vPos) };
            DirectX::XMStoreFloat3(&targetPos, DirectX::XMVectorLerp(vStartPos, vEndPos, smoothT));

            const DirectX::XMVECTOR qStart{ DirectX::XMQuaternionRotationRollPitchYawFromVector(DirectX::XMLoadFloat3(&m_blendStartRot)) };
            const DirectX::XMVECTOR qEnd{ DirectX::XMQuaternionRotationRollPitchYawFromVector(DirectX::XMLoadFloat3(&vRot)) };
            targetRot = ExtractEulerFromQuaternion(DirectX::XMQuaternionSlerp(qStart, qEnd, smoothT));

            m_fovDegrees = m_fovDegrees + (m_activeVirtualCamera->GetFovDegrees() - m_fovDegrees) * smoothT;
            ApplyProjectionSettings();
        }
        else
        {
            targetPos = vPos;
            targetRot = vRot;

            if (std::abs(m_fovDegrees - m_activeVirtualCamera->GetFovDegrees()) > 0.01f)
            {
                m_fovDegrees = m_activeVirtualCamera->GetFovDegrees();
                ApplyProjectionSettings();
            }
        }
    }
    else
    {
        const auto& [wPos, wRot] = ExtractWorldTransform(GetOwner()->transform.GetWorldMatrix());
        targetPos = wPos;
        targetRot = wRot;
    }

    if (!IsFloat3Equal(targetPos, m_lastPos) || !IsFloat3Equal(targetRot, m_lastRot))
    {
        m_camera->SetPosition(targetPos);
        m_camera->SetRotation(targetRot);
        m_lastPos = targetPos;
        m_lastRot = targetRot;

        if (m_activeVirtualCamera)
        {
            GetOwner()->SetPosition(targetPos);
            GetOwner()->SetRotation({
                DirectX::XMConvertToDegrees(targetRot.x),
                DirectX::XMConvertToDegrees(targetRot.y),
                DirectX::XMConvertToDegrees(targetRot.z)
                });
        }
    }
}

void CameraComponent::SetAspectRatio(float aspectRatio) noexcept
{
    constexpr float epsilon{ 0.001f };
    if (aspectRatio <= epsilon) return;

    m_aspectRatio = aspectRatio;
    ApplyProjectionSettings();
}

void CameraComponent::ApplyProjectionSettings() noexcept
{
    m_camera->SetPerspectiveFov(
        DirectX::XMConvertToRadians(m_fovDegrees),
        m_aspectRatio,
        m_nearZ,
        m_farZ);
}

void CameraComponent::DrawInspector()
{
    ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.0f, 1.0f), "CAMERA BRAIN");
    ImGui::Text("Active Target: %s", m_activeVirtualCamera ? m_activeVirtualCamera->GetOwner()->GetName().c_str() : "None");

    ImGui::Separator();
    ImGui::DragFloat("Blend Duration", &m_blendDuration, 0.1f, 0.0f, 10.0f);

    ImGui::Separator();
    bool projectionDirty{ false };
    projectionDirty |= ImGui::DragFloat("Near Clip", &m_nearZ, 0.01f, 0.01f, m_farZ - 0.01f);
    projectionDirty |= ImGui::DragFloat("Far Clip", &m_farZ, 1.0f, m_nearZ + 0.01f, 100000.0f);
    ImGui::DragFloat("Gizmo Draw Distance", &m_gizmoDrawDistance, 0.1f, 0.5f, 100.0f);

    if (projectionDirty)
    {
        ApplyProjectionSettings();
    }
}

void CameraComponent::DrawGizmo(ShapeRenderer* shapeRenderer) noexcept
{
    if (!shapeRenderer || !GetOwner()) return;

    const auto& [worldPos, worldRotRad] = ExtractWorldTransform(GetOwner()->transform.GetWorldMatrix());

    constexpr float r{ 1.0f }, g{ 0.85f }, b{ 0.0f }, a{ 1.0f };
    constexpr DirectX::XMFLOAT4 gizmoColor{ r, g, b, a };

    shapeRenderer->DrawFrustum(
        worldPos,
        worldRotRad,
        DirectX::XMConvertToRadians(m_fovDegrees),
        m_aspectRatio,
        m_nearZ,
        m_farZ,
        gizmoColor,
        m_gizmoDrawDistance);

    Camera* activeCam{ CameraController::Instance().GetActiveCamera().get() };
    if (activeCam && activeCam->CheckSphere(worldPos.x, worldPos.y, worldPos.z, 0.5f))
    {
        const DirectX::XMFLOAT3 activeCamRot{ activeCam->GetRotation() };

        VirtualCameraComponent::QueueGizmoIcon({
            worldPos.x, worldPos.y, worldPos.z,
            0.5f, 0.5f,
            0.0f, 0.0f, 0.0f, 0.0f,
            activeCamRot.x, activeCamRot.y, activeCamRot.z,
            r, g, b, a
            });
    }
}

void CameraComponent::Serialize(nlohmann::json& outJson) const
{
    outJson["BlendDuration"] = m_blendDuration;
    outJson["NearZ"] = m_nearZ;
    outJson["FarZ"] = m_farZ;
}

void CameraComponent::Deserialize(const nlohmann::json& inJson)
{
    m_blendDuration = inJson.value("BlendDuration", m_blendDuration);
    m_nearZ = inJson.value("NearZ", m_nearZ);
    m_farZ = inJson.value("FarZ", m_farZ);
    ApplyProjectionSettings();
}

REGISTER_COMPONENT(CameraComponent)