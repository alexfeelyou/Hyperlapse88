#include "WindowSwarmRenderer.h"
#include "System/GpuResourceUtils.h"
#include "System/Misc.h"

WindowSwarmRenderer::WindowSwarmRenderer(ID3D11Device* device)
{
    // 1. INPUT LAYOUT (Gabungan Data Vertex & Data Instance)
    D3D11_INPUT_ELEMENT_DESC layout[] =
    {
        // --- SLOT 0: Data Vertex Statis (Per-Vertex) ---
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },

        // --- SLOT 1: Data Instance Dinamis (Per-Instance) ---
        // PENTING: Perhatikan angka '1' di parameter dan flag D3D11_INPUT_PER_INSTANCE_DATA!
        { "INST_POS",  0, DXGI_FORMAT_R32G32B32_FLOAT,    1, 0, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
        { "INST_SIZE", 0, DXGI_FORMAT_R32G32_FLOAT,       1, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
        { "INST_COL",  0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
    };

    // Load Shaders (Pastikan path file .cso kamu benar!)
    GpuResourceUtils::LoadVertexShader(device, "Data/Shader/SwarmVS.cso", layout, ARRAYSIZE(layout), inputLayout.GetAddressOf(), vertexShader.GetAddressOf());
    GpuResourceUtils::LoadPixelShader(device, "Data/Shader/SwarmPS.cso", pixelShader.GetAddressOf());
    GpuResourceUtils::CreateConstantBuffer(device, sizeof(CbScene), constantBuffer.GetAddressOf());

    // 2. CREATE VERTEX BUFFER (Bentuk Kotak - Dibuat mengelilingi titik tengah 0,0)
    QuadVertex quad[4] = {
        { {-0.5f,  0.5f, 0.0f}, {0.0f, 0.0f} }, // Kiri Atas
        { { 0.5f,  0.5f, 0.0f}, {1.0f, 0.0f} }, // Kanan Atas
        { {-0.5f, -0.5f, 0.0f}, {0.0f, 1.0f} }, // Kiri Bawah
        { { 0.5f, -0.5f, 0.0f}, {1.0f, 1.0f} }  // Kanan Bawah
    };

    D3D11_BUFFER_DESC vbDesc = {};
    vbDesc.ByteWidth = sizeof(QuadVertex) * 4;
    vbDesc.Usage = D3D11_USAGE_IMMUTABLE; // Tidak akan pernah berubah
    vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA vbData = { quad, 0, 0 };
    device->CreateBuffer(&vbDesc, &vbData, vertexBuffer.GetAddressOf());

    // 3. CREATE INSTANCE BUFFER
    D3D11_BUFFER_DESC ibDesc = {};
    ibDesc.ByteWidth = sizeof(InstanceData) * MaxInstances;
    ibDesc.Usage = D3D11_USAGE_DYNAMIC;           // Bisa di-update tiap frame
    ibDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;  // Secara teknis ini diperlakukan seperti Vertex Buffer
    ibDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    device->CreateBuffer(&ibDesc, nullptr, instanceBuffer.GetAddressOf());

    instances.reserve(MaxInstances);
}

void WindowSwarmRenderer::Begin()
{
    instances.clear(); // Bersihkan antrean dari frame sebelumnya
}

void WindowSwarmRenderer::AddWindow(const DirectX::XMFLOAT3& pos, const DirectX::XMFLOAT2& size, const DirectX::XMFLOAT4& color)
{
    if (instances.size() < MaxInstances)
    {
        instances.push_back({ pos, size, color });
    }
}

void WindowSwarmRenderer::Render(ID3D11DeviceContext* dc, const DirectX::XMFLOAT4X4& viewProj, ID3D11ShaderResourceView* textureSRV)
{
    if (instances.empty() || !textureSRV) return;

    // 1. Update Constant Buffer (Matriks Kamera)
    CbScene cb = { viewProj };
    dc->UpdateSubresource(constantBuffer.Get(), 0, 0, &cb, 0, 0);

    // 2. Update Instance Buffer (Map -> memcpy -> Unmap)
    // D3D11_MAP_WRITE_DISCARD memastikan CPU tidak stall menunggu GPU selesai merender frame sebelumnya.
    D3D11_MAPPED_SUBRESOURCE mapped;
    if (SUCCEEDED(dc->Map(instanceBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
    {
        memcpy(mapped.pData, instances.data(), instances.size() * sizeof(InstanceData));
        dc->Unmap(instanceBuffer.Get(), 0);
    }

    // 3. Bind State ke Pipeline
    dc->VSSetShader(vertexShader.Get(), nullptr, 0);
    dc->PSSetShader(pixelShader.Get(), nullptr, 0);
    dc->IASetInputLayout(inputLayout.Get());
    dc->VSSetConstantBuffers(0, 1, constantBuffer.GetAddressOf());
    dc->PSSetShaderResources(0, 1, &textureSRV);

    // 4. BIND 2 BUFFER SEKALIGUS! (Ini rahasia Instancing)
    ID3D11Buffer* buffers[2] = { vertexBuffer.Get(), instanceBuffer.Get() };
    UINT strides[2] = { sizeof(QuadVertex), sizeof(InstanceData) };
    UINT offsets[2] = { 0, 0 };

    dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    dc->IASetVertexBuffers(0, 2, buffers, strides, offsets);

    // 5. EKSEKUSI! (Gambar 4 titik, tapi diulang otomatis oleh GPU sebanyak jumlah instance)
    dc->DrawInstanced(4, static_cast<UINT>(instances.size()), 0, 0);
}