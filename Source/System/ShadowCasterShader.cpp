#include "GpuResourceUtils.h"
#include "ShadowCasterShader.h"

ShadowCasterShader::ShadowCasterShader(ID3D11Device* device)
{
    GpuResourceUtils::LoadVertexShader(
        device,
        "Data/Shader/ShadowCasterVS.cso",
        Model::InputElementDescs.data(),
        static_cast<UINT>(Model::InputElementDescs.size()),
        m_inputLayout.GetAddressOf(),
        m_vertexShader.GetAddressOf()
    );

    GpuResourceUtils::CreateConstantBuffer(
        device,
        sizeof(CbShadow),
        m_shadowConstantBuffer.GetAddressOf()
    );
}

void ShadowCasterShader::Begin(const RenderContext& rc)
{
    ID3D11DeviceContext* const dc{ rc.deviceContext };

    dc->IASetInputLayout(m_inputLayout.Get());
    dc->VSSetShader(m_vertexShader.Get(), nullptr, 0);

    // Disable Pixel Shader for pure hardware depth-write velocity
    dc->PSSetShader(nullptr, nullptr, 0);

    ID3D11Buffer* const cbs[]{ m_shadowConstantBuffer.Get() };
    dc->VSSetConstantBuffers(3, 1, cbs);
}

void ShadowCasterShader::Update(const RenderContext& rc, const Model::Mesh& mesh)
{
    // Deliberately empty. The ModelRenderer lambda natively updates the bone matrices.
    // No material processing is needed for opaque shadow casting.
}

void ShadowCasterShader::End(const RenderContext& rc)
{
    ID3D11DeviceContext* const dc{ rc.deviceContext };

    dc->VSSetShader(nullptr, nullptr, 0);
    dc->IASetInputLayout(nullptr);

    ID3D11Buffer* const nullCbs[]{ nullptr };
    dc->VSSetConstantBuffers(3, 1, nullCbs);
}

void ShadowCasterShader::SetCascadeMatrix(ID3D11DeviceContext* dc, const DirectX::XMFLOAT4X4& cascadeMatrix) const noexcept
{
    CbShadow cb{};
    cb.lightViewProjection = cascadeMatrix;
    dc->UpdateSubresource(m_shadowConstantBuffer.Get(), 0, 0, &cb, 0, 0);
}