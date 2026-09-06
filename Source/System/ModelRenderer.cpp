#include "ProfilerManager.h"
#include "ModelRenderer.h"

namespace
{
    struct CascadeFrustumPlanes
    {
        DirectX::XMFLOAT4 planes[5]; // Left, Right, Bottom, Top, Far
    };

    struct CameraFrustumPlanes
    {
        DirectX::XMFLOAT4 planes[6]; // Left, Right, Bottom, Top, Near, Far
    };

    struct BoundingSphere
    {
        DirectX::XMFLOAT3 center{ 0.0f, 0.0f, 0.0f };
        float radius{ 0.0f };
    };

    [[nodiscard]] CascadeFrustumPlanes ExtractCascadePlanes(const DirectX::XMFLOAT4X4& M) noexcept
    {
        CascadeFrustumPlanes cp{};
        // Gribb-Hartmann extraction for DirectX LHS [0, 1] projection
        // Left plane
        cp.planes[0] = { M._14 + M._11, M._24 + M._21, M._34 + M._31, M._44 + M._41 };
        // Right plane
        cp.planes[1] = { M._14 - M._11, M._24 - M._21, M._34 - M._31, M._44 - M._41 };
        // Bottom plane
        cp.planes[2] = { M._14 + M._12, M._24 + M._22, M._34 + M._32, M._44 + M._42 };
        // Top plane
        cp.planes[3] = { M._14 - M._12, M._24 - M._22, M._34 - M._32, M._44 - M._42 };
        // Far plane
        cp.planes[4] = { M._14 - M._13, M._24 - M._23, M._34 - M._33, M._44 - M._43 };

        // Normalize plane equation coefficients
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
        cp.planes[0] = { M._14 + M._11, M._24 + M._21, M._34 + M._31, M._44 + M._41 }; // Left
        cp.planes[1] = { M._14 - M._11, M._24 - M._21, M._34 - M._31, M._44 - M._41 }; // Right
        cp.planes[2] = { M._14 + M._12, M._24 + M._22, M._34 + M._32, M._44 + M._42 }; // Bottom
        cp.planes[3] = { M._14 - M._12, M._24 - M._22, M._34 - M._32, M._44 - M._42 }; // Top
        cp.planes[4] = { M._13,         M._23,         M._33,         M._43 };         // Near
        cp.planes[5] = { M._14 - M._13, M._24 - M._23, M._34 - M._33, M._44 - M._43 }; // Far

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
        // Calculate true world transform for specific submesh
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

        // Transform the mesh's local bounding sphere to world space
        const DirectX::XMVECTOR vCenter = DirectX::XMLoadFloat3(&mesh.boundsCenter);
        DirectX::XMFLOAT3 worldCenter{};
        DirectX::XMStoreFloat3(&worldCenter, DirectX::XMVector3TransformCoord(vCenter, mWorld));

        // Extract accurate scaling
        const float scaleX = DirectX::XMVectorGetX(DirectX::XMVector3Length(mWorld.r[0]));
        const float scaleY = DirectX::XMVectorGetX(DirectX::XMVector3Length(mWorld.r[1]));
        const float scaleZ = DirectX::XMVectorGetX(DirectX::XMVector3Length(mWorld.r[2]));
        const float maxScale = (std::max)({ scaleX, scaleY, scaleZ });

        const float worldRadius = mesh.boundsRadius * (maxScale > 0.0001f ? maxScale : 1.0f);

        return { worldCenter, worldRadius };
    }
}

// コンストラクタ
ModelRenderer::ModelRenderer(ID3D11Device* device)
{
    GpuResourceUtils::CreateConstantBuffer(
        device, sizeof(CbScene), sceneConstantBuffer.GetAddressOf());
    GpuResourceUtils::CreateConstantBuffer(
        device, sizeof(CbSkeleton), skeletonConstantBuffer.GetAddressOf());
    GpuResourceUtils::CreateConstantBuffer(
        device, sizeof(CbObject), objectConstantBuffer.GetAddressOf());

    drawInfos.reserve(2000);
    transparencyDrawInfos.reserve(2000);

    shaders[static_cast<int>(ShaderId::Basic)] = std::make_unique<BasicShader>(device);
    shaders[static_cast<int>(ShaderId::Lambert)] = std::make_unique<LambertShader>(device);
    shaders[static_cast<int>(ShaderId::Phong)] = std::make_unique<PhongShader>(device);
    shaders[static_cast<int>(ShaderId::Pbr)] = std::make_unique<PbrShader>(device);
    shaders[static_cast<int>(ShaderId::Toon)] = std::make_unique<ToonShader>(device);

    m_outlineShader = std::make_unique<OutlineShader>(device);
    m_shadowCasterShader = std::make_unique<ShadowCasterShader>(device);
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
    drawInfo.castShadows = castShadows;
}

