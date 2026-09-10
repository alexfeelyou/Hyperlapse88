#include "ProfilerManager.h"
#include "ShapeRenderer.h"

// コンストラクタ
ShapeRenderer::ShapeRenderer(ID3D11Device* device)
{
	// 入力レイアウト
	D3D11_INPUT_ELEMENT_DESC inputElementDesc[] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
	};

	// 頂点シェーダー
	GpuResourceUtils::LoadVertexShader(
		device,
		"Data/Shader/ShapeRendererVS.cso",
		inputElementDesc,
		_countof(inputElementDesc),
		inputLayout.GetAddressOf(),
		vertexShader.GetAddressOf());

	// ピクセルシェーダー
	GpuResourceUtils::LoadPixelShader(
		device,
		"Data/Shader/ShapeRendererPS.cso",
		pixelShader.GetAddressOf());

	// 定数バッファ
	GpuResourceUtils::CreateConstantBuffer(
		device,
		sizeof(CbMesh),
		constantBuffer.GetAddressOf());

	// 箱メッシュ生成
	CreateBoxMesh(device, 1.0f, 1.0f, 1.0f);

	// 球メッシュ生成
	CreateSphereMesh(device, 1.0f, 32);

	// 半球メッシュ生成
	CreateHalfSphereMesh(device, 1.0f, 32);

	// 円柱メッシュ生成
	CreateCylinderMesh(device, 1.0f, 1.0f, -0.5f, 1.0f, 32);

	// 骨メッシュ生成
	CreateBoneMesh(device, 1.0f);
}

// 箱描画
void ShapeRenderer::DrawBox(
	const DirectX::XMFLOAT3& position,
	const DirectX::XMFLOAT3& angle,
	const DirectX::XMFLOAT3& size,
	const DirectX::XMFLOAT4& color)
{
	Instance& instance = instances.emplace_back();
	instance.mesh = &boxMesh;
	instance.color = color;

	DirectX::XMMATRIX S = DirectX::XMMatrixScaling(size.x, size.y, size.z);
	DirectX::XMMATRIX R = DirectX::XMMatrixRotationRollPitchYaw(angle.x, angle.y, angle.z);
	DirectX::XMMATRIX T = DirectX::XMMatrixTranslation(position.x, position.y, position.z);
	DirectX::XMStoreFloat4x4(&instance.worldTransform, S * R * T);
}

// 球描画
void ShapeRenderer::DrawSphere(
	const DirectX::XMFLOAT3& position,
	float radius,
	const DirectX::XMFLOAT4& color)
{
	Instance& instance = instances.emplace_back();
	instance.mesh = &sphereMesh;
	instance.color = color;

	DirectX::XMMATRIX S = DirectX::XMMatrixScaling(radius, radius, radius);
	DirectX::XMMATRIX T = DirectX::XMMatrixTranslation(position.x, position.y, position.z);
	DirectX::XMStoreFloat4x4(&instance.worldTransform, S * T);
}

// カプセル描画
void ShapeRenderer::DrawCapsule(const DirectX::XMFLOAT4X4& transform, float radius, float height, const DirectX::XMFLOAT4& color)
{
	DirectX::XMMATRIX Transform = DirectX::XMLoadFloat4x4(&transform);

	// Extract pure Rotation and Scale to prevent shearing
	DirectX::XMMATRIX RotScale = Transform;
	RotScale.r[3] = DirectX::XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);

	// Top Hemisphere
	{
		Instance& instance = instances.emplace_back();
		instance.mesh = &halfSphereMesh;

		DirectX::XMVECTOR Offset = DirectX::XMVectorSet(0.0f, height * 0.5f, 0.0f, 0.0f);
		DirectX::XMVECTOR Position = DirectX::XMVectorAdd(Transform.r[3], DirectX::XMVector3TransformNormal(Offset, RotScale));

		DirectX::XMMATRIX LocalScale = DirectX::XMMatrixScaling(radius, radius, radius);
		DirectX::XMMATRIX World = LocalScale * RotScale;
		World.r[3] = DirectX::XMVectorSetW(Position, 1.0f);

		DirectX::XMStoreFloat4x4(&instance.worldTransform, World);
		instance.color = color;
	}

	// Cylinder Body
	{
		Instance& instance = instances.emplace_back();
		instance.mesh = &cylinderMesh;

		DirectX::XMMATRIX LocalScale = DirectX::XMMatrixScaling(radius, height, radius);
		DirectX::XMMATRIX World = LocalScale * RotScale;
		World.r[3] = Transform.r[3];

		DirectX::XMStoreFloat4x4(&instance.worldTransform, World);
		instance.color = color;
	}

	// Bottom Hemisphere
	{
		Instance& instance = instances.emplace_back();
		instance.mesh = &halfSphereMesh;

		DirectX::XMVECTOR Offset = DirectX::XMVectorSet(0.0f, -height * 0.5f, 0.0f, 0.0f);
		DirectX::XMVECTOR Position = DirectX::XMVectorAdd(Transform.r[3], DirectX::XMVector3TransformNormal(Offset, RotScale));

		DirectX::XMMATRIX LocalRot = DirectX::XMMatrixRotationX(DirectX::XM_PI);
		DirectX::XMMATRIX LocalScale = DirectX::XMMatrixScaling(radius, radius, radius);
		DirectX::XMMATRIX World = LocalRot * LocalScale * RotScale;
		World.r[3] = DirectX::XMVectorSetW(Position, 1.0f);

		DirectX::XMStoreFloat4x4(&instance.worldTransform, World);
		instance.color = color;
	}
}

