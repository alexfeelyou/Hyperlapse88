#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <d3d11.h>
#include <DirectXMath.h>
#include <limits>
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

    void Draw(std::shared_ptr<Model> model, const DirectX::XMFLOAT4& color = { 1.0f, 1.0f, 1.0f, 1.0f }, bool castShadows = true, const std::vector<Material>* materials = nullptr);
    void Draw(std::shared_ptr<Model> model, DirectX::XMFLOAT4 color, const DirectX::XMFLOAT4X4& worldMatrix, bool castShadows = true, const std::vector<Material>* materials = nullptr);
    void Draw(std::shared_ptr<Model> model, DirectX::XMFLOAT4 color,
        const DirectX::XMFLOAT4X4& worldMatrix, const DirectX::XMFLOAT4X4& previousWorldMatrix,
        const std::vector<DirectX::XMFLOAT4X4>* currentNodeGlobals = nullptr,
        const std::vector<DirectX::XMFLOAT4X4>* previousNodeGlobals = nullptr,
        bool castShadows = true,
        const std::vector<Material>* materials = nullptr);

    // ï`âÊé¿çs
    void Render(const RenderContext& rc);

private:
    void DrawMeshVelocity(ID3D11DeviceContext* dc, const Model::Mesh& mesh, std::size_t skeletonSlot);

    // Single-frame linear arena allocator for zero heap allocations per frame
    class LinearFrameArena final
    {
    public:
        explicit LinearFrameArena(std::size_t capacityBytes)
            : m_capacity{ capacityBytes }
            , m_buffer{ std::make_unique<std::byte[]>(capacityBytes) }
        {}

        ~LinearFrameArena() = default;
        LinearFrameArena(const LinearFrameArena&) = delete;
        LinearFrameArena& operator=(const LinearFrameArena&) = delete;
        LinearFrameArena(LinearFrameArena&&) noexcept = default;
        LinearFrameArena& operator=(LinearFrameArena&&) noexcept = default;

        void Reset() noexcept
        {
            m_offset = 0;
        }

        [[nodiscard]] void* Allocate(std::size_t size, std::size_t alignment = alignof(std::max_align_t)) noexcept
        {
            const std::size_t currentAddress{ reinterpret_cast<std::size_t>(m_buffer.get() + m_offset) };
            const std::size_t padding{ (alignment - (currentAddress % alignment)) % alignment };
            if (m_offset + padding + size > m_capacity)
            {
                return nullptr;
            }
            m_offset += padding;
            void* const ptr{ m_buffer.get() + m_offset };
            m_offset += size;
            return ptr;
        }

        template <typename T>
        [[nodiscard]] T* AllocateArray(std::size_t count) noexcept
        {
            return static_cast<T*>(Allocate(sizeof(T) * count, alignof(T)));
        }

    private:
        std::size_t                  m_capacity{ 0 };
        std::size_t                  m_offset{ 0 };
        std::unique_ptr<std::byte[]> m_buffer{ nullptr };
    };

    // 64-bit Radix Sort Key layout:
    // Bits 63..60 : Pass ID (Shadow Cascades 0-3, Opaque, Mask, Transparent)
    // Bits 59..56 : Translucency Layer (0 = Opaque front-to-back, 1 = Transparent back-to-front)
    // Bits 55..48 : Shader ID (Pipeline State)
    // Bits 47..32 : Material Key (Texture & Parameter state hash)
    // Bits 31..16 : Geometry Key (Vertex & Index buffer pair hash)
    // Bits 15..0  : Quantized Depth (16-bit linear camera distance)
    [[nodiscard]] static constexpr uint64_t BuildDrawKey(
        uint64_t passId,
        uint64_t translucencyLayer,
        uint64_t shaderId,
        uint64_t materialKey,
        uint64_t geometryKey,
        uint16_t quantizedDepth) noexcept
    {
        return ((passId & 0xFULL) << 60)
            | ((translucencyLayer & 0xFULL) << 56)
            | ((shaderId & 0xFFULL) << 48)
            | ((materialKey & 0xFFFFULL) << 32)
            | ((geometryKey & 0xFFFFULL) << 16)
            | (static_cast<uint64_t>(quantizedDepth));
    }

    struct InstanceData
    {
        DirectX::XMFLOAT4X4 world;
        DirectX::XMFLOAT4X4 prevWorld;
        DirectX::XMFLOAT4   color;
    };

    struct DrawPacket
    {
        uint64_t            key{ 0 };
        const Model::Mesh* mesh{ nullptr };
        const Material* material{ nullptr };
        std::size_t         skeletonSlot{ 0 };
        DirectX::XMFLOAT4   color{ 1.0f, 1.0f, 1.0f, 1.0f };
        DirectX::XMFLOAT4X4 worldMatrix{};
        DirectX::XMFLOAT4X4 prevWorldMatrix{};
        bool                hasVelocity{ false };
        bool                useManualMatrix{ false };
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

    struct DrawInfo
    {
        std::shared_ptr<Model>  model{};
        const std::vector<DirectX::XMFLOAT4X4>* currentNodeGlobals{ nullptr };
        const std::vector<DirectX::XMFLOAT4X4>* previousNodeGlobals{ nullptr };
        DirectX::XMFLOAT4        color{};
        bool                     useManualMatrix{ false };
        DirectX::XMFLOAT4X4      worldMatrix{};
        DirectX::XMFLOAT4X4      previousWorldMatrix{};
        bool                     castShadows{ true };
        bool                     hasVelocity{ false };
        std::vector<std::size_t> skeletonSlots{};
        const std::vector<Material>* materials{ nullptr };
    };

    struct SkeletonSlot
    {
        Microsoft::WRL::ComPtr<ID3D11Buffer> currentBuffer{};
        Microsoft::WRL::ComPtr<ID3D11Buffer> previousBuffer{};
    };

    std::vector<SkeletonSlot> m_skeletonPool{};
    std::size_t m_skeletonPoolUsed{ 0 };

    [[nodiscard]] std::size_t AcquireSkeletonSlot(ID3D11Device* device);
    void ComputeAndUploadSkeleton(
        ID3D11DeviceContext* dc, std::size_t slotIndex, const Model::Mesh& mesh, bool useManual,
        bool needsPrevious, 
        const DirectX::XMFLOAT4X4& worldMatrix, const DirectX::XMFLOAT4X4& previousWorldMatrix,
        const std::vector<DirectX::XMFLOAT4X4>* currentNodeGlobals,
        const std::vector<DirectX::XMFLOAT4X4>* previousNodeGlobals);
    void BindSkeletonSlot(ID3D11DeviceContext* dc, std::size_t slotIndex) const noexcept;

    std::unique_ptr<Shader>              shaders[static_cast<int>(ShaderId::EnumCount)];
    std::unique_ptr<OutlineShader>       m_outlineShader;
    std::unique_ptr<ShadowCasterShader>  m_shadowCasterShader{};
    std::unique_ptr<VelocityShader>      m_velocityShader{};
    LinearFrameArena                     m_frameArena{ 4 * 1024 * 1024 }; // 4MB persistent frame bump arena
    std::vector<DrawInfo>                drawInfos{};

    Microsoft::WRL::ComPtr<ID3D11Buffer> sceneConstantBuffer;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_instanceBuffer;
    std::size_t                          m_instanceCapacity{ 10000 };
    std::size_t                          m_identitySkeletonSlot{ static_cast<std::size_t>(-1) };

    static constexpr std::size_t         kInvalidSkeletonSlot{ ~std::size_t{0} };
};