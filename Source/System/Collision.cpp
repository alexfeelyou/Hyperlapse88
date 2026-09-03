#include "Collision.h"

using namespace DirectX;

// Ray Cast against Model
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

// Intersect Sphere vs Sphere
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

// Intersect Sphere vs Cylinder
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

// Intersect Cube vs Cube
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

// Intersect Sphere vs Cube
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

// Check if Point is Inside Cube
bool Collision::IsPointInCube(
    const DirectX::XMFLOAT3& p,
    const DirectX::XMFLOAT3& pos,
    const DirectX::XMFLOAT3& size)
{
    return (p.x >= pos.x - size.x && p.x <= pos.x + size.x &&
        p.y >= pos.y - size.y && p.y <= pos.y + size.y &&
        p.z >= pos.z - size.z && p.z <= pos.z + size.z);
}

// Intersect Cylinder vs Cylinder
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
