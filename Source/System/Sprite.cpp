#include <fstream>
#include "Sprite.h"
#include "Misc.h"
#include "GpuResourceUtils.h"
#include <vector>

// コンストラクタ
Sprite::Sprite(ID3D11Device* device)
	: Sprite(device, nullptr)
{
}

// コンストラクタ
Sprite::Sprite(ID3D11Device* device, const char* filename)
{
	HRESULT hr = S_OK;

	// 頂点バッファの生成
	{
		// 頂点バッファを作成するための設定オプション
		const int MAX_VERTICES = 10000;
		D3D11_BUFFER_DESC buffer_desc = {};
		buffer_desc.ByteWidth = sizeof(Vertex) * MAX_VERTICES;
		buffer_desc.Usage = D3D11_USAGE_DYNAMIC;
		buffer_desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
		buffer_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		buffer_desc.MiscFlags = 0;
		buffer_desc.StructureByteStride = 0;
		// 頂点バッファオブジェクトの生成
		hr = device->CreateBuffer(&buffer_desc, nullptr, vertexBuffer.GetAddressOf());
		_ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));
	}

	// 頂点シェーダー
	{
		// 入力レイアウト
		D3D11_INPUT_ELEMENT_DESC inputElementDesc[] =
		{
			{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		};
		hr = GpuResourceUtils::LoadVertexShader(
			device,
			"Data/Shader/SpriteVS.cso",
			inputElementDesc,
			ARRAYSIZE(inputElementDesc),
			inputLayout.GetAddressOf(),
			vertexShader.GetAddressOf());
		_ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));

	}

	// ピクセルシェーダー
	{
		hr = GpuResourceUtils::LoadPixelShader(
			device,
			"Data/Shader/SpritePS.cso",
			pixelShader.GetAddressOf());
		_ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));
	}

	// テクスチャの生成	
	if (filename != nullptr)
	{
		// テクスチャファイル読み込み
		D3D11_TEXTURE2D_DESC desc;
		hr = GpuResourceUtils::LoadTexture(device, filename, shaderResourceView.GetAddressOf(), &desc);
		_ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));

		textureWidth = static_cast<float>(desc.Width);
		textureHeight = static_cast<float>(desc.Height);
	}
	else
	{
		// ダミーテクスチャ生成
		D3D11_TEXTURE2D_DESC desc;
		hr = GpuResourceUtils::CreateDummyTexture(device, 0xFFFFFFFF, shaderResourceView.GetAddressOf(),
			&desc);
		_ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));

		textureWidth = static_cast<float>(desc.Width);
		textureHeight = static_cast<float>(desc.Height);
	}
}

