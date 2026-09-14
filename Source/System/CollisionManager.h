#pragma once

#include <algorithm>
#include <cmath>
#include <DirectXMath.h>

// Axis-Aligned Bounding Box for rapid broad-phase rejection
struct AABB
{
    DirectX::XMFLOAT3 minPoint{};
    DirectX::XMFLOAT3 maxPoint{};
};

// Evaluates true if two AABBs overlap in 3D space.
// Used for extremely fast pre-collision culling before expensive math.
[[nodiscard]] constexpr bool CheckAABBIntersection(const AABB& a, const AABB& b) noexcept
{
    return (a.minPoint.x <= b.maxPoint.x && a.maxPoint.x >= b.minPoint.x) &&
        (a.minPoint.y <= b.maxPoint.y && a.maxPoint.y >= b.minPoint.y) &&
        (a.minPoint.z <= b.maxPoint.z && a.maxPoint.z >= b.minPoint.z);
}

class CollisionManager
{
public:
    CollisionManager() noexcept = default;
    ~CollisionManager() = default;

    // Delete copy/move semantics
    CollisionManager(const CollisionManager&) = delete;
    CollisionManager& operator=(const CollisionManager&) = delete;

    void Initialize() noexcept {}
    void Update(float elapsedTime) noexcept {}
};