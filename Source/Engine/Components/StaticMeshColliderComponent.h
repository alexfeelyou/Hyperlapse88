#pragma once

#include <DirectXMath.h>
#include <vector>
#include <PxPhysicsAPI.h>
#include "GameObject.h"
#include "IComponent.h"

// Bakes a static collision mesh for the GameObject based on its attached MeshComponent
class StaticMeshColliderComponent final : public IComponent
{
public:
    StaticMeshColliderComponent() = default;
    ~StaticMeshColliderComponent() override;

    StaticMeshColliderComponent(const StaticMeshColliderComponent&) = delete;
    StaticMeshColliderComponent& operator=(const StaticMeshColliderComponent&) = delete;

    void OnAttach(GameObject* owner) noexcept override;
    void Update(float dt) override;
    void DrawInspector() override;

    [[nodiscard]] const char* GetTypeName() const noexcept override { return "StaticMeshColliderComponent"; }

private:
    void RebuildPhysics() noexcept;

    [[nodiscard]] static constexpr bool IsFloatEqual(float a, float b, float epsilon = 0.0001f) noexcept;

    physx::PxRigidStatic* m_physxActor{ nullptr };
    std::vector<physx::PxTriangleMesh*> m_collisionMeshes{};

    // State caching to dynamically move the static mesh if tweaked in the Editor
    DirectX::XMFLOAT3 m_lastFramePos{ 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT3 m_lastFrameRot{ 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT3 m_lastFrameScale{ 1.0f, 1.0f, 1.0f };
};