#pragma once

#include <d3d11.h>
#include <DirectXMath.h>
#include <fstream>
#include <vector>
#include <wrl.h>
#include "Camera.h"
#include "GpuResourceUtils.h"
#include "Misc.h"

// スプライト
class Sprite
{
public:
	Sprite(ID3D11Device* device);
	Sprite(ID3D11Device* device, const char* filename);

	// 頂点データ
	struct Vertex
	{
		DirectX::XMFLOAT3	position;
		DirectX::XMFLOAT4	color;
		DirectX::XMFLOAT2	texcoord;
	};

	// Data structure for batching multiple 3D sprites in one draw call
	struct Sprite3DBatchData
	{
		float wx, wy, wz;                   // World Position
		float w, h;                         // Size
		float sx, sy, sw, sh;               // UV Slicing (Set sw/sh to 0 to use full image)
		float pitch, yaw, roll;             // Rotation
		float r, g, b, a;                   // Color
	};

	// 描画実行 (Screen Space)
	void Render(ID3D11DeviceContext* dc,
		float dx, float dy,					// 左上位置
		float dz,							// 奥行
		float dw, float dh,					// 幅、高さ
		float sx, float sy,					// 画像切り抜き位置
		float sw, float sh,					// 画像切り抜きサイズ
		float angle,						// 角度
		float r, float g, float b, float a	// 色
	) const;

	// 描画実行（World Space）
	void Render(ID3D11DeviceContext* dc,
		const Camera* camera,
		float wx, float wy, float wz,       // ワールド座標位置
		float w, float h,                   // 幅、高さ
		float pitch, float yaw, float roll, // 回転（ピッチ、ヨー、ロール）
		float r, float g, float b, float a  // 色
	) const;

	// 描画実行（World Space、UV切り抜き指定あり）
	void Render3D(ID3D11DeviceContext* dc,
		const Camera* camera,
		float wx, float wy, float wz,       // ワールド座標位置
		float w, float h,                   // 幅、高さ
		float sx, float sy,                 // UV Start (Pixel)
		float sw, float sh,                 // UV Size (Pixel)
		float pitch, float yaw, float roll, // 回転
		float r, float g, float b, float a  // 色
	) const;

	// 描画実行（テクスチャ切り抜き指定なし）
	void Render(ID3D11DeviceContext* dc,
		float dx, float dy,					// 左上位置
		float dz,							// 奥行
		float dw, float dh,					// 幅、高さ
		float angle,						// 角度
		float r, float g, float b, float a	// 色
	) const;

	void Render3DBatch(ID3D11DeviceContext* dc,
		const Camera* camera,
		const std::vector<Sprite3DBatchData>& batchData) const;

	void BindRenderState(ID3D11DeviceContext* dc) const;

private:
	Microsoft::WRL::ComPtr<ID3D11VertexShader>			vertexShader;
	Microsoft::WRL::ComPtr<ID3D11PixelShader>			pixelShader;
	Microsoft::WRL::ComPtr<ID3D11InputLayout>			inputLayout;

	Microsoft::WRL::ComPtr<ID3D11Buffer>				vertexBuffer;
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>	shaderResourceView;

	Microsoft::WRL::ComPtr<ID3D11BlendState> m_blendState;

	float textureWidth = 0;
	float textureHeight = 0;
};
