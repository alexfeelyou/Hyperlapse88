#include <algorithm>
#include <cmath>
#include "ProfilerManager.h"
#include "ShapeRenderer.h"

// コンストラクタ
ShapeRenderer::ShapeRenderer(ID3D11Device* device)
    : m_device(device)
{
    const D3D11_INPUT_ELEMENT_DESC inputElementDesc[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };

    GpuResourceUtils::LoadVertexShader(
        device,
        "Data/Shader/ShapeRendererVS.cso",
        inputElementDesc,
        _countof(inputElementDesc),
        m_inputLayout.GetAddressOf(),
        m_vertexShader.GetAddressOf());

    GpuResourceUtils::LoadPixelShader(
        device,
        "Data/Shader/ShapeRendererPS.cso",
        m_pixelShader.GetAddressOf());

    // Create the Constant Buffer with D3D11_USAGE_DYNAMIC
    D3D11_BUFFER_DESC cbDesc{};
    cbDesc.ByteWidth = sizeof(CbMesh);
    cbDesc.Usage = D3D11_USAGE_DYNAMIC;
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    cbDesc.MiscFlags = 0;
    cbDesc.StructureByteStride = 0;

    HRESULT hr{ device->CreateBuffer(&cbDesc, nullptr, m_constantBuffer.GetAddressOf()) };
    _ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));

    CreateBoxMesh(device, 1.0f, 1.0f, 1.0f);
    CreateSphereMesh(device, 1.0f, 32);
    CreateHalfSphereMesh(device, 1.0f, 32);
    CreateCylinderMesh(device, 1.0f, 1.0f, -0.5f, 1.0f, 32);
    CreateBoneMesh(device, 1.0f);
}

// 箱描画
void ShapeRenderer::DrawBox(const DirectX::XMFLOAT3& position, const DirectX::XMFLOAT3& angle, const DirectX::XMFLOAT3& size, const DirectX::XMFLOAT4& color)
{
    Instance& instance{ m_instances.emplace_back() };
    instance.mesh = &m_boxMesh; // Fixed prefix
    instance.color = color;

    const DirectX::XMMATRIX S{ DirectX::XMMatrixScaling(size.x, size.y, size.z) };
    const DirectX::XMMATRIX R{ DirectX::XMMatrixRotationRollPitchYaw(angle.x, angle.y, angle.z) };
    const DirectX::XMMATRIX T{ DirectX::XMMatrixTranslation(position.x, position.y, position.z) };
    DirectX::XMStoreFloat4x4(&instance.worldTransform, S * R * T);
}

// 球描画
void ShapeRenderer::DrawSphere(const DirectX::XMFLOAT3& position, float radius, const DirectX::XMFLOAT4& color)
{
    Instance& instance{ m_instances.emplace_back() };
    instance.mesh = &m_sphereMesh; // Fixed prefix
    instance.color = color;

    const DirectX::XMMATRIX S{ DirectX::XMMatrixScaling(radius, radius, radius) };
    const DirectX::XMMATRIX T{ DirectX::XMMatrixTranslation(position.x, position.y, position.z) };
    DirectX::XMStoreFloat4x4(&instance.worldTransform, S * T);
}

