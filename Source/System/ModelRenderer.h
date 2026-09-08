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
#include "OutlineShader.h"
#include "Shader.h"
#include "ShadowCasterShader.h"
#include "ToonShader.h"
#include "VelocityShader.h"

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

    void Draw(std::shared_ptr<Model> model, const DirectX::XMFLOAT4& color = { 1.0f, 1.0f, 1.0f, 1.0f }, bool castShadows = true);
    void Draw(std::shared_ptr<Model> model, DirectX::XMFLOAT4 color, const DirectX::XMFLOAT4X4& worldMatrix, bool castShadows = true);
    void Draw(std::shared_ptr<Model> model, DirectX::XMFLOAT4 color,
        const DirectX::XMFLOAT4X4& worldMatrix, const DirectX::XMFLOAT4X4& previousWorldMatrix,
        const std::vector<DirectX::XMFLOAT4X4>* currentNodeGlobals = nullptr,
        const std::vector<DirectX::XMFLOAT4X4>* previousNodeGlobals = nullptr,
        bool castShadows = true);

    // ï`âÊé¿çs
    void Render(const RenderContext& rc);

private:
    void DrawMeshVelocity(
        ID3D11DeviceContext* dc, const Model::Mesh& mesh, bool useManual,
        const DirectX::XMFLOAT4X4& worldMatrix, const DirectX::XMFLOAT4X4& previousWorldMatrix,
        const std::vector<DirectX::XMFLOAT4X4>* currentNodeGlobals,
        const std::vector<DirectX::XMFLOAT4X4>* previousNodeGlobals);

    struct MeshDrawCommand
    {
        const Model::Mesh* mesh{};
        const std::vector<DirectX::XMFLOAT4X4>* currentNodeGlobals{ nullptr };
        const std::vector<DirectX::XMFLOAT4X4>* previousNodeGlobals{ nullptr };
        DirectX::XMFLOAT4   color{};
        bool                useManualMatrix{ false };
        DirectX::XMFLOAT4X4 worldMatrix{};
        DirectX::XMFLOAT4X4 previousWorldMatrix{};
    };

    struct CbScene
    {
        DirectX::XMFLOAT4X4 viewProjection{};       // 64 bytes
        DirectX::XMFLOAT4   lightDirection{};       // 16 bytes
        DirectX::XMFLOAT4   lightColor{};           // 16 bytes
        DirectX::XMFLOAT4   cameraPosition{};       // 16 bytes
		DirectX::XMFLOAT4   cameraDirection{};      // 16 bytes
        DirectX::XMFLOAT4   ambientSkyColor{};      // 16 bytes
        DirectX::XMFLOAT4   ambientGroundColor{};   // 16 bytes
        DirectX::XMFLOAT4   packedParams{};         // 16 bytes (psxEnabled, psxResW, psxResH, padding)
        DirectX::XMINT4     lightCounts{};          // 16 bytes (pointCount, spotCount, padding, padding)

        DirectX::XMFLOAT4X4 cascadeMatrices[4]{};
        DirectX::XMFLOAT4   cascadeSplits{};        // x: Split 1, y: Split 2, z: Split 3, w: Split 4
        DirectX::XMFLOAT4   cascadeBias{};          // x, y, z, w map to cascades 0, 1, 2, 3
        DirectX::XMFLOAT4   shadowSettings{};       // x: CastShadows(1/0), y: Attenuation, z: padding, w: padding

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
        std::shared_ptr<Model>  model{};
        const std::vector<DirectX::XMFLOAT4X4>* currentNodeGlobals{ nullptr };
        const std::vector<DirectX::XMFLOAT4X4>* previousNodeGlobals{ nullptr };
        DirectX::XMFLOAT4       color{};
        bool                    useManualMatrix{ false };
        DirectX::XMFLOAT4X4     worldMatrix{};
        DirectX::XMFLOAT4X4     previousWorldMatrix{};
        bool                    castShadows{ true };
    };

    struct TransparencyDrawInfo
    {
        ShaderId                shaderId;
        const Model::Mesh* mesh;
        float                   distance;
        DirectX::XMFLOAT4       color;
        bool                    useManualMatrix{ false };
        DirectX::XMFLOAT4X4     worldMatrix;
        const std::vector<DirectX::XMFLOAT4X4>* currentNodeGlobals{ nullptr };
    };

    std::unique_ptr<Shader>					shaders[static_cast<int>(ShaderId::EnumCount)];
    std::unique_ptr<OutlineShader>          m_outlineShader;
    std::unique_ptr<ShadowCasterShader>     m_shadowCasterShader{};
    std::unique_ptr<VelocityShader>         m_velocityShader{};
    std::vector<DrawInfo>					drawInfos;
    std::vector<TransparencyDrawInfo>		transparencyDrawInfos;

    Microsoft::WRL::ComPtr<ID3D11Buffer>	sceneConstantBuffer;
    Microsoft::WRL::ComPtr<ID3D11Buffer>	skeletonConstantBuffer;
    Microsoft::WRL::ComPtr<ID3D11Buffer>	objectConstantBuffer;
    Microsoft::WRL::ComPtr<ID3D11Buffer>    previousSkeletonConstantBuffer;
};