#pragma once

#include <DirectXMath.h>
#include <algorithm>
#include <cmath>
#include <DirectXCollision.h>
#include <limits>
#include <vector>
#include "Model.h"
#include "Stage.h"

class Collision
{
public:
    static bool IntersectMovingSphereVsBoxInfiniteY(
        const DirectX::XMFLOAT3& prevPos,
        const DirectX::XMFLOAT3& currPos,
        float radius,
        const DebugWallData& wall,
        DirectX::XMFLOAT3& outHitPoint,
        DirectX::XMFLOAT3& outHitNormal,
        float& outT 
    );

    static bool IntersectRayVsOBB(
        const DirectX::XMFLOAT3& rayOrigin,
        const DirectX::XMFLOAT3& rayDir,
        float rayLength,
        const DebugWallData& wall,
        float& outDist,
        DirectX::XMFLOAT3& outNormal
    );

    static bool ResolveOBB(
        const DirectX::XMFLOAT3& entityPos,
        float entityRadius,
        const DebugWallData& wall,
        DirectX::XMFLOAT3& outFixPosition
    );

    static bool RayCast(
        const DirectX::XMFLOAT3& start,
        const DirectX::XMFLOAT3& end,
        const DirectX::XMFLOAT4X4& worldTransform,
        const Model* model,
        DirectX::XMFLOAT3& hitPosition,
        DirectX::XMFLOAT3& hitNormal
    );

    static bool IntersectSphereVsSphere(
        const DirectX::XMFLOAT3& positionA,
        float radiusA,
        const DirectX::XMFLOAT3& positionB,
        float radiusB,
        DirectX::XMFLOAT3& outPositionB
    );

    static bool IntersectSphereVsCylinder(
        const DirectX::XMFLOAT3& spherePosition,
        float sphereRadius,
        const DirectX::XMFLOAT3& cylinderPosition,
        float cylinderRadius,
        float cylinderHeight,
        DirectX::XMFLOAT3& outCylinderPosition
    );

    static bool IntersectCubeVsCube(
        const DirectX::XMFLOAT3& cubePositionA,
        const DirectX::XMFLOAT3& cubeSizeA,
        const DirectX::XMFLOAT3& cubePositionB,
        const DirectX::XMFLOAT3& cubeSizeB,
        DirectX::XMFLOAT3& outCubePositionB
    );

    static bool IntersectSphereVsCube(
        const DirectX::XMFLOAT3& spherePosition,
        float sphereRadius,
        const DirectX::XMFLOAT3& cubePosition,
        const DirectX::XMFLOAT3& cubeSize,
        DirectX::XMFLOAT3& outCubePosition
    );

    static bool IsPointInCube(
        const DirectX::XMFLOAT3& point,
        const DirectX::XMFLOAT3& cubePosition,
        const DirectX::XMFLOAT3& cubeSize
    );

    static bool IntersectCylinderVsCylinder(
        const DirectX::XMFLOAT3& positionA,
        float radiusA,
        float heightA,
        const DirectX::XMFLOAT3& positionB,
        float radiusB,
        float heightB,
        DirectX::XMFLOAT3& outPositionB
    );
};