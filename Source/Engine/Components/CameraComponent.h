#pragma once

#include <DirectXMath.h>
#include <memory>
#include <json.hpp>
#include "System/Sprite.h"
#include "Camera.h"
#include "IComponent.h"

// CameraComponent (The "Camera Brain")
// Acts as the physical rendering lens of the engine. It holds projection 
// parameters (FOV, aspect ratio, clipping) and strictly follows the absolute 
// world-space coordinates of its owning GameObject.
// In a cinematic architecture, this component lives on a single "Main Camera" 
// node, while lightweight Virtual Cameras drive the Transform of that node.
class CameraComponent final : public IComponent
{
public:
    CameraComponent();
    ~CameraComponent() override = default;

    // Delete copy semantics. A CameraComponent uniquely owns a Camera instance 
    // and cannot be trivially cloned.
    CameraComponent(const CameraComponent&) = delete;
    CameraComponent& operator=(const CameraComponent&) = delete;

    // Move semantics are explicitly allowed
    CameraComponent(CameraComponent&&) = default;
    CameraComponent& operator=(CameraComponent&&) = default;

	// Called immediately when the component is added to a GameObject
    void OnAttach(class GameObject* owner) noexcept override;

    // Extracts absolute world-transform from the owner and updates the lens
    void Update(float dt) override;

    // Renders component variables to the ImGui Inspector
    void DrawInspector() override;

    // Draws frustum boundaries in the Scene View
    void DrawGizmo(class ShapeRenderer* shapeRenderer) noexcept override;

    [[nodiscard]] const char* GetTypeName() const noexcept override { return "CameraComponent"; }

    // Serialization Hooks
    void Serialize(nlohmann::json& outJson) const override;
    void Deserialize(const nlohmann::json& inJson) override;

    // Provides access to the underlying DX11 Camera for the rendering pipeline
    [[nodiscard]] std::shared_ptr<Camera> GetCamera() const noexcept { return m_camera; }

    // Recomputes the projection for a new screen aspect ratio
    void SetAspectRatio(float aspectRatio) noexcept;

private:
    std::shared_ptr<Camera> m_camera{ std::make_shared<Camera>() };

    // Blending Engine State
    class VirtualCameraComponent* m_activeVirtualCamera{ nullptr };
    float m_blendDuration{ 1.5f };
    float m_blendTimer{ 0.0f };
    DirectX::XMFLOAT3 m_blendStartPos{};
    DirectX::XMFLOAT3 m_blendStartRot{};

    float m_fovDegrees{ 45.0f };
    float m_nearZ{ 0.2f };
    float m_farZ{ 1000.0f };
    float m_aspectRatio{ 16.0f / 9.0f };

    // Editor-only visualization distance, independent of the real far clip so a
    // far=1000 camera doesn't draw a gizmo that swallows the whole scene view
    float m_gizmoDrawDistance{ 5.0f };

    // Gizmo Rendering
    std::unique_ptr<Sprite> m_gizmoSprite{};
    bool m_iconLoaded{ false };

    // Pushes fovDegrees/near/far/aspect into the underlying Camera's projection matrix
    void ApplyProjectionSettings() noexcept;

	// Loads the 3D billboard icon for the Scene View gizmo. Called once on attach.
    void LoadGizmoIcon() noexcept;
};