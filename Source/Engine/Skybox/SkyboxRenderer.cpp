#include "System/GpuResourceUtils.h"
#include "SkyboxRenderer.h"

void SkyboxRenderer::Initialize(ID3D11Device* device)
{
    // Fullscreen Quad Vertex Buffer (Triangle Strip)
    const Vertex vertices[] = {
        { {-1.0f,  1.0f, 1.0f} }, // Top Left
        { { 1.0f,  1.0f, 1.0f} }, // Top Right
        { {-1.0f, -1.0f, 1.0f} }, // Bottom Left
        { { 1.0f, -1.0f, 1.0f} }  // Bottom Right
    };

    D3D11_BUFFER_DESC vbDesc{};
    vbDesc.ByteWidth = sizeof(vertices);
    vbDesc.Usage = D3D11_USAGE_IMMUTABLE;
    vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;

    D3D11_SUBRESOURCE_DATA vbData{};
    vbData.pSysMem = vertices;
    device->CreateBuffer(&vbDesc, &vbData, m_vertexBuffer.GetAddressOf());

    // Depth Stencil State (Write Mask Zero, Less Equal)
    D3D11_DEPTH_STENCIL_DESC dsDesc{};
    dsDesc.DepthEnable = TRUE;
    dsDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    dsDesc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
    device->CreateDepthStencilState(&dsDesc, m_depthStencilState.GetAddressOf());

    // Constant Buffer
    D3D11_BUFFER_DESC cbDesc{};
    cbDesc.Usage = D3D11_USAGE_DYNAMIC;
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    cbDesc.ByteWidth = sizeof(SkymapConstants);
    device->CreateBuffer(&cbDesc, nullptr, m_constantBuffer.GetAddressOf());

    // Sampler State
    D3D11_SAMPLER_DESC sampDesc{};
    sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
    sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
    sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    sampDesc.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
    device->CreateSamplerState(&sampDesc, m_samplerState.GetAddressOf());

    // Load Shaders via Engine Utils
    D3D11_INPUT_ELEMENT_DESC inputDesc[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 }
    };

    GpuResourceUtils::LoadVertexShader(device, "Data/Shader/Skybox_VS.cso", inputDesc, ARRAYSIZE(inputDesc), m_inputLayout.GetAddressOf(), m_vertexShader.GetAddressOf());
    GpuResourceUtils::LoadPixelShader(device, "Data/Shader/Skybox_PS.cso", m_pixelShader.GetAddressOf());
}

void SkyboxRenderer::Render(ID3D11DeviceContext* context, const Camera& camera, ID3D11ShaderResourceView* skyboxSRV) const noexcept
{
    if (!skyboxSRV) return;

    // Preserve original depth state
    Microsoft::WRL::ComPtr<ID3D11DepthStencilState> oldState{};
    UINT oldStencilRef{ 0 };
    context->OMGetDepthStencilState(oldState.GetAddressOf(), &oldStencilRef);

    // Bind custom depth state for far-plane rendering
    context->OMSetDepthStencilState(m_depthStencilState.Get(), 0);

    // Calculate Inverse View-Projection
    DirectX::XMMATRIX view = DirectX::XMLoadFloat4x4(&camera.GetView());
    DirectX::XMMATRIX proj = DirectX::XMLoadFloat4x4(&camera.GetProjection());
    DirectX::XMMATRIX invViewProj = DirectX::XMMatrixInverse(nullptr, DirectX::XMMatrixMultiply(view, proj));

    // Map Constant Buffer
    D3D11_MAPPED_SUBRESOURCE mappedResource{};
    if (SUCCEEDED(context->Map(m_constantBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource)))
    {
        auto* data = static_cast<SkymapConstants*>(mappedResource.pData);
        DirectX::XMStoreFloat4x4(&data->inverseViewProjection, invViewProj);
        DirectX::XMFLOAT3 camPos = camera.GetPosition();
        data->cameraPosition = { camPos.x, camPos.y, camPos.z, 1.0f };
        context->Unmap(m_constantBuffer.Get(), 0);
    }

    // Bind Pipeline
    const UINT stride = sizeof(Vertex);
    const UINT offset = 0;
    context->IASetVertexBuffers(0, 1, m_vertexBuffer.GetAddressOf(), &stride, &offset);
    context->IASetInputLayout(m_inputLayout.Get());
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

    context->VSSetShader(m_vertexShader.Get(), nullptr, 0);
    context->PSSetShader(m_pixelShader.Get(), nullptr, 0);

    ID3D11Buffer* cbuffers[] = { m_constantBuffer.Get() };
    context->VSSetConstantBuffers(7, 1, cbuffers);
    context->PSSetConstantBuffers(7, 1, cbuffers);

    context->PSSetShaderResources(0, 1, &skyboxSRV);
    context->PSSetSamplers(0, 1, m_samplerState.GetAddressOf());

    context->Draw(4, 0);

    // Restore original state
    context->OMSetDepthStencilState(oldState.Get(), oldStencilRef);
}