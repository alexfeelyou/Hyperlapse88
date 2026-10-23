#pragma once

#include "Shader.h"

class BasicShader : public Shader
{
public:
	BasicShader(ID3D11Device* device);
	~BasicShader() override = default;

	// 開始処理
	void Begin(const RenderContext& rc) override;

	// 更新処理
	void Update(const RenderContext& rc, const Model::Mesh& mesh) override;

	// 終了処理
	void End(const RenderContext& rc) override;

private:
	struct CbMesh
	{
		DirectX::XMFLOAT4 materialColor{ 1.0f, 1.0f, 1.0f, 1.0f };
		int               alphaMode{ 0 };
		float             alphaCutoff{ 0.5f };
		int               uvProjectionMode{ 0 };
		float             meshPadding{ 0.0f };
		DirectX::XMFLOAT2 uvTiling{ 1.0f, 1.0f };
		DirectX::XMFLOAT2 uvOffset{ 0.0f, 0.0f };
	};
	static_assert((sizeof(CbMesh) % 16) == 0, "CbMesh must be 16-byte aligned!");

	Microsoft::WRL::ComPtr<ID3D11VertexShader>		vertexShader;
	Microsoft::WRL::ComPtr<ID3D11PixelShader>		pixelShader;
	Microsoft::WRL::ComPtr<ID3D11InputLayout>		inputLayout;
	Microsoft::WRL::ComPtr<ID3D11Buffer>			meshConstantBuffer;
};
