#pragma once
#include <PxPhysicsAPI.h>

// =========================================================
// PhysXManager
// Manages PxFoundation as a true singleton with refcounting.
// Multiple scenes can safely create/destroy PxPhysics
// without ever double-creating or premature-releasing the
// Foundation.
//
// Usage:
//   Constructor : PhysXManager::GetOrCreate()
//   Destructor  : PhysXManager::Release()
//   Get ptr     : PhysXManager::Get()
// =========================================================

class PhysXManager
{
public:
    // Call once per scene constructor — creates Foundation on first call
    static physx::PxFoundation* GetOrCreate();

    // Call once per scene destructor — releases Foundation when refcount hits 0
    static void Release();

    // Get the raw Foundation ptr (valid between GetOrCreate and Release)
    static physx::PxFoundation* Get() { return s_foundation; }

private:
    static physx::PxDefaultAllocator     s_allocator;
    static physx::PxDefaultErrorCallback s_errorCallback;
    static physx::PxFoundation* s_foundation;
    static int                           s_refCount;
};