void ModelRenderer::Render(const RenderContext& rc)
{
    if (drawInfos.empty()) return;

    ID3D11DeviceContext* dc = rc.deviceContext;

    auto drawMesh = [&](const Model::Mesh& mesh, Shader* shader, bool useManual, const DirectX::XMFLOAT4X4& manualMatrix)
        {
            UINT stride = sizeof(Model::Vertex);
            UINT offset = 0;
            dc->IASetVertexBuffers(0, 1, mesh.vertexBuffer.GetAddressOf(), &stride, &offset);
            dc->IASetIndexBuffer(mesh.indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
            dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

            CbSkeleton cbSkeleton{};

            // Cache the manual world matrix once
            DirectX::XMMATRIX manualWorldMat = DirectX::XMLoadFloat4x4(&manualMatrix);

            if (mesh.bones.size() > 0)
            {
                for (size_t i = 0; i < mesh.bones.size(); ++i)
                {
                    const Model::Bone& bone = mesh.bones.at(i);

                    // Multiply bone's global model-space transform by the GameObject's world space
                    DirectX::XMMATRIX nodeGlobalMat = DirectX::XMLoadFloat4x4(&bone.node->globalTransform);
                    DirectX::XMMATRIX worldTransform = useManual
                        ? (nodeGlobalMat * manualWorldMat)
                        : DirectX::XMLoadFloat4x4(&bone.node->worldTransform);

                    DirectX::XMMATRIX offsetTransform = DirectX::XMLoadFloat4x4(&bone.offsetTransform);
                    DirectX::XMStoreFloat4x4(&cbSkeleton.boneTransforms[i], offsetTransform * worldTransform);
                }
            }
            else
            {
                // Multiply mesh's global model-space transform by the GameObject's world space
                if (useManual)
                {
                    DirectX::XMMATRIX nodeGlobalMat = DirectX::XMLoadFloat4x4(&mesh.node->globalTransform);
                    DirectX::XMStoreFloat4x4(&cbSkeleton.boneTransforms[0], nodeGlobalMat * manualWorldMat);
                }
                else
                {
                    cbSkeleton.boneTransforms[0] = mesh.node->worldTransform;
                }
            }

            dc->UpdateSubresource(skeletonConstantBuffer.Get(), 0, 0, &cbSkeleton, 0, 0);

            shader->Update(rc, mesh);
            dc->DrawIndexed(static_cast<UINT>(mesh.indices.size()), 0, 0);

            // このラムダは不透明・半透明どちらのパスからも呼ばれるので、
            // ここが唯一の DrawIndexed 呼び出し箇所になる
            PROFILE_DRAW_CALL();
            PROFILE_TRIANGLES(mesh.indices.size() / 3);   // インデックスバッファは三角形リストなので3で割る
    };

    if (rc.lightManager && rc.lightManager->GetDirectionalLight().castShadows)
    {
        // Compute matrix maths mathematically
        rc.lightManager->UpdateCascades(*rc.camera);

        // Store active Viewport & RTV to restore later
        ID3D11RenderTargetView* originalRTV{ nullptr };
        ID3D11DepthStencilView* originalDSV{ nullptr };
        dc->OMGetRenderTargets(1, &originalRTV, &originalDSV);

        UINT numViewports{ 1 };
        D3D11_VIEWPORT originalViewport{};
        dc->RSGetViewports(&numViewports, &originalViewport);

        // Configure strict rendering state for Shadows
        D3D11_VIEWPORT shadowViewport{};
        shadowViewport.Width = static_cast<float>(SHADOW_MAP_SIZE);
        shadowViewport.Height = static_cast<float>(SHADOW_MAP_SIZE);
        shadowViewport.MaxDepth = 1.0f;

        dc->OMSetBlendState(rc.renderState->GetBlendState(BlendState::Opaque), nullptr, 0xFFFFFFFF);
        
        // Explicitly enforce depth writing for the shadow pass
        dc->OMSetDepthStencilState(rc.renderState->GetDepthStencilState(DepthState::TestAndWrite), 0);

        dc->RSSetState(rc.renderState->GetRasterizerState(RasterizerState::SolidCullBack));

        m_shadowCasterShader->Begin(rc);

        // Re-bind skeleton matrices explicitly for the Shadow pass
        ID3D11Buffer* const vsShadowCbs[]{ skeletonConstantBuffer.Get() };
        dc->VSSetConstantBuffers(6, 1, vsShadowCbs);

        for (int i = 0; i < SHADOW_CASCADE_COUNT; ++i)
        {
            ID3D11DepthStencilView* cascadeDSV = rc.lightManager->GetCascadeDSV(i);

            // Only bind DSV (null RTV writes 4x faster)
            dc->ClearDepthStencilView(cascadeDSV, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
            dc->OMSetRenderTargets(0, nullptr, cascadeDSV);
            dc->RSSetViewports(1, &shadowViewport);

            const auto& cascadeMatrix = rc.lightManager->GetCascadeMatrices()[i];
            m_shadowCasterShader->SetCascadeMatrix(dc, cascadeMatrix);

            // Extract 5 light-space planes for this cascade
            const CascadeFrustumPlanes cascadePlanes = ExtractCascadePlanes(cascadeMatrix);

            for (const DrawInfo& drawInfo : drawInfos)
            {
                if (!drawInfo.castShadows || !drawInfo.model) continue;

                for (const Model::Mesh& mesh : drawInfo.model->GetMeshes())
                {
                    // Do not cast shadows from transparent or glass materials
                    if (mesh.material->alphaMode == AlphaMode::Blend) continue;

                    // PER-MESH CULLING: Safely skips chunks of the stage outside the shadow zone
                    const BoundingSphere sphere = CalculateMeshBoundingSphere(mesh, drawInfo.useManualMatrix, drawInfo.worldMatrix);

                    if (!IsSphereInCascade(cascadePlanes, sphere.center, sphere.radius))
                    {
                        continue;
                    }

                    drawMesh(mesh, m_shadowCasterShader.get(), drawInfo.useManualMatrix, drawInfo.worldMatrix);
                }
            }
        }

        m_shadowCasterShader->End(rc);

        // Restore Scene state
        dc->OMSetRenderTargets(1, &originalRTV, originalDSV);
        dc->RSSetViewports(1, &originalViewport);

        if (originalRTV) originalRTV->Release();
        if (originalDSV) originalDSV->Release();
    }

    // Update LightManager aggregation prior to scene rendering
    if (rc.lightManager)
    {
        const_cast<LightManager*>(rc.lightManager)->Update();
    }

    // シーン用定数バッファ更新
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

        // Pull dynamic environment colors from LightManager
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

		// Cascaded Shadow Map Parameters
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
        skeletonConstantBuffer.Get(),
        sceneConstantBuffer.Get(),
    };
    ID3D11Buffer* psConstantBuffers[] = {
        sceneConstantBuffer.Get(),
    };
    dc->VSSetConstantBuffers(6, _countof(vsConstantBuffers), vsConstantBuffers);
    dc->PSSetConstantBuffers(7, _countof(psConstantBuffers), psConstantBuffers);
    dc->PSSetConstantBuffers(2, 1, objectConstantBuffer.GetAddressOf());

    ID3D11SamplerState* samplerStates[] = {
        rc.renderState->GetSamplerState(SamplerState::LinearWrap)
    };
    dc->PSSetSamplers(0, _countof(samplerStates), samplerStates);

    dc->OMSetDepthStencilState(rc.renderState->GetDepthStencilState(DepthState::TestAndWrite), 0);
    dc->RSSetState(rc.renderState->GetRasterizerState(RasterizerState::SolidCullBack));

    DirectX::XMVECTOR CameraPosition = DirectX::XMLoadFloat3(&rc.camera->GetPosition());
    DirectX::XMVECTOR CameraFront = DirectX::XMLoadFloat3(&rc.camera->GetFront());

    // Set Opaque blend state unconditionally
    dc->OMSetBlendState(rc.renderState->GetBlendState(BlendState::Opaque), nullptr, 0xFFFFFFFF);

    // Setup buckets using std::array to group meshes by their requested shader
    std::array<std::vector<MeshDrawCommand>, static_cast<std::size_t>(ShaderId::EnumCount)> opaqueBuckets{};

    // Extract camera frustum planes ONCE per frame (O(1) overhead)
    DirectX::XMFLOAT4X4 matViewProj{};
    {
        const DirectX::XMMATRIX V = DirectX::XMLoadFloat4x4(&rc.camera->GetView());
        const DirectX::XMMATRIX P = DirectX::XMLoadFloat4x4(&rc.camera->GetProjection());
        DirectX::XMStoreFloat4x4(&matViewProj, V * P);
    }
    const CameraFrustumPlanes cameraPlanes = ExtractCameraFrustumPlanes(matViewProj);

    // Distribute meshes into transparent queue or their specific opaque shader bucket
    for (const DrawInfo& drawInfo : drawInfos)
    {
        if (!drawInfo.model) continue;

        for (const Model::Mesh& mesh : drawInfo.model->GetMeshes())
        {
            // PER-MESH CULLING: Tests individual parts of the character and stage
            const BoundingSphere sphere = CalculateMeshBoundingSphere(mesh, drawInfo.useManualMatrix, drawInfo.worldMatrix);

            if (!IsSphereInFrustum(cameraPlanes, sphere.center, sphere.radius))
            {
                continue; // Cull this specific submesh
            }

            if (mesh.material->alphaMode == AlphaMode::Blend ||
                (mesh.material->baseColor.w > 0.01f && mesh.material->baseColor.w < 0.99f))
            {
                TransparencyDrawInfo& transparencyDrawInfo{ transparencyDrawInfos.emplace_back() };
                transparencyDrawInfo.mesh = &mesh;
                transparencyDrawInfo.shaderId = static_cast<ShaderId>(mesh.material->shaderId);
                transparencyDrawInfo.color = drawInfo.color;
                transparencyDrawInfo.useManualMatrix = drawInfo.useManualMatrix;
                transparencyDrawInfo.worldMatrix = drawInfo.worldMatrix;

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
                transparencyDrawInfo.distance = DirectX::XMVectorGetX(DirectX::XMVector3Dot(CameraFront, Vec));
                continue;
            }

            // Route to correct opaque bucket using the Material's assigned shader
            const std::size_t shaderIndex{ static_cast<std::size_t>(mesh.material->shaderId) };
            opaqueBuckets[shaderIndex].emplace_back(MeshDrawCommand{
                &mesh, drawInfo.color, drawInfo.useManualMatrix, drawInfo.worldMatrix
                });
        }
    }
    drawInfos.clear();
    
    // Bind Shadow Sampler to slot s10
    ID3D11SamplerState* shadowSampler = rc.renderState->GetSamplerState(SamplerState::ShadowMap);
    dc->PSSetSamplers(10, 1, &shadowSampler);
    
    // Bind all 4 Cascade Depth Maps to slots t10-t13
    if (rc.lightManager && rc.lightManager->GetDirectionalLight().castShadows)
    {
        ID3D11ShaderResourceView* const* cascadeSRVs = rc.lightManager->GetCascadeSRVs();
        dc->PSSetShaderResources(10, 4, cascadeSRVs);
    }

    // Render opaque buckets
    for (std::size_t i{ 0 }; i < opaqueBuckets.size(); ++i)
    {
        if (opaqueBuckets[i].empty()) continue;

        Shader* const shader{ shaders[i].get() };
        if (!shader) continue;

        // Primary forward pass
        shader->Begin(rc);
        for (const MeshDrawCommand& cmd : opaqueBuckets[i])
        {
            CbObject cbObject{};
            cbObject.color = cmd.color;
            dc->UpdateSubresource(objectConstantBuffer.Get(), 0, 0, &cbObject, 0, 0);

            drawMesh(*cmd.mesh, shader, cmd.useManualMatrix, cmd.worldMatrix);
        }
        shader->End(rc);

		// Immediate dual-pass outline rendering for Toon shader
        if (static_cast<ShaderId>(i) == ShaderId::Toon)
        {
            dc->RSSetState(rc.renderState->GetRasterizerState(RasterizerState::SolidCullFront));

            m_outlineShader->Begin(rc);

            const DirectX::XMVECTOR camPos{ DirectX::XMLoadFloat3(&rc.camera->GetPosition()) };

            for (const MeshDrawCommand& cmd : opaqueBuckets[i])
            {
                if (!cmd.mesh->material->enableOutline || cmd.mesh->material->outlineWidth <= 0.0f)
                {
                    continue; // Feature disabled on this material
                }

                // Skip draw call if mesh origin is past fade end
                DirectX::XMMATRIX worldMat{};
                if (cmd.useManualMatrix)
                {
                    worldMat = DirectX::XMLoadFloat4x4(&cmd.mesh->node->globalTransform) * DirectX::XMLoadFloat4x4(&cmd.worldMatrix);
                }
                else
                {
                    worldMat = DirectX::XMLoadFloat4x4(&cmd.mesh->node->worldTransform);
                }

                // Extract position from matrix row 3 (_41, _42, _43)
                const DirectX::XMVECTOR objPos{ worldMat.r[3] };
                const float distanceSq{ DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(DirectX::XMVectorSubtract(objPos, camPos))) };
                const float fadeEndSq{ cmd.mesh->material->outlineFadeEnd * cmd.mesh->material->outlineFadeEnd };

                // Allow a small radius buffer (e.g. 25 units sq) to prevent large meshes from popping early
                static constexpr float s_radiusBufferSq{ 25.0f };
                if (distanceSq > (fadeEndSq + s_radiusBufferSq))
                {
                    continue; // Skip draw call entirely
                }

                drawMesh(*cmd.mesh, m_outlineShader.get(), cmd.useManualMatrix, cmd.worldMatrix);
            }

            m_outlineShader->End(rc);
            dc->RSSetState(rc.renderState->GetRasterizerState(RasterizerState::SolidCullBack));
        }

    }
    drawInfos.clear();

    // Set Transparency blend state and disable depth writes unconditionally
    dc->OMSetBlendState(rc.renderState->GetBlendState(BlendState::Transparency), nullptr, 0xFFFFFFFF);
    dc->OMSetDepthStencilState(rc.renderState->GetDepthStencilState(DepthState::TestOnly), 0);

    // カメラから遠い順にソート
    std::sort(transparencyDrawInfos.begin(), transparencyDrawInfos.end(),
        [](const TransparencyDrawInfo& lhs, const TransparencyDrawInfo& rhs)
        {
            return lhs.distance > rhs.distance;
        });

    // 半透明描画処理
    for (const TransparencyDrawInfo& transparencyDrawInfo : transparencyDrawInfos)
    {
        Shader* shader = shaders[static_cast<int>(transparencyDrawInfo.shaderId)].get();
        shader->Begin(rc);

        CbObject cbObject;
        cbObject.color = transparencyDrawInfo.color;
        dc->UpdateSubresource(objectConstantBuffer.Get(), 0, 0, &cbObject, 0, 0);

        drawMesh(*transparencyDrawInfo.mesh, shader,
            transparencyDrawInfo.useManualMatrix, transparencyDrawInfo.worldMatrix);

        shader->End(rc);
    }
    transparencyDrawInfos.clear();

    // 定数バッファ設定解除
    for (ID3D11Buffer*& vsConstantBuffer : vsConstantBuffers) { vsConstantBuffer = nullptr; }
    for (ID3D11Buffer*& psConstantBuffer : psConstantBuffers) { psConstantBuffer = nullptr; }
    dc->VSSetConstantBuffers(6, _countof(vsConstantBuffers), vsConstantBuffers);
    dc->PSSetConstantBuffers(7, _countof(psConstantBuffers), psConstantBuffers);

    ID3D11Buffer* nullBuffer = nullptr;
    dc->PSSetConstantBuffers(2, 1, &nullBuffer);

    for (ID3D11SamplerState*& samplerState : samplerStates) { samplerState = nullptr; }
    dc->PSSetSamplers(0, _countof(samplerStates), samplerStates);

    // Clean up Shadow Map Bindings
    ID3D11ShaderResourceView* const nullShadowSRVs[]{ nullptr, nullptr, nullptr, nullptr };
    dc->PSSetShaderResources(10, 4, nullShadowSRVs);
}