#pragma once

#include <DirectXMath.h>
#include <memory>
#include <json.hpp>
#include "Camera.h"
#include "IComponent.h"

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
    void DrawGizmo(class ShapeRenderer* shapeRenderer) noexcept override;

    [[nodiscard]] const char* GetTypeName() const noexcept override { return "CameraComponent"; }

    void Serialize(nlohmann::json& outJson) const override;
    void Deserialize(const nlohmann::json& inJson) override;

    [[nodiscard]] std::shared_ptr<Camera> GetCamera() const noexcept { return m_camera; }
    void SetAspectRatio(float aspectRatio) noexcept;

private:
    std::shared_ptr<Camera> m_camera{ std::make_shared<Camera>() };

    class VirtualCameraComponent* m_activeVirtualCamera{ nullptr };
    float m_blendDuration{ 1.5f };
    float m_blendTimer{ 0.0f };
    DirectX::XMFLOAT3 m_blendStartPos{};
    DirectX::XMFLOAT3 m_blendStartRot{};

    float m_fovDegrees{ 45.0f };
    float m_nearZ{ 0.2f };
    float m_farZ{ 1000.0f };
    float m_aspectRatio{ 16.0f / 9.0f };
    float m_gizmoDrawDistance{ 5.0f };

    DirectX::XMFLOAT3 m_lastPos{ 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT3 m_lastRot{ 0.0f, 0.0f, 0.0f };

    void ApplyProjectionSettings() noexcept;
};