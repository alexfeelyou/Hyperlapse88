#include <imgui.h>
#include "ComponentRegistry.h"
#include "GameObject.h"
#include "System/ShapeRenderer.h"
#include "CameraComponent.h"

CameraComponent::CameraComponent()
{
	ApplyProjectionSettings();
}

void CameraComponent::Update(float dt)
{
	if (!GetOwner()) return;

	// Transform is authored in degrees 
	// making every controller that writes to the Transform know about the difference
	const DirectX::XMFLOAT3& posDeg{ GetOwner()->transform.position };
	const DirectX::XMFLOAT3& rotDeg{ GetOwner()->transform.rotation };

	const DirectX::XMFLOAT3 rotRad{
		DirectX::XMConvertToRadians(rotDeg.x),
		DirectX::XMConvertToRadians(rotDeg.y),
		DirectX::XMConvertToRadians(rotDeg.z)
	};

	m_camera->SetPosition(posDeg);
	m_camera->SetRotation(rotRad);
}

void CameraComponent::SetAspectRatio(float aspectRatio) noexcept
{
	if (aspectRatio <= 0.0f) return; // Guard against div-by-zero on a degenerate window size

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

	projectionDirty |= ImGui::DragFloat("Field of View", &m_fovDegrees, 0.1f, 1.0f, 179.0f);
	projectionDirty |= ImGui::DragFloat("Near Clip", &m_nearZ, 0.01f, 0.01f, m_farZ - 0.01f);
	projectionDirty |= ImGui::DragFloat("Far Clip", &m_farZ, 1.0f, m_nearZ + 0.01f, 100000.0f);

	if (projectionDirty)
	{
		ApplyProjectionSettings();
	}

	ImGui::DragFloat("Gizmo Draw Distance", &m_gizmoDrawDistance, 0.1f, 0.5f, 100.0f);
}

void CameraComponent::DrawGizmo(ShapeRenderer* shapeRenderer) noexcept
{
	if (!shapeRenderer || !GetOwner()) return;

	constexpr DirectX::XMFLOAT4 gizmoColor{ 1.0f, 0.85f, 0.0f, 1.0f };

	shapeRenderer->DrawFrustum(
		GetOwner()->transform.position,
		DirectX::XMFLOAT3{
			DirectX::XMConvertToRadians(GetOwner()->transform.rotation.x),
			DirectX::XMConvertToRadians(GetOwner()->transform.rotation.y),
			DirectX::XMConvertToRadians(GetOwner()->transform.rotation.z)
		},
		DirectX::XMConvertToRadians(m_fovDegrees),
		m_aspectRatio,
		m_nearZ,
		m_farZ,
		gizmoColor,
		m_gizmoDrawDistance);
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

// Automatically registers CameraComponent before main() runs
REGISTER_COMPONENT(CameraComponent)