// カプセル描画
void ShapeRenderer::DrawCapsule(const DirectX::XMFLOAT4X4& transform, float radius, float height, const DirectX::XMFLOAT4& color)
{
    const DirectX::XMMATRIX Transform{ DirectX::XMLoadFloat4x4(&transform) };
    DirectX::XMMATRIX RotScale{ Transform };
    RotScale.r[3] = DirectX::XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);

    {
        Instance& instance{ m_instances.emplace_back() };
        instance.mesh = &m_halfSphereMesh; // Fixed prefix
        const DirectX::XMVECTOR Offset{ DirectX::XMVectorSet(0.0f, height * 0.5f, 0.0f, 0.0f) };
        const DirectX::XMVECTOR Position{ DirectX::XMVectorAdd(Transform.r[3], DirectX::XMVector3TransformNormal(Offset, RotScale)) };

        DirectX::XMMATRIX World{ DirectX::XMMatrixScaling(radius, radius, radius) * RotScale };
        World.r[3] = DirectX::XMVectorSetW(Position, 1.0f);

        DirectX::XMStoreFloat4x4(&instance.worldTransform, World);
        instance.color = color;
    }
    {
        Instance& instance{ m_instances.emplace_back() };
        instance.mesh = &m_cylinderMesh; // Fixed prefix

        DirectX::XMMATRIX World{ DirectX::XMMatrixScaling(radius, height, radius) * RotScale };
        World.r[3] = Transform.r[3];

        DirectX::XMStoreFloat4x4(&instance.worldTransform, World);
        instance.color = color;
    }
    {
        Instance& instance{ m_instances.emplace_back() };
        instance.mesh = &m_halfSphereMesh; // Fixed prefix
        const DirectX::XMVECTOR Offset{ DirectX::XMVectorSet(0.0f, -height * 0.5f, 0.0f, 0.0f) };
        const DirectX::XMVECTOR Position{ DirectX::XMVectorAdd(Transform.r[3], DirectX::XMVector3TransformNormal(Offset, RotScale)) };

        DirectX::XMMATRIX World{ DirectX::XMMatrixRotationX(DirectX::XM_PI) * DirectX::XMMatrixScaling(radius, radius, radius) * RotScale };
        World.r[3] = DirectX::XMVectorSetW(Position, 1.0f);

        DirectX::XMStoreFloat4x4(&instance.worldTransform, World);
        instance.color = color;
    }
}

// 骨描画
void ShapeRenderer::DrawBone(const DirectX::XMFLOAT4X4& transform, float length, const DirectX::XMFLOAT4& color)
{
    Instance& instance{ m_instances.emplace_back() };
    instance.mesh = &m_boneMesh; // Fixed prefix
    instance.color = color;

    DirectX::XMMATRIX W{ DirectX::XMLoadFloat4x4(&transform) };
    W.r[0] = DirectX::XMVectorScale(DirectX::XMVector3Normalize(W.r[0]), length);
    W.r[1] = DirectX::XMVectorScale(DirectX::XMVector3Normalize(W.r[1]), length);
    W.r[2] = DirectX::XMVectorScale(DirectX::XMVector3Normalize(W.r[2]), length);
    DirectX::XMStoreFloat4x4(&instance.worldTransform, W);
}

// 線描画 (Highly Optimized Dynamic Batching)
void ShapeRenderer::DrawLine(const DirectX::XMFLOAT3& start, const DirectX::XMFLOAT3& end, const DirectX::XMFLOAT4& color)
{
    LineBatch* targetBatch{ nullptr };

    for (auto& batch : m_lineBatches)
    {
        constexpr float epsilon{ 0.001f };
        if (std::abs(batch.color.x - color.x) < epsilon &&
            std::abs(batch.color.y - color.y) < epsilon &&
            std::abs(batch.color.z - color.z) < epsilon &&
            std::abs(batch.color.w - color.w) < epsilon)
        {
            targetBatch = &batch;
            break;
        }
    }

    if (!targetBatch)
    {
        targetBatch = &m_lineBatches.emplace_back();
        targetBatch->color = color;
    }

    targetBatch->vertices.push_back(start);
    targetBatch->vertices.push_back(end);
}

