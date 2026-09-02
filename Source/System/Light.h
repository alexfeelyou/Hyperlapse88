#pragma once

#include <DirectXMath.h>

struct DirectionalLight
{
	DirectX::XMFLOAT3	direction = { 0.0f, -0.707f, -0.707f };
	DirectX::XMFLOAT3	color = { 1.0f, 1.0f, 1.0f };
};

class LightManager
{
public:
	// ディレクショナルライト設定
	void SetDirectionalLight(DirectionalLight& light) { directionalLight = light; }

	// ディレクショナルライト取得
	const DirectionalLight& GetDirectionalLight() const { return directionalLight; }

private:
	DirectionalLight	directionalLight;
};
