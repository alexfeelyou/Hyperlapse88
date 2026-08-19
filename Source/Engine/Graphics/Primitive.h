#pragma once
#include <d3d11.h>
#include <DirectXMath.h>
#include <vector>
#include <memory>
#include <wrl/client.h>

using VECTOR2 = DirectX::XMFLOAT2;
using VECTOR3 = DirectX::XMFLOAT3;
using VECTOR4 = DirectX::XMFLOAT4;

class Primitive
{
public:
    Primitive(ID3D11Device* device);
    ~Primitive();

    // Draw rectangle (Float)
    void Rect(float x, float y, float w, float h,
        float cx, float cy, float angle,
        float r, float g, float b, float a);

    // Draw rectangle (Vector)
    void Rect(const VECTOR2& position, const VECTOR2& size,
        const VECTOR2& center, float angle,
        const VECTOR4& color);

    // Draw line
    void Line(float x1, float y1, float x2, float y2,
        float width, float r, float g, float b, float a);

    // Draw circle
    void Circle(float x, float y, float radius,
        float r, float g, float b, float a, int segments = 32);

    // Draw triangle
    void Triangle(float x1, float y1, float x2, float y2, float x3, float y3,
        float r, float g, float b, float a);

    void Render(ID3D11DeviceContext* context);

private:
    struct Vertex
    {
        VECTOR3 position;
        VECTOR4 color;
    };

    // Low-level function (Internal)
    void DrawRectInternal(const VECTOR2& pos, const VECTOR2& size, const VECTOR2& center, float angle, const VECTOR4& color);

    // DirectX Resources
    Microsoft::WRL::ComPtr<ID3D11VertexShader> vertexShader;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> pixelShader;
    Microsoft::WRL::ComPtr<ID3D11InputLayout> inputLayout;
    Microsoft::WRL::ComPtr<ID3D11Buffer> vertexBuffer;
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> rasterizerState;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilState> depthStencilState;

    // Batching System 
    std::vector<Vertex> batchVertices;
    const size_t MAX_VERTICES = 2048; // Buffer size
};