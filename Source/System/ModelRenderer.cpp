#include "ProfilerManager.h"
#include "ModelRenderer.h"

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
}

void ModelRenderer::Draw(std::shared_ptr<Model> model, const DirectX::XMFLOAT4& color)
{
    DrawInfo& drawInfo{ drawInfos.emplace_back() };
    drawInfo.model = std::move(model);
    drawInfo.color = color;
    drawInfo.useManualMatrix = false;
}

void ModelRenderer::Draw(std::shared_ptr<Model> model, DirectX::XMFLOAT4 color, const DirectX::XMFLOAT4X4& worldMatrix)
{
    DrawInfo& drawInfo{ drawInfos.emplace_back() };
    drawInfo.model = std::move(model);
    drawInfo.color = color;
    drawInfo.useManualMatrix = true;
    drawInfo.worldMatrix = worldMatrix;
}

void ModelRenderer::Render(const RenderContext& rc)
{
    if (drawInfos.empty()) return;

    ID3D11DeviceContext* dc = rc.deviceContext;

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

    DirectX::XMVECTOR CameraPosition = DirectX::XMLoadFloat3(&rc.camera->GetPosition());
    DirectX::XMVECTOR CameraFront = DirectX::XMLoadFloat3(&rc.camera->GetFront());

    // Set Opaque blend state unconditionally
    dc->OMSetBlendState(rc.renderState->GetBlendState(BlendState::Opaque), nullptr, 0xFFFFFFFF);

    // Setup buckets using std::array to group meshes by their requested shader
    std::array<std::vector<MeshDrawCommand>, static_cast<std::size_t>(ShaderId::EnumCount)> opaqueBuckets{};

    // Distribute meshes into transparent queue or their specific opaque shader bucket
    for (const DrawInfo& drawInfo : drawInfos)
    {
        for (const Model::Mesh& mesh : drawInfo.model->GetMeshes())
        {
            if (mesh.material->alphaMode == AlphaMode::Blend ||
                (mesh.material->baseColor.w > 0.01f && mesh.material->baseColor.w < 0.99f))
            {
                TransparencyDrawInfo& transparencyDrawInfo{ transparencyDrawInfos.emplace_back() };
                transparencyDrawInfo.mesh = &mesh;
                // Pull the shaderId directly from the individual material
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
}