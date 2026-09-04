#include "GpuResourceUtils.h"
#include "Misc.h"
#include "OutlineShader.h"

OutlineShader::OutlineShader(ID3D11Device* device)
{
    GpuResourceUtils::LoadVertexShader(device, "Data/Shader/OutlineVS.cso",
        Model::InputElementDescs.data(), static_cast<UINT>(Model::InputElementDescs.size()),
        m_inputLayout.GetAddressOf(), m_vertexShader.GetAddressOf());

    GpuResourceUtils::LoadPixelShader(device, "Data/Shader/OutlinePS.cso", m_pixelShader.GetAddressOf());
    GpuResourceUtils::CreateConstantBuffer(device, sizeof(CbOutline), m_constantBuffer.GetAddressOf());
}

void OutlineShader::Begin(const RenderContext& rc)
{
    ID3D11DeviceContext* const dc{ rc.deviceContext };
    dc->IASetInputLayout(m_inputLayout.Get());
    dc->VSSetShader(m_vertexShader.Get(), nullptr, 0);
    dc->PSSetShader(m_pixelShader.Get(), nullptr, 0);

    // Bind to VS (for fade math) and PS (for color/alpha)
    ID3D11Buffer* const cbs[]{ m_constantBuffer.Get() };
    dc->VSSetConstantBuffers(0, static_cast<UINT>(std::size(cbs)), cbs);
    dc->PSSetConstantBuffers(0, static_cast<UINT>(std::size(cbs)), cbs);
}

void OutlineShader::Update(const RenderContext& rc, const Model::Mesh& mesh)
{
    ID3D11DeviceContext* const dc{ rc.deviceContext };

    const CbOutline cbOutline{
        mesh.material->outlineColor,
        mesh.material->outlineWidth,
        mesh.material->outlineFadeStart,
        mesh.material->outlineFadeEnd,
        static_cast<int>(mesh.material->alphaMode),
        mesh.material->alphaCutoff,
        { 0.0f, 0.0f, 0.0f }
    };
    dc->UpdateSubresource(m_constantBuffer.Get(), 0, 0, &cbOutline, 0, 0);

    // Bind base texture for alpha cutoff testing
    ID3D11ShaderResourceView* const srvs[]{ mesh.material->baseMap.Get() };
    dc->PSSetShaderResources(0, static_cast<UINT>(std::size(srvs)), srvs);
}

void OutlineShader::End(const RenderContext& rc)
{
    ID3D11DeviceContext* const dc{ rc.deviceContext };
    dc->VSSetShader(nullptr, nullptr, 0);
    dc->PSSetShader(nullptr, nullptr, 0);
    dc->IASetInputLayout(nullptr);
}