// フラスタム描画
void ShapeRenderer::DrawFrustum(const DirectX::XMFLOAT3& position, const DirectX::XMFLOAT3& rotation, float fovY, float aspectRatio, float nearZ, float farZ, const DirectX::XMFLOAT4& color, float gizmoDrawDistance)
{
    const float visualFarZ{ (std::min)(farZ, gizmoDrawDistance) };

    const float tanHalfFovY{ std::tan(fovY * 0.5f) };
    const float nearHalfHeight{ tanHalfFovY * nearZ };
    const float nearHalfWidth{ nearHalfHeight * aspectRatio };
    const float farHalfHeight{ tanHalfFovY * visualFarZ };
    const float farHalfWidth{ farHalfHeight * aspectRatio };

    const DirectX::XMFLOAT3 localCorners[8]
    {
        { -nearHalfWidth,  nearHalfHeight, nearZ }, {  nearHalfWidth,  nearHalfHeight, nearZ },
        {  nearHalfWidth, -nearHalfHeight, nearZ }, { -nearHalfWidth, -nearHalfHeight, nearZ },
        { -farHalfWidth,   farHalfHeight,  visualFarZ }, {  farHalfWidth,   farHalfHeight,  visualFarZ },
        {  farHalfWidth,  -farHalfHeight,  visualFarZ }, { -farHalfWidth,  -farHalfHeight,  visualFarZ },
    };

    const DirectX::XMMATRIX rotationMatrix{ DirectX::XMMatrixRotationRollPitchYaw(rotation.x, rotation.y, rotation.z) };
    const DirectX::XMVECTOR positionVector{ DirectX::XMLoadFloat3(&position) };

    DirectX::XMFLOAT3 worldCorners[8]{};
    for (int i{ 0 }; i < 8; ++i)
    {
        const DirectX::XMVECTOR local{ DirectX::XMLoadFloat3(&localCorners[i]) };
        const DirectX::XMVECTOR world{ DirectX::XMVectorAdd(DirectX::XMVector3TransformNormal(local, rotationMatrix), positionVector) };
        DirectX::XMStoreFloat3(&worldCorners[i], world);
    }

    // Near plane rectangle
    DrawLine(worldCorners[0], worldCorners[1], color);
    DrawLine(worldCorners[1], worldCorners[2], color);
    DrawLine(worldCorners[2], worldCorners[3], color);
    DrawLine(worldCorners[3], worldCorners[0], color);

    // Connect camera origin to near plane for visual clarity
    DrawLine(position, worldCorners[0], color);
    DrawLine(position, worldCorners[1], color);
    DrawLine(position, worldCorners[2], color);
    DrawLine(position, worldCorners[3], color);

    // Far plane rectangle
    DrawLine(worldCorners[4], worldCorners[5], color);
    DrawLine(worldCorners[5], worldCorners[6], color);
    DrawLine(worldCorners[6], worldCorners[7], color);
    DrawLine(worldCorners[7], worldCorners[4], color);

    // Connecting edges between near and far planes
    DrawLine(worldCorners[0], worldCorners[4], color);
    DrawLine(worldCorners[1], worldCorners[5], color);
    DrawLine(worldCorners[2], worldCorners[6], color);
    DrawLine(worldCorners[3], worldCorners[7], color);
}

