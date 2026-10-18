#pragma once

#include <d3d11.h>
#include <wrl/client.h>
#include <DirectXMath.h>
#include "Camera.h"

class SkyboxRenderer final
{
public:
    SkyboxRenderer() = default;
    ~SkyboxRenderer() = default;

    SkyboxRenderer(const SkyboxRenderer&) = delete;
    SkyboxRenderer& operator=(const SkyboxRenderer&) = delete;

    void Initialize(ID3D11Device* device);
    void Render(ID3D11DeviceContext* context, const Camera& camera, ID3D11ShaderResourceView* skyboxSRV) const noexcept;

private:
    struct alignas(16) SkymapConstants
    {
        DirectX::XMFLOAT4X4 inverseViewProjection{};
        DirectX::XMFLOAT4   cameraPosition{};
        DirectX::XMFLOAT4   sunDirectionAndCosRadius{ 0.0f, 0.707f, 0.707f, 0.9992f };
        DirectX::XMFLOAT4   sunColorAndCorona{ 1.0f, 0.9f, 0.7f, 1.5f };
    };
    static_assert((sizeof(SkymapConstants) % 16) == 0, "SkymapConstants must be 16-byte aligned!");

    struct Vertex { DirectX::XMFLOAT3 position; };

    Microsoft::WRL::ComPtr<ID3D11VertexShader> m_vertexShader{};
    Microsoft::WRL::ComPtr<ID3D11PixelShader> m_pixelShader{};
    Microsoft::WRL::ComPtr<ID3D11InputLayout> m_inputLayout{};
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_vertexBuffer{};
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_constantBuffer{};
    Microsoft::WRL::ComPtr<ID3D11DepthStencilState> m_depthStencilState{};
    Microsoft::WRL::ComPtr<ID3D11SamplerState> m_samplerState{};
};