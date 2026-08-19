#include "Collision.h"

using namespace DirectX;

// =================================================================
// IntersectSegmentVsAABB (The "Solid Plane" Check)
// =================================================================
bool Collision::IntersectMovingSphereVsBoxInfiniteY(
    const DirectX::XMFLOAT3& prevPos,
    const DirectX::XMFLOAT3& currPos,
    float radius,
    const DebugWallData& wall,
    DirectX::XMFLOAT3& outHitPoint,
    DirectX::XMFLOAT3& outHitNormal,
    float& outT)
{
    // 1. Prepare Matrices
    XMMATRIX matRot = XMMatrixRotationRollPitchYaw(
        XMConvertToRadians(wall.Rotation.x),
        XMConvertToRadians(wall.Rotation.y),
        XMConvertToRadians(wall.Rotation.z)
    );
    XMMATRIX matInvRot = XMMatrixTranspose(matRot);
    XMVECTOR vWallPos = XMLoadFloat3(&wall.Position);

    // 2. Transform Positions to Local Space
    // We treat the wall as the center of the universe (0,0,0) with no rotation.
    XMVECTOR vPrev = XMLoadFloat3(&prevPos);
    XMVECTOR vCurr = XMLoadFloat3(&currPos);

    XMVECTOR vRelPrev = XMVectorSubtract(vPrev, vWallPos);
    XMVECTOR vRelCurr = XMVectorSubtract(vCurr, vWallPos);

    XMVECTOR vLocPrev = XMVector3TransformNormal(vRelPrev, matInvRot);
    XMVECTOR vLocCurr = XMVector3TransformNormal(vRelCurr, matInvRot);

    XMFLOAT3 p0, p1;
    XMStoreFloat3(&p0, vLocPrev);
    XMStoreFloat3(&p1, vLocCurr);

    // 3. Define the "Expanded" Box
    // We expand the box by the ball's radius.
    // This allows us to treat the ball as a single point (Raycast).
    float maxX = wall.Scale.x + radius;
    float minX = -maxX;
    float maxZ = wall.Scale.z + radius;
    float minZ = -maxZ;

    // Direction vector
    float dx = p1.x - p0.x;
    float dz = p1.z - p0.z;

    // Check if we are already inside (Deep Stuck Fix)
    // Ignore Y axis completely
    if (p0.x >= minX && p0.x <= maxX && p0.z >= minZ && p0.z <= maxZ)
    {
        // We started inside. Push out to nearest face.
        float dXPos = fabsf(maxX - p0.x);
        float dXNeg = fabsf(minX - p0.x);
        float dZPos = fabsf(maxZ - p0.z);
        float dZNeg = fabsf(minZ - p0.z);

        float minD = (std::min)({ dXPos, dXNeg, dZPos, dZNeg });

        outT = 0.0f;
        outHitPoint = prevPos;

        // Determine Normal in World Space
        XMVECTOR vNorm;
        if (minD == dXPos)      vNorm = XMVectorSet(1, 0, 0, 0);
        else if (minD == dXNeg) vNorm = XMVectorSet(-1, 0, 0, 0);
        else if (minD == dZPos) vNorm = XMVectorSet(0, 0, 1, 0);
        else                    vNorm = XMVectorSet(0, 0, -1, 0);

        vNorm = XMVector3TransformNormal(vNorm, matRot);
        XMStoreFloat3(&outHitNormal, vNorm);
        return true;
    }

    // 4. Ray-Slab Intersection (Slab Method) on X and Z
    // We want to find the first time 't' (0..1) the segment enters the box.
    float tEnter = 0.0f;
    float tExit = 1.0f;
    float tNormalX = 0.0f, tNormalZ = 0.0f; // Local normal components

    // --- X Axis ---
    if (fabsf(dx) < 1e-6f) // Parallel to X planes
    {
        if (p0.x < minX || p0.x > maxX) return false; // Parallel and outside
    }
    else
    {
        float invDir = 1.0f / dx;
        float t1 = (minX - p0.x) * invDir;
        float t2 = (maxX - p0.x) * invDir;
        float s = -1.0f; // Sign of normal if t1 is hit

        if (t1 > t2) { std::swap(t1, t2); s = 1.0f; }

        if (t1 > tEnter)
        {
            tEnter = t1;
            tNormalX = (invDir < 0) ? 1.0f : -1.0f;
            tNormalZ = 0.0f;
        }
        if (t2 < tExit) tExit = t2;

        if (tEnter > tExit) return false; // Miss
    }

    // --- Z Axis ---
    if (fabsf(dz) < 1e-6f) // Parallel to Z planes
    {
        if (p0.z < minZ || p0.z > maxZ) return false;
    }
    else
    {
        float invDir = 1.0f / dz;
        float t1 = (minZ - p0.z) * invDir;
        float t2 = (maxZ - p0.z) * invDir;

        if (t1 > t2) { std::swap(t1, t2); }

        if (t1 > tEnter)
        {
            tEnter = t1;
            tNormalX = 0.0f;
            tNormalZ = (invDir < 0) ? 1.0f : -1.0f;
        }
        if (t2 < tExit) tExit = t2;

        if (tEnter > tExit) return false;
    }

    // If we reached here, we have an intersection at tEnter
    outT = tEnter;

    // Transform Normal to World
    XMVECTOR vLocNorm = XMVectorSet(tNormalX, 0.0f, tNormalZ, 0.0f);
    XMVECTOR vWorldNorm = XMVector3TransformNormal(vLocNorm, matRot);
    XMStoreFloat3(&outHitNormal, vWorldNorm);

    // Calculate Hit Point in World
    XMVECTOR vHit = XMVectorAdd(vPrev, XMVectorScale(XMVectorSubtract(vCurr, vPrev), tEnter));
    XMStoreFloat3(&outHitPoint, vHit);

    return true;
}
// =================================================================
// IntersectRayVsOBB (Slab Method)
// =================================================================
bool Collision::IntersectRayVsOBB(
    const DirectX::XMFLOAT3& rayOrigin,
    const DirectX::XMFLOAT3& rayDir,
    float rayLength,
    const DebugWallData& wall,
    float& outDist,
    DirectX::XMFLOAT3& outNormal)
{
    XMVECTOR vRayOrigin = XMLoadFloat3(&rayOrigin);
    XMVECTOR vRayDir = XMLoadFloat3(&rayDir);
    XMVECTOR vWallPos = XMLoadFloat3(&wall.Position);

    // Create Rotation Matrix
    XMMATRIX matRot = XMMatrixRotationRollPitchYaw(
        XMConvertToRadians(wall.Rotation.x),
        XMConvertToRadians(wall.Rotation.y),
        XMConvertToRadians(wall.Rotation.z)
    );
    XMMATRIX matInvRot = XMMatrixTranspose(matRot);

    // Transform Ray to Local Space
    XMVECTOR vRelOrigin = XMVectorSubtract(vRayOrigin, vWallPos);
    XMVECTOR vLocalOrigin = XMVector3TransformNormal(vRelOrigin, matInvRot);
    XMVECTOR vLocalDir = XMVector3TransformNormal(vRayDir, matInvRot);

    XMFLOAT3 localOrigin; XMStoreFloat3(&localOrigin, vLocalOrigin);
    XMFLOAT3 localDir;    XMStoreFloat3(&localDir, vLocalDir);

    // [ROBUSTNESS] Inflate the box slightly for the ray check.
    // This helps catch cases where the ball center is slightly misaligned.
    // We treat Y as infinite.
    float padding = 0.1f;
    XMFLOAT3 extents = wall.Scale;
    extents.x += padding;
    extents.z += padding;
    extents.y = 10000.0f; // Infinite Height Hack

    float tMin = 0.0f;
    float tMax = rayLength;

    XMFLOAT3 localNormal = { 0, 0, 0 };
    int hitAxis = -1;

    // --- Check X Axis ---
    if (fabsf(localDir.x) > 1e-6f)
    {
        float invD = 1.0f / localDir.x;
        float t0 = (-extents.x - localOrigin.x) * invD;
        float t1 = (extents.x - localOrigin.x) * invD;
        if (invD < 0.0f) std::swap(t0, t1);

        if (t0 > tMin) { tMin = t0; hitAxis = 0; }
        if (t1 < tMax) { tMax = t1; }
        if (tMin > tMax) return false;
    }
    else if (localOrigin.x < -extents.x || localOrigin.x > extents.x) return false;

    // --- Check Y Axis ---
    if (fabsf(localDir.y) > 1e-6f)
    {
        float invD = 1.0f / localDir.y;
        float t0 = (-extents.y - localOrigin.y) * invD;
        float t1 = (extents.y - localOrigin.y) * invD;
        if (invD < 0.0f) std::swap(t0, t1);

        if (t0 > tMin) { tMin = t0; hitAxis = 1; }
        if (t1 < tMax) { tMax = t1; }
        if (tMin > tMax) return false;
    }
    // No "else" for Y because it's infinite, ray always "inside" Y-slab unless way off

    // --- Check Z Axis ---
    if (fabsf(localDir.z) > 1e-6f)
    {
        float invD = 1.0f / localDir.z;
        float t0 = (-extents.z - localOrigin.z) * invD;
        float t1 = (extents.z - localOrigin.z) * invD;
        if (invD < 0.0f) std::swap(t0, t1);

        if (t0 > tMin) { tMin = t0; hitAxis = 2; }
        if (t1 < tMax) { tMax = t1; }
        if (tMin > tMax) return false;
    }
    else if (localOrigin.z < -extents.z || localOrigin.z > extents.z) return false;

    // Construct Normal
    if (hitAxis == 0) localNormal = { (localDir.x < 0) ? 1.0f : -1.0f, 0.0f, 0.0f };
    else if (hitAxis == 1) localNormal = { 0.0f, (localDir.y < 0) ? 1.0f : -1.0f, 0.0f };
    else if (hitAxis == 2) localNormal = { 0.0f, 0.0f, (localDir.z < 0) ? 1.0f : -1.0f };

    XMVECTOR vLocalNorm = XMLoadFloat3(&localNormal);
    XMVECTOR vWorldNorm = XMVector3TransformNormal(vLocalNorm, matRot);
    XMStoreFloat3(&outNormal, vWorldNorm);

    outDist = tMin;
    return true;
}

