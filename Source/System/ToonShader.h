#pragma once

#include <d3d11.h>
#include <DirectXMath.h>
#include <wrl/client.h>
#include "Model.h"
#include "Shader.h"

// Forward declarations 
class RenderContext;

class ToonShader final : public Shader
{
public:
    // Make single-argument constructor explicit to prevent unintended implicit conversions
    explicit ToonShader(ID3D11Device* device);
    ~ToonShader() override = default;

    // Delete copy/move semantics to enforce strict ownership of COM pointers and prevent double-free logic errors
    ToonShader(const ToonShader&) = delete;
    ToonShader& operator=(const ToonShader&) = delete;
    ToonShader(ToonShader&&) = delete;
    ToonShader& operator=(ToonShader&&) = delete;

    void Begin(const RenderContext& rc) override;
    void Update(const RenderContext& rc, const Model::Mesh& mesh) override;
    void End(const RenderContext& rc) override;

private:
    struct CbMesh
	{
		DirectX::XMFLOAT4 materialColor{ 1.0f, 1.0f, 1.0f, 1.0f }; // 16 bytes
		DirectX::XMFLOAT3 emissiveColor{ 0.0f, 0.0f, 0.0f };       // 12 bytes
		float             roughness{ 0.5f };                       // 4 bytes
		int               alphaMode{ 0 };                          // 4 bytes
		float             alphaCutoff{ 0.5f };                     // 4 bytes
		DirectX::XMFLOAT2 padding{ 0.0f, 0.0f };                   // 8 bytes
	};

    // Catch padding/alignment logic errors at compile time
    static_assert((sizeof(CbMesh) % 16) == 0, "CbMesh constant buffer must be 16-byte aligned!");

    Microsoft::WRL::ComPtr<ID3D11VertexShader> m_vertexShader{};
    Microsoft::WRL::ComPtr<ID3D11PixelShader>  m_pixelShader{};
    Microsoft::WRL::ComPtr<ID3D11InputLayout>  m_inputLayout{};
    Microsoft::WRL::ComPtr<ID3D11Buffer>       m_meshConstantBuffer{};
};