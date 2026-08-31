#include <cmath>
#include <imgui.h>
#include "System/PhysicsManager.h"
#include "System/Model.h"
#include "MeshComponent.h"
#include "StaticMeshColliderComponent.h"

[[nodiscard]] constexpr bool StaticMeshColliderComponent::IsFloatEqual(float a, float b, float epsilon) noexcept
{
    return (a >= b - epsilon) && (a <= b + epsilon);
}

StaticMeshColliderComponent::~StaticMeshColliderComponent()
{
    auto* scene{ PhysicsManager::Instance().GetScene() };
    if (m_physxActor && scene)
    {
        scene->removeActor(*m_physxActor);
        m_physxActor->release();
    }
    for (auto* mesh : m_collisionMeshes)
    {
        if (mesh) mesh->release();
    }
}

void StaticMeshColliderComponent::OnAttach(GameObject* owner) noexcept
{
    IComponent::OnAttach(owner);
    RebuildPhysics();
}

void StaticMeshColliderComponent::Update(float dt)
{
    if (!m_owner || !m_physxActor) return;

    const Transform& t{ m_owner->transform };

    const bool moved{
        !IsFloatEqual(t.position.x, m_lastFramePos.x) || !IsFloatEqual(t.position.y, m_lastFramePos.y) || !IsFloatEqual(t.position.z, m_lastFramePos.z) ||
        !IsFloatEqual(t.rotation.x, m_lastFrameRot.x) || !IsFloatEqual(t.rotation.y, m_lastFrameRot.y) || !IsFloatEqual(t.rotation.z, m_lastFrameRot.z) ||
        !IsFloatEqual(t.scale.x, m_lastFrameScale.x) || !IsFloatEqual(t.scale.y, m_lastFrameScale.y) || !IsFloatEqual(t.scale.z, m_lastFrameScale.z)
    };

    if (moved)
    {
        // If the scale changes, PhysX requires a complete shape rebuild.
        // If only position/rotation changed, we just update the global pose.
        const bool scaleChanged{ !IsFloatEqual(t.scale.x, m_lastFrameScale.x) || !IsFloatEqual(t.scale.y, m_lastFrameScale.y) || !IsFloatEqual(t.scale.z, m_lastFrameScale.z) };

        if (scaleChanged)
        {
            RebuildPhysics();
        }
        else
        {
            DirectX::XMVECTOR q{ DirectX::XMQuaternionRotationRollPitchYaw(
                DirectX::XMConvertToRadians(t.rotation.x),
                DirectX::XMConvertToRadians(t.rotation.y),
                DirectX::XMConvertToRadians(t.rotation.z)
            ) };
            DirectX::XMFLOAT4 qF;
            DirectX::XMStoreFloat4(&qF, q);

            physx::PxTransform newPose{
                physx::PxVec3(t.position.x, t.position.y, t.position.z),
                physx::PxQuat(qF.x, qF.y, qF.z, qF.w)
            };

            m_physxActor->setGlobalPose(newPose);
        }

        m_lastFramePos = t.position;
        m_lastFrameRot = t.rotation;
        m_lastFrameScale = t.scale;
    }
}

void StaticMeshColliderComponent::RebuildPhysics() noexcept
{
    if (!m_owner) return;

    // Grab sibling MeshComponent to read vertex data
    MeshComponent* meshComp{ m_owner->GetComponent<MeshComponent>() };
    if (!meshComp || !meshComp->GetModel()) return;

    auto* physics{ PhysicsManager::Instance().GetPhysics() };
    auto* scene{ PhysicsManager::Instance().GetScene() };
    auto* material{ PhysicsManager::Instance().GetDefaultMaterial() };

    if (!physics || !scene || !material) return;

    // Clean up old physics data
    if (m_physxActor)
    {
        scene->removeActor(*m_physxActor);
        m_physxActor->release();
        m_physxActor = nullptr;
    }
    for (auto* mesh : m_collisionMeshes) { if (mesh) mesh->release(); }
    m_collisionMeshes.clear();

    // Cook new mesh geometries
    physx::PxTolerancesScale physxScale{ physics->getTolerancesScale() };
    physx::PxCookingParams params{ physxScale };
    params.meshPreprocessParams |= physx::PxMeshPreprocessingFlag::eDISABLE_CLEAN_MESH;

    for (const auto& mesh : meshComp->GetModel()->GetMeshes())
    {
        if (mesh.vertices.empty()) continue;

        std::vector<physx::PxVec3> bakedVertices;
        bakedVertices.reserve(mesh.vertices.size());
        DirectX::XMMATRIX globalMat{ DirectX::XMLoadFloat4x4(&mesh.node->globalTransform) };

        for (const auto& v : mesh.vertices)
        {
            DirectX::XMVECTOR pos{ DirectX::XMVector3TransformCoord(DirectX::XMLoadFloat3(&v.position), globalMat) };
            DirectX::XMFLOAT3 f;
            DirectX::XMStoreFloat3(&f, pos);
            bakedVertices.emplace_back(f.x, f.y, f.z);
        }

        physx::PxTriangleMeshDesc meshDesc;
        meshDesc.points.count = static_cast<physx::PxU32>(bakedVertices.size());
        meshDesc.points.stride = sizeof(physx::PxVec3);
        meshDesc.points.data = bakedVertices.data();
        meshDesc.triangles.count = static_cast<physx::PxU32>(mesh.indices.size() / 3);
        meshDesc.triangles.stride = 3 * sizeof(uint32_t);
        meshDesc.triangles.data = mesh.indices.data();

        if (physx::PxTriangleMesh * triMesh{ PxCreateTriangleMesh(params, meshDesc, physics->getPhysicsInsertionCallback()) })
        {
            m_collisionMeshes.push_back(triMesh);
        }
    }

    // Set Pose & Scale
    const Transform& t{ m_owner->transform };
    DirectX::XMVECTOR q{ DirectX::XMQuaternionRotationRollPitchYaw(DirectX::XMConvertToRadians(t.rotation.x), DirectX::XMConvertToRadians(t.rotation.y), DirectX::XMConvertToRadians(t.rotation.z)) };
    DirectX::XMFLOAT4 qF; DirectX::XMStoreFloat4(&qF, q);

    m_physxActor = physics->createRigidStatic(physx::PxTransform(physx::PxVec3(t.position.x, t.position.y, t.position.z), physx::PxQuat(qF.x, qF.y, qF.z, qF.w)));

    const float sx{ (std::max)(0.001f, t.scale.x) };
    const float sy{ (std::max)(0.001f, t.scale.y) };
    const float sz{ (std::max)(0.001f, t.scale.z) };
    physx::PxMeshScale pxScale{ physx::PxVec3(sx, sy, sz), physx::PxQuat(physx::PxIdentity) };

    for (physx::PxTriangleMesh* triMesh : m_collisionMeshes)
    {
        physx::PxTriangleMeshGeometry geom(triMesh, pxScale);
        if (physx::PxShape * shape{ physics->createShape(geom, *material) })
        {
            m_physxActor->attachShape(*shape);
            shape->release();
        }
    }

    scene->addActor(*m_physxActor);

    m_lastFramePos = t.position; m_lastFrameRot = t.rotation; m_lastFrameScale = t.scale;
}

void StaticMeshColliderComponent::DrawInspector()
{
    ImGui::TextDisabled("Physics Collision baked from attached MeshComponent.");
    if (ImGui::Button("Force Rebuild Physics"))
    {
        RebuildPhysics();
    }
}

// Register with Component Factory
#include "ComponentRegistry.h"
REGISTER_COMPONENT(StaticMeshColliderComponent)