// 骨描画
void ShapeRenderer::DrawBone(
	const DirectX::XMFLOAT4X4& transform,
	float length,
	const DirectX::XMFLOAT4& color)
{
	Instance& instance = instances.emplace_back();
	instance.mesh = &boneMesh;
	instance.color = color;

	DirectX::XMMATRIX W = DirectX::XMLoadFloat4x4(&transform);
	W.r[0] = DirectX::XMVectorScale(DirectX::XMVector3Normalize(W.r[0]), length);
	W.r[1] = DirectX::XMVectorScale(DirectX::XMVector3Normalize(W.r[1]), length);
	W.r[2] = DirectX::XMVectorScale(DirectX::XMVector3Normalize(W.r[2]), length);
	DirectX::XMStoreFloat4x4(&instance.worldTransform, W);
}

// メッシュ生成
void ShapeRenderer::CreateMesh(ID3D11Device* device, const std::vector<DirectX::XMFLOAT3>& vertices, Mesh& mesh)
{
	D3D11_BUFFER_DESC desc = {};
	desc.ByteWidth = static_cast<UINT>(sizeof(DirectX::XMFLOAT3) * vertices.size());
	desc.Usage = D3D11_USAGE_IMMUTABLE;
	desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	desc.CPUAccessFlags = 0;
	desc.MiscFlags = 0;
	desc.StructureByteStride = 0;
	D3D11_SUBRESOURCE_DATA subresourceData = {};
	subresourceData.pSysMem = vertices.data();
	subresourceData.SysMemPitch = 0;
	subresourceData.SysMemSlicePitch = 0;

	HRESULT hr = device->CreateBuffer(&desc, &subresourceData, mesh.vertexBuffer.GetAddressOf());
	_ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));

	mesh.vertexCount = static_cast<UINT>(vertices.size());
}

// 箱メッシュ作成
void ShapeRenderer::CreateBoxMesh(ID3D11Device* device, float width, float height, float depth)
{
	DirectX::XMFLOAT3 positions[8] =
	{
		// top
		{ -width,  height, -depth},
		{  width,  height, -depth},
		{  width,  height,  depth},
		{ -width,  height,  depth},
		// bottom
		{ -width, -height, -depth},
		{  width, -height, -depth},
		{  width, -height,  depth},
		{ -width, -height,  depth},
	};

	std::vector<DirectX::XMFLOAT3> vertices;
	vertices.resize(32);

	// top
	vertices.emplace_back(positions[0]);
	vertices.emplace_back(positions[1]);
	vertices.emplace_back(positions[1]);
	vertices.emplace_back(positions[2]);
	vertices.emplace_back(positions[2]);
	vertices.emplace_back(positions[3]);
	vertices.emplace_back(positions[3]);
	vertices.emplace_back(positions[0]);
	// bottom
	vertices.emplace_back(positions[4]);
	vertices.emplace_back(positions[5]);
	vertices.emplace_back(positions[5]);
	vertices.emplace_back(positions[6]);
	vertices.emplace_back(positions[6]);
	vertices.emplace_back(positions[7]);
	vertices.emplace_back(positions[7]);
	vertices.emplace_back(positions[4]);
	// side
	vertices.emplace_back(positions[0]);
	vertices.emplace_back(positions[4]);
	vertices.emplace_back(positions[1]);
	vertices.emplace_back(positions[5]);
	vertices.emplace_back(positions[2]);
	vertices.emplace_back(positions[6]);
	vertices.emplace_back(positions[3]);
	vertices.emplace_back(positions[7]);

	// メッシュ生成
	CreateMesh(device, vertices, boxMesh);
}

