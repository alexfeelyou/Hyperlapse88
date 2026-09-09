#include "ProfilerManager.h"
#include "ModelRenderer.h"

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
}

ModelRenderer::ModelRenderer(ID3D11Device* device)
{
    GpuResourceUtils::CreateConstantBuffer(device, sizeof(CbScene), sceneConstantBuffer.GetAddressOf());
    GpuResourceUtils::CreateConstantBuffer(device, sizeof(CbObject), objectConstantBuffer.GetAddressOf());

    drawInfos.reserve(2000);
    transparencyDrawInfos.reserve(2000);
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
            const DirectX::XMMATRIX nodeGlobalMat{ DirectX::XMLoadFloat4x4(&mesh.node->globalTransform) };
            DirectX::XMStoreFloat4x4(&cbCurrent.boneTransforms[0], nodeGlobalMat * manualWorldMat);
            if (needsPrevious)
            {
                DirectX::XMStoreFloat4x4(&cbPrevious.boneTransforms[0], nodeGlobalMat * previousManualWorldMat);
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

void ModelRenderer::Draw(std::shared_ptr<Model> model, const DirectX::XMFLOAT4& color, bool castShadows)
{
    DrawInfo& drawInfo{ drawInfos.emplace_back() };
    drawInfo.model = std::move(model);
    drawInfo.color = color;
    drawInfo.useManualMatrix = false;
    drawInfo.castShadows = castShadows;
}

void ModelRenderer::Draw(std::shared_ptr<Model> model, DirectX::XMFLOAT4 color, const DirectX::XMFLOAT4X4& worldMatrix, bool castShadows)
{
    DrawInfo& drawInfo{ drawInfos.emplace_back() };
    drawInfo.model = std::move(model);
    drawInfo.color = color;
    drawInfo.useManualMatrix = true;
    drawInfo.worldMatrix = worldMatrix;
    drawInfo.previousWorldMatrix = worldMatrix;
    drawInfo.castShadows = castShadows;
}

void ModelRenderer::Draw(std::shared_ptr<Model> model, DirectX::XMFLOAT4 color,
    const DirectX::XMFLOAT4X4& worldMatrix, const DirectX::XMFLOAT4X4& previousWorldMatrix,
    const std::vector<DirectX::XMFLOAT4X4>* currentNodeGlobals,
    const std::vector<DirectX::XMFLOAT4X4>* previousNodeGlobals, bool castShadows)
{
    DrawInfo info{ std::move(model), currentNodeGlobals, previousNodeGlobals, color, true, worldMatrix, previousWorldMatrix, castShadows };
    info.hasVelocity = true; 
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

    m_skeletonPoolUsed = 0; // reset the frame arena — buffers themselves are reused, not freed

    // PREPARE PASS: compute + upload every drawn mesh's skeleton exactly once. Shadow cascades,
    // opaque, outline, transparency, and velocity all just bind these buffers afterward.
    for (DrawInfo& drawInfo : drawInfos)
    {
        if (!drawInfo.model) continue;

        const auto& meshes{ drawInfo.model->GetMeshes() };
        drawInfo.skeletonSlots.resize(meshes.size());

        for (std::size_t meshIdx{ 0 }; meshIdx < meshes.size(); ++meshIdx)
        {
            const std::size_t slot{ AcquireSkeletonSlot(device.Get()) };
            ComputeAndUploadSkeleton(dc, slot, meshes[meshIdx], drawInfo.useManualMatrix,
                drawInfo.hasVelocity, // ADD THIS ARGUMENT
                drawInfo.worldMatrix, drawInfo.previousWorldMatrix,
                drawInfo.currentNodeGlobals, drawInfo.previousNodeGlobals);
            drawInfo.skeletonSlots[meshIdx] = slot;
        }
    }

    auto drawMesh = [&](const Model::Mesh& mesh, Shader* shader, std::size_t skeletonSlot)
        {
            UINT stride{ sizeof(Model::Vertex) };
            UINT offset{ 0 };
            dc->IASetVertexBuffers(0, 1, mesh.vertexBuffer.GetAddressOf(), &stride, &offset);
            dc->IASetIndexBuffer(mesh.indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
            dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

            BindSkeletonSlot(dc, skeletonSlot);

            shader->Update(rc, mesh);
            dc->DrawIndexed(static_cast<UINT>(mesh.indices.size()), 0, 0);

            PROFILE_DRAW_CALL();
            PROFILE_TRIANGLES(mesh.indices.size() / 3);
        };

    if (rc.lightManager && rc.lightManager->GetDirectionalLight().castShadows)
    {
        rc.lightManager->UpdateCascades(*rc.camera);

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

        for (int i = 0; i < SHADOW_CASCADE_COUNT; ++i)
        {
            ID3D11DepthStencilView* cascadeDSV = rc.lightManager->GetCascadeDSV(i);

            dc->ClearDepthStencilView(cascadeDSV, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
            dc->OMSetRenderTargets(0, nullptr, cascadeDSV);
            dc->RSSetViewports(1, &shadowViewport);

            const auto& cascadeMatrix = rc.lightManager->GetCascadeMatrices()[i];
            m_shadowCasterShader->SetCascadeMatrix(dc, cascadeMatrix);

            const CascadeFrustumPlanes cascadePlanes = ExtractCascadePlanes(cascadeMatrix);

            for (const DrawInfo& drawInfo : drawInfos)
            {
                if (!drawInfo.castShadows || !drawInfo.model) continue;

                const auto& meshes{ drawInfo.model->GetMeshes() };
                for (std::size_t meshIdx{ 0 }; meshIdx < meshes.size(); ++meshIdx)
                {
                    const Model::Mesh& mesh{ meshes[meshIdx] };
                    if (mesh.material->alphaMode == AlphaMode::Blend) continue;

                    const BoundingSphere sphere{ CalculateMeshBoundingSphere(mesh, drawInfo.useManualMatrix, drawInfo.worldMatrix) };
                    if (!IsSphereInCascade(cascadePlanes, sphere.center, sphere.radius)) continue;

                    drawMesh(mesh, m_shadowCasterShader.get(), drawInfo.skeletonSlots[meshIdx]);
                }
            }
        }

        m_shadowCasterShader->End(rc);

        dc->OMSetRenderTargets(1, &originalRTV, originalDSV);
        dc->RSSetViewports(1, &originalViewport);

        if (originalRTV) originalRTV->Release();
        if (originalDSV) originalDSV->Release();
    }

    if (rc.lightManager)
    {
        const_cast<LightManager*>(rc.lightManager)->Update();
    }

    {
        static LightManager defaultLightManager;
        const LightManager* const lightManager{ rc.lightManager ? rc.lightManager : &defaultLightManager };

        CbScene cbScene{};
        const DirectX::XMMATRIX V{ DirectX::XMLoadFloat4x4(&rc.camera->GetView()) };
        const DirectX::XMMATRIX P{ DirectX::XMLoadFloat4x4(&rc.camera->GetProjection()) };
        DirectX::XMStoreFloat4x4(&cbScene.viewProjection, V * P);

        const DirectionalLight& dirLight{ lightManager->GetDirectionalLight() };
        cbScene.lightDirection = { dirLight.direction.x, dirLight.direction.y, dirLight.direction.z, 0.0f };
        cbScene.lightColor = { dirLight.color.x * dirLight.intensity, dirLight.color.y * dirLight.intensity, dirLight.color.z * dirLight.intensity, 1.0f };

        const DirectX::XMFLOAT3& eye{ rc.camera->GetPosition() };
        cbScene.cameraPosition = { eye.x, eye.y, eye.z, 1.0f };

        const DirectX::XMFLOAT3& front{ rc.camera->GetFront() };
        cbScene.cameraDirection = { front.x, front.y, front.z, 0.0f };

        cbScene.ambientSkyColor = lightManager->GetEffectiveSkyColor();
        cbScene.ambientGroundColor = lightManager->GetEffectiveGroundColor();

        cbScene.packedParams = {
            rc.psxEnabled ? 1.0f : 0.0f,
            (std::max)(1.0f, rc.psxResWidth),
            (std::max)(1.0f, rc.psxResHeight),
            0.0f
        };

        cbScene.lightCounts = {
            lightManager->GetPointLightCount(),
            lightManager->GetSpotLightCount(),
            0, 0
        };

        const auto& pLights{ lightManager->GetPointLights() };
        for (int i{ 0 }; i < 8; ++i) cbScene.pointLights[i] = pLights[i];

        const auto& sLights{ lightManager->GetSpotLights() };
        for (int i{ 0 }; i < 8; ++i) cbScene.spotLights[i] = sLights[i];

        if (dirLight.castShadows)
        {
            const auto& matrices = lightManager->GetCascadeMatrices();
            for (int i = 0; i < 4; ++i) cbScene.cascadeMatrices[i] = matrices[i];

            cbScene.cascadeSplits = { dirLight.splitDistances[1], dirLight.splitDistances[2], dirLight.splitDistances[3], dirLight.splitDistances[4] };
            cbScene.cascadeBias = { dirLight.shadowBias[0], dirLight.shadowBias[1], dirLight.shadowBias[2], dirLight.shadowBias[3] };
            cbScene.shadowSettings = { 1.0f, dirLight.shadowAttenuation, 0.0f, 0.0f };
        }
        else
        {
            cbScene.shadowSettings = { 0.0f, 1.0f, 0.0f, 0.0f };
        }

        dc->UpdateSubresource(sceneConstantBuffer.Get(), 0, 0, &cbScene, 0, 0);
    }

    ID3D11Buffer* vsConstantBuffers[] = {
        sceneConstantBuffer.Get(),
    };
    ID3D11Buffer* psConstantBuffers[] = {
        sceneConstantBuffer.Get(),
    };
    dc->VSSetConstantBuffers(7, _countof(vsConstantBuffers), vsConstantBuffers);
    dc->PSSetConstantBuffers(7, _countof(psConstantBuffers), psConstantBuffers);
    dc->PSSetConstantBuffers(2, 1, objectConstantBuffer.GetAddressOf());

    ID3D11SamplerState* samplerStates[] = {
        rc.renderState->GetSamplerState(SamplerState::AnisotropicWrap)
    };
    dc->PSSetSamplers(0, _countof(samplerStates), samplerStates);

    dc->OMSetDepthStencilState(rc.renderState->GetDepthStencilState(DepthState::TestAndWrite), 0);
    dc->RSSetState(rc.renderState->GetRasterizerState(RasterizerState::SolidCullBack));

    DirectX::XMVECTOR CameraPosition = DirectX::XMLoadFloat3(&rc.camera->GetPosition());
    DirectX::XMVECTOR CameraFront = DirectX::XMLoadFloat3(&rc.camera->GetFront());

    dc->OMSetBlendState(rc.renderState->GetBlendState(BlendState::Opaque), nullptr, 0xFFFFFFFF);

    std::array<std::vector<MeshDrawCommand>, static_cast<std::size_t>(ShaderId::EnumCount)> opaqueBuckets{};

    DirectX::XMFLOAT4X4 matViewProj{};
    {
        const DirectX::XMMATRIX V = DirectX::XMLoadFloat4x4(&rc.camera->GetView());
        const DirectX::XMMATRIX P = DirectX::XMLoadFloat4x4(&rc.camera->GetProjection());
        DirectX::XMStoreFloat4x4(&matViewProj, V * P);
    }
    const CameraFrustumPlanes cameraPlanes = ExtractCameraFrustumPlanes(matViewProj);

    for (const DrawInfo& drawInfo : drawInfos)
    {
        if (!drawInfo.model) continue;

        const auto& meshes{ drawInfo.model->GetMeshes() };
        for (std::size_t meshIdx{ 0 }; meshIdx < meshes.size(); ++meshIdx)
        {
            const Model::Mesh& mesh{ meshes[meshIdx] };
            const std::size_t skeletonSlot{ drawInfo.skeletonSlots[meshIdx] };

            const BoundingSphere sphere{ CalculateMeshBoundingSphere(mesh, drawInfo.useManualMatrix, drawInfo.worldMatrix) };
            if (!IsSphereInFrustum(cameraPlanes, sphere.center, sphere.radius)) continue;

            if (mesh.material->alphaMode == AlphaMode::Blend ||
                (mesh.material->baseColor.w > 0.01f && mesh.material->baseColor.w < 0.99f))
            {
                TransparencyDrawInfo& t{ transparencyDrawInfos.emplace_back() };
                t.mesh = &mesh;
                t.shaderId = static_cast<ShaderId>(mesh.material->shaderId);
                t.color = drawInfo.color;
                t.skeletonSlot = skeletonSlot;

                DirectX::XMFLOAT4X4 transformMatrix{};
                if (drawInfo.useManualMatrix)
                {
                    DirectX::XMMATRIX nodeGlobalMat{ DirectX::XMLoadFloat4x4(&mesh.node->globalTransform) };
                    DirectX::XMMATRIX manualWorldMat{ DirectX::XMLoadFloat4x4(&drawInfo.worldMatrix) };
                    DirectX::XMStoreFloat4x4(&transformMatrix, nodeGlobalMat * manualWorldMat);
                }
                else
                {
                    transformMatrix = mesh.node->worldTransform;
                }

                DirectX::XMVECTOR Position{ DirectX::XMVectorSet(transformMatrix._41, transformMatrix._42, transformMatrix._43, 1.0f) };
                DirectX::XMVECTOR Vec{ DirectX::XMVectorSubtract(Position, CameraPosition) };
                t.distance = DirectX::XMVectorGetX(DirectX::XMVector3Dot(CameraFront, Vec));
                continue;
            }

            const std::size_t shaderIndex{ static_cast<std::size_t>(mesh.material->shaderId) };
            opaqueBuckets[shaderIndex].emplace_back(MeshDrawCommand{
    &mesh, skeletonSlot, drawInfo.color, drawInfo.useManualMatrix, drawInfo.worldMatrix, drawInfo.hasVelocity
                });
        }
    }
    drawInfos.clear();

    ID3D11SamplerState* shadowSampler = rc.renderState->GetSamplerState(SamplerState::ShadowMap);
    dc->PSSetSamplers(10, 1, &shadowSampler);

    if (rc.lightManager && rc.lightManager->GetDirectionalLight().castShadows)
    {
        ID3D11ShaderResourceView* const* cascadeSRVs = rc.lightManager->GetCascadeSRVs();
        dc->PSSetShaderResources(10, 4, cascadeSRVs);
    }

    for (std::size_t i{ 0 }; i < opaqueBuckets.size(); ++i)
    {
        if (opaqueBuckets[i].empty()) continue;

        Shader* const shader{ shaders[i].get() };
        if (!shader) continue;

        shader->Begin(rc);
        for (const MeshDrawCommand& cmd : opaqueBuckets[i])
        {
            CbObject cbObject{};
            cbObject.color = cmd.color;
            dc->UpdateSubresource(objectConstantBuffer.Get(), 0, 0, &cbObject, 0, 0);

            drawMesh(*cmd.mesh, shader, cmd.skeletonSlot);
        }
        shader->End(rc);

        if (static_cast<ShaderId>(i) == ShaderId::Toon)
        {
            dc->RSSetState(rc.renderState->GetRasterizerState(RasterizerState::SolidCullFront));

            m_outlineShader->Begin(rc);
            const DirectX::XMVECTOR camPos{ DirectX::XMLoadFloat3(&rc.camera->GetPosition()) };

            for (const MeshDrawCommand& cmd : opaqueBuckets[i])
            {
                if (!cmd.mesh->material->enableOutline || cmd.mesh->material->outlineWidth <= 0.0f)
                {
                    continue;
                }

                DirectX::XMMATRIX worldMat{};
                if (cmd.useManualMatrix)
                {
                    worldMat = DirectX::XMLoadFloat4x4(&cmd.mesh->node->globalTransform) * DirectX::XMLoadFloat4x4(&cmd.worldMatrix);
                }
                else
                {
                    worldMat = DirectX::XMLoadFloat4x4(&cmd.mesh->node->worldTransform);
                }

                const DirectX::XMVECTOR objPos{ worldMat.r[3] };
                const float distanceSq{ DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(DirectX::XMVectorSubtract(objPos, camPos))) };
                const float fadeEndSq{ cmd.mesh->material->outlineFadeEnd * cmd.mesh->material->outlineFadeEnd };

                static constexpr float s_radiusBufferSq{ 25.0f };
                if (distanceSq > (fadeEndSq + s_radiusBufferSq)) continue;

                drawMesh(*cmd.mesh, m_outlineShader.get(), cmd.skeletonSlot);
            }

            m_outlineShader->End(rc);
            dc->RSSetState(rc.renderState->GetRasterizerState(RasterizerState::SolidCullBack));
        }
    }

    if (rc.velocityRenderTargetView)
    {
        ID3D11RenderTargetView* originalRTV{ nullptr };
        ID3D11DepthStencilView* originalDSV{ nullptr };
        dc->OMGetRenderTargets(1, &originalRTV, &originalDSV);

        ID3D11RenderTargetView* const velocityRTV{ rc.velocityRenderTargetView };
        dc->OMSetRenderTargets(1, &velocityRTV, originalDSV);

        // Static geometry no longer gets drawn into this buffer at all, so pixels behind it need an
        // explicit zero here — that's what tells the resolve shader "no motion, trust history fully".
        constexpr float s_zeroVelocity[4]{ 0.0f, 0.0f, 0.0f, 0.0f };
        dc->ClearRenderTargetView(velocityRTV, s_zeroVelocity);

        dc->OMSetDepthStencilState(rc.renderState->GetDepthStencilState(DepthState::TestOnly), 0);
        dc->OMSetBlendState(rc.renderState->GetBlendState(BlendState::Opaque), nullptr, 0xFFFFFFFF);

        m_velocityShader->Begin(rc);

        DirectX::XMFLOAT4X4 currentViewProjection{};
        DirectX::XMStoreFloat4x4(&currentViewProjection, DirectX::XMLoadFloat4x4(&rc.camera->GetView()) * DirectX::XMLoadFloat4x4(&rc.camera->GetProjection()));
        m_velocityShader->SetViewProjections(dc, currentViewProjection, rc.camera->GetPreviousViewProjection());

        for (const auto& bucket : opaqueBuckets)
        {
            for (const MeshDrawCommand& cmd : bucket)
            {
                if (!cmd.hasVelocity) continue; // static/untracked geometry: skip entirely, its velocity is zero by the clear above
                DrawMeshVelocity(dc, *cmd.mesh, cmd.skeletonSlot);
            }
        }

        m_velocityShader->End(rc);

        dc->OMSetRenderTargets(1, &originalRTV, originalDSV);
        if (originalRTV) originalRTV->Release();
        if (originalDSV) originalDSV->Release();
    }

    dc->OMSetBlendState(rc.renderState->GetBlendState(BlendState::Transparency), nullptr, 0xFFFFFFFF);
    dc->OMSetDepthStencilState(rc.renderState->GetDepthStencilState(DepthState::TestOnly), 0);

    std::sort(transparencyDrawInfos.begin(), transparencyDrawInfos.end(),
        [](const TransparencyDrawInfo& lhs, const TransparencyDrawInfo& rhs)
        {
            return lhs.distance > rhs.distance;
        });

    for (const TransparencyDrawInfo& transparencyDrawInfo : transparencyDrawInfos)
    {
        Shader* shader = shaders[static_cast<int>(transparencyDrawInfo.shaderId)].get();
        shader->Begin(rc);

        CbObject cbObject;
        cbObject.color = transparencyDrawInfo.color;
        dc->UpdateSubresource(objectConstantBuffer.Get(), 0, 0, &cbObject, 0, 0);

        drawMesh(*transparencyDrawInfo.mesh, shader, transparencyDrawInfo.skeletonSlot);

        shader->End(rc);
    }
    transparencyDrawInfos.clear();

    for (ID3D11Buffer*& vsConstantBuffer : vsConstantBuffers) { vsConstantBuffer = nullptr; }
    for (ID3D11Buffer*& psConstantBuffer : psConstantBuffers) { psConstantBuffer = nullptr; }
    dc->VSSetConstantBuffers(7, _countof(vsConstantBuffers), vsConstantBuffers);
    dc->PSSetConstantBuffers(7, _countof(psConstantBuffers), psConstantBuffers);

    ID3D11Buffer* nullBuffer = nullptr;
    dc->PSSetConstantBuffers(2, 1, &nullBuffer);

    for (ID3D11SamplerState*& samplerState : samplerStates) { samplerState = nullptr; }
    dc->PSSetSamplers(0, _countof(samplerStates), samplerStates);

    ID3D11ShaderResourceView* const nullShadowSRVs[]{ nullptr, nullptr, nullptr, nullptr };
    dc->PSSetShaderResources(10, 4, nullShadowSRVs);
}