// =================================================================
// ResolveOBB (EXISTING)
// =================================================================
bool Collision::ResolveOBB(
    const DirectX::XMFLOAT3& entityPos,
    float entityRadius,
    const DebugWallData& wall,
    DirectX::XMFLOAT3& outFixPosition)
{
    XMVECTOR vEntityPos = XMLoadFloat3(&entityPos);
    XMVECTOR vWallPos = XMLoadFloat3(&wall.Position);
    XMVECTOR vRelPos = XMVectorSubtract(vEntityPos, vWallPos);

    XMMATRIX matRot = XMMatrixRotationRollPitchYaw(
        XMConvertToRadians(wall.Rotation.x),
        XMConvertToRadians(wall.Rotation.y),
        XMConvertToRadians(wall.Rotation.z)
    );
    XMMATRIX matInvRot = XMMatrixTranspose(matRot);
    XMVECTOR vLocalPos = XMVector3TransformNormal(vRelPos, matInvRot);

    float localX = XMVectorGetX(vLocalPos);
    float localY = XMVectorGetY(vLocalPos);
    float localZ = XMVectorGetZ(vLocalPos);

    float extentX = wall.Scale.x;
    float extentY = wall.Scale.y;
    float extentZ = wall.Scale.z;

    float overlapX = (extentX + entityRadius) - fabsf(localX);
    float overlapY = (extentY + entityRadius) - fabsf(localY);
    float overlapZ = (extentZ + entityRadius) - fabsf(localZ);

    if (overlapX <= 0 || overlapY <= 0 || overlapZ <= 0) return false;

    float pushX = 0, pushY = 0, pushZ = 0;

    if (overlapX < overlapY && overlapX < overlapZ)
        pushX = (localX > 0) ? overlapX : -overlapX;
    else if (overlapY < overlapZ)
        pushY = (localY > 0) ? overlapY : -overlapY;
    else
        pushZ = (localZ > 0) ? overlapZ : -overlapZ;

    XMVECTOR vLocalPush = XMVectorSet(pushX, pushY, pushZ, 0.0f);
    XMVECTOR vWorldPush = XMVector3TransformNormal(vLocalPush, matRot);
    XMVECTOR vNewPos = XMVectorAdd(vEntityPos, vWorldPush);
    XMStoreFloat3(&outFixPosition, vNewPos);

    return true;
}
// =================================================================
// RayCast
// =================================================================
bool Collision::RayCast(
    const DirectX::XMFLOAT3& start,
    const DirectX::XMFLOAT3& end,
    const DirectX::XMFLOAT4X4& worldTransform,
    const Model* model,
    DirectX::XMFLOAT3& hitPosition,
    DirectX::XMFLOAT3& hitNormal)
{
    if (!model) return false;

    bool hit = false;
    float nearestDist = FLT_MAX;

    XMVECTOR WorldRayStart = XMLoadFloat3(&start);
    XMVECTOR WorldRayEnd = XMLoadFloat3(&end);
    XMVECTOR WorldRayVec = XMVectorSubtract(WorldRayEnd, WorldRayStart);
    XMVECTOR WorldRayLengthVec = XMVector3Length(WorldRayVec);

    float rayLength = XMVectorGetX(WorldRayLengthVec);
    if (rayLength <= 0.0001f) return false;

    nearestDist = rayLength;
    XMMATRIX ParentWorldTransform = XMLoadFloat4x4(&worldTransform);

    const auto& meshes = model->GetMeshes();
    const auto& nodes = model->GetNodes();

    for (const auto& mesh : meshes)
    {
        if (mesh.nodeIndex < 0 || mesh.nodeIndex >= nodes.size()) continue;

        const Model::Node& node = nodes[mesh.nodeIndex];
        XMMATRIX GlobalTransform = XMLoadFloat4x4(&node.globalTransform);
        XMMATRIX MeshWorldTransform = XMMatrixMultiply(GlobalTransform, ParentWorldTransform);
        XMVECTOR Determinant;
        XMMATRIX InverseWorldTransform = XMMatrixInverse(&Determinant, MeshWorldTransform);
        XMVECTOR LocalRayStart = XMVector3TransformCoord(WorldRayStart, InverseWorldTransform);
        XMVECTOR LocalRayEnd = XMVector3TransformCoord(WorldRayEnd, InverseWorldTransform);
        XMVECTOR LocalRayVec = XMVectorSubtract(LocalRayEnd, LocalRayStart);
        XMVECTOR LocalRayDir = XMVector3Normalize(LocalRayVec);

        float localRayDist = XMVectorGetX(XMVector3Length(LocalRayVec));

        size_t indexCount = mesh.indices.size();
        for (size_t i = 0; i < indexCount; i += 3)
        {
            uint32_t i0 = mesh.indices[i];
            uint32_t i1 = mesh.indices[i + 1];
            uint32_t i2 = mesh.indices[i + 2];

            XMVECTOR V0 = XMLoadFloat3(&mesh.vertices[i0].position);
            XMVECTOR V1 = XMLoadFloat3(&mesh.vertices[i1].position);
            XMVECTOR V2 = XMLoadFloat3(&mesh.vertices[i2].position);

            float dist = 0.0f;

            if (TriangleTests::Intersects(LocalRayStart, LocalRayDir, V0, V1, V2, dist))
            {
                if (dist > 0 && dist < localRayDist)
                {
                    XMVECTOR LocalHitPos = XMVectorAdd(LocalRayStart, XMVectorScale(LocalRayDir, dist));
                    XMVECTOR WorldHitPos = XMVector3TransformCoord(LocalHitPos, MeshWorldTransform);

                    float worldDist = XMVectorGetX(XMVector3Length(XMVectorSubtract(WorldHitPos, WorldRayStart)));

                    if (worldDist < nearestDist)
                    {
                        XMVECTOR E1 = XMVectorSubtract(V1, V0);
                        XMVECTOR E2 = XMVectorSubtract(V2, V1);
                        XMVECTOR LocalNormal = XMVector3Cross(E1, E2);
                        LocalNormal = XMVector3Normalize(LocalNormal);

                        if (XMVectorGetX(XMVector3Dot(LocalRayDir, LocalNormal)) < 0.0f)
                        {
                            XMVECTOR WorldHitNormal = XMVector3TransformNormal(LocalNormal, MeshWorldTransform);
                            WorldHitNormal = XMVector3Normalize(WorldHitNormal);
                            nearestDist = worldDist;
                            XMStoreFloat3(&hitPosition, WorldHitPos);
                            XMStoreFloat3(&hitNormal, WorldHitNormal);
                            hit = true;
                        }
                    }
                }
            }
        }
    }
    return hit;
}

