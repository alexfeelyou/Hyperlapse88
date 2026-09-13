#pragma once

#include <DirectXMath.h>
#include <memory>
#include <json.hpp>
#include "Camera.h"
#include "IComponent.h"

// CameraComponent (The "Camera Brain")
class CameraComponent final : public IComponent
{
public:
    CameraComponent();
    ~CameraComponent() override = default;

    CameraComponent(const CameraComponent&) = delete;
    CameraComponent& operator=(const CameraComponent&) = delete;
    CameraComponent(CameraComponent&&) = default;
    CameraComponent& operator=(CameraComponent&&) = default;

    void OnAttach(class GameObject* owner) noexcept override;
    void Update(float dt) override;
    void DrawInspector() override;
    void DrawGizmo(const GizmoContext& ctx) noexcept override;

    [[nodiscard]] const char* GetTypeName() const noexcept override { return "CameraComponent"; }

    void Serialize(nlohmann::json& outJson) const override;
    void Deserialize(const nlohmann::json& inJson) override;

    [[nodiscard]] std::shared_ptr<Camera> GetCamera() const noexcept { return m_camera; }
    void SetAspectRatio(float aspectRatio) noexcept;

    // Game Feel API
    // Adds trauma for screen shake. 
    void AddTrauma(float amount) noexcept;

    // Sets the target zoom offset distance. The camera smoothly lerps to this value.
    void SetDynamicZoomOffset(float zoomOffset) noexcept { m_targetZoomOffset = zoomOffset; }

private:
    std::shared_ptr<Camera> m_camera{ std::make_shared<Camera>() };

    // Blending Engine State
    class VirtualCameraComponent* m_activeVirtualCamera{ nullptr };
    float m_blendDuration{ 1.5f };
    float m_blendTimer{ 0.0f };
    DirectX::XMFLOAT3 m_blendStartPos{};
    DirectX::XMFLOAT3 m_blendStartRot{};

    // Lens Defaults
    float m_fovDegrees{ 45.0f };
    float m_nearZ{ 0.2f };
    float m_farZ{ 1000.0f };
    float m_aspectRatio{ 16.0f / 9.0f };
    float m_gizmoDrawDistance{ 5.0f };

    // Game Feel Layer: Shake 
    float m_trauma{ 0.0f };
    float m_traumaDecay{ 1.5f };
    float m_maxShakeOffset{ 2.0f }; // Maximum positional offset at 100% trauma
    DirectX::XMFLOAT3 m_shakeOffset{ 0.0f, 0.0f, 0.0f };

    // Game Feel Layer: Combat Zoom
    float m_targetZoomOffset{ 0.0f };
    float m_currentZoomOffset{ 0.0f };
    float m_zoomLerpSpeed{ 2.5f };

    // Determines which local axis the camera pushes out/in during a zoom
    DirectX::XMFLOAT3 m_zoomAxis{ 0.0f, 1.0f, -0.5f };

    // Dirty-tracking for camera updates 
    bool m_isDirty{ true };
    int m_dirtyFrames{ 2 };
    std::uint32_t m_lastVCamDirtyFrame{ 0 };
    DirectX::XMFLOAT3 m_lastVCamPos{ 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT3 m_lastVCamRot{ 0.0f, 0.0f, 0.0f };

    // Dirty-tracking cache for zero matrix overhead when idle
    DirectX::XMFLOAT3 m_lastPos{ 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT3 m_lastRot{ 0.0f, 0.0f, 0.0f };

    void ApplyProjectionSettings() noexcept;
};