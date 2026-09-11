#pragma once

#include <DirectXMath.h>
#include <json.hpp>
#include <memory>
#include <string>
#include "System/Sprite.h"
#include "IComponent.h"


// VirtualCameraComponent
// A data-driven shot descriptor. It does not render the scene; it calculates 
// framing, tracking, and damping. The CameraBrain reads the highest-priority 
// VirtualCamera and snaps to its coordinates.
class VirtualCameraComponent final : public IComponent
{
public:
    // Constructor automatically registers this camera to the global brain pool
    VirtualCameraComponent();

    // Destructor cleanly unregisters it
    ~VirtualCameraComponent() override;

    VirtualCameraComponent(const VirtualCameraComponent&) = delete;
    VirtualCameraComponent& operator=(const VirtualCameraComponent&) = delete;
    VirtualCameraComponent(VirtualCameraComponent&&) = delete;
    VirtualCameraComponent& operator=(VirtualCameraComponent&&) = delete;

    void OnAttach(class GameObject* owner) noexcept override;
    void Update(float dt) override;
    void DrawInspector() override;
    void DrawGizmo(class ShapeRenderer* shapeRenderer) noexcept override;

    [[nodiscard]] const char* GetTypeName() const noexcept override { return "VirtualCameraComponent"; }

    void Serialize(nlohmann::json& outJson) const override;
    void Deserialize(const nlohmann::json& inJson) override;

    // Accessors 
    [[nodiscard]] int GetPriority() const noexcept { return m_priority; }
    [[nodiscard]] float GetFovDegrees() const noexcept { return m_fovDegrees; }
    [[nodiscard]] float GetNearZ() const noexcept { return m_nearZ; }
    [[nodiscard]] float GetFarZ() const noexcept { return m_farZ; }

    // Static registry accessor for the Camera Brain
    [[nodiscard]] static const std::vector<VirtualCameraComponent*>& GetRegistry() noexcept { return s_registry; }
private:
    // Ensures only one list exists across the entire translation unit
    static inline std::vector<VirtualCameraComponent*> s_registry{};

    int m_priority{ 10 };

    // Target Identification 
    std::string m_followTargetName{};
    std::string m_lookAtTargetName{};

    // Framing & Smoothing
    DirectX::XMFLOAT3 m_followOffset{ 0.0f, 5.0f, -10.0f };
    float m_positionDamping{ 5.0f };
    float m_rotationDamping{ 5.0f };

    // Lens Settings
    float m_fovDegrees{ 45.0f };
    float m_nearZ{ 0.2f };
    float m_farZ{ 1000.0f };
    float m_gizmoDrawDistance{ 5.0f };

    // Gizmo Rendering
    std::unique_ptr<Sprite> m_gizmoSprite{};
    bool m_iconLoaded{ false };

    // Internal Helpers
    [[nodiscard]] class GameObject* FindTargetByName(const std::string& name) const noexcept;
    void LoadGizmoIcon() noexcept;
};