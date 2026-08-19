#pragma once

#include <vector>
#include <wrl.h>
#include <d3d11.h>
#include <DirectXMath.h>

class WindowSwarmRenderer
{
public:
    // Ini data yang sangat ringan! Cuma 36 Byte per jendela.
    // 2000 jendela = cuma ~72 KB memori yang dikirim ke GPU tiap frame. Sangat efisien!
    struct InstanceData
    {
        DirectX::XMFLOAT3 position;
        DirectX::XMFLOAT2 size;
        DirectX::XMFLOAT4 color;
    };

    WindowSwarmRenderer(ID3D11Device* device);

    // Dipanggil 1x setiap awal frame
    void Begin();

    // Dipanggil berulang kali untuk ngumpulin data (CPU Side, Super Cepat)
    void AddWindow(const DirectX::XMFLOAT3& pos, const DirectX::XMFLOAT2& size, const DirectX::XMFLOAT4& color);

    // Tembakkan semua ke GPU dalam 1x Draw Call!
    void Render(ID3D11DeviceContext* dc, const DirectX::XMFLOAT4X4& viewProj, ID3D11ShaderResourceView* textureSRV);

private:
    static const UINT MaxInstances = 2000; // Kapasitas maksimal sayap

    struct QuadVertex {
        DirectX::XMFLOAT3 position;
        DirectX::XMFLOAT2 texcoord;
    };

    struct CbScene {
        DirectX::XMFLOAT4X4 viewProjection;
    };

    std::vector<InstanceData> instances;

    Microsoft::WRL::ComPtr<ID3D11VertexShader>  vertexShader;
    Microsoft::WRL::ComPtr<ID3D11PixelShader>   pixelShader;
    Microsoft::WRL::ComPtr<ID3D11InputLayout>   inputLayout;

    Microsoft::WRL::ComPtr<ID3D11Buffer>        vertexBuffer;   // Statis
    Microsoft::WRL::ComPtr<ID3D11Buffer>        instanceBuffer; // Dinamis
    Microsoft::WRL::ComPtr<ID3D11Buffer>        constantBuffer;
};