// =================================================================
// IntersectSphereVsSphere
// =================================================================
bool Collision::IntersectSphereVsSphere(
    const DirectX::XMFLOAT3& positionA, float radiusA,
    const DirectX::XMFLOAT3& positionB, float radiusB,
    DirectX::XMFLOAT3& outPositionB)
{
    XMVECTOR PosA = XMLoadFloat3(&positionA);
    XMVECTOR PosB = XMLoadFloat3(&positionB);
    XMVECTOR Vec = XMVectorSubtract(PosB, PosA);
    XMVECTOR LenSq = XMVector3LengthSq(Vec);

    float lenSq;
    XMStoreFloat(&lenSq, LenSq);
    float range = radiusA + radiusB;

    if (lenSq > range * range) return false;

    XMVECTOR N = XMVector3Normalize(Vec);
    XMVECTOR Push = XMVectorScale(N, range - sqrtf(lenSq));
    XMVECTOR NewPosB = XMVectorAdd(PosB, Push);

    XMStoreFloat3(&outPositionB, NewPosB);
    return true;
}

// =================================================================
// IntersectSphereVsCylinder
// =================================================================
bool Collision::IntersectSphereVsCylinder(
    const DirectX::XMFLOAT3& spherePos, float sphereRad,
    const DirectX::XMFLOAT3& cylPos, float cylRad, float cylHeight,
    DirectX::XMFLOAT3& outCylPos)
{
    if (spherePos.y > cylPos.y + cylHeight + sphereRad) return false;
    if (spherePos.y < cylPos.y - sphereRad) return false;

    float vx = spherePos.x - cylPos.x;
    float vz = spherePos.z - cylPos.z;
    float distSq = vx * vx + vz * vz;
    float range = sphereRad + cylRad;

    if (distSq >= range * range) return false;

    float dist = sqrtf(distSq);
    if (dist > 0.0001f)
    {
        vx /= dist;
        vz /= dist;
        float pushAmt = range - dist;
        outCylPos = cylPos;
        outCylPos.x -= vx * pushAmt;
        outCylPos.z -= vz * pushAmt;
    }
    return true;
}