// 描画実行
void ShapeRenderer::Render(ID3D11DeviceContext* dc, const DirectX::XMFLOAT4X4& view, const DirectX::XMFLOAT4X4& projection)
{
    dc->VSSetShader(m_vertexShader.Get(), nullptr, 0);
    dc->PSSetShader(m_pixelShader.Get(), nullptr, 0);
    dc->IASetInputLayout(m_inputLayout.Get());
    dc->VSSetConstantBuffers(0, 1, m_constantBuffer.GetAddressOf());

    const DirectX::XMMATRIX V{ DirectX::XMLoadFloat4x4(&view) };
    const DirectX::XMMATRIX P{ DirectX::XMLoadFloat4x4(&projection) };
    const DirectX::XMMATRIX VP{ V * P };

    const UINT stride{ sizeof(DirectX::XMFLOAT3) };
    const UINT offset{ 0 };

    // Render Solid Instances (Boxes, Spheres, Capsules)
    if (!m_instances.empty())
    {
        dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);

        std::sort(m_instances.begin(), m_instances.end(), [](const Instance& a, const Instance& b) noexcept {
            return a.mesh < b.mesh;
            });

        Mesh* currentMesh{ nullptr };

        for (const Instance& instance : m_instances)
        {
            if (currentMesh != instance.mesh)
            {
                currentMesh = instance.mesh;
                dc->IASetVertexBuffers(0, 1, currentMesh->vertexBuffer.GetAddressOf(), &stride, &offset);
            }

            const DirectX::XMMATRIX W{ DirectX::XMLoadFloat4x4(&instance.worldTransform) };

            CbMesh cbMesh{};
            DirectX::XMStoreFloat4x4(&cbMesh.worldViewProjection, W * VP);
            cbMesh.color = instance.color;

            D3D11_MAPPED_SUBRESOURCE mappedCb{};
            if (SUCCEEDED(dc->Map(m_constantBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedCb)))
            {
                std::memcpy(mappedCb.pData, &cbMesh, sizeof(CbMesh));
                dc->Unmap(m_constantBuffer.Get(), 0);
            }

            dc->Draw(instance.mesh->vertexCount, 0);
            PROFILE_DRAW_CALL();
        }
        m_instances.clear();
    }

    // Render Batched Lines
    if (!m_lineBatches.empty())
    {
        dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);

        CbMesh cbMesh{};
        DirectX::XMStoreFloat4x4(&cbMesh.worldViewProjection, VP); // Lines are pre-transformed to World space

        for (auto& batch : m_lineBatches)
        {
            if (batch.vertices.empty()) continue;

            const UINT vertexCount{ static_cast<UINT>(batch.vertices.size()) };

            if (vertexCount > m_dynamicLineVBCapacity)
            {
                m_dynamicLineVBCapacity = (std::max)(m_dynamicLineVBCapacity * 2, vertexCount + 2048);

                D3D11_BUFFER_DESC vbDesc{};
                vbDesc.ByteWidth = sizeof(DirectX::XMFLOAT3) * m_dynamicLineVBCapacity;
                vbDesc.Usage = D3D11_USAGE_DYNAMIC;
                vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
                vbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
                vbDesc.MiscFlags = 0;

                const HRESULT hr{ m_device->CreateBuffer(&vbDesc, nullptr, m_dynamicLineVB.ReleaseAndGetAddressOf()) };
                _ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));
            }

            D3D11_MAPPED_SUBRESOURCE mappedVb{};
            if (SUCCEEDED(dc->Map(m_dynamicLineVB.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedVb)))
            {
                std::memcpy(mappedVb.pData, batch.vertices.data(), sizeof(DirectX::XMFLOAT3) * vertexCount);
                dc->Unmap(m_dynamicLineVB.Get(), 0);
            }

            cbMesh.color = batch.color;
            D3D11_MAPPED_SUBRESOURCE mappedCb{};
            if (SUCCEEDED(dc->Map(m_constantBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedCb)))
            {
                std::memcpy(mappedCb.pData, &cbMesh, sizeof(CbMesh));
                dc->Unmap(m_constantBuffer.Get(), 0);
            }

            dc->IASetVertexBuffers(0, 1, m_dynamicLineVB.GetAddressOf(), &stride, &offset);
            dc->Draw(vertexCount, 0);
            PROFILE_DRAW_CALL();

            batch.vertices.clear();
        }
    }
}

// メッシュ生成
void ShapeRenderer::CreateMesh(ID3D11Device* device, const std::vector<DirectX::XMFLOAT3>& vertices, Mesh& mesh)
{
    D3D11_BUFFER_DESC desc = {};
    desc.ByteWidth = static_cast<UINT>(sizeof(DirectX::XMFLOAT3) * vertices.size());
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    desc.CPUAccessFlags = 0;
    desc.MiscFlags = 0;
    desc.StructureByteStride = 0;
    D3D11_SUBRESOURCE_DATA subresourceData = {};
    subresourceData.pSysMem = vertices.data();

    HRESULT hr = device->CreateBuffer(&desc, &subresourceData, mesh.vertexBuffer.GetAddressOf());
    _ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));

    mesh.vertexCount = static_cast<UINT>(vertices.size());
}

