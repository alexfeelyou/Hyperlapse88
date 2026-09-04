#pragma once

#include <d3d11.h>
#include <DirectXMath.h>
#include <wrl/client.h>
#include "Model.h"
#include "Shader.h"

class RenderContext;

class OutlineShader final : public Shader
{
public:
    explicit OutlineShader(ID3D11Device* device);
    ~OutlineShader() override = default;

    OutlineShader(const OutlineShader&) = delete;
    OutlineShader& operator=(const OutlineShader&) = delete;
    OutlineShader(OutlineShader&&) = delete;
    OutlineShader& operator=(OutlineShader&&) = delete;

    void Begin(const RenderContext& rc) override;
    void Update(const RenderContext& rc, const Model::Mesh& mesh) override;
    void End(const RenderContext& rc) override;

private:
    struct CbOutline
    {
        DirectX::XMFLOAT4 outlineColor{ 0.0f, 0.0f, 0.0f, 1.0f }; // 16 bytes
        float             outlineWidth{ 0.0f };                   // 4 bytes
        float             outlineFadeStart{ 12.0f };              // 4 bytes
        float             outlineFadeEnd{ 28.0f };                // 4 bytes
        int               alphaMode{ 0 };                         // 4 bytes
        float             alphaCutoff{ 0.5f };                    // 4 bytes
        DirectX::XMFLOAT3 padding{ 0.0f, 0.0f, 0.0f };            // 12 bytes
    };
    static_assert((sizeof(CbOutline) % 16) == 0, "CbOutline must be 16-byte aligned!");

    Microsoft::WRL::ComPtr<ID3D11VertexShader> m_vertexShader{};
    Microsoft::WRL::ComPtr<ID3D11PixelShader>  m_pixelShader{};
    Microsoft::WRL::ComPtr<ID3D11InputLayout>  m_inputLayout{};
    Microsoft::WRL::ComPtr<ID3D11Buffer>       m_constantBuffer{};
};