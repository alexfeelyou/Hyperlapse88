#include <algorithm>
#include <cmath>
#include <imgui.h>
#include <utility>
#include "System/Graphics.h"
#include "System/ShapeRenderer.h"
#include "CameraComponent.h"
#include "CameraController.h"
#include "ComponentRegistry.h"
#include "GameObject.h"

// Anonymous namespace for internal helper functions (Internal Linkage)
namespace
{
    // Decomposes a 4x4 world matrix into absolute position and Euler rotation (radians).
    [[nodiscard]] std::pair<DirectX::XMFLOAT3, DirectX::XMFLOAT3> ExtractWorldTransform(const DirectX::XMFLOAT4X4& worldMatrix) noexcept
    {
        const DirectX::XMMATRIX matWorld{ DirectX::XMLoadFloat4x4(&worldMatrix) };

        DirectX::XMVECTOR vScale{};
        DirectX::XMVECTOR vRotQuat{};
        DirectX::XMVECTOR vTrans{};

        // Extract scale, rotation, and translation components
        if (DirectX::XMMatrixDecompose(&vScale, &vRotQuat, &vTrans, matWorld))
        {
            DirectX::XMFLOAT3 pos{};
            DirectX::XMStoreFloat3(&pos, vTrans);

            // Convert quaternion back to a rotation matrix to extract Euler angles
            DirectX::XMFLOAT4X4 mRot{};
            DirectX::XMStoreFloat4x4(&mRot, DirectX::XMMatrixRotationQuaternion(vRotQuat));

            // Extract Pitch (X), Yaw (Y), Roll (Z) in radians
            // Clamp pitch to prevent NaN from precision errors when evaluating asin()
            const float pitch{ std::asinf(std::clamp(-mRot._32, -1.0f, 1.0f)) };
            float yaw{ 0.0f };
            float roll{ 0.0f };

            // Guard against Gimbal Lock (when looking straight up or down, cos(pitch) nears 0)
            constexpr float epsilon{ 0.0001f };
            if (std::cos(pitch) > epsilon)
            {
                yaw = std::atan2(mRot._31, mRot._33);
                roll = std::atan2(mRot._12, mRot._22);
            }
            else
            {
                // In gimbal lock, roll and yaw axes align. Force roll to 0 and calculate yaw
                yaw = std::atan2(-mRot._13, mRot._11);
                roll = 0.0f;
            }

            return { pos, { pitch, yaw, roll } };
        }

        // Safe fallback in the rare event matrix decomposition fails (e.g., zero scale)
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
    LoadGizmoIcon();
}

void CameraComponent::LoadGizmoIcon() noexcept
{
    if (m_iconLoaded) return;
    m_gizmoSprite = std::make_unique<Sprite>(Graphics::Instance().GetDevice(), "Data/Icon/Gizmo/Camera.png");
    m_iconLoaded = true;
}

void CameraComponent::Update(float dt)
{
    if (!GetOwner()) return;

    // Use Structured Binding to unpack the decomposed world matrix safely.
    // This allows the Camera Brain to render correctly even if its GameObject 
    // is deeply nested in the hierarchy (e.g. tracking a Player Socket).
    const auto& [worldPos, worldRotRad] = ExtractWorldTransform(GetOwner()->transform.GetWorldMatrix());

    m_camera->SetPosition(worldPos);
    m_camera->SetRotation(worldRotRad); // m_camera expects radians natively
}

void CameraComponent::SetAspectRatio(float aspectRatio) noexcept
{
    // Guard against degenerate window states (e.g., minimized application)
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
    bool projectionDirty{ false };

    // Use bitwise OR to evaluate all dirty flags without short-circuiting UI rendering
    projectionDirty |= ImGui::DragFloat("Field of View", &m_fovDegrees, 0.1f, 1.0f, 179.0f);
    projectionDirty |= ImGui::DragFloat("Near Clip", &m_nearZ, 0.01f, 0.01f, m_farZ - 0.01f);
    projectionDirty |= ImGui::DragFloat("Far Clip", &m_farZ, 1.0f, m_nearZ + 0.01f, 100000.0f);

    ImGui::Separator();
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

    // Brain Camera Color: Distinct Yellow
    constexpr float r{ 1.0f }, g{ 0.85f }, b{ 0.0f }, a{ 1.0f };
    constexpr DirectX::XMFLOAT4 gizmoColor{ r, g, b, a };

    // Draw Yellow Frustum
    shapeRenderer->DrawFrustum(
        worldPos,
        worldRotRad,
        DirectX::XMConvertToRadians(m_fovDegrees),
        m_aspectRatio,
        m_nearZ,
        m_farZ,
        gizmoColor,
        m_gizmoDrawDistance);

    // Draw Yellow Tinted 3D Billboard
    if (m_gizmoSprite)
    {
        Camera* activeCam{ CameraController::Instance().GetActiveCamera().get() };
        if (activeCam && activeCam->CheckSphere(worldPos.x, worldPos.y, worldPos.z, 0.5f))
        {
            auto dc{ Graphics::Instance().GetDeviceContext() };
            const DirectX::XMFLOAT3 activeCamRot{ activeCam->GetRotation() };

            m_gizmoSprite->Render(
                dc, activeCam,
                worldPos.x, worldPos.y, worldPos.z,
                0.5f, 0.5f,
                activeCamRot.x, activeCamRot.y, activeCamRot.z,
                r, g, b, a  // Tints the icon Yellow
            );
        }
    }
}

void CameraComponent::Serialize(nlohmann::json& outJson) const
{
    outJson["FovDegrees"] = m_fovDegrees;
    outJson["NearZ"] = m_nearZ;
    outJson["FarZ"] = m_farZ;
}

void CameraComponent::Deserialize(const nlohmann::json& inJson)
{
    m_fovDegrees = inJson.value("FovDegrees", m_fovDegrees);
    m_nearZ = inJson.value("NearZ", m_nearZ);
    m_farZ = inJson.value("FarZ", m_farZ);
    ApplyProjectionSettings();
}

// Automatically registers CameraComponent in the Factory before main() executes
REGISTER_COMPONENT(CameraComponent)