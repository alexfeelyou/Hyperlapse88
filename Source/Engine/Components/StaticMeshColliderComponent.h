#pragma once

#include <DirectXMath.h>
#include <vector>
#include <PxPhysicsAPI.h>
#include <json.hpp>
#include "GameObject.h"
#include "IComponent.h"
#include "System/CollisionLayer.h"

// Scoped enum for proxy shapes. ConvexHull removed per requirements.
enum class ColliderShapeType : std::uint8_t
{
    Box = 0,
    Sphere,
    Capsule,
    TriangleMesh
};

// Data-driven configuration with local offsets for Gizmo manipulation
struct StaticColliderConfig
{
    ColliderShapeType shapeType{ ColliderShapeType::Box };
    std::uint32_t     layer{ CollisionLayer::WorldStatic };
    std::uint32_t     collidesWith{ CollisionLayer::All };
    DirectX::XMFLOAT3 proxyExtents{ 1.0f, 1.0f, 1.0f };
    DirectX::XMFLOAT3 localOffset{ 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT3 localRotation{ 0.0f, 0.0f, 0.0f };
    bool              isTrigger{ false };
};

class StaticMeshColliderComponent final : public IComponent
{
public:
    StaticMeshColliderComponent() noexcept = default;
    explicit StaticMeshColliderComponent(const StaticColliderConfig& config) noexcept;
    ~StaticMeshColliderComponent() noexcept override;

    StaticMeshColliderComponent(const StaticMeshColliderComponent&) = delete;
    StaticMeshColliderComponent& operator=(const StaticMeshColliderComponent&) = delete;
    StaticMeshColliderComponent(StaticMeshColliderComponent&&) noexcept = default;
    StaticMeshColliderComponent& operator=(StaticMeshColliderComponent&&) noexcept = default;

    void OnAttach(GameObject* owner) noexcept override;
    void OnEnable() noexcept override;
    void OnDisable() noexcept override;
    void AutoFitToMesh() noexcept;

    void Update(float dt) override;
    void DrawInspector() override;

    void Render(ModelRenderer* renderer) override;
    void Serialize(nlohmann::json& json) const override;
    void Deserialize(const nlohmann::json& json) override;

    // Mutators for Gizmo Redirection
    [[nodiscard]] StaticColliderConfig& GetConfig() noexcept { return m_config; }
    void MarkDirty() noexcept { RebuildPhysics(); }

    [[nodiscard]] const char* GetTypeName() const noexcept override { return "StaticMeshColliderComponent"; }

private:
    void RebuildPhysics() noexcept;
    void ApplyFilterData(physx::PxShape* shape) const noexcept;

    [[nodiscard]] static constexpr bool IsFloatEqual(float a, float b, float epsilon = 0.0001f) noexcept
    {
        return (a >= b - epsilon) && (a <= b + epsilon);
    }

    StaticColliderConfig m_config{};
    physx::PxRigidStatic* m_physxActor{ nullptr };
    std::vector<physx::PxShape*> m_attachedShapes{};

    bool m_showDebug{ false };

    DirectX::XMFLOAT3 m_lastFramePos{ 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT3 m_lastFrameRot{ 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT3 m_lastFrameScale{ 1.0f, 1.0f, 1.0f };
};