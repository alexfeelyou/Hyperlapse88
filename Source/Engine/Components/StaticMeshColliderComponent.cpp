#include <algorithm>
#include <imgui.h>
#include "System/Graphics.h"
#include "System/PhysicsManager.h"
#include "System/ShapeRenderer.h"
#include "ComponentRegistry.h"
#include "EditorManager.h"
#include "MeshComponent.h"
#include "StaticMeshColliderComponent.h"

// Constructor injects configuration immediately via the member-initializer list
StaticMeshColliderComponent::StaticMeshColliderComponent(const StaticColliderConfig& config) noexcept
    : m_config{ config }
{}

StaticMeshColliderComponent::~StaticMeshColliderComponent() noexcept
{
    // Release the actor from the physics scene
    if (auto* scene{ PhysicsManager::Instance().GetScene() }; scene && m_physxActor)
    {
        scene->removeActor(*m_physxActor);
        m_physxActor->release();
        m_physxActor = nullptr;
    }
    m_attachedShapes.clear();
}

void StaticMeshColliderComponent::OnAttach(GameObject* owner) noexcept
{
    IComponent::OnAttach(owner);
    // Auto-fit immediately if a model already exists when this component is added
    AutoFitToMesh();
}

void StaticMeshColliderComponent::OnEnable() noexcept
{
    // Re-insert into the spatial BVH tree to re-enable raycasts/sweeps
    if (m_physxActor)
    {
        auto* scene{ PhysicsManager::Instance().GetScene() };
        if (scene && m_physxActor->getScene() == nullptr)
        {
            scene->addActor(*m_physxActor);
        }
    }
}

void StaticMeshColliderComponent::OnDisable() noexcept
{
    // Extract from the spatial BVH tree to completely sever collision
    if (m_physxActor)
    {
        auto* scene{ PhysicsManager::Instance().GetScene() };
        if (scene && m_physxActor->getScene() == scene)
        {
            scene->removeActor(*m_physxActor);
        }
    }
}

void StaticMeshColliderComponent::AutoFitToMesh() noexcept
{
    if (const auto* meshComp = m_owner->GetComponent<MeshComponent>())
    {
        if (const auto model = meshComp->GetModel())
        {
            // Reset rotations and snap to the mathematical center of the model
            m_config.localOffset = model->GetBoundsCenter();
            m_config.localRotation = { 0.0f, 0.0f, 0.0f };
            const float r{ model->GetBoundsRadius() };

            if (m_config.shapeType == ColliderShapeType::Box)
                m_config.proxyExtents = { r, r, r };
            else if (m_config.shapeType == ColliderShapeType::Sphere)
                m_config.proxyExtents.x = r;
            else if (m_config.shapeType == ColliderShapeType::Capsule)
            {
                m_config.proxyExtents.x = r * 0.5f;
                m_config.proxyExtents.y = r;
            }
            RebuildPhysics();
        }
    }
}

void StaticMeshColliderComponent::Update(const float dt)
{
    if (!m_owner) return;

    if (!m_physxActor)
    {
        RebuildPhysics();
        if (!m_physxActor) return;
    }

    const Transform& t{ m_owner->transform };

    // Early-out if the transform hasn't changed to save CPU cycles on matrix math.
    const bool isTransformDirty{
        !IsFloatEqual(t.position.x, m_lastFramePos.x) ||
        !IsFloatEqual(t.position.y, m_lastFramePos.y) ||
        !IsFloatEqual(t.position.z, m_lastFramePos.z) ||
        !IsFloatEqual(t.rotation.x, m_lastFrameRot.x) ||
        !IsFloatEqual(t.rotation.y, m_lastFrameRot.y) ||
        !IsFloatEqual(t.rotation.z, m_lastFrameRot.z)
    };

    if (isTransformDirty)
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

        // Update the global pose of the rigid body directly without destroying/recreating it
        m_physxActor->setGlobalPose(newPose);

        m_lastFramePos = t.position;
        m_lastFrameRot = t.rotation;
    }
}