// 球メッシュ作成
void ShapeRenderer::CreateSphereMesh(ID3D11Device* device, float radius, int subdivisions)
{
	float step = DirectX::XM_2PI / subdivisions;

	std::vector<DirectX::XMFLOAT3> vertices;

	// XZ平面
	for (int i = 0; i < subdivisions; ++i)
	{
		for (int j = 0; j < 2; ++j)
		{
			float theta = step * ((i + j) % subdivisions);

			DirectX::XMFLOAT3& p = vertices.emplace_back();
			p.x = sinf(theta) * radius;
			p.y = 0.0f;
			p.z = cosf(theta) * radius;
		}
	}
	// XY平面
	for (int i = 0; i < subdivisions; ++i)
	{
		for (int j = 0; j < 2; ++j)
		{
			float theta = step * ((i + j) % subdivisions);

			DirectX::XMFLOAT3& p = vertices.emplace_back();
			p.x = sinf(theta) * radius;
			p.y = cosf(theta) * radius;
			p.z = 0.0f;
		}
	}
	// YZ平面
	for (int i = 0; i < subdivisions; ++i)
	{
		for (int j = 0; j < 2; ++j)
		{
			float theta = step * ((i + j) % subdivisions);

			DirectX::XMFLOAT3& p = vertices.emplace_back();
			p.x = 0.0f;
			p.y = sinf(theta) * radius;
			p.z = cosf(theta) * radius;
		}
	}

	// メッシュ生成
	CreateMesh(device, vertices, sphereMesh);
}

// 半球メッシュ作成
void ShapeRenderer::CreateHalfSphereMesh(ID3D11Device* device, float radius, int subdivisions)
{
	std::vector<DirectX::XMFLOAT3> vertices;

	float theta_step = DirectX::XM_2PI / subdivisions;

	// XZ平面
	for (int i = 0; i < subdivisions; ++i)
	{
		for (int j = 0; j < 2; ++j)
		{
			float theta = theta_step * ((i + j) % subdivisions);

			DirectX::XMFLOAT3& v = vertices.emplace_back();

			v.x = sinf(theta) * radius;
			v.y = 0.0f;
			v.z = cosf(theta) * radius;
		}
	}
	// XY平面
	for (int i = 0; i < subdivisions / 2; ++i)
	{
		for (int j = 0; j < 2; ++j)
		{
			float theta = theta_step * ((i + j) % subdivisions) - DirectX::XM_PIDIV2;

			DirectX::XMFLOAT3& v = vertices.emplace_back();

			v.x = sinf(theta) * radius;
			v.y = cosf(theta) * radius;
			v.z = 0.0f;
		}
	}
	// YZ平面
	for (int i = 0; i < subdivisions / 2; ++i)
	{
		for (int j = 0; j < 2; ++j)
		{
			float theta = theta_step * ((i + j) % subdivisions);

			DirectX::XMFLOAT3& v = vertices.emplace_back();

			v.x = 0.0f;
			v.y = sinf(theta) * radius;
			v.z = cosf(theta) * radius;
		}
	}

	// メッシュ生成
	CreateMesh(device, vertices, halfSphereMesh);
}

// 円柱
void ShapeRenderer::CreateCylinderMesh(ID3D11Device* device, float radius1, float radius2, float start, float height, int subdivisions)
{
	std::vector<DirectX::XMFLOAT3> vertices;

	float theta_step = DirectX::XM_2PI / subdivisions;

	// XZ平面
	for (int i = 0; i < subdivisions; ++i)
	{
		for (int j = 0; j < 2; ++j)
		{
			float theta = theta_step * ((i + j) % subdivisions);

			DirectX::XMFLOAT3& v = vertices.emplace_back();

			v.x = sinf(theta) * radius1;
			v.y = start;
			v.z = cosf(theta) * radius1;
		}
	}
	for (int i = 0; i < subdivisions; ++i)
	{
		for (int j = 0; j < 2; ++j)
		{
			float theta = theta_step * ((i + j) % subdivisions);

			DirectX::XMFLOAT3& v = vertices.emplace_back();

			v.x = sinf(theta) * radius2;
			v.y = start + height;
			v.z = cosf(theta) * radius2;
		}
	}
	// XY平面
	{
		vertices.emplace_back(DirectX::XMFLOAT3(0.0f, start, radius1));
		vertices.emplace_back(DirectX::XMFLOAT3(0.0f, start + height, radius2));
		vertices.emplace_back(DirectX::XMFLOAT3(0.0f, start, -radius1));
		vertices.emplace_back(DirectX::XMFLOAT3(0.0f, start + height, -radius2));
	}
	// YZ平面
	{
		vertices.emplace_back(DirectX::XMFLOAT3(radius1, start, 0.0f));
		vertices.emplace_back(DirectX::XMFLOAT3(radius2, start + height, 0.0f));
		vertices.emplace_back(DirectX::XMFLOAT3(-radius1, start, 0.0f));
		vertices.emplace_back(DirectX::XMFLOAT3(-radius2, start + height, 0.0f));
	}

	// メッシュ生成
	CreateMesh(device, vertices, cylinderMesh);
}