// =================================================================
// IntersectCubeVsCube
// =================================================================
bool Collision::IntersectCubeVsCube(
    const DirectX::XMFLOAT3& posA, const DirectX::XMFLOAT3& sizeA,
    const DirectX::XMFLOAT3& posB, const DirectX::XMFLOAT3& sizeB,
    DirectX::XMFLOAT3& outPosB)
{
    float minAx = posA.x - sizeA.x; float maxAx = posA.x + sizeA.x;
    float minAy = posA.y - sizeA.y; float maxAy = posA.y + sizeA.y;
    float minAz = posA.z - sizeA.z; float maxAz = posA.z + sizeA.z;

    float minBx = posB.x - sizeB.x; float maxBx = posB.x + sizeB.x;
    float minBy = posB.y - sizeB.y; float maxBy = posB.y + sizeB.y;
    float minBz = posB.z - sizeB.z; float maxBz = posB.z + sizeB.z;

    if (maxAx < minBx || minAx > maxBx) return false;
    if (maxAy < minBy || minAy > maxBy) return false;
    if (maxAz < minBz || minAz > maxBz) return false;

    float diffX1 = maxAx - minBx; float diffX2 = maxBx - minAx;
    float penX = (diffX1 < diffX2) ? diffX1 : -diffX2;

    float diffY1 = maxAy - minBy; float diffY2 = maxBy - minAy;
    float penY = (diffY1 < diffY2) ? diffY1 : -diffY2;

    float diffZ1 = maxAz - minBz; float diffZ2 = maxBz - minAz;
    float penZ = (diffZ1 < diffZ2) ? diffZ1 : -diffZ2;

    float absX = fabsf(penX);
    float absY = fabsf(penY);
    float absZ = fabsf(penZ);

    outPosB = posB;

    if (absX < absY && absX < absZ) outPosB.x += penX;
    else if (absY < absZ)           outPosB.y += penY;
    else                            outPosB.z += penZ;

    return true;
}

