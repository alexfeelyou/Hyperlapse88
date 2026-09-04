#include "GpuResourceUtils.h"
#include "Misc.h"
#include "ToonShader.h"

ToonShader::ToonShader(ID3D11Device* device)
{
    // Load Vertex Shader and generate Input Layout matching the global Model format
    GpuResourceUtils::LoadVertexShader(
        device,
        "Data/Shader/ToonVS.cso",
        Model::InputElementDescs.data(),
        static_cast<UINT>(Model::InputElementDescs.size()),
        m_inputLayout.GetAddressOf(),
        m_vertexShader.GetAddressOf());

    // Load Pixel Shader
    GpuResourceUtils::LoadPixelShader(
        device,
        "Data/Shader/ToonPS.cso",
        m_pixelShader.GetAddressOf());

    // Create 16-byte aligned constant buffer for per-mesh properties
    GpuResourceUtils::CreateConstantBuffer(
        device,
        sizeof(CbMesh),
        m_meshConstantBuffer.GetAddressOf());
}

void ToonShader::Begin(const RenderContext& rc)
{
    ID3D11DeviceContext* const dc{ rc.deviceContext };

    dc->IASetInputLayout(m_inputLayout.Get());
    dc->VSSetShader(m_vertexShader.Get(), nullptr, 0);
    dc->PSSetShader(m_pixelShader.Get(), nullptr, 0);

    // Bind the per-mesh constant buffer to slot 0 in the Pixel Shader
    ID3D11Buffer* const cbs[]{ m_meshConstantBuffer.Get() };
    dc->PSSetConstantBuffers(0, static_cast<UINT>(std::size(cbs)), cbs);
}

void ToonShader::Update(const RenderContext& rc, const Model::Mesh& mesh)
{
    ID3D11DeviceContext* const dc{ rc.deviceContext };

    // Map material properties into the struct 
    const CbMesh cbMesh{
         mesh.material->baseColor,
         mesh.material->emissiveColor,
         mesh.material->roughness,
         static_cast<int>(mesh.material->alphaMode),
         mesh.material->alphaCutoff,
         { 0.0f, 0.0f }
    };
    dc->UpdateSubresource(m_meshConstantBuffer.Get(), 0, 0, &cbMesh, 0, 0);

    // Bind diffuse map
    ID3D11ShaderResourceView* const srvs[]{ mesh.material->baseMap.Get() };
    dc->PSSetShaderResources(0, static_cast<UINT>(std::size(srvs)), srvs);
}

void ToonShader::End(const RenderContext& rc)
{
    ID3D11DeviceContext* const dc{ rc.deviceContext };

    // Unbind pipeline state to prevent resource hazards in subsequent passes
    dc->VSSetShader(nullptr, nullptr, 0);
    dc->PSSetShader(nullptr, nullptr, 0);
    dc->IASetInputLayout(nullptr);

    ID3D11Buffer* const nullBuffers[]{ nullptr };
    dc->PSSetConstantBuffers(0, static_cast<UINT>(std::size(nullBuffers)), nullBuffers);

    ID3D11ShaderResourceView* const nullSrvs[]{ nullptr };
    dc->PSSetShaderResources(0, static_cast<UINT>(std::size(nullSrvs)), nullSrvs);
}