// 箱メッシュ作成
void ShapeRenderer::CreateBoxMesh(ID3D11Device* device, float width, float height, float depth)
{
    DirectX::XMFLOAT3 positions[8] =
    {
        { -width,  height, -depth}, {  width,  height, -depth},
        {  width,  height,  depth}, { -width,  height,  depth},
        { -width, -height, -depth}, {  width, -height, -depth},
        {  width, -height,  depth}, { -width, -height,  depth},
    };

    std::vector<DirectX::XMFLOAT3> vertices;
    vertices.resize(32);

    vertices.emplace_back(positions[0]); vertices.emplace_back(positions[1]);
    vertices.emplace_back(positions[1]); vertices.emplace_back(positions[2]);
    vertices.emplace_back(positions[2]); vertices.emplace_back(positions[3]);
    vertices.emplace_back(positions[3]); vertices.emplace_back(positions[0]);
    vertices.emplace_back(positions[4]); vertices.emplace_back(positions[5]);
    vertices.emplace_back(positions[5]); vertices.emplace_back(positions[6]);
    vertices.emplace_back(positions[6]); vertices.emplace_back(positions[7]);
    vertices.emplace_back(positions[7]); vertices.emplace_back(positions[4]);
    vertices.emplace_back(positions[0]); vertices.emplace_back(positions[4]);
    vertices.emplace_back(positions[1]); vertices.emplace_back(positions[5]);
    vertices.emplace_back(positions[2]); vertices.emplace_back(positions[6]);
    vertices.emplace_back(positions[3]); vertices.emplace_back(positions[7]);

    CreateMesh(device, vertices, m_boxMesh); // Fixed prefix
}

// 球メッシュ作成
void ShapeRenderer::CreateSphereMesh(ID3D11Device* device, float radius, int subdivisions)
{
    float step = DirectX::XM_2PI / subdivisions;
    std::vector<DirectX::XMFLOAT3> vertices;

    for (int i = 0; i < subdivisions; ++i)
    {
        for (int j = 0; j < 2; ++j)
        {
            float theta = step * ((i + j) % subdivisions);
            DirectX::XMFLOAT3& p = vertices.emplace_back();
            p.x = sinf(theta) * radius; p.y = 0.0f; p.z = cosf(theta) * radius;
        }
    }
    for (int i = 0; i < subdivisions; ++i)
    {
        for (int j = 0; j < 2; ++j)
        {
            float theta = step * ((i + j) % subdivisions);
            DirectX::XMFLOAT3& p = vertices.emplace_back();
            p.x = sinf(theta) * radius; p.y = cosf(theta) * radius; p.z = 0.0f;
        }
    }
    for (int i = 0; i < subdivisions; ++i)
    {
        for (int j = 0; j < 2; ++j)
        {
            float theta = step * ((i + j) % subdivisions);
            DirectX::XMFLOAT3& p = vertices.emplace_back();
            p.x = 0.0f; p.y = sinf(theta) * radius; p.z = cosf(theta) * radius;
        }
    }

    CreateMesh(device, vertices, m_sphereMesh); // Fixed prefix
}

// 半球メッシュ作成
void ShapeRenderer::CreateHalfSphereMesh(ID3D11Device* device, float radius, int subdivisions)
{
    std::vector<DirectX::XMFLOAT3> vertices;
    float theta_step = DirectX::XM_2PI / subdivisions;

    for (int i = 0; i < subdivisions; ++i)
    {
        for (int j = 0; j < 2; ++j)
        {
            float theta = theta_step * ((i + j) % subdivisions);
            DirectX::XMFLOAT3& v = vertices.emplace_back();
            v.x = sinf(theta) * radius; v.y = 0.0f; v.z = cosf(theta) * radius;
        }
    }
    for (int i = 0; i < subdivisions / 2; ++i)
    {
        for (int j = 0; j < 2; ++j)
        {
            float theta = theta_step * ((i + j) % subdivisions) - DirectX::XM_PIDIV2;
            DirectX::XMFLOAT3& v = vertices.emplace_back();
            v.x = sinf(theta) * radius; v.y = cosf(theta) * radius; v.z = 0.0f;
        }
    }
    for (int i = 0; i < subdivisions / 2; ++i)
    {
        for (int j = 0; j < 2; ++j)
        {
            float theta = theta_step * ((i + j) % subdivisions);
            DirectX::XMFLOAT3& v = vertices.emplace_back();
            v.x = 0.0f; v.y = sinf(theta) * radius; v.z = cosf(theta) * radius;
        }
    }

    CreateMesh(device, vertices, m_halfSphereMesh); // Fixed prefix
}