// 描画実行
void Sprite::Render(ID3D11DeviceContext* dc,
	float dx, float dy,					// 左上位置
	float dz,							// 奥行
	float dw, float dh,					// 幅、高さ
	float sx, float sy,					// 画像切り抜き位置
	float sw, float sh,					// 画像切り抜きサイズ
	float angle,						// 角度
	float r, float g, float b, float a	// 色
	) const
{
	// 頂点座標
	DirectX::XMFLOAT2 positions[] = {
		DirectX::XMFLOAT2(dx,      dy),			// 左上
		DirectX::XMFLOAT2(dx + dw, dy),			// 右上
		DirectX::XMFLOAT2(dx,      dy + dh),	// 左下
		DirectX::XMFLOAT2(dx + dw, dy + dh),	// 右下
	};

	// テクスチャ座標
	DirectX::XMFLOAT2 texcoords[] = {
		DirectX::XMFLOAT2(sx,      sy),			// 左上
		DirectX::XMFLOAT2(sx + sw, sy),			// 右上
		DirectX::XMFLOAT2(sx,      sy + sh),	// 左下
		DirectX::XMFLOAT2(sx + sw, sy + sh),	// 右下
	};

	// スプライトの中心で回転させるために４頂点の中心位置が
	// 原点(0, 0)になるように一旦頂点を移動させる。
	float mx = dx + dw * 0.5f;
	float my = dy + dh * 0.5f;
	for (auto& p : positions)
	{
		p.x -= mx;
		p.y -= my;
	}

	// 頂点を回転させる
	float theta = DirectX::XMConvertToRadians(angle);
	float c = cosf(theta);
	float s = sinf(theta);
	for (auto& p : positions)
	{
		DirectX::XMFLOAT2 r = p;
		p.x = c * r.x + -s * r.y;
		p.y = s * r.x + c * r.y;
	}

	// 回転のために移動させた頂点を元の位置に戻す
	for (auto& p : positions)
	{
		p.x += mx;
		p.y += my;
	}

	// 現在設定されているビューポートからスクリーンサイズを取得する。
	D3D11_VIEWPORT viewport;
	UINT numViewports = 1;
	dc->RSGetViewports(&numViewports, &viewport);
	float screenWidth = viewport.Width;
	float screenHeight = viewport.Height;

	// スクリーン座標系からNDC座標系へ変換する。
	for (DirectX::XMFLOAT2& p : positions)
	{
		p.x = 2.0f * p.x / screenWidth - 1.0f;
		p.y = 1.0f - 2.0f * p.y / screenHeight;
	}

	// 頂点バッファの内容の編集を開始する。
	D3D11_MAPPED_SUBRESOURCE mappedSubresource;
	HRESULT hr = dc->Map(vertexBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedSubresource);
	_ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));

	// 頂点バッファの内容を編集
	Vertex* v = static_cast<Vertex*>(mappedSubresource.pData);
	for (int i = 0; i < 4; ++i)
	{
		v[i].position.x = positions[i].x;
		v[i].position.y = positions[i].y;
		v[i].position.z = dz;

		v[i].color.x = r;
		v[i].color.y = g;
		v[i].color.z = b;
		v[i].color.w = a;

		v[i].texcoord.x = texcoords[i].x / textureWidth;
		v[i].texcoord.y = texcoords[i].y / textureHeight;
	}

	// 頂点バッファの内容の編集を終了する。
	dc->Unmap(vertexBuffer.Get(), 0);

	// GPUに描画するためのデータを渡す
	UINT stride = sizeof(Vertex);
	UINT offset = 0;
	dc->IASetVertexBuffers(0, 1, vertexBuffer.GetAddressOf(), &stride, &offset);
	dc->IASetInputLayout(inputLayout.Get());
	dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
	dc->VSSetShader(vertexShader.Get(), nullptr, 0);
	dc->PSSetShader(pixelShader.Get(), nullptr, 0);
	dc->PSSetShaderResources(0, 1, shaderResourceView.GetAddressOf());

	// 描画
	dc->Draw(4, 0);
}

// 描画実行（テクスチャ切り抜き指定なし）
void Sprite::Render(ID3D11DeviceContext* dc,
	float dx, float dy,					// 左上位置
	float dz,							// 奥行
	float dw, float dh,					// 幅、高さ
	float angle,						// 角度
	float r, float g, float b, float a	// 色
	) const
{
	Render(dc, dx, dy, dz, dw, dh, 0, 0, textureWidth, textureHeight, angle, r, g, b, a);
}

