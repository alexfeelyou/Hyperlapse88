#pragma once

#include "Shader.h"

class PhongShader : public Shader
{
public:
	PhongShader(ID3D11Device* device);
	~PhongShader() override = default;

	// 開始処理
	void Begin(const RenderContext& rc) override;

	// 更新処理
	void Update(const RenderContext& rc, const Model::Mesh& mesh) override;

	// 終了処理
	void End(const RenderContext& rc) override;

private:
	struct CbMesh
	{
		DirectX::XMFLOAT4 materialColor{ 1.0f, 1.0f, 1.0f, 1.0f }; // 16 bytes
		DirectX::XMFLOAT3 emissiveColor{ 0.0f, 0.0f, 0.0f };       // 12 bytes
		float             roughness{ 0.5f };                       // 4 bytes
		int               alphaMode{ 0 };                          // 4 bytes
		float             alphaCutoff{ 0.5f };                     // 4 bytes
		float             subsurfaceStrength{ 0.0f };              // 4 bytes
		int               uvProjectionMode{ 0 };                   // 4 bytes
		DirectX::XMFLOAT2 uvTiling{ 1.0f, 1.0f };                  // 8 bytes
		DirectX::XMFLOAT2 uvOffset{ 0.0f, 0.0f };                  // 8 bytes
	};
	static_assert((sizeof(CbMesh) % 16) == 0, "CbMesh must be 16-byte aligned!");

	Microsoft::WRL::ComPtr<ID3D11VertexShader>		vertexShader;
	Microsoft::WRL::ComPtr<ID3D11PixelShader>		pixelShader;
	Microsoft::WRL::ComPtr<ID3D11InputLayout>		inputLayout;
	Microsoft::WRL::ComPtr<ID3D11Buffer>			meshConstantBuffer;
};
