#pragma once

#include <d3d11.h>
#include <DirectXMath.h>
#include <wrl/client.h>
#include "Shader.h"

// Outputs per-pixel screen-space motion for opaque geometry, consumed by TemporalAAEffect.
class VelocityShader final : public Shader
{
public:
    explicit VelocityShader(ID3D11Device* device);
    ~VelocityShader() override = default;

    VelocityShader(const VelocityShader&) = delete;
    VelocityShader& operator=(const VelocityShader&) = delete;

    void Begin(const RenderContext& rc) override;
    void Update(const RenderContext& rc, const Model::Mesh& mesh) override;
    void End(const RenderContext& rc) override;

    void SetViewProjections(
        ID3D11DeviceContext* dc,
        const DirectX::XMFLOAT4X4& currentViewProjection,
        const DirectX::XMFLOAT4X4& previousViewProjection) const noexcept;

private:
    struct CbVelocity
    {
        DirectX::XMFLOAT4X4 currentViewProjection{};
        DirectX::XMFLOAT4X4 previousViewProjection{};
    };
    static_assert((sizeof(CbVelocity) % 16) == 0, "CbVelocity constant buffer must be 16-byte aligned!");

    Microsoft::WRL::ComPtr<ID3D11VertexShader> m_vertexShader{};
    Microsoft::WRL::ComPtr<ID3D11PixelShader>  m_pixelShader{};
    Microsoft::WRL::ComPtr<ID3D11InputLayout>  m_inputLayout{};
    Microsoft::WRL::ComPtr<ID3D11Buffer>       m_velocityConstantBuffer{};
};