// 描画実行（3D空間にスプライトを描画、細分割バージョン）
void Sprite::Render(ID3D11DeviceContext* dc,
	const Camera* camera,
	float wx, float wy, float wz,
	float w, float h,
	float pitch, float yaw, float roll,
	float r, float g, float b, float a) const
{
	using namespace DirectX;

	// 分割設定
	const int divX = 40;
	const int divY = 40;

	std::vector<Vertex> vertices;
	vertices.reserve(divX * divY * 6);

	// 行列計算
	XMMATRIX matWorld = XMMatrixRotationRollPitchYaw(pitch, yaw, roll) * XMMatrixTranslation(wx, wy, wz);
	XMMATRIX matVP = XMLoadFloat4x4(&camera->GetView()) * XMLoadFloat4x4(&camera->GetProjection());

	float cellW = w / divX;
	float cellH = h / divY;
	float startX = -w / 2.0f;
	float startY = h / 2.0f;

	for (int y = 0; y < divY; ++y)
	{
		for (int x = 0; x < divX; ++x)
		{
			// 座標とUVの計算
			float x0 = startX + x * cellW;
			float y0 = startY - y * cellH;
			float x1 = startX + (x + 1) * cellW;
			float y1 = startY - (y + 1) * cellH;

			float u0 = (float)x / divX;
			float v0 = (float)y / divY;
			float u1 = (float)(x + 1) / divX;
			float v1 = (float)(y + 1) / divY;

			// ローカル頂点定義
			XMFLOAT3 localPos[6] = {
				{ x0, y0, 0 }, { x1, y0, 0 }, { x0, y1, 0 },
				{ x1, y0, 0 }, { x1, y1, 0 }, { x0, y1, 0 }
			};
			XMFLOAT2 localUV[6] = {
				{ u0, v0 }, { u1, v0 }, { u0, v1 },
				{ u1, v0 }, { u1, v1 }, { u0, v1 }
			};

			XMVECTOR clipPos[6];
			bool isValid[6];

			// 座標変換とクリッピング判定
			for (int i = 0; i < 6; ++i)
			{
				XMVECTOR vPos = XMVector3TransformCoord(XMLoadFloat3(&localPos[i]), matWorld);
				clipPos[i] = XMVector3Transform(vPos, matVP); // W成分が必要なためTransformを使用

				// カメラ手前(NearZ)の判定
				isValid[i] = (XMVectorGetW(clipPos[i]) > 0.1f);
			}

			// 三角形生成ラムダ式
			auto AddTriangle = [&](int i0, int i1, int i2)
				{
					if (isValid[i0] && isValid[i1] && isValid[i2])
					{
						int indices[] = { i0, i1, i2 };
						for (int idx : indices)
						{
							XMVECTOR finalPos = clipPos[idx] / XMVectorGetW(clipPos[idx]); // 透視除算
							Vertex vert;
							XMStoreFloat3(&vert.position, finalPos);
							vert.color = XMFLOAT4(r, g, b, a);
							vert.texcoord = localUV[idx];
							vertices.push_back(vert);
						}
					}
				};

			// 2つの三角形を追加
			AddTriangle(0, 1, 2);
			AddTriangle(3, 4, 5);
		}
	}

	if (vertices.empty()) return;

	// GPUバッファ更新
	D3D11_MAPPED_SUBRESOURCE ms;
	if (SUCCEEDED(dc->Map(vertexBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &ms)))
	{
		memcpy(ms.pData, vertices.data(), sizeof(Vertex) * vertices.size());
		dc->Unmap(vertexBuffer.Get(), 0);
	}

	// 描画設定
	UINT stride = sizeof(Vertex);
	UINT offset = 0;
	dc->IASetVertexBuffers(0, 1, vertexBuffer.GetAddressOf(), &stride, &offset);
	dc->IASetInputLayout(inputLayout.Get());
	dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST); // LISTを使用

	dc->VSSetShader(vertexShader.Get(), nullptr, 0);
	dc->PSSetShader(pixelShader.Get(), nullptr, 0);
	dc->PSSetShaderResources(0, 1, shaderResourceView.GetAddressOf());

	dc->Draw(static_cast<UINT>(vertices.size()), 0);
}

