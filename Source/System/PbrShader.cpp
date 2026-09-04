#include "GpuResourceUtils.h"
#include "PbrShader.h"

PbrShader::PbrShader(ID3D11Device* device)
{
    GpuResourceUtils::LoadVertexShader(
        device,
        "Data/Shader/PbrVS.cso",
        Model::InputElementDescs.data(),
        static_cast<UINT>(Model::InputElementDescs.size()),
        m_inputLayout.GetAddressOf(),
        m_vertexShader.GetAddressOf()
    );

    GpuResourceUtils::LoadPixelShader(
        device,
        "Data/Shader/PbrPS.cso",
        m_pixelShader.GetAddressOf()
    );

    GpuResourceUtils::CreateConstantBuffer(
        device,
        sizeof(CbMesh),
        m_meshConstantBuffer.GetAddressOf()
    );
}

void PbrShader::Begin(const RenderContext& rc)
{
    ID3D11DeviceContext* const dc{ rc.deviceContext };

    dc->IASetInputLayout(m_inputLayout.Get());
    dc->VSSetShader(m_vertexShader.Get(), nullptr, 0);
    dc->PSSetShader(m_pixelShader.Get(), nullptr, 0);

    ID3D11Buffer* const cbs[]{ m_meshConstantBuffer.Get() };
    dc->PSSetConstantBuffers(0, _countof(cbs), cbs);
}

void PbrShader::Update(const RenderContext& rc, const Model::Mesh& mesh)
{
    ID3D11DeviceContext* const dc{ rc.deviceContext };

    // Bit-pack texture existence to avoid HLSL branching overhead
    int flags{ 0 };
    if (mesh.material->metalnessRoughnessMap) flags |= (1 << 0);
    if (mesh.material->emissiveMap)           flags |= (1 << 1);
    if (mesh.material->occlusionMap)          flags |= (1 << 2);

    const CbMesh cbMesh{
        mesh.material->baseColor,
        mesh.material->emissiveColor,
        mesh.material->roughness,
        mesh.material->metalness,
        mesh.material->occlusionStrength,
        static_cast<int>(mesh.material->alphaMode),
        mesh.material->alphaCutoff,
        flags,
        { 0.0f, 0.0f, 0.0f }
    };

    dc->UpdateSubresource(m_meshConstantBuffer.Get(), 0, 0, &cbMesh, 0, 0);

    // Bind all PBR textures. Missing textures naturally bind as nullptr.
    ID3D11ShaderResourceView* const srvs[]{
        mesh.material->baseMap.Get(),
        mesh.material->normalMap.Get(),
        mesh.material->metalnessRoughnessMap.Get(),
        mesh.material->emissiveMap.Get(),
        mesh.material->occlusionMap.Get()
    };

    dc->PSSetShaderResources(0, _countof(srvs), srvs);
}

void PbrShader::End(const RenderContext& rc)
{
    ID3D11DeviceContext* const dc{ rc.deviceContext };

    dc->VSSetShader(nullptr, nullptr, 0);
    dc->PSSetShader(nullptr, nullptr, 0);
    dc->IASetInputLayout(nullptr);

    ID3D11Buffer* const nullCbs[]{ nullptr };
    dc->PSSetConstantBuffers(0, _countof(nullCbs), nullCbs);

    ID3D11ShaderResourceView* const nullSrvs[]{ nullptr, nullptr, nullptr, nullptr, nullptr };
    dc->PSSetShaderResources(0, _countof(nullSrvs), nullSrvs);
}