void StaticMeshColliderComponent::ApplyFilterData(physx::PxShape* shape) const noexcept
{
    // Assign collision layers and masks to the physical shape.
    // PxFilterData packs 4 words. We use word0 for 'my layer' and word1 for 'layers I hit'.
    // This allows the engine's broad-phase to cull collision pairs instantly via bitwise AND, bypassing expensive AABB checks.
    physx::PxFilterData filterData{};
    filterData.word0 = m_config.layer;
    filterData.word1 = m_config.collidesWith;

    shape->setSimulationFilterData(filterData);

    // If marked as a trigger, disable solid collision and flag it for overlap events
    if (m_config.isTrigger)
    {
        shape->setFlag(physx::PxShapeFlag::eSIMULATION_SHAPE, false);
        shape->setFlag(physx::PxShapeFlag::eTRIGGER_SHAPE, true);
    }
}

void StaticMeshColliderComponent::RebuildPhysics() noexcept
{
    if (!m_owner) return;

    auto* physics{ PhysicsManager::Instance().GetPhysics() };
    auto* scene{ PhysicsManager::Instance().GetScene() };
    auto* material{ PhysicsManager::Instance().GetDefaultMaterial() };

    if (!physics || !scene || !material) return;

    if (m_physxActor)
    {
        scene->removeActor(*m_physxActor);
        m_physxActor->release();
        m_physxActor = nullptr;
    }
    m_attachedShapes.clear();

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

    // Identify if this is a Triangle Mesh
    const bool isTriangleMesh{ m_config.shapeType == ColliderShapeType::TriangleMesh };

    // Hard-lock local rotation to zero for Triangle Meshes to prevent dislocation
    const DirectX::XMVECTOR localQ = isTriangleMesh
        ? DirectX::XMQuaternionIdentity()
        : DirectX::XMQuaternionRotationRollPitchYaw(
            DirectX::XMConvertToRadians(m_config.localRotation.x),
            DirectX::XMConvertToRadians(m_config.localRotation.y),
            DirectX::XMConvertToRadians(m_config.localRotation.z)
        );

    DirectX::XMFLOAT4 localQF{};
    DirectX::XMStoreFloat4(&localQF, localQ);

    // Hard-lock local offset to zero for Triangle Meshes
    const physx::PxTransform relativePose{
        isTriangleMesh ? physx::PxVec3{ 0.0f, 0.0f, 0.0f } : physx::PxVec3{ m_config.localOffset.x, m_config.localOffset.y, m_config.localOffset.z },
        physx::PxQuat{ localQF.x, localQF.y, localQF.z, localQF.w }
    };

    physx::PxShape* primaryShape{ nullptr };

    switch (m_config.shapeType)
    {
    case ColliderShapeType::Box:
    {
        const float hX{ (std::max)(0.001f, m_config.proxyExtents.x * t.scale.x * 0.5f) };
        const float hY{ (std::max)(0.001f, m_config.proxyExtents.y * t.scale.y * 0.5f) };
        const float hZ{ (std::max)(0.001f, m_config.proxyExtents.z * t.scale.z * 0.5f) };
        primaryShape = physics->createShape(physx::PxBoxGeometry{ hX, hY, hZ }, *material);
        primaryShape->setLocalPose(relativePose);
        break;
    }
    case ColliderShapeType::Sphere:
    {
        const float radius{ (std::max)(0.001f, m_config.proxyExtents.x * t.scale.x) };
        primaryShape = physics->createShape(physx::PxSphereGeometry{ radius }, *material);
        primaryShape->setLocalPose(relativePose);
        break;
    }
    case ColliderShapeType::Capsule:
    {
        const float radius{ (std::max)(0.001f, m_config.proxyExtents.x * t.scale.x) };
        const float halfHeight{ (std::max)(0.001f, m_config.proxyExtents.y * t.scale.y * 0.5f) };
        primaryShape = physics->createShape(physx::PxCapsuleGeometry{ radius, halfHeight }, *material);

        const physx::PxTransform uprightPose{
            physx::PxVec3{ 0.0f, 0.0f, 0.0f },
            physx::PxQuat{ DirectX::XM_PIDIV2, physx::PxVec3{ 0.0f, 0.0f, 1.0f } }
        };
        primaryShape->setLocalPose(relativePose * uprightPose);
        break;
    }
    case ColliderShapeType::TriangleMesh:
    {
        const auto* meshComp{ m_owner->GetComponent<MeshComponent>() };
        if (meshComp && meshComp->GetModel())
        {
            const auto& cookedMeshes{ PhysicsManager::Instance().GetOrCreateTriangleMeshes(meshComp->GetModel().get()) };
            const physx::PxMeshScale pxScale{
                physx::PxVec3{ t.scale.x, t.scale.y, t.scale.z },
                physx::PxQuat{ physx::PxIdentity }
            };

            for (auto* triMesh : cookedMeshes)
            {
                if (physx::PxShape * subShape{ physics->createShape(physx::PxTriangleMeshGeometry{ triMesh, pxScale }, *material) })
                {
                    ApplyFilterData(subShape);
                    subShape->setLocalPose(relativePose);
                    m_physxActor->attachShape(*subShape);
                    m_attachedShapes.push_back(subShape);
                    subShape->release();
                }
            }
        }
        break;
    }
    }

    if (primaryShape)
    {
        ApplyFilterData(primaryShape);
        m_physxActor->attachShape(*primaryShape);
        m_attachedShapes.push_back(primaryShape);
        primaryShape->release();
    }

    // Only insert into the live physics scene if the GameObject is actively checked on
    if (!m_attachedShapes.empty() && m_owner->IsActive())
    {
        scene->addActor(*m_physxActor);
    }

    m_lastFramePos = t.position;
    m_lastFrameRot = t.rotation;
    m_lastFrameScale = t.scale;
}