// 円柱
void ShapeRenderer::CreateCylinderMesh(ID3D11Device* device, float radius1, float radius2, float start, float height, int subdivisions)
{
    std::vector<DirectX::XMFLOAT3> vertices;
    float theta_step = DirectX::XM_2PI / subdivisions;

    for (int i = 0; i < subdivisions; ++i)
    {
        for (int j = 0; j < 2; ++j)
        {
            float theta = theta_step * ((i + j) % subdivisions);
            DirectX::XMFLOAT3& v = vertices.emplace_back();
            v.x = sinf(theta) * radius1; v.y = start; v.z = cosf(theta) * radius1;
        }
    }
    for (int i = 0; i < subdivisions; ++i)
    {
        for (int j = 0; j < 2; ++j)
        {
            float theta = theta_step * ((i + j) % subdivisions);
            DirectX::XMFLOAT3& v = vertices.emplace_back();
            v.x = sinf(theta) * radius2; v.y = start + height; v.z = cosf(theta) * radius2;
        }
    }
    vertices.emplace_back(DirectX::XMFLOAT3(0.0f, start, radius1));
    vertices.emplace_back(DirectX::XMFLOAT3(0.0f, start + height, radius2));
    vertices.emplace_back(DirectX::XMFLOAT3(0.0f, start, -radius1));
    vertices.emplace_back(DirectX::XMFLOAT3(0.0f, start + height, -radius2));

    vertices.emplace_back(DirectX::XMFLOAT3(radius1, start, 0.0f));
    vertices.emplace_back(DirectX::XMFLOAT3(radius2, start + height, 0.0f));
    vertices.emplace_back(DirectX::XMFLOAT3(-radius1, start, 0.0f));
    vertices.emplace_back(DirectX::XMFLOAT3(-radius2, start + height, 0.0f));

    CreateMesh(device, vertices, m_cylinderMesh); // Fixed prefix
}

// 骨メッシュ作成
void ShapeRenderer::CreateBoneMesh(ID3D11Device* device, float length)
{
    float width = length * 0.25f;
    DirectX::XMFLOAT3 positions[8] =
    {
        { -0.00f,  0.00f,  0.00f}, {  width,  0.00f,  width},
        {  0.00f,  0.00f,  length}, { -width,  0.00f,  width},
        {  0.00f,  width,  width}, {  0.00f, -width,  width},
    };

    std::vector<DirectX::XMFLOAT3> vertices;
    vertices.reserve(24);

    vertices.emplace_back(positions[0]); vertices.emplace_back(positions[1]);
    vertices.emplace_back(positions[1]); vertices.emplace_back(positions[2]);
    vertices.emplace_back(positions[2]); vertices.emplace_back(positions[3]);
    vertices.emplace_back(positions[3]); vertices.emplace_back(positions[0]);

    vertices.emplace_back(positions[0]); vertices.emplace_back(positions[4]);
    vertices.emplace_back(positions[4]); vertices.emplace_back(positions[2]);
    vertices.emplace_back(positions[2]); vertices.emplace_back(positions[5]);
    vertices.emplace_back(positions[5]); vertices.emplace_back(positions[0]);

    vertices.emplace_back(positions[1]); vertices.emplace_back(positions[4]);
    vertices.emplace_back(positions[4]); vertices.emplace_back(positions[3]);
    vertices.emplace_back(positions[3]); vertices.emplace_back(positions[5]);
    vertices.emplace_back(positions[5]); vertices.emplace_back(positions[1]);

    CreateMesh(device, vertices, m_boneMesh); // Fixed prefix
}