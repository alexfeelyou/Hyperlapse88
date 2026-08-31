#include "StaticMeshColliderComponent.h"
#include <algorithm>
#include <imgui.h>
#include "ComponentRegistry.h"
#include "MeshComponent.h"
#include "System/Model.h"
#include "System/PhysicsManager.h"

StaticMeshColliderComponent::~StaticMeshColliderComponent()
{
    // Detach and release actor from scene safely
    auto* scene{ PhysicsManager::Instance().GetScene() };
    if (m_physxActor && scene)
    {
        scene->removeActor(*m_physxActor);
        m_physxActor->release();
        m_physxActor = nullptr;
    }

    // Clear shape handles (lifetime managed by actor)
    m_attachedShapes.clear();
}

void StaticMeshColliderComponent::OnAttach(GameObject* owner) noexcept
{
    IComponent::OnAttach(owner);
    RebuildPhysics();
}

void StaticMeshColliderComponent::Update(const float dt)
{
    if (!m_owner || !m_physxActor)
    {
        return;
    }

    const Transform& t{ m_owner->transform };

    // Check whether transform components changed this frame
    const bool posChanged{
        !IsFloatEqual(t.position.x, m_lastFramePos.x) ||
        !IsFloatEqual(t.position.y, m_lastFramePos.y) ||
        !IsFloatEqual(t.position.z, m_lastFramePos.z)
    };

    const bool rotChanged{
        !IsFloatEqual(t.rotation.x, m_lastFrameRot.x) ||
        !IsFloatEqual(t.rotation.y, m_lastFrameRot.y) ||
        !IsFloatEqual(t.rotation.z, m_lastFrameRot.z)
    };

    const bool scaleChanged{
        !IsFloatEqual(t.scale.x, m_lastFrameScale.x) ||
        !IsFloatEqual(t.scale.y, m_lastFrameScale.y) ||
        !IsFloatEqual(t.scale.z, m_lastFrameScale.z)
    };

    // Update global actor pose if position or rotation changed
    if (posChanged || rotChanged)
    {
        const DirectX::XMVECTOR q{ DirectX::XMQuaternionRotationRollPitchYaw(
            DirectX::XMConvertToRadians(t.rotation.x),
            DirectX::XMConvertToRadians(t.rotation.y),
            DirectX::XMConvertToRadians(t.rotation.z)
        ) };

        DirectX::XMFLOAT4 qF{};
        DirectX::XMStoreFloat4(&qF, q);

        const physx::PxTransform newPose{
            physx::PxVec3{ t.position.x, t.position.y, t.position.z },
            physx::PxQuat{ qF.x, qF.y, qF.z, qF.w }
        };

        m_physxActor->setGlobalPose(newPose);
        m_lastFramePos = t.position;
        m_lastFrameRot = t.rotation;
    }

    // Update geometry scale on shapes directly without re-cooking
    if (scaleChanged)
    {
        UpdateShapeScale(t.scale);
        m_lastFrameScale = t.scale;
    }
}

void StaticMeshColliderComponent::UpdateShapeScale(const DirectX::XMFLOAT3& newScale) noexcept
{
    const float sx{ (std::max)(0.001f, newScale.x) };
    const float sy{ (std::max)(0.001f, newScale.y) };
    const float sz{ (std::max)(0.001f, newScale.z) };
    const physx::PxMeshScale pxScale{ physx::PxVec3{ sx, sy, sz }, physx::PxQuat{ physx::PxIdentity } };

    // Update each attached shape's geometry using the cached triangle mesh pointer
    for (const auto& shapeData : m_attachedShapes)
    {
        if (shapeData.shape && shapeData.triMesh)
        {
            // Reconstruct the geometry with the original mesh and the new scale
            const physx::PxTriangleMeshGeometry triGeom{ shapeData.triMesh, pxScale };
            shapeData.shape->setGeometry(triGeom);
        }
    }
}

void StaticMeshColliderComponent::RebuildPhysics() noexcept
{
    if (!m_owner)
    {
        return;
    }

    // Query sibling MeshComponent for the model reference
    const auto* meshComp{ m_owner->GetComponent<MeshComponent>() };
    if (!meshComp || !meshComp->GetModel())
    {
        return;
    }

    auto* physics{ PhysicsManager::Instance().GetPhysics() };
    auto* scene{ PhysicsManager::Instance().GetScene() };
    auto* material{ PhysicsManager::Instance().GetDefaultMaterial() };

    if (!physics || !scene || !material)
    {
        return;
    }

    // Clean up existing actor before rebuilding
    if (m_physxActor)
    {
        scene->removeActor(*m_physxActor);
        m_physxActor->release();
        m_physxActor = nullptr;
    }
    m_attachedShapes.clear();

    // Retrieve shared pre-cooked triangle meshes from the central cache
    const auto& cookedMeshes{ PhysicsManager::Instance().GetOrCreateTriangleMeshes(meshComp->GetModel().get()) };
    if (cookedMeshes.empty())
    {
        return;
    }

    // Setup initial actor pose
    const Transform& t{ m_owner->transform };
    const DirectX::XMVECTOR q{ DirectX::XMQuaternionRotationRollPitchYaw(
        DirectX::XMConvertToRadians(t.rotation.x),
        DirectX::XMConvertToRadians(t.rotation.y),
        DirectX::XMConvertToRadians(t.rotation.z)
    ) };

    DirectX::XMFLOAT4 qF{};
    DirectX::XMStoreFloat4(&qF, q);

    m_physxActor = physics->createRigidStatic(physx::PxTransform{
        physx::PxVec3{ t.position.x, t.position.y, t.position.z },
        physx::PxQuat{ qF.x, qF.y, qF.z, qF.w }
        });

    const float sx{ (std::max)(0.001f, t.scale.x) };
    const float sy{ (std::max)(0.001f, t.scale.y) };
    const float sz{ (std::max)(0.001f, t.scale.z) };
    const physx::PxMeshScale pxScale{ physx::PxVec3{ sx, sy, sz }, physx::PxQuat{ physx::PxIdentity } };

    // Attach all cached sub-mesh collision shapes to the single static actor
    for (auto* triMesh : cookedMeshes)
    {
        if (!triMesh)
        {
            continue;
        }

        const physx::PxTriangleMeshGeometry geom{ triMesh, pxScale };
        if (physx::PxShape * shape{ physics->createShape(geom, *material) })
        {
            m_physxActor->attachShape(*shape);
            m_attachedShapes.push_back({ shape, triMesh });

            shape->release(); 
        }
    }

    scene->addActor(*m_physxActor);

    m_lastFramePos = t.position;
    m_lastFrameRot = t.rotation;
    m_lastFrameScale = t.scale;
}

void StaticMeshColliderComponent::DrawInspector()
{
    ImGui::TextDisabled("Physics Collision baked from shared MeshComponent.");
    ImGui::Text("Attached Collision Shapes: %zu", m_attachedShapes.size());

    if (ImGui::Button("Force Rebind Physics"))
    {
        RebuildPhysics();
    }
}

// Automatically register component with dynamic factory
REGISTER_COMPONENT(StaticMeshColliderComponent)