#pragma once

#include <DirectXMath.h>
#include <json.hpp>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include "System/Sprite.h"
#include "IComponent.h"

class VirtualCameraComponent final : public IComponent
{
public:
    VirtualCameraComponent();
    ~VirtualCameraComponent() override;

    VirtualCameraComponent(const VirtualCameraComponent&) = delete;
    VirtualCameraComponent& operator=(const VirtualCameraComponent&) = delete;
    VirtualCameraComponent(VirtualCameraComponent&&) = delete;
    VirtualCameraComponent& operator=(VirtualCameraComponent&&) = delete;

    void OnAttach(class GameObject* owner) noexcept override;
    void Update(float dt) override;
    void DrawInspector() override;
    void DrawGizmo(const GizmoContext& ctx) noexcept override;
    void ForceSync() noexcept;
    void OnEnable() noexcept override { s_globalDirtyFrame++; }
    void OnDisable() noexcept override { s_globalDirtyFrame++; }

    [[nodiscard]] const char* GetTypeName() const noexcept override { return "VirtualCameraComponent"; }
    void SetActiveShot(bool active) noexcept { m_isActiveShot = active; }
    static std::uint32_t GetGlobalDirtyFrame() noexcept { return s_globalDirtyFrame; }

    void Serialize(nlohmann::json& outJson) const override;
    void Deserialize(const nlohmann::json& inJson) override;

    [[nodiscard]] int GetPriority() const noexcept { return m_priority; }
    [[nodiscard]] float GetFovDegrees() const noexcept { return m_fovDegrees; }
    [[nodiscard]] float GetNearZ() const noexcept { return m_nearZ; }
    [[nodiscard]] float GetFarZ() const noexcept { return m_farZ; }

    // Orbit Rig (Third-Person Follow Camera) 
    void SetOrbitEnabled(bool enabled) noexcept { m_orbitEnabled = enabled; }
    [[nodiscard]] bool IsOrbitEnabled() const noexcept { return m_orbitEnabled; }

    // Yaw/pitch are stored in radians. Setters wrap/clamp internally so callers never
    // have to reason about angle limits or the -pi/pi wraparound themselves.
    void SetOrbitYaw(float yawRadians) noexcept;
    void AddOrbitYaw(float deltaRadians) noexcept;
    [[nodiscard]] float GetOrbitYaw() const noexcept { return m_orbitYaw; }

    void SetOrbitPitch(float pitchRadians) noexcept;
    void AddOrbitPitch(float deltaRadians) noexcept;
    [[nodiscard]] float GetOrbitPitch() const noexcept { return m_orbitPitch; }

    void SetOrbitDistance(float distance) noexcept;
    void AddOrbitDistance(float delta) noexcept;
    [[nodiscard]] float GetOrbitDistance() const noexcept { return m_orbitDistance; }

    // World-space offset added to the follow target's position to find the orbit pivot
    // (e.g. chest height, so the camera doesn't orbit around the character's feet).
    void SetOrbitPivotOffset(const DirectX::XMFLOAT3& offset) noexcept { m_orbitPivotOffset = offset; }
    [[nodiscard]] const DirectX::XMFLOAT3& GetOrbitPivotOffset() const noexcept { return m_orbitPivotOffset; }

    [[nodiscard]] static const std::vector<VirtualCameraComponent*>& GetRegistry() noexcept { return s_registry; }

    // Shared Batch Pipeline
    static void FlushGizmos(ID3D11DeviceContext* dc, const class Camera* activeCam) noexcept;
    static void QueueGizmoIcon(const Sprite::Sprite3DBatchData& data) noexcept { s_gizmoBatchData.push_back(data); }
    static void EnsureSharedGizmoLoaded() noexcept;

private:
    static inline std::vector<VirtualCameraComponent*> s_registry{};
    static inline std::shared_ptr<Sprite> s_sharedGizmoSprite{ nullptr };
    static inline std::vector<Sprite::Sprite3DBatchData> s_gizmoBatchData{};

    int m_priority{ 10 };

    std::string m_followTargetName{};
    std::string m_lookAtTargetName{};

    class GameObject* m_followTarget{ nullptr };
    class GameObject* m_lookAtTarget{ nullptr };

    DirectX::XMFLOAT3 m_followOffset{ 0.0f, 5.0f, -10.0f };
    float m_positionDamping{ 5.0f };
    float m_rotationDamping{ 5.0f };

    float m_fovDegrees{ 45.0f };
    float m_nearZ{ 0.2f };
    float m_farZ{ 1000.0f };
    float m_gizmoDrawDistance{ 5.0f };

    // Orbit Rig State
    bool m_orbitEnabled{ false };

    float m_orbitYaw{ DirectX::XM_PI };
    float m_orbitPitch{ 0.0f };         // Radians. Clamped to [m_orbitMinPitch, m_orbitMaxPitch].
    float m_orbitDistance{ 6.0f };      // Distance from the pivot to the camera eye.

    float m_orbitMinPitch{ DirectX::XMConvertToRadians(-80.0f) };
    float m_orbitMaxPitch{ DirectX::XMConvertToRadians(80.0f) };
    float m_orbitMinDistance{ 1.5f };
    float m_orbitMaxDistance{ 12.0f };

    DirectX::XMFLOAT3 m_orbitPivotOffset{ 0.0f, 1.6f, 0.0f };

    float m_orbitPositionDamping{ 12.0f };
    float m_orbitRotationDamping{ 20.0f };

    DirectX::XMFLOAT3 m_cachedPos{ 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT3 m_cachedRot{ 0.0f, 0.0f, 0.0f };

    bool m_isActiveShot{ false };

    static inline std::uint32_t s_globalDirtyFrame{ 0 };
    bool m_isDirty{ false };

    [[nodiscard]] class GameObject* FindTargetByName(const std::string& name) const noexcept;
    void ResolveTargets() noexcept;

    // Computes the desired world-space eye position and pitch/yaw rotation (radians) for
    // the current orbit yaw/pitch/distance around the follow target's pivot.
    [[nodiscard]] std::pair<DirectX::XMFLOAT3, DirectX::XMFLOAT3> ResolveOrbitTransform() const noexcept;
};