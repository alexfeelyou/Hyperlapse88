#include <algorithm>
#include <cassert>
#include <DirectXMath.h>
#include "System/PhysicsManager.h"
#include "System/Model.h"

void PhysicsManager::Initialize()
{
    // Create PhysX Foundation
    m_foundation.reset(PxCreateFoundation(PX_PHYSICS_VERSION, m_allocator, m_errorCallback));
    assert(m_foundation && "PhysX Foundation initialization failed!");

    // Initialize main Physics SDK
    const physx::PxTolerancesScale scale{};
    m_physics.reset(PxCreatePhysics(PX_PHYSICS_VERSION, *m_foundation, scale, true, nullptr));
    assert(m_physics && "PhysX Physics SDK initialization failed!");

    // Configure scene descriptor with dual-thread CPU dispatcher and BVH midphase
    physx::PxSceneDesc sceneDesc{ m_physics->getTolerancesScale() };
    sceneDesc.gravity = physx::PxVec3{ 0.0f, -9.81f, 0.0f };

    m_dispatcher.reset(physx::PxDefaultCpuDispatcherCreate(2));
    sceneDesc.cpuDispatcher = m_dispatcher.get();
    sceneDesc.filterShader = physx::PxDefaultSimulationFilterShader;

    // Create Scene, Character Controller Manager, and Default Surface Material
    m_scene.reset(m_physics->createScene(sceneDesc));
    assert(m_scene && "PhysX Scene creation failed!");

    m_controllerManager.reset(PxCreateControllerManager(*m_scene));
    m_defaultMaterial.reset(m_physics->createMaterial(0.5f, 0.5f, 0.1f));
}

void PhysicsManager::Simulate(const float dt)
{
    if (!m_scene)
    {
        return;
    }

    // Accumulate elapsed frame delta time
    m_accumulator += dt;

    int stepCount{ 0 };
    while (m_accumulator >= PhysicsConfig::s_fixedTimestep && stepCount < PhysicsConfig::s_maxSubSteps)
    {
        // Execute fixed simulation step using pre-allocated scratch memory buffer
        m_scene->simulate(
            PhysicsConfig::s_fixedTimestep,
            nullptr,
            m_scratchBuffer.data(),
            static_cast<physx::PxU32>(m_scratchBuffer.size()),
            true
        );
        m_scene->fetchResults(true);

        m_accumulator -= PhysicsConfig::s_fixedTimestep;
        ++stepCount;
    }

    // Drop leftover time if spiral-of-death threshold is exceeded to avoid frame hitching
    if (stepCount >= PhysicsConfig::s_maxSubSteps)
    {
        m_accumulator = 0.0f;
    }
}

void PhysicsManager::Shutdown() noexcept
{
    // Release all cached triangle meshes before tearing down the Physics SDK
    for (auto& [modelPtr, meshList] : m_meshCache)
    {
        for (auto* mesh : meshList)
        {
            if (mesh)
            {
                mesh->release();
            }
        }
    }
    m_meshCache.clear();

    // Release scene actors and controllers via RAII smart pointers
    m_defaultMaterial.reset();
    m_controllerManager.reset();
    m_scene.reset();
    m_dispatcher.reset();
    m_physics.reset();
    m_foundation.reset();
}

[[nodiscard]] const std::vector<physx::PxTriangleMesh*>& PhysicsManager::GetOrCreateTriangleMeshes(const Model* model)
{
    // Return empty fallback list on invalid pointer
    static const std::vector<physx::PxTriangleMesh*> s_emptyMeshes{};
    if (!model)
    {
        return s_emptyMeshes;
    }

    // Return cached mesh vector if previously cooked
    if (const auto it{ m_meshCache.find(model) }; it != m_meshCache.end())
    {
        return it->second;
    }

    // Cook new mesh vector, emplace into cache map, and return reference
    auto cookedMeshes{ CookModelMeshes(model) };
    const auto [insertedIt, success] { m_meshCache.emplace(model, std::move(cookedMeshes)) };
    return insertedIt->second;
}

[[nodiscard]] std::vector<physx::PxTriangleMesh*> PhysicsManager::CookModelMeshes(const Model* model)
{
    std::vector<physx::PxTriangleMesh*> results{};
    if (!model || !m_physics)
    {
        return results;
    }

    // Configure production-grade cooking parameters with BVH34 midphase structures
    const physx::PxTolerancesScale tolerances{ m_physics->getTolerancesScale() };
    physx::PxCookingParams cookingParams{ tolerances };

    // Use fast midphase queries with 4 triangles per leaf
    cookingParams.midphaseDesc.setToDefault(physx::PxMeshMidPhase::eBVH34);
    cookingParams.midphaseDesc.mBVH34Desc.numPrimsPerLeaf = 4;
    cookingParams.suppressTriangleMeshRemapTable = true;

    for (const auto& subMesh : model->GetMeshes())
    {
        if (subMesh.vertices.empty() || subMesh.indices.empty())
        {
            continue;
        }

        // Bake bone / node transforms into local vertex array
        std::vector<physx::PxVec3> bakedVertices{};
        bakedVertices.reserve(subMesh.vertices.size());
        const DirectX::XMMATRIX globalMat{ DirectX::XMLoadFloat4x4(&subMesh.node->globalTransform) };

        for (const auto& vertex : subMesh.vertices)
        {
            const DirectX::XMVECTOR localPos{ DirectX::XMLoadFloat3(&vertex.position) };
            const DirectX::XMVECTOR transformedPos{ DirectX::XMVector3TransformCoord(localPos, globalMat) };

            DirectX::XMFLOAT3 bakedPos{};
            DirectX::XMStoreFloat3(&bakedPos, transformedPos);
            bakedVertices.emplace_back(bakedPos.x, bakedPos.y, bakedPos.z);
        }

        // Setup triangle mesh descriptor
        physx::PxTriangleMeshDesc meshDesc{};
        meshDesc.points.count = static_cast<physx::PxU32>(bakedVertices.size());
        meshDesc.points.stride = sizeof(physx::PxVec3);
        meshDesc.points.data = bakedVertices.data();
        meshDesc.triangles.count = static_cast<physx::PxU32>(subMesh.indices.size() / 3);
        meshDesc.triangles.stride = 3 * sizeof(std::uint32_t);
        meshDesc.triangles.data = subMesh.indices.data();

        // Cook the mesh directly into the runtime PhysX insertion callback
        if (physx::PxTriangleMesh * triMesh{ PxCreateTriangleMesh(cookingParams, meshDesc, m_physics->getPhysicsInsertionCallback()) })
        {
            results.push_back(triMesh);
        }
    }

    return results;
}