// =================================================================
// IntersectSphereVsCube
// =================================================================
bool Collision::IntersectSphereVsCube(
    const DirectX::XMFLOAT3& spherePos, float sphereRad,
    const DirectX::XMFLOAT3& cubePos, const DirectX::XMFLOAT3& cubeSize,
    DirectX::XMFLOAT3& outCubePos)
{
    float closestX = (std::max)(cubePos.x - cubeSize.x, (std::min)(spherePos.x, cubePos.x + cubeSize.x));
    float closestY = (std::max)(cubePos.y - cubeSize.y, (std::min)(spherePos.y, cubePos.y + cubeSize.y));
    float closestZ = (std::max)(cubePos.z - cubeSize.z, (std::min)(spherePos.z, cubePos.z + cubeSize.z));

    float dx = spherePos.x - closestX;
    float dy = spherePos.y - closestY;
    float dz = spherePos.z - closestZ;

    float distSq = dx * dx + dy * dy + dz * dz;

    if (distSq > sphereRad * sphereRad) return false;

    float dist = sqrtf(distSq);
    if (dist > 0.0001f)
    {
        float push = sphereRad - dist;
        outCubePos = cubePos;
        outCubePos.x -= (dx / dist) * push;
        outCubePos.y -= (dy / dist) * push;
        outCubePos.z -= (dz / dist) * push;
    }
    else
    {
        outCubePos = cubePos;
        if (spherePos.x > cubePos.x) outCubePos.x -= sphereRad;
        else outCubePos.x += sphereRad;
    }

    return true;
}