void Sprite::Render3D(ID3D11DeviceContext* dc,
	const Camera* camera,
	float wx, float wy, float wz,
	float w, float h,
	float sx, float sy,
	float sw, float sh,
	float pitch, float yaw, float roll,
	float r, float g, float b, float a) const
{
	using namespace DirectX;

	// 1. Setup Data Vertex (Hanya 1 Quad / 4 Titik)
	// Urutan: Kiri-Atas, Kanan-Atas, Kiri-Bawah, Kanan-Bawah (Triangle Strip)
	Vertex vertices[4];

	// Hitung UV dalam 0.0 - 1.0
	float u0 = sx / textureWidth;
	float v0 = sy / textureHeight;
	float u1 = (sx + sw) / textureWidth;
	float v1 = (sy + sh) / textureHeight;

	// Set UV & Warna
	vertices[0].texcoord = { u0, v0 }; vertices[0].color = { r, g, b, a };
	vertices[1].texcoord = { u1, v0 }; vertices[1].color = { r, g, b, a };
	vertices[2].texcoord = { u0, v1 }; vertices[2].color = { r, g, b, a };
	vertices[3].texcoord = { u1, v1 }; vertices[3].color = { r, g, b, a };

	// 2. Hitung Matrix
	// Pivot huruf ada di tengah
	XMMATRIX matWorld = XMMatrixRotationRollPitchYaw(pitch, yaw, roll) * XMMatrixTranslation(wx, wy, wz);
	XMMATRIX matVP = XMLoadFloat4x4(&camera->GetView()) * XMLoadFloat4x4(&camera->GetProjection());

	// 3. Transformasi Vertex Manual (Billboard Logic)
	float halfW = w / 2.0f;
	float halfH = h / 2.0f;

	// Posisi lokal relatif terhadap titik tengah (wx, wy, wz)
	XMFLOAT3 localPos[4] = {
		{ -halfW,  halfH, 0 }, // Kiri Atas
		{  halfW,  halfH, 0 }, // Kanan Atas
		{ -halfW, -halfH, 0 }, // Kiri Bawah
		{  halfW, -halfH, 0 }  // Kanan Bawah
	};

	for (int i = 0; i < 4; ++i)
	{
		// Local -> World
		XMVECTOR vPos = XMLoadFloat3(&localPos[i]);
		vPos = XMVector3TransformCoord(vPos, matWorld);

		// World -> Clip Space (Wajib untuk Shader)
		// Disini kita lakukan trik: Kita kirim posisi World ke Buffer, 
		// tapi Shader VS kamu sepertinya mengharapkan posisi Clip Space (Screen).
		// Karena SpriteVS.cso kamu didesain untuk 2D Screen Space (-1 s/d 1), 
		// kita harus melakukan transformasi VP di CPU sini.

		XMVECTOR vClip = XMVector3Transform(vPos, matVP);

		// Perspective Divide (Penting agar menjadi -1 s/d 1 NDC)
		// PERHATIAN: Shader 2D kamu mungkin tidak handle W component. 
		// Idealnya kamu punya Shader 3D terpisah. 
		// TAPI, agar kompatibel dengan Shader 2D "SpriteVS" yang ada:
		// Kita simpan hasil proyeksi yang sudah dibagi W.

		float vW = XMVectorGetW(vClip);
		if (vW < 0.1f) vW = 0.1f; // Cegah bagi nol

		XMStoreFloat3(&vertices[i].position, vClip / vW);

		// Simpan Z asli untuk Depth Buffer (Optional, tergantung shader)
		// vertices[i].position.z = XMVectorGetZ(vClip) / vW; 
	}

	// 4. Update Buffer & Draw
	D3D11_MAPPED_SUBRESOURCE ms;
	if (SUCCEEDED(dc->Map(vertexBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &ms)))
	{
		memcpy(ms.pData, vertices, sizeof(Vertex) * 4);
		dc->Unmap(vertexBuffer.Get(), 0);
	}

	UINT stride = sizeof(Vertex);
	UINT offset = 0;
	dc->IASetVertexBuffers(0, 1, vertexBuffer.GetAddressOf(), &stride, &offset);
	dc->IASetInputLayout(inputLayout.Get());
	dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

	dc->VSSetShader(vertexShader.Get(), nullptr, 0);
	dc->PSSetShader(pixelShader.Get(), nullptr, 0);
	dc->PSSetShaderResources(0, 1, shaderResourceView.GetAddressOf());

	dc->Draw(4, 0);
}

