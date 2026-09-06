#pragma once

#include <d3d11.h>
#include <DirectXMath.h>
#include <wrl/client.h>
#include "Shader.h"

class ShadowCasterShader final : public Shader
{
public:
    explicit ShadowCasterShader(ID3D11Device* device);
    ~ShadowCasterShader() override = default;

    ShadowCasterShader(const ShadowCasterShader&) = delete;
    ShadowCasterShader& operator=(const ShadowCasterShader&) = delete;

    void Begin(const RenderContext& rc) override;
    void Update(const RenderContext& rc, const Model::Mesh& mesh) override;
    void End(const RenderContext& rc) override;

    // Injects the current cascade's Crop Matrix before drawing
    void SetCascadeMatrix(ID3D11DeviceContext* dc, const DirectX::XMFLOAT4X4& cascadeMatrix) const noexcept;

private:
    struct CbShadow
    {
        DirectX::XMFLOAT4X4 lightViewProjection{};
    };

    Microsoft::WRL::ComPtr<ID3D11VertexShader> m_vertexShader{};
    Microsoft::WRL::ComPtr<ID3D11InputLayout>  m_inputLayout{};
    Microsoft::WRL::ComPtr<ID3D11Buffer>       m_shadowConstantBuffer{};
};