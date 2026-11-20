#pragma once

#include <array>
#include <cstdint>
#include <DirectXMath.h>
#include <json.hpp>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include "System/Sprite.h"
#include "IComponent.h"

// Discrete gameplay camera profiles
enum class CameraProfileType : std::uint8_t
{
    locomotion = 0, // Default exploratory third-person view
    sprint,         // Low, tight zoom with widened FOV for speed parallax
    dash,           // High-speed inertia trail, looser damping (pulls camera back)
    slide,          // Ground-skimming, compact vantage point
    wall_run,       // Lateral shoulder offset with diagonal Dutch roll tilt
    roll_landing,   // Ground impact compression dipping low before rebound
    count
};

// POD container holding target lens and framing parameters
struct CameraProfile
{
    float orbitDistance{ 3.3f };
    float fovDegrees{ 48.0f };
    DirectX::XMFLOAT3 pivotOffset{ 0.0f, 1.15f, 0.0f };
    float shoulderOffset{ 0.5f };
    float rollDegrees{ 0.0f };
    float positionDamping{ 5.0f };
    float rotationDamping{ 20.0f };
    float blendSpeed{ 6.0f }; // Exponential convergence rate (1/s)
};

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
    void LateUpdate(float dt) override;
    void Step(float dt);
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
    void SetOrbitShoulderOffset(float offset) noexcept { m_orbitShoulderOffset = offset; }
    [[nodiscard]] float GetOrbitShoulderOffset() const noexcept { return m_orbitShoulderOffset; }

    // Camera Collision (Sphere Sweep)
    void SetCameraCollisionEnabled(bool enabled) noexcept { m_cameraCollisionEnabled = enabled; }
    [[nodiscard]] bool IsCameraCollisionEnabled() const noexcept { return m_cameraCollisionEnabled; }
    void SetCameraCollisionRadius(float radius) noexcept { m_cameraCollisionRadius = radius; }
    [[nodiscard]] float GetCameraCollisionRadius() const noexcept { return m_cameraCollisionRadius; }

    // Profile Injection Interface
    void SetTargetProfile(CameraProfileType type, int wallSide = 0) noexcept;
    [[nodiscard]] CameraProfileType GetTargetProfile() const noexcept { return m_targetProfileType; }
    [[nodiscard]] CameraProfile& GetProfile(CameraProfileType type) noexcept { return m_profiles[static_cast<std::size_t>(type)]; }
    [[nodiscard]] const CameraProfile& GetProfile(CameraProfileType type) const noexcept { return m_profiles[static_cast<std::size_t>(type)]; }
    [[nodiscard]] const CameraProfile& GetCurrentProfile() const noexcept { return m_currentProfile; }

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
    float m_orbitShoulderOffset{ 0.6f };

    bool m_cameraCollisionEnabled{ true };
    float m_cameraCollisionRadius{ 0.15f };

    float m_orbitPositionDamping{ 12.0f };
    float m_orbitRotationDamping{ 20.0f };

    DirectX::XMFLOAT3 m_cachedPos{ 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT3 m_cachedRot{ 0.0f, 0.0f, 0.0f };

    bool m_isActiveShot{ false };
    bool m_hasSteppedThisFrame{ false };

    static inline std::uint32_t s_globalDirtyFrame{ 0 };
    bool m_isDirty{ false };

    // Contiguous profile table and live interpolated blend state
    std::array<CameraProfile, static_cast<std::size_t>(CameraProfileType::count)> m_profiles{};
    CameraProfile m_currentProfile{};
    CameraProfileType m_targetProfileType{ CameraProfileType::locomotion };
    int m_wallSide{ 0 }; // -1 = Wall to left, +1 = Wall to right, 0 = Neutral

    [[nodiscard]] class GameObject* FindTargetByName(const std::string& name) const noexcept;
    void ResolveTargets() noexcept;

    struct OrbitTransformResult
    {
        DirectX::XMFLOAT3 eyePos{ 0.0f, 0.0f, 0.0f };
        DirectX::XMFLOAT3 rotationRadians{ 0.0f, 0.0f, 0.0f };
        DirectX::XMFLOAT3 pivotPos{ 0.0f, 0.0f, 0.0f };
    };

    // Computes the desired world-space eye position, pitch/yaw/roll rotation (radians),
    // and camera-oriented pivot position around the follow target.
    [[nodiscard]] OrbitTransformResult ResolveOrbitTransform() const noexcept;
};