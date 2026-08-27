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
#include "PhongShader.h"
#include "Shader.h"

enum class ShaderId
{
    Basic,
    Lambert,
    Phong,

    EnumCount
};

class ModelRenderer
{
public:
    ModelRenderer(ID3D11Device* device);
    ~ModelRenderer() {}

    void Draw(ShaderId shaderId, std::shared_ptr<Model> model, const DirectX::XMFLOAT4& color = { 1.0f, 1.0f, 1.0f, 1.0f });

    void Draw(ShaderId shader, std::shared_ptr<Model> model, DirectX::XMFLOAT4 color, const DirectX::XMFLOAT4X4& worldMatrix);

    // ï`âÊé¿çs
    void Render(const RenderContext& rc);

private:
    struct CbScene
    {
        DirectX::XMFLOAT4X4		viewProjection;
        DirectX::XMFLOAT4		lightDirection;
        DirectX::XMFLOAT4		lightColor;
        DirectX::XMFLOAT4		cameraPosition;
        float                   psxEnabled;
        float                   psxResWidth;
        float                   psxResHeight;
        float                   padding;
    };

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
        ShaderId				shaderId;
        std::shared_ptr<Model>	model;
        DirectX::XMFLOAT4       color; 

        bool                    useManualMatrix = false;
        DirectX::XMFLOAT4X4     worldMatrix;
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