void StaticMeshColliderComponent::Render(ModelRenderer* renderer)
{
    if (!m_showDebug || !m_owner) return;

    auto* shapeRenderer{ Graphics::Instance().GetShapeRenderer() };
    if (!shapeRenderer || m_attachedShapes.empty()) return;

    const Transform& t{ m_owner->transform };
    const DirectX::XMFLOAT3 radRot{
        DirectX::XMConvertToRadians(t.rotation.x),
        DirectX::XMConvertToRadians(t.rotation.y),
        DirectX::XMConvertToRadians(t.rotation.z)
    };

    constexpr DirectX::XMFLOAT4 debugColor{ 0.2f, 1.0f, 0.2f, 0.4f };

    const DirectX::XMMATRIX objWorld{ DirectX::XMLoadFloat4x4(&t.GetWorldMatrix()) };

    const bool isTriangleMesh{ m_config.shapeType == ColliderShapeType::TriangleMesh };

    const DirectX::XMMATRIX locScale{ DirectX::XMMatrixScaling(1.0f, 1.0f, 1.0f) };

    // Mirror the exact same Triangle Mesh hard-locks into the rendering matrix
    const DirectX::XMMATRIX locRot = isTriangleMesh ? DirectX::XMMatrixIdentity() : DirectX::XMMatrixRotationRollPitchYaw(
        DirectX::XMConvertToRadians(m_config.localRotation.x),
        DirectX::XMConvertToRadians(m_config.localRotation.y),
        DirectX::XMConvertToRadians(m_config.localRotation.z));

    const DirectX::XMMATRIX locTrans = isTriangleMesh ? DirectX::XMMatrixIdentity() : DirectX::XMMatrixTranslation(m_config.localOffset.x, m_config.localOffset.y, m_config.localOffset.z);

    const DirectX::XMMATRIX compWorld{ locScale * locRot * locTrans * objWorld };

    DirectX::XMVECTOR vScale, vRotQuat, vTrans;
    DirectX::XMMatrixDecompose(&vScale, &vRotQuat, &vTrans, compWorld);

    DirectX::XMFLOAT3 wPos, wRot;
    DirectX::XMStoreFloat3(&wPos, vTrans);

    const DirectX::XMFLOAT4X4 mRot{ [&]() {
        DirectX::XMFLOAT4X4 temp;
        DirectX::XMStoreFloat4x4(&temp, DirectX::XMMatrixRotationQuaternion(vRotQuat));
        return temp;
    }() };

    wRot.x = asinf(std::clamp(-mRot._32, -1.0f, 1.0f));
    if (cosf(wRot.x) > 0.0001f) {
        wRot.y = atan2f(mRot._31, mRot._33);
        wRot.z = atan2f(mRot._12, mRot._22);
    }
    else {
        wRot.y = atan2f(-mRot._13, mRot._11);
        wRot.z = 0.0f;
    }

    switch (m_config.shapeType)
    {
    case ColliderShapeType::Box:
    {
        const DirectX::XMFLOAT3 halfExtents{
            (std::max)(0.001f, m_config.proxyExtents.x * t.scale.x * 0.5f),
            (std::max)(0.001f, m_config.proxyExtents.y * t.scale.y * 0.5f),
            (std::max)(0.001f, m_config.proxyExtents.z * t.scale.z * 0.5f)
        };
        shapeRenderer->DrawBox(wPos, wRot, halfExtents, debugColor);
        break;
    }
    case ColliderShapeType::Sphere:
    {
        const float radius{ (std::max)(0.001f, m_config.proxyExtents.x * t.scale.x) };
        shapeRenderer->DrawSphere(wPos, radius, debugColor);
        break;
    }
    case ColliderShapeType::Capsule:
    {
        const float radius{ (std::max)(0.001f, m_config.proxyExtents.x * t.scale.x) };
        const float height{ (std::max)(0.001f, m_config.proxyExtents.y * t.scale.y) };

        DirectX::XMFLOAT4X4 transformMatrix{};
        DirectX::XMStoreFloat4x4(&transformMatrix, compWorld);
        shapeRenderer->DrawCapsule(transformMatrix, radius, height, debugColor);
        break;
    }
    case ColliderShapeType::TriangleMesh:
    {
        for (physx::PxShape* shape : m_attachedShapes)
        {
            if (!shape || shape->getGeometry().getType() != physx::PxGeometryType::eTRIANGLEMESH) continue;

            const physx::PxTriangleMeshGeometry& geom = static_cast<const physx::PxTriangleMeshGeometry&>(shape->getGeometry());

            const physx::PxTriangleMesh* triMesh{ geom.triangleMesh };
            const physx::PxU32 triCount{ triMesh->getNbTriangles() };
            const physx::PxVec3* vertices{ triMesh->getVertices() };
            const void* indices{ triMesh->getTriangles() };

            const bool has16Bit{ (triMesh->getTriangleMeshFlags() & physx::PxTriangleMeshFlag::e16_BIT_INDICES) == physx::PxTriangleMeshFlag::e16_BIT_INDICES };

            const physx::PxTransform globalPose{ m_physxActor->getGlobalPose() * shape->getLocalPose() };
            const physx::PxMeshScale pxScale{ geom.scale };

            for (physx::PxU32 i{ 0 }; i < triCount; ++i)
            {
                physx::PxU32 i0, i1, i2;
                if (has16Bit)
                {
                    const auto* idx = reinterpret_cast<const physx::PxU16*>(indices) + (i * 3);
                    i0 = idx[0]; i1 = idx[1]; i2 = idx[2];
                }
                else
                {
                    const auto* idx = reinterpret_cast<const physx::PxU32*>(indices) + (i * 3);
                    i0 = idx[0]; i1 = idx[1]; i2 = idx[2];
                }

                const physx::PxVec3 v0{ globalPose.transform(pxScale.transform(vertices[i0])) };
                const physx::PxVec3 v1{ globalPose.transform(pxScale.transform(vertices[i1])) };
                const physx::PxVec3 v2{ globalPose.transform(pxScale.transform(vertices[i2])) };

                shapeRenderer->DrawLine({ v0.x, v0.y, v0.z }, { v1.x, v1.y, v1.z }, debugColor);
                shapeRenderer->DrawLine({ v1.x, v1.y, v1.z }, { v2.x, v2.y, v2.z }, debugColor);
                shapeRenderer->DrawLine({ v2.x, v2.y, v2.z }, { v0.x, v0.y, v0.z }, debugColor);
            }
        }
        break;
    }
    }
}