// =================================================================
// IsPointInCube
// =================================================================
bool Collision::IsPointInCube(
    const DirectX::XMFLOAT3& p,
    const DirectX::XMFLOAT3& pos,
    const DirectX::XMFLOAT3& size)
{
    return (p.x >= pos.x - size.x && p.x <= pos.x + size.x &&
        p.y >= pos.y - size.y && p.y <= pos.y + size.y &&
        p.z >= pos.z - size.z && p.z <= pos.z + size.z);
}

// =================================================================
// IntersectCylinderVsCylinder
// =================================================================
bool Collision::IntersectCylinderVsCylinder(
    const DirectX::XMFLOAT3& posA, float radA, float hA,
    const DirectX::XMFLOAT3& posB, float radB, float hB,
    DirectX::XMFLOAT3& outPosB)
{
    if (posA.y > posB.y + hB) return false;
    if (posA.y + hA < posB.y) return false;

    float vx = posB.x - posA.x;
    float vz = posB.z - posA.z;
    float range = radA + radB;
    float distSq = vx * vx + vz * vz;

    if (distSq > range * range) return false;

    float dist = sqrtf(distSq);
    if (dist > 0.0001f)
    {
        vx /= dist;
        vz /= dist;
        float push = range - dist;
        outPosB = posB;
        outPosB.x += vx * push;
        outPosB.z += vz * push;
    }
    return true;
}
