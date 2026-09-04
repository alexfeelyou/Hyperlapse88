#pragma once

#include <algorithm>
#include <d3d11.h>
#include <DirectXMath.h>
#include <memory>
#include <vector>
#include <wrl.h>
#include "BasicShader.h"
#include "GpuResourceUtils.h"
#include "LambertShader.h"
#include "Misc.h"
#include "Model.h"
#include "PbrShader.h"
#include "PhongShader.h"
#include "Shader.h"
#include "ToonShader.h"

enum class ShaderId
{
    Basic,
    Lambert,
    Phong,
    Pbr,
    Toon, 

    EnumCount
};

class ModelRenderer
{
public:
    ModelRenderer(ID3D11Device* device);
    ~ModelRenderer() {}

    void Draw(std::shared_ptr<Model> model, const DirectX::XMFLOAT4& color = { 1.0f, 1.0f, 1.0f, 1.0f });

    void Draw(std::shared_ptr<Model> model, DirectX::XMFLOAT4 color, const DirectX::XMFLOAT4X4& worldMatrix);

    // ï`âÊé¿çs
    void Render(const RenderContext& rc);

private:
    struct MeshDrawCommand
    {
        const Model::Mesh* mesh{};
        DirectX::XMFLOAT4   color{};
        bool                useManualMatrix{ false };
        DirectX::XMFLOAT4X4 worldMatrix{};
    };

    struct CbScene
    {
        DirectX::XMFLOAT4X4 viewProjection{};       // 64 bytes
        DirectX::XMFLOAT4   lightDirection{};       // 16 bytes
        DirectX::XMFLOAT4   lightColor{};           // 16 bytes
        DirectX::XMFLOAT4   cameraPosition{};       // 16 bytes
        DirectX::XMFLOAT4   ambientSkyColor{};      // 16 bytes
        DirectX::XMFLOAT4   ambientGroundColor{};   // 16 bytes
        DirectX::XMFLOAT4   packedParams{};         // 16 bytes (psxEnabled, psxResW, psxResH, padding)
        DirectX::XMINT4     lightCounts{};          // 16 bytes (pointCount, spotCount, padding, padding)

        PointLightData      pointLights[8]{};       // 256 bytes
        SpotLightData       spotLights[8]{};        // 384 bytes
    };
    static_assert((sizeof(CbScene) % 16) == 0, "CbScene constant buffer must be 16-byte aligned!");

    struct CbSkeleton
    {
        DirectX::XMFLOAT4X4		boneTransforms[256];
    };

    struct CbObject
    {
        DirectX::XMFLOAT4       color;
    };

    struct DrawInfo
    {
        std::shared_ptr<Model>	model{};
        DirectX::XMFLOAT4       color{};
        bool                    useManualMatrix{ false };
        DirectX::XMFLOAT4X4     worldMatrix{};
    };

    struct TransparencyDrawInfo
    {
        ShaderId				shaderId;
        const Model::Mesh* mesh;
        float					distance;
        DirectX::XMFLOAT4       color; 

        bool                    useManualMatrix = false;
        DirectX::XMFLOAT4X4     worldMatrix;
    };

    std::unique_ptr<Shader>					shaders[static_cast<int>(ShaderId::EnumCount)];
    std::vector<DrawInfo>					drawInfos;
    std::vector<TransparencyDrawInfo>		transparencyDrawInfos;

    Microsoft::WRL::ComPtr<ID3D11Buffer>	sceneConstantBuffer;
    Microsoft::WRL::ComPtr<ID3D11Buffer>	skeletonConstantBuffer;
    Microsoft::WRL::ComPtr<ID3D11Buffer>	objectConstantBuffer;
};