#include "PhysicsManager.h"
#include <cassert>

void PhysicsManager::Initialize()
{
    m_foundation.reset(PxCreateFoundation(PX_PHYSICS_VERSION, m_allocator, m_errorCallback));
    assert(m_foundation != nullptr && "PhysX Foundation failed to initialize!");

    m_physics.reset(PxCreatePhysics(PX_PHYSICS_VERSION, *m_foundation, physx::PxTolerancesScale(), true, nullptr));
    assert(m_physics != nullptr && "PhysX Physics failed to initialize!");

    physx::PxSceneDesc sceneDesc{ m_physics->getTolerancesScale() };
    sceneDesc.gravity = physx::PxVec3(0.0f, -9.81f, 0.0f);

    m_dispatcher.reset(physx::PxDefaultCpuDispatcherCreate(2));
    sceneDesc.cpuDispatcher = m_dispatcher.get();
    sceneDesc.filterShader = physx::PxDefaultSimulationFilterShader;

    m_scene.reset(m_physics->createScene(sceneDesc));
    m_controllerManager.reset(PxCreateControllerManager(*m_scene));
    m_defaultMaterial.reset(m_physics->createMaterial(0.5f, 0.5f, 0.1f));
}

void PhysicsManager::Simulate(float dt)
{
    if (m_scene)
    {
        m_scene->simulate(dt);
        m_scene->fetchResults(true);
    }
}

void PhysicsManager::Shutdown() noexcept
{
    m_defaultMaterial.reset();
    m_controllerManager.reset();
    m_scene.reset();
    m_dispatcher.reset();
    m_physics.reset();
    m_foundation.reset();
}