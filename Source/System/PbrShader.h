#pragma once

#include <d3d11.h>
#include <DirectXMath.h>
#include <wrl/client.h>
#include "Shader.h"

class PbrShader final : public Shader
{
public:
    explicit PbrShader(ID3D11Device* device);
    ~PbrShader() override = default;

    PbrShader(const PbrShader&) = delete;
    PbrShader& operator=(const PbrShader&) = delete;
    PbrShader(PbrShader&&) = default;
    PbrShader& operator=(PbrShader&&) = default;

    void Begin(const RenderContext& rc) override;
    void Update(const RenderContext& rc, const Model::Mesh& mesh) override;
    void End(const RenderContext& rc) override;

private:
    // Aligned to 16-byte boundaries for HLSL
    struct CbMesh
    {
        DirectX::XMFLOAT4 materialColor{ 1.0f, 1.0f, 1.0f, 1.0f };
        DirectX::XMFLOAT3 emissiveColor{ 0.0f, 0.0f, 0.0f };
        float             roughness{ 0.5f };
        float             metallic{ 0.0f };
        float             occlusionStrength{ 1.0f };
        int               alphaMode{ 0 };
        float             alphaCutoff{ 0.5f };

        // Bitmask: Bit 0 = HasMetalRoughMap, Bit 1 = HasEmissiveMap, Bit 2 = HasAOMap
        int               textureFlags{ 0 };
        DirectX::XMFLOAT3 meshPadding{ 0.0f, 0.0f, 0.0f };
    };

    Microsoft::WRL::ComPtr<ID3D11VertexShader> m_vertexShader{};
    Microsoft::WRL::ComPtr<ID3D11PixelShader>  m_pixelShader{};
    Microsoft::WRL::ComPtr<ID3D11InputLayout>  m_inputLayout{};
    Microsoft::WRL::ComPtr<ID3D11Buffer>       m_meshConstantBuffer{};
};