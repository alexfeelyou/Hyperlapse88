#pragma once

#include <memory>
#include "Camera.h"
#include "IComponent.h"

// Wraps a Camera and keeps it synced to the owning GameObject's Transform every frame
class CameraComponent final : public IComponent
{
public:
	CameraComponent();

	void Update(float dt) override;
	void DrawInspector() override;
	void DrawGizmo(class ShapeRenderer* shapeRenderer) noexcept override;

	[[nodiscard]] const char* GetTypeName() const noexcept override { return "CameraComponent"; }

	void Serialize(nlohmann::json& outJson) const override;
	void Deserialize(const nlohmann::json& inJson) override;

	[[nodiscard]] std::shared_ptr<Camera> GetCamera() const noexcept { return m_camera; }

	// Recomputes the projection for a new screen aspect ratio
	void SetAspectRatio(float aspectRatio) noexcept;

private:
	std::shared_ptr<Camera> m_camera{ std::make_shared<Camera>() };

	float m_fovDegrees{ 45.0f };
	float m_nearZ{ 0.1f };
	float m_farZ{ 1000.0f };
	float m_aspectRatio{ 16.0f / 9.0f };

	// Editor-only visualization distance, independent of the real far clip so a
	// far=1000 camera doesn't draw a gizmo that swallows the whole scene view
	float m_gizmoDrawDistance{ 5.0f };

	// Pushes fovDegrees/near/far/aspect into the underlying Camera's projection
	void ApplyProjectionSettings() noexcept;
};