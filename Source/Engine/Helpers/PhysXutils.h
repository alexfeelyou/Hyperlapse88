#pragma once

#include <PxPhysicsAPI.h>

// =========================================================
// PHYSX SHARED UTILITIES
// Dipakai oleh semua Scene yang menggunakan PhysX.
// =========================================================

struct PhysXDeleter
{
    template <typename T>
    void operator()(T* ptr) const { if (ptr) ptr->release(); }
};