// 骨メッシュ作成
void ShapeRenderer::CreateBoneMesh(ID3D11Device* device, float length)
{
	float width = length * 0.25f;
	DirectX::XMFLOAT3 positions[8] =
	{
		{ -0.00f,  0.00f,  0.00f},
		{  width,  0.00f,  width},
		{  0.00f,  0.00f,  length},
		{ -width,  0.00f,  width},
		{  0.00f,  width,  width},
		{  0.00f, -width,  width},
	};

	std::vector<DirectX::XMFLOAT3> vertices;
	vertices.reserve(24);

	// xz
	vertices.emplace_back(positions[0]);
	vertices.emplace_back(positions[1]);
	vertices.emplace_back(positions[1]);
	vertices.emplace_back(positions[2]);
	vertices.emplace_back(positions[2]);
	vertices.emplace_back(positions[3]);
	vertices.emplace_back(positions[3]);
	vertices.emplace_back(positions[0]);
	// yz
	vertices.emplace_back(positions[0]);
	vertices.emplace_back(positions[4]);
	vertices.emplace_back(positions[4]);
	vertices.emplace_back(positions[2]);
	vertices.emplace_back(positions[2]);
	vertices.emplace_back(positions[5]);
	vertices.emplace_back(positions[5]);
	vertices.emplace_back(positions[0]);
	// xy
	vertices.emplace_back(positions[1]);
	vertices.emplace_back(positions[4]);
	vertices.emplace_back(positions[4]);
	vertices.emplace_back(positions[3]);
	vertices.emplace_back(positions[3]);
	vertices.emplace_back(positions[5]);
	vertices.emplace_back(positions[5]);
	vertices.emplace_back(positions[1]);

	// メッシュ生成
	CreateMesh(device, vertices, boneMesh);
}

// 線描画
void ShapeRenderer::DrawLine(const DirectX::XMFLOAT3& start, const DirectX::XMFLOAT3& end, const DirectX::XMFLOAT4& color)
{
	// Draws a 3D line using a stretched box primitive.
	DirectX::XMVECTOR vStart{ DirectX::XMLoadFloat3(&start) };
	DirectX::XMVECTOR vEnd{ DirectX::XMLoadFloat3(&end) };
	DirectX::XMVECTOR vDir{ DirectX::XMVectorSubtract(vEnd, vStart) };
	DirectX::XMVECTOR vLen{ DirectX::XMVector3Length(vDir) };

	const float length{ DirectX::XMVectorGetX(vLen) };
	if (length < 0.0001f) return; // Prevent division by zero

	vDir = DirectX::XMVectorScale(vDir, 1.0f / length);
	DirectX::XMFLOAT3 dir{};
	DirectX::XMStoreFloat3(&dir, vDir);

	// Decompose direction into pitch and yaw
	const float pitch{ asinf(-dir.y) };
	const float yaw{ atan2f(dir.x, dir.z) };

	DirectX::XMFLOAT3 mid{};
	DirectX::XMStoreFloat3(&mid, DirectX::XMVectorScale(DirectX::XMVectorAdd(vStart, vEnd), 0.5f));

	// Draw a box that is 0.01 units thick, scaled to exactly the distance between A and B
	DrawBox(mid, { pitch, yaw, 0.0f }, { 0.01f, 0.01f, length * 0.5f }, color);
}

