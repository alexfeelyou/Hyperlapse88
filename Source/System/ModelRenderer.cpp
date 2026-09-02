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

    // シーン用定数バッファ更新
    {
        static LightManager defaultLightManager;
        const LightManager* lightManager = rc.lightManager ? rc.lightManager : &defaultLightManager;

        CbScene cbScene{};
        DirectX::XMMATRIX V = DirectX::XMLoadFloat4x4(&rc.camera->GetView());
        DirectX::XMMATRIX P = DirectX::XMLoadFloat4x4(&rc.camera->GetProjection());
        DirectX::XMStoreFloat4x4(&cbScene.viewProjection, V * P);
        const DirectionalLight& directionalLight = lightManager->GetDirectionalLight();
        cbScene.lightDirection.x = directionalLight.direction.x;
        cbScene.lightDirection.y = directionalLight.direction.y;
        cbScene.lightDirection.z = directionalLight.direction.z;
        cbScene.lightColor.x = directionalLight.color.x;
        cbScene.lightColor.y = directionalLight.color.y;
        cbScene.lightColor.z = directionalLight.color.z;
        const DirectX::XMFLOAT3& eye = rc.camera->GetPosition();
        cbScene.cameraPosition.x = eye.x;
        cbScene.cameraPosition.y = eye.y;
        cbScene.cameraPosition.z = eye.z;
        cbScene.psxEnabled = rc.psxEnabled ? 1.0f : 0.0f;
        cbScene.psxResWidth = (std::max)(1.0f, rc.psxResWidth);
        cbScene.psxResHeight = (std::max)(1.0f, rc.psxResHeight);
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

                    // [FIX] Multiply bone's global model-space transform by the GameObject's world space
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

        // Fast fail guard: Prevent hard crashes if shader initialization failed
        if (!shader)
        {
            _ASSERT_EXPR_A(false, "Critical Render Error: Shader instance is null!");
            continue;
        }

        shader->Begin(rc);

        for (const MeshDrawCommand& cmd : opaqueBuckets[i])
        {
            CbObject cbObject{};
            cbObject.color = cmd.color;
            dc->UpdateSubresource(objectConstantBuffer.Get(), 0, 0, &cbObject, 0, 0);

            drawMesh(*cmd.mesh, shader, cmd.useManualMatrix, cmd.worldMatrix);
        }

        shader->End(rc);
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