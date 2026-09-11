#pragma once

#include <d3d11.h>
#include <DirectXMath.h>
#include <vector>
#include <wrl.h>
#include "GpuResourceUtils.h"
#include "Misc.h"

// A highly optimized debug renderer utilizing Dynamic Line Batching and 
// Instance State Sorting to minimize GPU state changes and Draw Calls.
class ShapeRenderer
{
public:
    explicit ShapeRenderer(ID3D11Device* device);
    ~ShapeRenderer() = default;

    ShapeRenderer(const ShapeRenderer&) = delete;
    ShapeRenderer& operator=(const ShapeRenderer&) = delete;
    ShapeRenderer(ShapeRenderer&&) = default;
    ShapeRenderer& operator=(ShapeRenderer&&) = default;

    void DrawBox(
        const DirectX::XMFLOAT3& position,
        const DirectX::XMFLOAT3& angle,
        const DirectX::XMFLOAT3& size,
        const DirectX::XMFLOAT4& color);

    void DrawSphere(
        const DirectX::XMFLOAT3& position,
        float radius,
        const DirectX::XMFLOAT4& color);

    void DrawCapsule(
        const DirectX::XMFLOAT4X4& transform,
        float radius,
        float height,
        const DirectX::XMFLOAT4& color);

    void DrawBone(
        const DirectX::XMFLOAT4X4& transform,
        float length,
        const DirectX::XMFLOAT4& color);

    // Uses highly optimized CPU batching instead of individual matrix instances
    void DrawLine(
        const DirectX::XMFLOAT3& start,
        const DirectX::XMFLOAT3& end,
        const DirectX::XMFLOAT4& color);

    void DrawFrustum(
        const DirectX::XMFLOAT3& position,
        const DirectX::XMFLOAT3& rotation,
        float fovY,
        float aspectRatio,
        float nearZ,
        float farZ,
        const DirectX::XMFLOAT4& color,
        float gizmoDrawDistance = 5.0f);

    // Dispatches all batched shapes to the GPU
    void Render(
        ID3D11DeviceContext* dc,
        const DirectX::XMFLOAT4X4& view,
        const DirectX::XMFLOAT4X4& projection);

private:
    struct Mesh
    {
        Microsoft::WRL::ComPtr<ID3D11Buffer> vertexBuffer{};
        UINT                                 vertexCount{ 0 };
    };

    struct Instance
    {
        Mesh* mesh{ nullptr };
        DirectX::XMFLOAT4X4 worldTransform{};
        DirectX::XMFLOAT4   color{};
    };

    // Groups continuous lines by color to draw them in a single batch
    struct LineBatch
    {
        std::vector<DirectX::XMFLOAT3> vertices{};
        DirectX::XMFLOAT4              color{};
    };

    // 16-byte aligned Constant Buffer structure
    struct alignas(16) CbMesh
    {
        DirectX::XMFLOAT4X4 worldViewProjection{};
        DirectX::XMFLOAT4   color{};
    };

    // Initialization Helpers
    void CreateMesh(ID3D11Device* device, const std::vector<DirectX::XMFLOAT3>& vertices, Mesh& mesh);
    void CreateBoxMesh(ID3D11Device* device, float width, float height, float depth);
    void CreateSphereMesh(ID3D11Device* device, float radius, int subdivisions);
    void CreateHalfSphereMesh(ID3D11Device* device, float radius, int subdivisions);
    void CreateCylinderMesh(ID3D11Device* device, float radius1, float radius2, float start, float height, int subdivisions);
    void CreateBoneMesh(ID3D11Device* device, float length);

    // Device caching for dynamic buffer resizing
    Microsoft::WRL::ComPtr<ID3D11Device> m_device{};

    // Solid Primitives
    Mesh                  m_boxMesh{};
    Mesh                  m_sphereMesh{};
    Mesh                  m_halfSphereMesh{};
    Mesh                  m_cylinderMesh{};
    Mesh                  m_boneMesh{};
    std::vector<Instance> m_instances{};

    // Dynamic Line Batching resources
    std::vector<LineBatch>               m_lineBatches{};
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_dynamicLineVB{};
    UINT                                 m_dynamicLineVBCapacity{ 0 };

    // Shaders & States
    Microsoft::WRL::ComPtr<ID3D11VertexShader> m_vertexShader{};
    Microsoft::WRL::ComPtr<ID3D11PixelShader>  m_pixelShader{};
    Microsoft::WRL::ComPtr<ID3D11InputLayout>  m_inputLayout{};
    Microsoft::WRL::ComPtr<ID3D11Buffer>       m_constantBuffer{};
};