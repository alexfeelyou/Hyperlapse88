#pragma once

#include <DirectXMath.h>
#include <vector>
#include <PxPhysicsAPI.h>
#include "GameObject.h"
#include "IComponent.h"

// Attaches static physics collision to a GameObject using shared, pre-cooked mesh geometry
class StaticMeshColliderComponent final : public IComponent
{
public:
    StaticMeshColliderComponent() = default;
    ~StaticMeshColliderComponent() override;

    // Enforce strict component ownership
    StaticMeshColliderComponent(const StaticMeshColliderComponent&) = delete;
    StaticMeshColliderComponent& operator=(const StaticMeshColliderComponent&) = delete;
    StaticMeshColliderComponent(StaticMeshColliderComponent&&) noexcept = default;
    StaticMeshColliderComponent& operator=(StaticMeshColliderComponent&&) noexcept = default;

    void OnAttach(GameObject* owner) noexcept override;
    void Update(float dt) override;
    void DrawInspector() override;

    [[nodiscard]] const char* GetTypeName() const noexcept override { return "StaticMeshColliderComponent"; }

private:
    // Tracks attached collision shapes and their source meshes for direct scale updates
    struct ShapeData
    {
        physx::PxShape* shape{ nullptr };
        physx::PxTriangleMesh* triMesh{ nullptr };
    };

    std::vector<ShapeData> m_attachedShapes{};

    // Creates or re-attaches the PxRigidStatic actor to the PhysX scene
    void RebuildPhysics() noexcept;

    // Updates existing shape scale without invoking the mesh cooking pipeline
    void UpdateShapeScale(const DirectX::XMFLOAT3& newScale) noexcept;

    // Evaluates float equality within engine precision tolerances
    [[nodiscard]] static constexpr bool IsFloatEqual(float a, float b, float epsilon = 0.0001f) noexcept
    {
        return (a >= b - epsilon) && (a <= b + epsilon);
    }

    physx::PxRigidStatic* m_physxActor{ nullptr };

    // Transform tracking for dirty-state propagation
    DirectX::XMFLOAT3 m_lastFramePos{ 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT3 m_lastFrameRot{ 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT3 m_lastFrameScale{ 1.0f, 1.0f, 1.0f };
};