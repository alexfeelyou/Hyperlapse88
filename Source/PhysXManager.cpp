#include "PhysXManager.h"
#include <cassert>

// =========================================================
// Static member definitions
// =========================================================
physx::PxDefaultAllocator     PhysXManager::s_allocator;
physx::PxDefaultErrorCallback PhysXManager::s_errorCallback;
physx::PxFoundation* PhysXManager::s_foundation = nullptr;
int                           PhysXManager::s_refCount = 0;

// =========================================================
// GetOrCreate
// =========================================================
physx::PxFoundation* PhysXManager::GetOrCreate()
{
    if (s_refCount == 0)
    {
        s_foundation = PxCreateFoundation(
            PX_PHYSICS_VERSION, s_allocator, s_errorCallback);
        assert(s_foundation && "PhysXManager: PxCreateFoundation failed!");
    }
    s_refCount++;
    return s_foundation;
}

// =========================================================
// Release
// =========================================================
void PhysXManager::Release()
{
    if (s_refCount <= 0) return; // guard against double-release

    s_refCount--;
    if (s_refCount == 0 && s_foundation)
    {
        s_foundation->release();
        s_foundation = nullptr;
    }
}