void Sprite::Render3DBatch(ID3D11DeviceContext* dc,
	const Camera* camera,
	const std::vector<Sprite3DBatchData>& batchData) const
{
	using namespace DirectX; // <--- TAMBAHKAN BARIS INI DI SINI!

	if (batchData.empty()) return;

	std::vector<Vertex> vertices;
	vertices.reserve(batchData.size() * 6); // 1 Kotak = 6 Titik (2 Segitiga)

	XMMATRIX matVP = XMLoadFloat4x4(&camera->GetView()) * XMLoadFloat4x4(&camera->GetProjection());

	for (const auto& data : batchData)
	{
		// Fitur Auto-Full Texture: Jika sw/sh 0, anggap pakai seluruh gambar
		float actualSW = (data.sw <= 0.001f) ? textureWidth : data.sw;
		float actualSH = (data.sh <= 0.001f) ? textureHeight : data.sh;

		// Kalkulasi UV 0.0 -> 1.0
		float u0 = data.sx / textureWidth;
		float v0 = data.sy / textureHeight;
		float u1 = (data.sx + actualSW) / textureWidth;
		float v1 = (data.sy + actualSH) / textureHeight;

		DirectX::XMFLOAT4 color = { data.r, data.g, data.b, data.a };

		XMMATRIX matWorld = XMMatrixRotationRollPitchYaw(data.pitch, data.yaw, data.roll) * XMMatrixTranslation(data.wx, data.wy, data.wz);

		float halfW = data.w / 2.0f;
		float halfH = data.h / 2.0f;

		// Posisi lokal (Kiri-Atas, Kanan-Atas, Kiri-Bawah, Kanan-Bawah)
		DirectX::XMFLOAT3 localPos[4] = {
			{ -halfW,  halfH, 0 },
			{  halfW,  halfH, 0 },
			{ -halfW, -halfH, 0 },
			{  halfW, -halfH, 0 }
		};

		Vertex v[4];
		for (int i = 0; i < 4; ++i)
		{
			XMVECTOR vPos = XMVector3TransformCoord(XMLoadFloat3(&localPos[i]), matWorld);
			XMVECTOR vClip = XMVector3Transform(vPos, matVP);

			float vW = XMVectorGetW(vClip);
			if (vW < 0.1f) vW = 0.1f; // Cegah error dibagi nol

			XMStoreFloat3(&v[i].position, vClip / vW);
			v[i].color = color;
		}

		v[0].texcoord = { u0, v0 };
		v[1].texcoord = { u1, v0 };
		v[2].texcoord = { u0, v1 };
		v[3].texcoord = { u1, v1 };

		// Susun Triangle List (6 Titik)
		vertices.push_back(v[0]); vertices.push_back(v[1]); vertices.push_back(v[2]); // Segitiga Atas
		vertices.push_back(v[1]); vertices.push_back(v[3]); vertices.push_back(v[2]); // Segitiga Bawah
	}

	// --- SATU KALI BUKA VRAM UNTUK SEMUA SAYAP ---
	D3D11_MAPPED_SUBRESOURCE ms;
	if (SUCCEEDED(dc->Map(vertexBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &ms)))
	{
		memcpy(ms.pData, vertices.data(), sizeof(Vertex) * vertices.size());
		dc->Unmap(vertexBuffer.Get(), 0);
	}

	UINT stride = sizeof(Vertex);
	UINT offset = 0;
	dc->IASetVertexBuffers(0, 1, vertexBuffer.GetAddressOf(), &stride, &offset);
	dc->IASetInputLayout(inputLayout.Get());
	dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST); // <--- KUNCI BATCHING

	dc->VSSetShader(vertexShader.Get(), nullptr, 0);
	dc->PSSetShader(pixelShader.Get(), nullptr, 0);
	dc->PSSetShaderResources(0, 1, shaderResourceView.GetAddressOf());

	// --- SATU KALI DRAW CALL UNTUK SEMUA SAYAP ---
	dc->Draw(static_cast<UINT>(vertices.size()), 0);
}