void StaticMeshColliderComponent::Serialize(nlohmann::json& json) const
{
    json["ShapeType"] = static_cast<int>(m_config.shapeType);
    json["Layer"] = m_config.layer;
    json["CollidesWith"] = m_config.collidesWith;

    // Bounds Extents
    json["ProxyExtX"] = m_config.proxyExtents.x;
    json["ProxyExtY"] = m_config.proxyExtents.y;
    json["ProxyExtZ"] = m_config.proxyExtents.z;

    // Gizmo Local Offsets
    json["LocalOffsetX"] = m_config.localOffset.x;
    json["LocalOffsetY"] = m_config.localOffset.y;
    json["LocalOffsetZ"] = m_config.localOffset.z;

    json["LocalRotX"] = m_config.localRotation.x;
    json["LocalRotY"] = m_config.localRotation.y;
    json["LocalRotZ"] = m_config.localRotation.z;

    json["IsTrigger"] = m_config.isTrigger;
    json["ShowDebug"] = m_showDebug;
}

void StaticMeshColliderComponent::Deserialize(const nlohmann::json& json)
{
    m_config.shapeType = static_cast<ColliderShapeType>(json.value("ShapeType", static_cast<int>(ColliderShapeType::Box)));
    m_config.layer = json.value("Layer", CollisionLayer::WorldStatic);
    m_config.collidesWith = json.value("CollidesWith", CollisionLayer::All);

    m_config.proxyExtents = {
        json.value("ProxyExtX", 1.0f),
        json.value("ProxyExtY", 1.0f),
        json.value("ProxyExtZ", 1.0f)
    };

    m_config.localOffset = {
        json.value("LocalOffsetX", 0.0f),
        json.value("LocalOffsetY", 0.0f),
        json.value("LocalOffsetZ", 0.0f)
    };

    m_config.localRotation = {
        json.value("LocalRotX", 0.0f),
        json.value("LocalRotY", 0.0f),
        json.value("LocalRotZ", 0.0f)
    };

    m_config.isTrigger = json.value("IsTrigger", false);
    m_showDebug = json.value("ShowDebug", false);

    RebuildPhysics();
}