// フラスタム描画
void ShapeRenderer::DrawFrustum(
	const DirectX::XMFLOAT3& position,
	const DirectX::XMFLOAT3& rotation,
	float fovY,
	float aspectRatio,
	float nearZ,
	float farZ,
	const DirectX::XMFLOAT4& color,
	float gizmoDrawDistance)
{
	// Visualize out to whichever is smaller: the camera's real far clip, or the
	// requested gizmo distance. Prevents a far=1000 camera from drawing a frustum
	// that swallows the entire scene view.
	const float visualFarZ{ (std::min)(farZ, gizmoDrawDistance) };

	// Half-extents of the near/far planes, derived from vertical FOV and aspect ratio
	const float tanHalfFovY{ tanf(fovY * 0.5f) };
	const float nearHalfHeight{ tanHalfFovY * nearZ };
	const float nearHalfWidth{ nearHalfHeight * aspectRatio };
	const float farHalfHeight{ tanHalfFovY * visualFarZ };
	const float farHalfWidth{ farHalfHeight * aspectRatio };

	// 8 corners in the camera's local (view) space - near plane first, then far plane,
	// each wound top-left, top-right, bottom-right, bottom-left
	const DirectX::XMFLOAT3 localCorners[8]
	{
		{ -nearHalfWidth,  nearHalfHeight, nearZ }, {  nearHalfWidth,  nearHalfHeight, nearZ },
		{  nearHalfWidth, -nearHalfHeight, nearZ }, { -nearHalfWidth, -nearHalfHeight, nearZ },
		{ -farHalfWidth,   farHalfHeight,  visualFarZ }, {  farHalfWidth,   farHalfHeight,  visualFarZ },
		{  farHalfWidth,  -farHalfHeight,  visualFarZ }, { -farHalfWidth,  -farHalfHeight,  visualFarZ },
	};

	// Transform every corner from local view space into world space using the camera's pose
	const DirectX::XMMATRIX rotationMatrix{ DirectX::XMMatrixRotationRollPitchYaw(rotation.x, rotation.y, rotation.z) };
	const DirectX::XMVECTOR positionVector{ DirectX::XMLoadFloat3(&position) };

	DirectX::XMFLOAT3 worldCorners[8]{};
	for (int i{ 0 }; i < 8; ++i)
	{
		const DirectX::XMVECTOR local{ DirectX::XMLoadFloat3(&localCorners[i]) };
		const DirectX::XMVECTOR world{ DirectX::XMVectorAdd(DirectX::XMVector3TransformNormal(local, rotationMatrix), positionVector) };
		DirectX::XMStoreFloat3(&worldCorners[i], world);
	}

	// Near plane rectangle
	DrawLine(worldCorners[0], worldCorners[1], color);
	DrawLine(worldCorners[1], worldCorners[2], color);
	DrawLine(worldCorners[2], worldCorners[3], color);
	DrawLine(worldCorners[3], worldCorners[0], color);

	// Far plane rectangle
	DrawLine(worldCorners[4], worldCorners[5], color);
	DrawLine(worldCorners[5], worldCorners[6], color);
	DrawLine(worldCorners[6], worldCorners[7], color);
	DrawLine(worldCorners[7], worldCorners[4], color);

	// Connecting edges between near and far planes
	DrawLine(worldCorners[0], worldCorners[4], color);
	DrawLine(worldCorners[1], worldCorners[5], color);
	DrawLine(worldCorners[2], worldCorners[6], color);
	DrawLine(worldCorners[3], worldCorners[7], color);
}

// 描画実行
void ShapeRenderer::Render(
	ID3D11DeviceContext* dc,
	const DirectX::XMFLOAT4X4& view,
	const DirectX::XMFLOAT4X4& projection)
{
	// シェーダー設定
	dc->VSSetShader(vertexShader.Get(), nullptr, 0);
	dc->PSSetShader(pixelShader.Get(), nullptr, 0);
	dc->IASetInputLayout(inputLayout.Get());

	// 定数バッファ設定
	dc->VSSetConstantBuffers(0, 1, constantBuffer.GetAddressOf());

	// ビュープロジェクション行列作成
	DirectX::XMMATRIX V = DirectX::XMLoadFloat4x4(&view);
	DirectX::XMMATRIX P = DirectX::XMLoadFloat4x4(&projection);
	DirectX::XMMATRIX VP = V * P;

	// プリミティブ設定
	UINT stride = sizeof(DirectX::XMFLOAT3);
	UINT offset = 0;
	dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);

	for (const Instance& instance : instances)
	{
		// 頂点バッファ設定
		dc->IASetVertexBuffers(0, 1, instance.mesh->vertexBuffer.GetAddressOf(), &stride, &offset);

		// ワールドビュープロジェクション行列作成
		DirectX::XMMATRIX W = DirectX::XMLoadFloat4x4(&instance.worldTransform);
		DirectX::XMMATRIX WVP = W * VP;

		// 定数バッファ更新
		CbMesh cbMesh;
		DirectX::XMStoreFloat4x4(&cbMesh.worldViewProjection, WVP);
		cbMesh.color = instance.color;

		dc->UpdateSubresource(constantBuffer.Get(), 0, 0, &cbMesh, 0, 0);

		// 描画
		dc->Draw(instance.mesh->vertexCount, 0);
		PROFILE_DRAW_CALL();   // instances のループ1回転ごとに1回のDrawコール
	}
	instances.clear();
}
