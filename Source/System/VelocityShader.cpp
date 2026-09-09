#include "GpuResourceUtils.h"
#include "VelocityShader.h"

VelocityShader::VelocityShader(ID3D11Device* device)
{
    GpuResourceUtils::LoadVertexShader(
        device,
        "Data/Shader/VelocityVS.cso",
        Model::InputElementDescs.data(),
        static_cast<UINT>(Model::InputElementDescs.size()),
        m_inputLayout.GetAddressOf(),
        m_vertexShader.GetAddressOf()
    );

    GpuResourceUtils::LoadPixelShader(device, "Data/Shader/VelocityPS.cso", m_pixelShader.GetAddressOf());
    GpuResourceUtils::CreateConstantBuffer(device, sizeof(CbVelocity), m_velocityConstantBuffer.GetAddressOf());
}

void VelocityShader::Begin(const RenderContext& rc)
{
    ID3D11DeviceContext* const dc{ rc.deviceContext };

    dc->IASetInputLayout(m_inputLayout.Get());
    dc->VSSetShader(m_vertexShader.Get(), nullptr, 0);
    dc->PSSetShader(m_pixelShader.Get(), nullptr, 0);

    ID3D11Buffer* const cbs[]{ m_velocityConstantBuffer.Get() };
    dc->VSSetConstantBuffers(8, 1, cbs); // register b8
}

void VelocityShader::Update(const RenderContext& rc, const Model::Mesh& mesh)
{
    // Deliberately empty — ModelRenderer calls SetTransforms() per-object before each draw instead
}

void VelocityShader::End(const RenderContext& rc)
{
    ID3D11DeviceContext* const dc{ rc.deviceContext };

    dc->VSSetShader(nullptr, nullptr, 0);
    dc->PSSetShader(nullptr, nullptr, 0);
    dc->IASetInputLayout(nullptr);

    ID3D11Buffer* const nullCbs[]{ nullptr };
    dc->VSSetConstantBuffers(8, 1, nullCbs);
}

void VelocityShader::SetViewProjections(
    ID3D11DeviceContext* dc,
    const DirectX::XMFLOAT4X4& currentViewProjection,
    const DirectX::XMFLOAT4X4& previousViewProjection) const noexcept
{
    CbVelocity cb{};
    cb.currentViewProjection = currentViewProjection;
    cb.previousViewProjection = previousViewProjection;
    dc->UpdateSubresource(m_velocityConstantBuffer.Get(), 0, nullptr, &cb, 0, 0);
}