#pragma once

#include <array>
#include <cstddef>
#include <memory>
#include <unordered_map>
#include <vector>
#include <PxPhysicsAPI.h>

class Model;

namespace PhysicsConfig
{
    // Fixed simulation delta time (60 Hz) for deterministic physics stepping
    inline constexpr float s_fixedTimestep{ 1.0f / 60.0f };

    // Maximum sub-steps per frame to prevent the "spiral of death" during frame drops
    inline constexpr int s_maxSubSteps{ 4 };

    // 64 KB scratch buffer size for zero-allocation contact generation in PxScene::simulate
    inline constexpr std::size_t s_scratchBufferSize{ 64 * 1024 };

    // Tolerance thresholds for floating-point transform dirty checking
    inline constexpr float s_transformEpsilon{ 0.0001f };
}

// Custom deleter functor ensuring correct RAII release of PhysX SDK resources
struct PhysXDeleter
{
    template <typename T>
    void operator()(T* resource) const noexcept
    {
        // Guard against double-release or null pointer invocation
        if (resource)
        {
            resource->release();
        }
    }
};

// Manages PhysX core instances, asset mesh caching, and fixed-stepping physics simulation
class PhysicsManager final
{
public:
    // Singleton access enforcing global lifecycle ownership
    [[nodiscard]] static PhysicsManager& Instance() noexcept
    {
        static PhysicsManager s_instance{};
        return s_instance;
    }

    // Delete copy and move semantics to preserve unique engine ownership
    PhysicsManager(const PhysicsManager&) = delete;
    PhysicsManager& operator=(const PhysicsManager&) = delete;
    PhysicsManager(PhysicsManager&&) = delete;
    PhysicsManager& operator=(PhysicsManager&&) = delete;

    // Initializes Foundation, Physics SDK, CPU dispatcher, Scene, and default materials
    void Initialize();

    // Steps the physics simulation using an internal fixed-time accumulator
    void Simulate(float dt);

    // Cleans up all physics actors, scene meshes, and SDK resources in strict reverse order
    void Shutdown() noexcept;

    // Queries or cooks and caches PhysX triangle meshes for a given 3D model asset
    [[nodiscard]] const std::vector<physx::PxTriangleMesh*>& GetOrCreateTriangleMeshes(const Model* model);

    // Core accessors
    [[nodiscard]] physx::PxPhysics* GetPhysics() const noexcept { return m_physics.get(); }
    [[nodiscard]] physx::PxScene* GetScene() const noexcept { return m_scene.get(); }
    [[nodiscard]] physx::PxMaterial* GetDefaultMaterial() const noexcept { return m_defaultMaterial.get(); }
    [[nodiscard]] physx::PxControllerManager* GetControllerManager() const noexcept { return m_controllerManager.get(); }

private:
    PhysicsManager() = default;
    ~PhysicsManager() = default;

    // Cooks sub-meshes for a model using BVH34 midphase acceleration structures
    [[nodiscard]] std::vector<physx::PxTriangleMesh*> CookModelMeshes(const Model* model);

    // PhysX allocators and error handlers
    physx::PxDefaultAllocator     m_allocator{};
    physx::PxDefaultErrorCallback m_errorCallback{};

    // Core PhysX SDK handles wrapped in RAII smart pointers
    std::unique_ptr<physx::PxFoundation, PhysXDeleter>            m_foundation{};
    std::unique_ptr<physx::PxPhysics, PhysXDeleter>               m_physics{};
    std::unique_ptr<physx::PxDefaultCpuDispatcher, PhysXDeleter>  m_dispatcher{};
    std::unique_ptr<physx::PxScene, PhysXDeleter>                 m_scene{};
    std::unique_ptr<physx::PxControllerManager, PhysXDeleter>     m_controllerManager{};
    std::unique_ptr<physx::PxMaterial, PhysXDeleter>              m_defaultMaterial{};

    // Centralized collision cache: maps source Model pointers to cooked PhysX geometry
    std::unordered_map<const Model*, std::vector<physx::PxTriangleMesh*>> m_meshCache{};

    // Simulation accumulator for fixed time-stepping
    float m_accumulator{ 0.0f };

    // 16-byte aligned temporary scratch memory block to eliminate runtime heap thrashing
    alignas(16) std::array<std::byte, PhysicsConfig::s_scratchBufferSize> m_scratchBuffer{};
};