void StaticMeshColliderComponent::DrawInspector()
{
    ImGui::TextDisabled("Static Physics Proxy");
    ImGui::Separator();

    IComponent* selectedComp = EditorManager::Instance().GetSelectedComponent();
    const bool isTriangleMesh{ m_config.shapeType == ColliderShapeType::TriangleMesh };

    // Lock out Gizmo if TriangleMesh is active
    if (isTriangleMesh && selectedComp == this)
    {
        EditorManager::Instance().SetSelectedComponent(nullptr);
        selectedComp = nullptr;
    }

    if (!isTriangleMesh)
    {
        const bool isActiveContext{ selectedComp == this };
        if (isActiveContext)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{ 0.929f, 0.094f, 0.541f, 1.00f });
        }

		// Gizmo redirection button to allow the user to manipulate the collider bounds in the scene view
        if (ImGui::Button("Edit Bounds with Gizmo", ImVec2{ -1.0f, 30.0f }))
        {
            EditorManager::Instance().SetSelectedComponent(isActiveContext ? nullptr : this);
        }

        if (isActiveContext) ImGui::PopStyleColor();

        // Manual user trigger for auto-fitting
        if (ImGui::Button("Fit to Mesh", ImVec2{ -1.0f, 30.0f }))
        {
            AutoFitToMesh();
        }
    }

    ImGui::Spacing();
    ImGui::Checkbox("Show Debug Collision", &m_showDebug);

    int shapeTypeAsInt{ static_cast<int>(m_config.shapeType) };
    const char* shapeNames[]{ "Box", "Sphere", "Capsule", "TriangleMesh" };

    if (ImGui::Combo("Proxy Type", &shapeTypeAsInt, shapeNames, IM_ARRAYSIZE(shapeNames)))
    {
        m_config.shapeType = static_cast<ColliderShapeType>(shapeTypeAsInt);

        // Clean the dirty offsets when switching to Triangle Mesh so it fits perfectly again
        if (m_config.shapeType == ColliderShapeType::TriangleMesh)
        {
            m_config.localOffset = { 0.0f, 0.0f, 0.0f };
            m_config.localRotation = { 0.0f, 0.0f, 0.0f };
        }
        RebuildPhysics();
    }

    if (!isTriangleMesh)
    {
        if (ImGui::DragFloat3("Proxy Extents", &m_config.proxyExtents.x, 0.1f, 0.001f, 100.0f)) RebuildPhysics();
        if (ImGui::DragFloat3("Local Offset", &m_config.localOffset.x, 0.05f)) RebuildPhysics();
        if (ImGui::DragFloat3("Local Rotation", &m_config.localRotation.x, 0.5f)) RebuildPhysics();
    }
}

// Automatically register component with dynamic factory
REGISTER_COMPONENT(StaticMeshColliderComponent)