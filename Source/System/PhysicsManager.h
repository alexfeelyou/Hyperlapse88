#pragma once

#include <memory>
#include <PxPhysicsAPI.h>

// Custom deleter for PhysX smart pointers
struct PhysXDeleter
{
    template <typename T>
    void operator()(T* ptr) const noexcept
    {
        if (ptr) ptr->release();
    }
};

// Globally manages the PhysX foundation, scene, and simulation stepping
class PhysicsManager
{
public:
    static PhysicsManager& Instance() noexcept
    {
        static PhysicsManager s_instance{};
        return s_instance;
    }

    PhysicsManager(const PhysicsManager&) = delete;
    PhysicsManager& operator=(const PhysicsManager&) = delete;

    void Initialize();
    void Simulate(float dt);
    void Shutdown() noexcept;

    // Accessors for components
    [[nodiscard]] physx::PxPhysics* GetPhysics() const noexcept { return m_physics.get(); }
    [[nodiscard]] physx::PxScene* GetScene() const noexcept { return m_scene.get(); }
    [[nodiscard]] physx::PxMaterial* GetDefaultMaterial() const noexcept { return m_defaultMaterial.get(); }
    [[nodiscard]] physx::PxControllerManager* GetControllerManager() const noexcept { return m_controllerManager.get(); }

private:
    PhysicsManager() = default;
    ~PhysicsManager() = default;

    physx::PxDefaultAllocator m_allocator{};
    physx::PxDefaultErrorCallback m_errorCallback{};

    std::unique_ptr<physx::PxFoundation, PhysXDeleter> m_foundation{};
    std::unique_ptr<physx::PxPhysics, PhysXDeleter> m_physics{};
    std::unique_ptr<physx::PxDefaultCpuDispatcher, PhysXDeleter> m_dispatcher{};
    std::unique_ptr<physx::PxScene, PhysXDeleter> m_scene{};
    std::unique_ptr<physx::PxControllerManager, PhysXDeleter> m_controllerManager{};
    std::unique_ptr<physx::PxMaterial, PhysXDeleter> m_defaultMaterial{};
};