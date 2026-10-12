#include "ProfilerManager.h"
#include "ModelRenderer.h"
#include "StringHash.h"

namespace
{
    struct CascadeFrustumPlanes
    {
        DirectX::XMFLOAT4 planes[5];
    };

    struct CameraFrustumPlanes
    {
        DirectX::XMFLOAT4 planes[6];
    };

    struct BoundingSphere
    {
        DirectX::XMFLOAT3 center{ 0.0f, 0.0f, 0.0f };
        float radius{ 0.0f };
    };

    [[nodiscard]] CascadeFrustumPlanes ExtractCascadePlanes(const DirectX::XMFLOAT4X4& M) noexcept
    {
        CascadeFrustumPlanes cp{};
        cp.planes[0] = { M._14 + M._11, M._24 + M._21, M._34 + M._31, M._44 + M._41 };
        cp.planes[1] = { M._14 - M._11, M._24 - M._21, M._34 - M._31, M._44 - M._41 };
        cp.planes[2] = { M._14 + M._12, M._24 + M._22, M._34 + M._32, M._44 + M._42 };
        cp.planes[3] = { M._14 - M._12, M._24 - M._22, M._34 - M._32, M._44 - M._42 };
        cp.planes[4] = { M._14 - M._13, M._24 - M._23, M._34 - M._33, M._44 - M._43 };

        for (auto& p : cp.planes)
        {
            const float len = std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z);
            if (len > 0.00001f)
            {
                const float invLen = 1.0f / len;
                p.x *= invLen; p.y *= invLen; p.z *= invLen; p.w *= invLen;
            }
        }
        return cp;
    }

    [[nodiscard]] CameraFrustumPlanes ExtractCameraFrustumPlanes(const DirectX::XMFLOAT4X4& M) noexcept
    {
        CameraFrustumPlanes cp{};
        cp.planes[0] = { M._14 + M._11, M._24 + M._21, M._34 + M._31, M._44 + M._41 };
        cp.planes[1] = { M._14 - M._11, M._24 - M._21, M._34 - M._31, M._44 - M._41 };
        cp.planes[2] = { M._14 + M._12, M._24 + M._22, M._34 + M._32, M._44 + M._42 };
        cp.planes[3] = { M._14 - M._12, M._24 - M._22, M._34 - M._32, M._44 - M._42 };
        cp.planes[4] = { M._13,         M._23,         M._33,         M._43 };
        cp.planes[5] = { M._14 - M._13, M._24 - M._23, M._34 - M._33, M._44 - M._43 };

        for (auto& p : cp.planes)
        {
            const float len = std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z);
            if (len > 0.00001f)
            {
                const float invLen = 1.0f / len;
                p.x *= invLen; p.y *= invLen; p.z *= invLen; p.w *= invLen;
            }
        }
        return cp;
    }

    [[nodiscard]] bool IsSphereInCascade(const CascadeFrustumPlanes& cp, const DirectX::XMFLOAT3& center, float radius) noexcept
    {
        for (const auto& p : cp.planes)
        {
            if ((p.x * center.x + p.y * center.y + p.z * center.z + p.w) < -radius)
            {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] bool IsSphereInFrustum(const CameraFrustumPlanes& cp, const DirectX::XMFLOAT3& center, float radius) noexcept
    {
        for (const auto& p : cp.planes)
        {
            if ((p.x * center.x + p.y * center.y + p.z * center.z + p.w) < -radius)
            {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] BoundingSphere CalculateMeshBoundingSphere(
        const Model::Mesh& mesh,
        bool useManualMatrix,
        const DirectX::XMFLOAT4X4& manualMatrix) noexcept
    {
        DirectX::XMMATRIX mWorld{};
        if (useManualMatrix)
        {
            DirectX::XMMATRIX nodeGlobalMat = DirectX::XMLoadFloat4x4(&mesh.node->globalTransform);
            DirectX::XMMATRIX manualWorldMat = DirectX::XMLoadFloat4x4(&manualMatrix);
            mWorld = nodeGlobalMat * manualWorldMat;
        }
        else
        {
            mWorld = DirectX::XMLoadFloat4x4(&mesh.node->worldTransform);
        }

        const DirectX::XMVECTOR vCenter = DirectX::XMLoadFloat3(&mesh.boundsCenter);
        DirectX::XMFLOAT3 worldCenter{};
        DirectX::XMStoreFloat3(&worldCenter, DirectX::XMVector3TransformCoord(vCenter, mWorld));

        const float scaleX = DirectX::XMVectorGetX(DirectX::XMVector3Length(mWorld.r[0]));
        const float scaleY = DirectX::XMVectorGetX(DirectX::XMVector3Length(mWorld.r[1]));
        const float scaleZ = DirectX::XMVectorGetX(DirectX::XMVector3Length(mWorld.r[2]));
        const float maxScale = (std::max)({ scaleX, scaleY, scaleZ });

        const float worldRadius = mesh.boundsRadius * (maxScale > 0.0001f ? maxScale : 1.0f);

        return { worldCenter, worldRadius };
    }

    // Hashes only the properties consumed by the active main pass shader
    [[nodiscard]] uint16_t HashMaterial(const Material* mat) noexcept
    {
        if (!mat) return 0;

        // 16-byte aligned payload capturing surface parameters
        struct alignas(16) MaterialKeyData
        {
            void* baseMap{ nullptr };
            void* normalMap{ nullptr };
            void* emissiveMap{ nullptr };
            void* occlusionMap{ nullptr };
            void* metalnessRoughnessMap{ nullptr };
            DirectX::XMFLOAT4 baseColor{ 1.0f, 1.0f, 1.0f, 1.0f };
            DirectX::XMFLOAT3 emissiveColor{ 0.0f, 0.0f, 0.0f };
            float roughness{ 0.5f };
            float metalness{ 0.0f };
            float occlusionStrength{ 1.0f };
            float alphaCutoff{ 0.5f };
            int alphaMode{ 0 };
        };

        // Value-initialization guarantees all internal padding bytes are zeroed
        MaterialKeyData data{};

        // Common to all forward surface shaders
        data.baseMap = mat->baseMap.Get();
        data.baseColor = mat->baseColor;
        data.alphaCutoff = mat->alphaCutoff;
        data.alphaMode = static_cast<int>(mat->alphaMode);

        // Scope hashing strictly to properties evaluated by the active shader permutation
        switch (static_cast<ShaderId>(mat->shaderId))
        {
        case ShaderId::Basic:
            // Basic shader ignores lighting, normal mapping, PBR maps, and outlines
            break;

        case ShaderId::Lambert:
            data.emissiveColor = mat->emissiveColor;
            break;

        case ShaderId::Phong:
            data.normalMap = mat->normalMap.Get();
            data.emissiveColor = mat->emissiveColor;
            data.roughness = mat->roughness;
            break;

        case ShaderId::Pbr:
            data.normalMap = mat->normalMap.Get();
            data.emissiveMap = mat->emissiveMap.Get();
            data.occlusionMap = mat->occlusionMap.Get();
            data.metalnessRoughnessMap = mat->metalnessRoughnessMap.Get();
            data.emissiveColor = mat->emissiveColor;
            data.roughness = mat->roughness;
            data.metalness = mat->metalness;
            data.occlusionStrength = mat->occlusionStrength;
            break;

        case ShaderId::Toon:
            // Surface pass only evaluates diffuse, emissive, and stepped specular
            data.emissiveColor = mat->emissiveColor;
            data.roughness = mat->roughness;
            break;

        default:
            break;
        }

        const std::uint64_t hash{ Core::BufferHash(&data, sizeof(MaterialKeyData)) };
        return static_cast<uint16_t>(hash ^ (hash >> 16) ^ (hash >> 32) ^ (hash >> 48));
    }

    // Hashes only the parameters consumed by OutlineShader
    [[nodiscard]] uint16_t HashOutlineMaterial(const Material* mat) noexcept
    {
        if (!mat) return 0;

        struct alignas(16) OutlineKeyData
        {
            void* baseMap{ nullptr };
            DirectX::XMFLOAT4 outlineColor{ 0.0f, 0.0f, 0.0f, 1.0f };
            float outlineWidth{ 0.0f };
            float outlineFadeStart{ 0.0f };
            float outlineFadeEnd{ 0.0f };
            float alphaCutoff{ 0.5f };
            int alphaMode{ 0 };
            float padding[3]{ 0.0f, 0.0f, 0.0f };
        };

        OutlineKeyData data{};
        data.baseMap = mat->baseMap.Get();
        data.outlineColor = mat->outlineColor;
        data.outlineWidth = mat->outlineWidth;
        data.outlineFadeStart = mat->outlineFadeStart;
        data.outlineFadeEnd = mat->outlineFadeEnd;
        data.alphaCutoff = mat->alphaCutoff;
        data.alphaMode = static_cast<int>(mat->alphaMode);

        const std::uint64_t hash{ Core::BufferHash(&data, sizeof(OutlineKeyData)) };
        return static_cast<uint16_t>(hash ^ (hash >> 16) ^ (hash >> 32) ^ (hash >> 48));
    }
}

ModelRenderer::ModelRenderer(ID3D11Device* device)
{
    GpuResourceUtils::CreateConstantBuffer(device, sizeof(CbScene), sceneConstantBuffer.GetAddressOf());

    D3D11_BUFFER_DESC idesc{};
    idesc.ByteWidth = sizeof(InstanceData) * static_cast<UINT>(m_instanceCapacity);
    idesc.Usage = D3D11_USAGE_DYNAMIC;
    idesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    idesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    device->CreateBuffer(&idesc, nullptr, m_instanceBuffer.GetAddressOf());

    drawInfos.reserve(2000);
    m_skeletonPool.reserve(16);

    shaders[static_cast<int>(ShaderId::Basic)] = std::make_unique<BasicShader>(device);
    shaders[static_cast<int>(ShaderId::Lambert)] = std::make_unique<LambertShader>(device);
    shaders[static_cast<int>(ShaderId::Phong)] = std::make_unique<PhongShader>(device);
    shaders[static_cast<int>(ShaderId::Pbr)] = std::make_unique<PbrShader>(device);
    shaders[static_cast<int>(ShaderId::Toon)] = std::make_unique<ToonShader>(device);

    m_outlineShader = std::make_unique<OutlineShader>(device);
    m_shadowCasterShader = std::make_unique<ShadowCasterShader>(device);
    m_velocityShader = std::make_unique<VelocityShader>(device);
}

std::size_t ModelRenderer::AcquireSkeletonSlot(ID3D11Device* device)
{
    const std::size_t index{ m_skeletonPoolUsed++ };
    if (index >= m_skeletonPool.size())
    {
        // Grows only the first time this many simultaneous meshes-with-transforms are drawn
        // in one frame. Every subsequent frame at or below that count allocates nothing.
        SkeletonSlot slot{};
        GpuResourceUtils::CreateConstantBuffer(device, sizeof(CbSkeleton), slot.currentBuffer.GetAddressOf());
        GpuResourceUtils::CreateConstantBuffer(device, sizeof(CbSkeleton), slot.previousBuffer.GetAddressOf());
        m_skeletonPool.push_back(std::move(slot));
    }
    return index;
}

void ModelRenderer::ComputeAndUploadSkeleton(
    ID3D11DeviceContext* dc, std::size_t slotIndex, const Model::Mesh& mesh, bool useManual,
    bool needsPrevious,
    const DirectX::XMFLOAT4X4& worldMatrix, const DirectX::XMFLOAT4X4& previousWorldMatrix,
    const std::vector<DirectX::XMFLOAT4X4>* currentNodeGlobals,
    const std::vector<DirectX::XMFLOAT4X4>* previousNodeGlobals)
{
    // Bypass buffer writes entirely if this slot is the shared global identity slot
    if (slotIndex == m_identitySkeletonSlot) return;

    SkeletonSlot& slot{ m_skeletonPool[slotIndex] };

    CbSkeleton cbCurrent{};
    CbSkeleton cbPrevious{}; // only populated and uploaded when needsPrevious is true

    const DirectX::XMMATRIX manualWorldMat{ DirectX::XMLoadFloat4x4(&worldMatrix) };
    const DirectX::XMMATRIX previousManualWorldMat{ DirectX::XMLoadFloat4x4(&previousWorldMatrix) };

    if (!mesh.bones.empty())
    {
        for (std::size_t i{ 0 }; i < mesh.bones.size(); ++i)
        {
            const Model::Bone& bone{ mesh.bones[i] };

            DirectX::XMMATRIX nodeGlobalMat;
            if (currentNodeGlobals && bone.nodeIndex >= 0 && static_cast<std::size_t>(bone.nodeIndex) < currentNodeGlobals->size())
            {
                nodeGlobalMat = DirectX::XMLoadFloat4x4(&(*currentNodeGlobals)[bone.nodeIndex]);
            }
            else
            {
                nodeGlobalMat = DirectX::XMLoadFloat4x4(&bone.node->globalTransform);
            }

            const DirectX::XMMATRIX offsetTransform{ DirectX::XMLoadFloat4x4(&bone.offsetTransform) };
            const DirectX::XMMATRIX worldTransform{ useManual
                ? (nodeGlobalMat * manualWorldMat)
                : DirectX::XMLoadFloat4x4(&bone.node->worldTransform) };
            DirectX::XMStoreFloat4x4(&cbCurrent.boneTransforms[i], offsetTransform * worldTransform);

            if (needsPrevious)
            {
                DirectX::XMMATRIX prevNodeGlobalMat;
                if (previousNodeGlobals && bone.nodeIndex >= 0 && static_cast<std::size_t>(bone.nodeIndex) < previousNodeGlobals->size())
                {
                    prevNodeGlobalMat = DirectX::XMLoadFloat4x4(&(*previousNodeGlobals)[bone.nodeIndex]);
                }
                else
                {
                    prevNodeGlobalMat = nodeGlobalMat;
                }

                const DirectX::XMMATRIX previousWorldTransform{ useManual
                    ? (prevNodeGlobalMat * previousManualWorldMat)
                    : DirectX::XMLoadFloat4x4(&bone.node->worldTransform) };
                DirectX::XMStoreFloat4x4(&cbPrevious.boneTransforms[i], offsetTransform * previousWorldTransform);
            }
        }
    }
    else
    {
        if (useManual)
        {
            // Unskinned meshes must track the animated node
            DirectX::XMMATRIX nodeGlobalMat;

            // If the AnimationComponent provided evaluated globals, use them
            if (currentNodeGlobals && mesh.nodeIndex >= 0 && static_cast<std::size_t>(mesh.nodeIndex) < currentNodeGlobals->size())
            {
                nodeGlobalMat = DirectX::XMLoadFloat4x4(&(*currentNodeGlobals)[mesh.nodeIndex]);
            }
            else
            {
                // Fallback to static bind pose if no AnimationComponent exists
                nodeGlobalMat = DirectX::XMLoadFloat4x4(&mesh.node->globalTransform);
            }

            DirectX::XMStoreFloat4x4(&cbCurrent.boneTransforms[0], nodeGlobalMat * manualWorldMat);

            if (needsPrevious)
            {
                DirectX::XMMATRIX prevNodeGlobalMat;
                if (previousNodeGlobals && mesh.nodeIndex >= 0 && static_cast<std::size_t>(mesh.nodeIndex) < previousNodeGlobals->size())
                {
                    prevNodeGlobalMat = DirectX::XMLoadFloat4x4(&(*previousNodeGlobals)[mesh.nodeIndex]);
                }
                else
                {
                    prevNodeGlobalMat = nodeGlobalMat;
                }

                DirectX::XMStoreFloat4x4(&cbPrevious.boneTransforms[0], prevNodeGlobalMat * previousManualWorldMat);
            }
        }
        else
        {
            cbCurrent.boneTransforms[0] = mesh.node->worldTransform;
            if (needsPrevious)
            {
                cbPrevious.boneTransforms[0] = mesh.node->worldTransform;
            }
        }
    }

    dc->UpdateSubresource(slot.currentBuffer.Get(), 0, nullptr, &cbCurrent, 0, 0);
    if (needsPrevious)
    {
        dc->UpdateSubresource(slot.previousBuffer.Get(), 0, nullptr, &cbPrevious, 0, 0);
    }
}

void ModelRenderer::BindSkeletonSlot(ID3D11DeviceContext* dc, std::size_t slotIndex) const noexcept
{
    const SkeletonSlot& slot{ m_skeletonPool[slotIndex] };
    ID3D11Buffer* const currentCb{ slot.currentBuffer.Get() };
    ID3D11Buffer* const previousCb{ slot.previousBuffer.Get() };
    dc->VSSetConstantBuffers(6, 1, &currentCb);  
    dc->VSSetConstantBuffers(9, 1, &previousCb); 
}

void ModelRenderer::Draw(std::shared_ptr<Model> model, const DirectX::XMFLOAT4& color, bool castShadows, const std::vector<Material>* materials)
{
    DrawInfo& drawInfo{ drawInfos.emplace_back() };
    drawInfo.model = std::move(model);
    drawInfo.color = color;
    drawInfo.useManualMatrix = false;
    drawInfo.castShadows = castShadows;
    drawInfo.materials = materials;
}

void ModelRenderer::Draw(std::shared_ptr<Model> model, DirectX::XMFLOAT4 color, const DirectX::XMFLOAT4X4& worldMatrix, bool castShadows, const std::vector<Material>* materials)
{
    DrawInfo& drawInfo{ drawInfos.emplace_back() };
    drawInfo.model = std::move(model);
    drawInfo.color = color;
    drawInfo.useManualMatrix = true;
    drawInfo.worldMatrix = worldMatrix;
    drawInfo.previousWorldMatrix = worldMatrix;
    drawInfo.castShadows = castShadows;
    drawInfo.materials = materials;
}

void ModelRenderer::Draw(std::shared_ptr<Model> model, DirectX::XMFLOAT4 color,
    const DirectX::XMFLOAT4X4& worldMatrix, const DirectX::XMFLOAT4X4& previousWorldMatrix,
    const std::vector<DirectX::XMFLOAT4X4>* currentNodeGlobals,
    const std::vector<DirectX::XMFLOAT4X4>* previousNodeGlobals, bool castShadows,
    const std::vector<Material>* materials)
{
    DrawInfo info{ std::move(model), currentNodeGlobals, previousNodeGlobals, color, true, worldMatrix, previousWorldMatrix, castShadows };
    info.hasVelocity = true;
    info.materials = materials;
    drawInfos.push_back(std::move(info));
}

void ModelRenderer::DrawMeshVelocity(ID3D11DeviceContext* dc, const Model::Mesh& mesh, std::size_t skeletonSlot)
{
    UINT stride{ sizeof(Model::Vertex) };
    UINT offset{ 0 };
    dc->IASetVertexBuffers(0, 1, mesh.vertexBuffer.GetAddressOf(), &stride, &offset);
    dc->IASetIndexBuffer(mesh.indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
    dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    BindSkeletonSlot(dc, skeletonSlot);

    dc->DrawIndexed(static_cast<UINT>(mesh.indices.size()), 0, 0);

    PROFILE_DRAW_CALL();
    PROFILE_TRIANGLES(mesh.indices.size() / 3);
}

void ModelRenderer::Render(const RenderContext& rc)
{
    if (drawInfos.empty()) return;

    ID3D11DeviceContext* const dc{ rc.deviceContext };
    Microsoft::WRL::ComPtr<ID3D11Device> device{};
    dc->GetDevice(device.GetAddressOf());

    m_skeletonPoolUsed = 0;
    m_frameArena.Reset();
    dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    // Create the global Identity Skeleton Slot once per frame for ALL static meshes to share
    m_identitySkeletonSlot = AcquireSkeletonSlot(device.Get());
    {
        SkeletonSlot& idSlot = m_skeletonPool[m_identitySkeletonSlot];
        CbSkeleton cbId{};
        const DirectX::XMMATRIX identity = DirectX::XMMatrixIdentity();
        for (int i = 0; i < 256; ++i)
        {
            DirectX::XMStoreFloat4x4(&cbId.boneTransforms[i], identity);
        }
        dc->UpdateSubresource(idSlot.currentBuffer.Get(), 0, nullptr, &cbId, 0, 0);
        dc->UpdateSubresource(idSlot.previousBuffer.Get(), 0, nullptr, &cbId, 0, 0);
    }

    for (DrawInfo& drawInfo : drawInfos)
    {
        if (!drawInfo.model) continue;

        const auto& meshes{ drawInfo.model->GetMeshes() };
        drawInfo.skeletonSlots.resize(meshes.size());

        for (std::size_t meshIdx{ 0 }; meshIdx < meshes.size(); ++meshIdx)
        {
            if (meshes[meshIdx].bones.empty())
            {
                drawInfo.skeletonSlots[meshIdx] = m_identitySkeletonSlot;
            }
            else
            {
                const std::size_t slot{ AcquireSkeletonSlot(device.Get()) };
                ComputeAndUploadSkeleton(dc, slot, meshes[meshIdx], drawInfo.useManualMatrix,
                    drawInfo.hasVelocity, drawInfo.worldMatrix, drawInfo.previousWorldMatrix,
                    drawInfo.currentNodeGlobals, drawInfo.previousNodeGlobals);
                drawInfo.skeletonSlots[meshIdx] = slot;
            }
        }
    }

    std::size_t instanceOffset = 0;

    // THE PASS DISPATCHER: Single contiguous upload per pass, zero draw-loop mapping stalls
    auto executeBatches = [&](const DrawPacket* packets, std::size_t count, Shader* forcedShader = nullptr)
        {
            if (count == 0) return;

            // Dynamic Buffer Allocation & Wrap Guard
            if (instanceOffset + count > m_instanceCapacity)
            {
                if (count > m_instanceCapacity)
                {
                    m_instanceCapacity = (std::max)(m_instanceCapacity * 2, count + 1024);
                    D3D11_BUFFER_DESC idesc{};
                    idesc.ByteWidth = sizeof(InstanceData) * static_cast<UINT>(m_instanceCapacity);
                    idesc.Usage = D3D11_USAGE_DYNAMIC;
                    idesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
                    idesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
                    m_instanceBuffer.Reset();
                    device->CreateBuffer(&idesc, nullptr, m_instanceBuffer.GetAddressOf());
                }
                instanceOffset = 0;
            }

            // Stream all instance transforms for the entire pass in a single map operation
            const D3D11_MAP mapType{ (instanceOffset == 0) ? D3D11_MAP_WRITE_DISCARD : D3D11_MAP_WRITE_NO_OVERWRITE };
            D3D11_MAPPED_SUBRESOURCE mapped{};
            dc->Map(m_instanceBuffer.Get(), 0, mapType, 0, &mapped);
            InstanceData* const instData{ static_cast<InstanceData*>(mapped.pData) + instanceOffset };

            const DirectX::XMMATRIX identity{ DirectX::XMMatrixIdentity() };
            for (std::size_t i{ 0 }; i < count; ++i)
            {
                const DrawPacket& p{ packets[i] };
                if (p.mesh->bones.empty())
                {
                    DirectX::XMStoreFloat4x4(&instData[i].world, DirectX::XMMatrixTranspose(DirectX::XMLoadFloat4x4(&p.worldMatrix)));
                    DirectX::XMStoreFloat4x4(&instData[i].prevWorld, DirectX::XMMatrixTranspose(DirectX::XMLoadFloat4x4(&p.prevWorldMatrix)));
                }
                else
                {
                    DirectX::XMStoreFloat4x4(&instData[i].world, identity);
                    DirectX::XMStoreFloat4x4(&instData[i].prevWorld, identity);
                }
                instData[i].color = p.color;
            }
            dc->Unmap(m_instanceBuffer.Get(), 0);

            // Bind Slot 1 Instance Buffer once for the entire pass
            ID3D11Buffer* const instBuf{ m_instanceBuffer.Get() };
            const UINT instStride{ sizeof(InstanceData) };
            const UINT zeroOffset{ 0 };
            dc->IASetVertexBuffers(1, 1, &instBuf, &instStride, &zeroOffset);

            // Hot Dispatch Loop: Zero Map/Unmap overhead, pure state caching & dispatches
            Shader* activeShader{ forcedShader };
            ShaderId activeShaderId{ ShaderId::EnumCount };
            const Material* currentMaterial{ nullptr };
            ID3D11Buffer* currentVB{ nullptr };
            ID3D11Buffer* currentIB{ nullptr };
            std::size_t currentSlot{ kInvalidSkeletonSlot };

            const bool isDepthOrVelocityPass{ (forcedShader == m_shadowCasterShader.get() || forcedShader == m_velocityShader.get()) };

            for (std::size_t pIdx{ 0 }; pIdx < count; )
            {
                const DrawPacket& firstPacket{ packets[pIdx] };
                std::size_t batchCount{ 1 };
                const bool isStatic{ firstPacket.mesh->bones.empty() };

                if (isStatic)
                {
                    if (isDepthOrVelocityPass)
                    {
                        // Depth/Velocity: Dissimilar materials on identical geometry coalesced into single draw
                        while (pIdx + batchCount < count &&
                            packets[pIdx + batchCount].mesh == firstPacket.mesh &&
                            packets[pIdx + batchCount].mesh->bones.empty())
                        {
                            ++batchCount;
                        }
                    }
                    else
                    {
                        // Color/Outline: Coalesce identical geometry sharing the exact same 48-bit PSO & Material state key
                        const bool isTransparent{ (((firstPacket.key >> 60) & 0xFULL) == 6) };
                        const uint64_t firstStateKey{ firstPacket.key & 0xFFFFFFFFFFFF0000ULL };

                        while (pIdx + batchCount < count &&
                            packets[pIdx + batchCount].mesh == firstPacket.mesh &&
                            (!isTransparent ? ((packets[pIdx + batchCount].key & 0xFFFFFFFFFFFF0000ULL) == firstStateKey)
                                : (packets[pIdx + batchCount].key == firstPacket.key)) &&
                            packets[pIdx + batchCount].mesh->bones.empty())
                        {
                            ++batchCount;
                        }
                    }
                }

                if (!forcedShader)
                {
                    const ShaderId packetShaderId{ static_cast<ShaderId>((firstPacket.key >> 48) & 0xFF) };
                    if (packetShaderId != activeShaderId)
                    {
                        if (activeShader) activeShader->End(rc);
                        activeShaderId = packetShaderId;
                        activeShader = shaders[static_cast<int>(packetShaderId)].get();
                        if (activeShader) activeShader->Begin(rc);
                        currentMaterial = nullptr;
                        currentVB = nullptr;
                        currentIB = nullptr;
                        currentSlot = kInvalidSkeletonSlot;
                    }
                }

                if (firstPacket.mesh->vertexBuffer.Get() != currentVB)
                {
                    ID3D11Buffer* const vb{ firstPacket.mesh->vertexBuffer.Get() };
                    const UINT stride{ sizeof(Model::Vertex) };
                    dc->IASetVertexBuffers(0, 1, &vb, &stride, &zeroOffset);
                    currentVB = vb;
                }

                if (firstPacket.mesh->indexBuffer.Get() != currentIB)
                {
                    dc->IASetIndexBuffer(firstPacket.mesh->indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
                    currentIB = firstPacket.mesh->indexBuffer.Get();
                }

                if (firstPacket.skeletonSlot != currentSlot)
                {
                    BindSkeletonSlot(dc, firstPacket.skeletonSlot);
                    currentSlot = firstPacket.skeletonSlot;
                }

                if (!isDepthOrVelocityPass && firstPacket.material != currentMaterial)
                {
                    // Transiently inject component material into mesh for uniform upload, then restore
                    Model::Mesh* const nonConstMesh{ const_cast<Model::Mesh*>(firstPacket.mesh) };
                    Material* const originalMaterial{ nonConstMesh->material };
                    nonConstMesh->material = const_cast<Material*>(firstPacket.material);

                    if (activeShader) activeShader->Update(rc, *nonConstMesh);

                    nonConstMesh->material = originalMaterial;
                    currentMaterial = firstPacket.material;
                }

                dc->DrawIndexedInstanced(
                    static_cast<UINT>(firstPacket.mesh->indices.size()),
                    static_cast<UINT>(batchCount),
                    0,
                    0,
                    static_cast<UINT>(instanceOffset + pIdx)
                );

                pIdx += batchCount;
                PROFILE_DRAW_CALL();
                PROFILE_TRIANGLES(firstPacket.mesh->indices.size() / 3 * batchCount);
            }

            if (activeShader && !forcedShader)
            {
                activeShader->End(rc);
            }

            instanceOffset += count;
        };

    // Fast bitwise pointer hash eliminating stack frame buffer hashing
    auto hashPointer = [](const void* ptr) noexcept -> uint16_t
        {
            const uintptr_t val{ reinterpret_cast<uintptr_t>(ptr) };
            return static_cast<uint16_t>((val >> 4) ^ (val >> 20));
        };

    if (rc.lightManager)
    {
        const_cast<LightManager*>(rc.lightManager)->Update();
        if (rc.lightManager->GetDirectionalLight().castShadows)
        {
            rc.lightManager->UpdateCascades(*rc.camera);
        }
    }

        std::size_t totalSceneMeshCount{ 0 };
        for (const auto& drawInfo : drawInfos) if (drawInfo.model) totalSceneMeshCount += drawInfo.model->GetMeshes().size();

        // Stream Extraction Arrays
        DrawPacket* const opaquePackets{ m_frameArena.AllocateArray<DrawPacket>(totalSceneMeshCount) };
        DrawPacket* const transparentPackets{ m_frameArena.AllocateArray<DrawPacket>(totalSceneMeshCount) };
        DrawPacket* const velocityPackets{ m_frameArena.AllocateArray<DrawPacket>(totalSceneMeshCount) };
        DrawPacket* const outlinePackets{ m_frameArena.AllocateArray<DrawPacket>(totalSceneMeshCount) };
        DrawPacket* const cascadePackets[4]{
            m_frameArena.AllocateArray<DrawPacket>(totalSceneMeshCount),
            m_frameArena.AllocateArray<DrawPacket>(totalSceneMeshCount),
            m_frameArena.AllocateArray<DrawPacket>(totalSceneMeshCount),
            m_frameArena.AllocateArray<DrawPacket>(totalSceneMeshCount)
        };

        std::size_t opaqueCount{ 0 }, transparentCount{ 0 }, velocityCount{ 0 }, outlineCount{ 0 };
        std::size_t cascadeCount[4]{ 0, 0, 0, 0 };

        DirectX::XMFLOAT4X4 matViewProj{};
        {
            const DirectX::XMMATRIX V = DirectX::XMLoadFloat4x4(&rc.camera->GetView());
            const DirectX::XMMATRIX P = DirectX::XMLoadFloat4x4(&rc.camera->GetProjection());
            DirectX::XMStoreFloat4x4(&matViewProj, V * P);
        }
        const CameraFrustumPlanes cameraPlanes = ExtractCameraFrustumPlanes(matViewProj);
        const DirectX::XMVECTOR CameraPosition = DirectX::XMLoadFloat3(&rc.camera->GetPosition());
        const DirectX::XMVECTOR CameraFront = DirectX::XMLoadFloat3(&rc.camera->GetFront());

        // Single extraction pass: Route objects cleanly to all sub-pipelines
        for (const DrawInfo& drawInfo : drawInfos)
        {
            if (!drawInfo.model) continue;

            for (std::size_t meshIdx{ 0 }; meshIdx < drawInfo.model->GetMeshes().size(); ++meshIdx)
            {
                const Model::Mesh& mesh{ drawInfo.model->GetMeshes()[meshIdx] };
                // Remap via mesh.materialIndex so all submeshes find their designated material slot
                const Material* const mat{ (drawInfo.materials && mesh.materialIndex >= 0 &&
                    static_cast<std::size_t>(mesh.materialIndex) < drawInfo.materials->size())
                    ? &(*drawInfo.materials)[mesh.materialIndex]
                    : mesh.material };

                DirectX::XMMATRIX mWorld, mPrevWorld;
                if (drawInfo.useManualMatrix)
                {
                    DirectX::XMMATRIX nodeGlobalMat = DirectX::XMLoadFloat4x4(&mesh.node->globalTransform);
                    mWorld = nodeGlobalMat * DirectX::XMLoadFloat4x4(&drawInfo.worldMatrix);
                    mPrevWorld = nodeGlobalMat * DirectX::XMLoadFloat4x4(&drawInfo.previousWorldMatrix);
                }
                else
                {
                    mWorld = DirectX::XMLoadFloat4x4(&mesh.node->worldTransform);
                    mPrevWorld = mWorld;
                }

                DrawPacket packet{};
                packet.mesh = &mesh;
                packet.material = mat;
                packet.skeletonSlot = drawInfo.skeletonSlots[meshIdx];
                packet.color = drawInfo.color;
                DirectX::XMStoreFloat4x4(&packet.worldMatrix, mWorld);
                DirectX::XMStoreFloat4x4(&packet.prevWorldMatrix, mPrevWorld);

                const BoundingSphere sphere{ CalculateMeshBoundingSphere(mesh, drawInfo.useManualMatrix, drawInfo.worldMatrix) };

                // Shadows: Group strictly by mesh identity to maximize multi-material depth batching
                if (drawInfo.castShadows && mat->alphaMode != AlphaMode::Blend)
                {
                    for (int i{ 0 }; i < SHADOW_CASCADE_COUNT; ++i)
                    {
                        if (rc.lightManager && rc.lightManager->GetDirectionalLight().castShadows)
                        {
                            const CascadeFrustumPlanes cPlanes{ ExtractCascadePlanes(rc.lightManager->GetCascadeMatrices()[i]) };
                            if (IsSphereInCascade(cPlanes, sphere.center, sphere.radius))
                            {
                                DrawPacket shadowPacket{ packet };
                                shadowPacket.key = reinterpret_cast<uint64_t>(&mesh);
                                cascadePackets[i][cascadeCount[i]++] = shadowPacket;
                            }
                        }
                    }
                }

                // Main Camera Frustum Check
                if (!IsSphereInFrustum(cameraPlanes, sphere.center, sphere.radius)) continue;

                const float distance = DirectX::XMVectorGetX(DirectX::XMVector3Dot(CameraFront, DirectX::XMVectorSubtract(mWorld.r[3], CameraPosition)));
                const float clampedDist = std::clamp(distance, 0.0f, 1000.0f);
                const uint16_t quantizedDepth = static_cast<uint16_t>((clampedDist / 1000.0f) * 65535.0f);

                uint64_t passId{ 4 };
                uint64_t translucencyLayer{ 0 };
                uint16_t depthKey{ quantizedDepth };

                if (mat->alphaMode == AlphaMode::Blend || (mat->baseColor.w > 0.01f && mat->baseColor.w < 0.99f))
                {
                    passId = 6;
                    translucencyLayer = 1;
                    depthKey = static_cast<uint16_t>(65535 - quantizedDepth); // Back-to-front
                }
                else if (mat->alphaMode == AlphaMode::Mask)
                {
                    passId = 5;
                }

                const uint64_t shaderId{ static_cast<uint64_t>(mat->shaderId) };
                packet.key = BuildDrawKey(passId, translucencyLayer, shaderId, HashMaterial(mat), hashPointer(mesh.vertexBuffer.Get()), depthKey);

                if (passId == 6) transparentPackets[transparentCount++] = packet;
                else opaquePackets[opaqueCount++] = packet;

                if (drawInfo.hasVelocity && passId < 6)
                {
                    DrawPacket velocityPacket{ packet };
                    velocityPacket.key = reinterpret_cast<uint64_t>(&mesh);
                    velocityPackets[velocityCount++] = velocityPacket;
                }

                if (shaderId == static_cast<uint64_t>(ShaderId::Toon) && mat->enableOutline && mat->outlineWidth > 0.0f)
                {
                    const float fadeEndSq{ mat->outlineFadeEnd * mat->outlineFadeEnd };
                    const float distSq{ DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(DirectX::XMVectorSubtract(mWorld.r[3], CameraPosition))) };
                    if (distSq <= (fadeEndSq + 25.0f))
                    {
                        DrawPacket outlinePacket{ packet };
                        // Build dedicated outline draw key to allow outline instances to coalesce independently
                        outlinePacket.key = BuildDrawKey(4, 0, static_cast<uint64_t>(ShaderId::Toon), HashOutlineMaterial(mat), hashPointer(mesh.vertexBuffer.Get()), depthKey);
                        outlinePackets[outlineCount++] = outlinePacket;
                    }
                }
            }
        }
        drawInfos.clear();

        // Independent radix/stream sorting
        auto packetSort = [](const DrawPacket& a, const DrawPacket& b) noexcept
            {
                if (a.key != b.key) return a.key < b.key;
                return a.mesh < b.mesh; // Break key ties deterministically by geometry pointer
            };
        std::sort(opaquePackets, opaquePackets + opaqueCount, packetSort);
        std::sort(transparentPackets, transparentPackets + transparentCount, packetSort);
        std::sort(outlinePackets, outlinePackets + outlineCount, packetSort);
        std::sort(velocityPackets, velocityPackets + velocityCount, packetSort);
        for (int i = 0; i < 4; ++i) std::sort(cascadePackets[i], cascadePackets[i] + cascadeCount[i], packetSort);

        // RENDER PASSES 
        if (rc.lightManager && rc.lightManager->GetDirectionalLight().castShadows)
        {
            ID3D11RenderTargetView* originalRTV{ nullptr };
            ID3D11DepthStencilView* originalDSV{ nullptr };
            dc->OMGetRenderTargets(1, &originalRTV, &originalDSV);

            UINT numViewports{ 1 };
            D3D11_VIEWPORT originalViewport{};
            dc->RSGetViewports(&numViewports, &originalViewport);

            D3D11_VIEWPORT shadowViewport{};
            shadowViewport.Width = static_cast<float>(SHADOW_MAP_SIZE);
            shadowViewport.Height = static_cast<float>(SHADOW_MAP_SIZE);
            shadowViewport.MaxDepth = 1.0f;

            dc->OMSetBlendState(rc.renderState->GetBlendState(BlendState::Opaque), nullptr, 0xFFFFFFFF);
            dc->OMSetDepthStencilState(rc.renderState->GetDepthStencilState(DepthState::TestAndWrite), 0);
            dc->RSSetState(rc.renderState->GetRasterizerState(RasterizerState::SolidCullBack));

            m_shadowCasterShader->Begin(rc);
            for (int i{ 0 }; i < SHADOW_CASCADE_COUNT; ++i)
            {
                ID3D11DepthStencilView* const cascadeDSV{ rc.lightManager->GetCascadeDSV(i) };
                dc->ClearDepthStencilView(cascadeDSV, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
                dc->OMSetRenderTargets(0, nullptr, cascadeDSV);
                dc->RSSetViewports(1, &shadowViewport);

                m_shadowCasterShader->SetCascadeMatrix(dc, rc.lightManager->GetCascadeMatrices()[i]);
                executeBatches(cascadePackets[i], cascadeCount[i], m_shadowCasterShader.get());
            }
            m_shadowCasterShader->End(rc);

            dc->OMSetRenderTargets(1, &originalRTV, originalDSV);
            dc->RSSetViewports(1, &originalViewport);
            if (originalRTV) originalRTV->Release();
            if (originalDSV) originalDSV->Release();
        }

        {
            static LightManager defaultLightManager;
            const LightManager* const lightManager{ rc.lightManager ? rc.lightManager : &defaultLightManager };

            CbScene cbScene{};
            cbScene.viewProjection = matViewProj;
            const DirectionalLight& dirLight{ lightManager->GetDirectionalLight() };
            cbScene.lightDirection = { dirLight.direction.x, dirLight.direction.y, dirLight.direction.z, 0.0f };
            cbScene.lightColor = { dirLight.color.x * dirLight.intensity, dirLight.color.y * dirLight.intensity, dirLight.color.z * dirLight.intensity, 1.0f };
            cbScene.cameraPosition = { DirectX::XMVectorGetX(CameraPosition), DirectX::XMVectorGetY(CameraPosition), DirectX::XMVectorGetZ(CameraPosition), 1.0f };
            cbScene.cameraDirection = { DirectX::XMVectorGetX(CameraFront), DirectX::XMVectorGetY(CameraFront), DirectX::XMVectorGetZ(CameraFront), 0.0f };
            cbScene.ambientSkyColor = lightManager->GetEffectiveSkyColor();
            cbScene.ambientGroundColor = lightManager->GetEffectiveGroundColor();
            cbScene.packedParams = { rc.psxEnabled ? 1.0f : 0.0f, (std::max)(1.0f, rc.psxResWidth), (std::max)(1.0f, rc.psxResHeight), 0.0f };
            cbScene.lightCounts = { lightManager->GetPointLightCount(), lightManager->GetSpotLightCount(), 0, 0 };

            for (int i{ 0 }; i < 8; ++i) cbScene.pointLights[i] = lightManager->GetPointLights()[i];
            for (int i{ 0 }; i < 8; ++i) cbScene.spotLights[i] = lightManager->GetSpotLights()[i];

            if (dirLight.castShadows)
            {
                for (int i = 0; i < 4; ++i) cbScene.cascadeMatrices[i] = lightManager->GetCascadeMatrices()[i];
                cbScene.cascadeSplits = { dirLight.splitDistances[1], dirLight.splitDistances[2], dirLight.splitDistances[3], dirLight.splitDistances[4] };
                cbScene.cascadeBias = { dirLight.shadowBias[0], dirLight.shadowBias[1], dirLight.shadowBias[2], dirLight.shadowBias[3] };
                cbScene.shadowSettings = { 1.0f, dirLight.shadowAttenuation, 0.0f, 0.0f };
            }
            else cbScene.shadowSettings = { 0.0f, 1.0f, 0.0f, 0.0f };

            dc->UpdateSubresource(sceneConstantBuffer.Get(), 0, 0, &cbScene, 0, 0);
        }

        ID3D11Buffer* const sceneCbs[] = { sceneConstantBuffer.Get() };
        dc->VSSetConstantBuffers(7, 1, sceneCbs);
        dc->PSSetConstantBuffers(7, 1, sceneCbs);

        ID3D11SamplerState* const samplers[] = { rc.renderState->GetSamplerState(SamplerState::AnisotropicWrap) };
        dc->PSSetSamplers(0, 1, samplers);

        ID3D11SamplerState* const shadowSampler = rc.renderState->GetSamplerState(SamplerState::ShadowMap);
        dc->PSSetSamplers(10, 1, &shadowSampler);

        if (rc.lightManager && rc.lightManager->GetDirectionalLight().castShadows)
        {
            ID3D11ShaderResourceView* const* cascadeSRVs = rc.lightManager->GetCascadeSRVs();
            dc->PSSetShaderResources(10, 4, cascadeSRVs);
        }

        dc->OMSetDepthStencilState(rc.renderState->GetDepthStencilState(DepthState::TestAndWrite), 0);
        dc->RSSetState(rc.renderState->GetRasterizerState(RasterizerState::SolidCullBack));
        dc->OMSetBlendState(rc.renderState->GetBlendState(BlendState::Opaque), nullptr, 0xFFFFFFFF);

        // Main Opaque
        executeBatches(opaquePackets, opaqueCount);

        // Toon Outlines
        if (outlineCount > 0)
        {
            dc->RSSetState(rc.renderState->GetRasterizerState(RasterizerState::SolidCullFront));
            m_outlineShader->Begin(rc);
            executeBatches(outlinePackets, outlineCount, m_outlineShader.get());
            m_outlineShader->End(rc);
            dc->RSSetState(rc.renderState->GetRasterizerState(RasterizerState::SolidCullBack));
        }

        // Velocity Pass
        if (rc.velocityRenderTargetView && velocityCount > 0)
        {
            ID3D11RenderTargetView* originalRTV{ nullptr };
            ID3D11DepthStencilView* originalDSV{ nullptr };
            dc->OMGetRenderTargets(1, &originalRTV, &originalDSV);

            ID3D11RenderTargetView* const velocityRTV{ rc.velocityRenderTargetView };
            dc->OMSetRenderTargets(1, &velocityRTV, originalDSV);

            constexpr float s_zeroVelocity[4]{ 0.0f, 0.0f, 0.0f, 0.0f };
            dc->ClearRenderTargetView(velocityRTV, s_zeroVelocity);

            dc->OMSetDepthStencilState(rc.renderState->GetDepthStencilState(DepthState::TestOnly), 0);
            dc->OMSetBlendState(rc.renderState->GetBlendState(BlendState::Opaque), nullptr, 0xFFFFFFFF);

            m_velocityShader->Begin(rc);
            DirectX::XMFLOAT4X4 currentViewProjection{};
            DirectX::XMStoreFloat4x4(&currentViewProjection, DirectX::XMLoadFloat4x4(&rc.camera->GetView()) * DirectX::XMLoadFloat4x4(&rc.camera->GetProjection()));
            m_velocityShader->SetViewProjections(dc, currentViewProjection, rc.camera->GetPreviousViewProjection());

            executeBatches(velocityPackets, velocityCount, m_velocityShader.get());
            m_velocityShader->End(rc);

            dc->OMSetRenderTargets(1, &originalRTV, originalDSV);
            if (originalRTV) originalRTV->Release();
            if (originalDSV) originalDSV->Release();
        }

        // Transparent Pass
        dc->OMSetBlendState(rc.renderState->GetBlendState(BlendState::Transparency), nullptr, 0xFFFFFFFF);
        dc->OMSetDepthStencilState(rc.renderState->GetDepthStencilState(DepthState::TestOnly), 0);
        executeBatches(transparentPackets, transparentCount);

        // Unbind cleanly
        ID3D11Buffer* const nullBuffers[]{ nullptr, nullptr };
        UINT nullStrides[]{ 0, 0 };
        UINT nullOffsets[]{ 0, 0 };
        dc->IASetVertexBuffers(0, 2, nullBuffers, nullStrides, nullOffsets);

        dc->VSSetConstantBuffers(7, 1, nullBuffers);
        dc->PSSetConstantBuffers(7, 1, nullBuffers);
        ID3D11SamplerState* const nullSamplers[]{ nullptr };
        dc->PSSetSamplers(0, 1, nullSamplers);
        ID3D11ShaderResourceView* const nullShadowSRVs[]{ nullptr, nullptr, nullptr, nullptr };
        dc->PSSetShaderResources(10, 4, nullShadowSRVs);
}