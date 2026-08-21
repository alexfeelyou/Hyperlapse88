#pragma once

#include <PxPhysicsAPI.h>

struct PhysXDeleter
{
    template <typename T>
    void operator()(T* ptr) const { if (ptr) ptr->release(); }
};