#include <cstring>
#include "ProfilerManager.h"
#include "ModelRenderer.h"
#include "StringHash.h"

namespace
{
    struct CascadeFrustumPlanes
    {
        DirectX::XMFLOAT4 planes[5]{};
    };

    struct CameraFrustumPlanes
    {
        DirectX::XMFLOAT4 planes[6]{};
    };

    struct BoundingBox
    {
        DirectX::XMFLOAT3 center{ 0.0f, 0.0f, 0.0f };
        DirectX::XMFLOAT3 extents{ 0.0f, 0.0f, 0.0f };
    };

    [[nodiscard]] CascadeFrustumPlanes ExtractCascadePlanes(const DirectX::XMFLOAT4X4& M) noexcept
    {
        CascadeFrustumPlanes cp{};
        cp.planes[0] = { M._14 + M._11, M._24 + M._21, M._34 + M._31, M._44 + M._41 };
        cp.planes[1] = { M._14 - M._11, M._24 - M._21, M._34 - M._31, M._44 - M._41 };
        cp.planes[2] = { M._14 + M._12, M._24 + M._22, M._34 + M._32, M._44 + M._42 };
        cp.planes[3] = { M._14 - M._12, M._24 - M._22, M._34 - M._32, M._44 - M._42 };
        cp.planes[4] = { M._14 - M._13, M._24 - M._23, M._34 - M._33, M._44 - M._43 };

        for (auto& p : cp.planes)
        {
            const float len{ std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z) };
            if (len > 0.00001f)
            {
                const float invLen{ 1.0f / len };
                p.x *= invLen; p.y *= invLen; p.z *= invLen; p.w *= invLen;
            }
        }
        return cp;
    }

    [[nodiscard]] CameraFrustumPlanes ExtractCameraFrustumPlanes(const DirectX::XMFLOAT4X4& M) noexcept
    {
        CameraFrustumPlanes cp{};
        cp.planes[0] = { M._14 + M._11, M._24 + M._21, M._34 + M._31, M._44 + M._41 };
        cp.planes[1] = { M._14 - M._11, M._24 - M._21, M._34 - M._31, M._44 - M._41 };
        cp.planes[2] = { M._14 + M._12, M._24 + M._22, M._34 + M._32, M._44 + M._42 };
        cp.planes[3] = { M._14 - M._12, M._24 - M._22, M._34 - M._32, M._44 - M._42 };
        cp.planes[4] = { M._13,         M._23,         M._33,         M._43 };
        cp.planes[5] = { M._14 - M._13, M._24 - M._23, M._34 - M._33, M._44 - M._43 };

        for (auto& p : cp.planes)
        {
            const float len{ std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z) };
            if (len > 0.00001f)
            {
                const float invLen{ 1.0f / len };
                p.x *= invLen; p.y *= invLen; p.z *= invLen; p.w *= invLen;
            }
        }
        return cp;
    }

    // 256-bit AVX2 Structure of Arrays (SoA) Frustum Culler
    // Evaluates 8 bounding volumes simultaneously across 8 AVX2 lanes (__m256) with zero branching
    struct alignas(32) AvxPlane final
    {
        __m256 pX{};
        __m256 pY{};
        __m256 pZ{};
        __m256 pW{};
        __m256 absX{};
        __m256 absY{};
        __m256 absZ{};
    };

    struct alignas(32) AvxFrustum final
    {
        AvxPlane          planes[6]{};
        DirectX::XMFLOAT4 rawPlanes[6]{};
        DirectX::XMFLOAT3 absNormals[6]{};
        std::size_t       planeCount{ 6 };

        void Init(const DirectX::XMFLOAT4* inPlanes, std::size_t count) noexcept
        {
            planeCount = count;
            for (std::size_t i{ 0 }; i < count && i < 6; ++i)
            {
                rawPlanes[i] = inPlanes[i];
                absNormals[i] = { std::abs(inPlanes[i].x), std::abs(inPlanes[i].y), std::abs(inPlanes[i].z) };

                planes[i].pX = _mm256_set1_ps(inPlanes[i].x);
                planes[i].pY = _mm256_set1_ps(inPlanes[i].y);
                planes[i].pZ = _mm256_set1_ps(inPlanes[i].z);
                planes[i].pW = _mm256_set1_ps(inPlanes[i].w);
                planes[i].absX = _mm256_set1_ps(absNormals[i].x);
                planes[i].absY = _mm256_set1_ps(absNormals[i].y);
                planes[i].absZ = _mm256_set1_ps(absNormals[i].z);
            }
        }

        // 8-Way AVX2 Parallel Frustum Culling
        // Returns an 8-bit mask where bit `lane` is 1 if visible, 0 if culled
        [[nodiscard]] inline uint32_t Cull8AABB(
            __m256 cX, __m256 cY, __m256 cZ,
            __m256 eX, __m256 eY, __m256 eZ) const noexcept
        {
            __m256 anyPlaneCulled{ _mm256_setzero_ps() };

            for (std::size_t i{ 0 }; i < planeCount; ++i)
            {
                const __m256 dot{ _mm256_add_ps(
                    _mm256_mul_ps(planes[i].pX, cX),
                    _mm256_add_ps(_mm256_mul_ps(planes[i].pY, cY), _mm256_mul_ps(planes[i].pZ, cZ))) };
                const __m256 dist{ _mm256_add_ps(planes[i].pW, dot) };
                const __m256 rEff{ _mm256_add_ps(
                    _mm256_mul_ps(planes[i].absX, eX),
                    _mm256_add_ps(_mm256_mul_ps(planes[i].absY, eY), _mm256_mul_ps(planes[i].absZ, eZ))) };

                const __m256 isCulled{ _mm256_cmp_ps(_mm256_add_ps(dist, rEff), _mm256_setzero_ps(), _CMP_LT_OQ) };
                anyPlaneCulled = _mm256_or_ps(anyPlaneCulled, isCulled);
            }

            const uint32_t culledMask{ static_cast<uint32_t>(_mm256_movemask_ps(anyPlaneCulled)) };
            return (~culledMask) & 0xFFu;
        }

        // Single AABB evaluation for dynamic shadow receiver rays
        [[nodiscard]] inline bool IsVisibleAABB(const DirectX::XMFLOAT3& c, const DirectX::XMFLOAT3& e) const noexcept
        {
            for (std::size_t i{ 0 }; i < planeCount; ++i)
            {
                const float dist{ rawPlanes[i].w + rawPlanes[i].x * c.x + rawPlanes[i].y * c.y + rawPlanes[i].z * c.z };
                const float rEff{ absNormals[i].x * e.x + absNormals[i].y * e.y + absNormals[i].z * e.z };
                if (dist + rEff < 0.0f)
                {
                    return false;
                }
            }
            return true;
        }
    };

    // Computes tight transformed World-space AABB for static geometry (Arvo's method) or dynamic bone hierarchies
    [[nodiscard]] BoundingBox CalculateMeshAABB(
        const Model::Mesh& mesh,
        bool useManualMatrix,
        const DirectX::XMFLOAT4X4& manualMatrix,
        const std::vector<DirectX::XMFLOAT4X4>* currentNodeGlobals) noexcept
    {
        const DirectX::XMMATRIX manualWorldMat{ DirectX::XMLoadFloat4x4(&manualMatrix) };

        // Skinned mesh with active bones: evaluate dynamic bounds around all animated joints
        if (!mesh.bones.empty())
        {
            DirectX::XMFLOAT3 minB{ FLT_MAX, FLT_MAX, FLT_MAX };
            DirectX::XMFLOAT3 maxB{ -FLT_MAX, -FLT_MAX, -FLT_MAX };
            bool hasValidBone{ false };

            for (const auto& bone : mesh.bones)
            {
                DirectX::XMMATRIX nodeGlobalMat;
                if (currentNodeGlobals && bone.nodeIndex >= 0 && static_cast<std::size_t>(bone.nodeIndex) < currentNodeGlobals->size())
                {
                    nodeGlobalMat = DirectX::XMLoadFloat4x4(&(*currentNodeGlobals)[bone.nodeIndex]);
                }
                else
                {
                    nodeGlobalMat = DirectX::XMLoadFloat4x4(&bone.node->globalTransform);
                }

                const DirectX::XMMATRIX boneWorld{ useManualMatrix
                    ? (nodeGlobalMat * manualWorldMat)
                    : DirectX::XMLoadFloat4x4(&bone.node->worldTransform) };

                DirectX::XMFLOAT4X4 bw{};
                DirectX::XMStoreFloat4x4(&bw, boneWorld);

                minB.x = (std::min)(minB.x, bw._41);
                minB.y = (std::min)(minB.y, bw._42);
                minB.z = (std::min)(minB.z, bw._43);

                maxB.x = (std::max)(maxB.x, bw._41);
                maxB.y = (std::max)(maxB.y, bw._42);
                maxB.z = (std::max)(maxB.z, bw._43);
                hasValidBone = true;
            }

            if (hasValidBone && minB.x <= maxB.x)
            {
                // Scale padding by instance transform scale
                const float scaleX{ DirectX::XMVectorGetX(DirectX::XMVector3Length(manualWorldMat.r[0])) };
                const float scaleY{ DirectX::XMVectorGetX(DirectX::XMVector3Length(manualWorldMat.r[1])) };
                const float scaleZ{ DirectX::XMVectorGetX(DirectX::XMVector3Length(manualWorldMat.r[2])) };
                const float maxScale{ (std::max)({ scaleX, scaleY, scaleZ }) };

                // Padding encapsulates skin surface, hair, and footwear extending beyond bone joints
                const float padding{ (std::max)(0.6f, mesh.boundsRadius * 0.35f) * (maxScale > 0.0001f ? maxScale : 1.0f) };

                minB.x -= padding; maxB.x += padding;
                minB.y -= padding; maxB.y += padding;
                minB.z -= padding; maxB.z += padding;

                return {
                    { (minB.x + maxB.x) * 0.5f, (minB.y + maxB.y) * 0.5f, (minB.z + maxB.z) * 0.5f },
                    { (maxB.x - minB.x) * 0.5f, (maxB.y - minB.y) * 0.5f, (maxB.z - minB.z) * 0.5f }
                };
            }
        }

        // Static or unskinned mesh path: respect animated parent node if present
        DirectX::XMMATRIX mWorld{};
        if (useManualMatrix)
        {
            DirectX::XMMATRIX nodeGlobalMat;
            if (currentNodeGlobals && mesh.nodeIndex >= 0 && static_cast<std::size_t>(mesh.nodeIndex) < currentNodeGlobals->size())
            {
                nodeGlobalMat = DirectX::XMLoadFloat4x4(&(*currentNodeGlobals)[mesh.nodeIndex]);
            }
            else
            {
                nodeGlobalMat = DirectX::XMLoadFloat4x4(&mesh.node->globalTransform);
            }
            mWorld = nodeGlobalMat * manualWorldMat;
        }
        else
        {
            mWorld = DirectX::XMLoadFloat4x4(&mesh.node->worldTransform);
        }

        const DirectX::XMVECTOR vCenter{ DirectX::XMLoadFloat3(&mesh.boundsCenter) };
        DirectX::XMFLOAT3 worldCenter{};
        DirectX::XMStoreFloat3(&worldCenter, DirectX::XMVector3TransformCoord(vCenter, mWorld));

        DirectX::XMFLOAT3 localExtents{ mesh.boundsExtents };
        if (localExtents.x <= 0.0001f && localExtents.y <= 0.0001f && localExtents.z <= 0.0001f)
        {
            const float r{ mesh.boundsRadius > 0.0f ? mesh.boundsRadius : 1.0f };
            localExtents = { r, r, r };
        }

        DirectX::XMFLOAT4X4 M{};
        DirectX::XMStoreFloat4x4(&M, mWorld);

        const float worldExtentX{ std::abs(M._11) * localExtents.x + std::abs(M._21) * localExtents.y + std::abs(M._31) * localExtents.z };
        const float worldExtentY{ std::abs(M._12) * localExtents.x + std::abs(M._22) * localExtents.y + std::abs(M._32) * localExtents.z };
        const float worldExtentZ{ std::abs(M._13) * localExtents.x + std::abs(M._23) * localExtents.y + std::abs(M._33) * localExtents.z };

        return { worldCenter, { worldExtentX, worldExtentY, worldExtentZ } };
    }

    // Hashes only the properties consumed by the active main pass shader
    [[nodiscard]] uint16_t HashMaterial(const Material* mat) noexcept
    {
        if (!mat) return 0;

        // 16-byte aligned payload capturing surface parameters
        struct alignas(16) MaterialKeyData
        {
            void* baseMap{ nullptr };
            void* normalMap{ nullptr };
            void* emissiveMap{ nullptr };
            void* occlusionMap{ nullptr };
            void* metalnessRoughnessMap{ nullptr };
            DirectX::XMFLOAT3 emissiveColor{ 0.0f, 0.0f, 0.0f };
            float roughness{ 0.5f };
            float metalness{ 0.0f };
            float occlusionStrength{ 1.0f };
            float alphaCutoff{ 0.5f };
            int alphaMode{ 0 };
        };

        // Value-initialization guarantees all internal padding bytes are zeroed
        MaterialKeyData data{};

        // Common to all forward surface shaders (baseColor streamed per-instance in Slot 1)
        data.baseMap = mat->baseMap.Get();
        data.alphaCutoff = mat->alphaCutoff;
        data.alphaMode = static_cast<int>(mat->alphaMode);

        // Scope hashing strictly to properties evaluated by the active shader permutation
        switch (static_cast<ShaderId>(mat->shaderId))
        {
        case ShaderId::Basic:
            // Basic shader ignores lighting, normal mapping, PBR maps, and outlines
            break;

        case ShaderId::Lambert:
            data.emissiveColor = mat->emissiveColor;
            break;

        case ShaderId::Phong:
            data.normalMap = mat->normalMap.Get();
            data.emissiveColor = mat->emissiveColor;
            data.roughness = mat->roughness;
            break;

        case ShaderId::Pbr:
            data.normalMap = mat->normalMap.Get();
            data.emissiveMap = mat->emissiveMap.Get();
            data.occlusionMap = mat->occlusionMap.Get();
            data.metalnessRoughnessMap = mat->metalnessRoughnessMap.Get();
            data.emissiveColor = mat->emissiveColor;
            data.roughness = mat->roughness;
            data.metalness = mat->metalness;
            data.occlusionStrength = mat->occlusionStrength;
            break;

        case ShaderId::Toon:
            // Surface pass only evaluates diffuse, emissive, and stepped specular
            data.emissiveColor = mat->emissiveColor;
            data.roughness = mat->roughness;
            break;

        default:
            break;
        }

        const std::uint64_t hash{ Core::BufferHash(&data, sizeof(MaterialKeyData)) };
        return static_cast<uint16_t>(hash ^ (hash >> 16) ^ (hash >> 32) ^ (hash >> 48));
    }

    // Hashes only the parameters consumed by OutlineShader
    [[nodiscard]] uint16_t HashOutlineMaterial(const Material* mat) noexcept
    {
        if (!mat) return 0;

        struct alignas(16) OutlineKeyData
        {
            void* baseMap{ nullptr };
            DirectX::XMFLOAT4 outlineColor{ 0.0f, 0.0f, 0.0f, 1.0f };
            float outlineWidth{ 0.0f };
            float outlineFadeStart{ 0.0f };
            float outlineFadeEnd{ 0.0f };
            float alphaCutoff{ 0.5f };
            int alphaMode{ 0 };
            float padding[3]{ 0.0f, 0.0f, 0.0f };
        };

        OutlineKeyData data{};
        data.baseMap = mat->baseMap.Get();
        data.outlineColor = mat->outlineColor;
        data.outlineWidth = mat->outlineWidth;
        data.outlineFadeStart = mat->outlineFadeStart;
        data.outlineFadeEnd = mat->outlineFadeEnd;
        data.alphaCutoff = mat->alphaCutoff;
        data.alphaMode = static_cast<int>(mat->alphaMode);

        const std::uint64_t hash{ Core::BufferHash(&data, sizeof(OutlineKeyData)) };
        return static_cast<uint16_t>(hash ^ (hash >> 16) ^ (hash >> 32) ^ (hash >> 48));
    }

    // Evaluates semantic property equality across active forward shader parameters
    [[nodiscard]] bool AreMaterialsEquivalent(const Material* a, const Material* b, ShaderId shaderId) noexcept
    {
        if (a == b) return true;
        if (!a || !b) return false;
        if (a->baseMap.Get() != b->baseMap.Get()) return false;
        if (a->alphaMode != b->alphaMode || a->alphaCutoff != b->alphaCutoff) return false;

        switch (shaderId)
        {
        case ShaderId::Basic:
            return true;

        case ShaderId::Lambert:
            return (a->emissiveColor.x == b->emissiveColor.x &&
                a->emissiveColor.y == b->emissiveColor.y &&
                a->emissiveColor.z == b->emissiveColor.z);

        case ShaderId::Phong:
            return (a->normalMap.Get() == b->normalMap.Get() &&
                a->roughness == b->roughness &&
                a->emissiveColor.x == b->emissiveColor.x &&
                a->emissiveColor.y == b->emissiveColor.y &&
                a->emissiveColor.z == b->emissiveColor.z);

        case ShaderId::Pbr:
            return (a->normalMap.Get() == b->normalMap.Get() &&
                a->metalnessRoughnessMap.Get() == b->metalnessRoughnessMap.Get() &&
                a->emissiveMap.Get() == b->emissiveMap.Get() &&
                a->occlusionMap.Get() == b->occlusionMap.Get() &&
                a->roughness == b->roughness &&
                a->metalness == b->metalness &&
                a->occlusionStrength == b->occlusionStrength &&
                a->emissiveColor.x == b->emissiveColor.x &&
                a->emissiveColor.y == b->emissiveColor.y &&
                a->emissiveColor.z == b->emissiveColor.z);

        case ShaderId::Toon:
            return (a->roughness == b->roughness &&
                a->emissiveColor.x == b->emissiveColor.x &&
                a->emissiveColor.y == b->emissiveColor.y &&
                a->emissiveColor.z == b->emissiveColor.z);

        default:
            return false;
        }
    }

    // Evaluates semantic property equality strictly for outline parameters
    [[nodiscard]] bool AreOutlineMaterialsEquivalent(const Material* a, const Material* b) noexcept
    {
        if (a == b) return true;
        if (!a || !b) return false;
        return (a->baseMap.Get() == b->baseMap.Get() &&
            a->outlineColor.x == b->outlineColor.x &&
            a->outlineColor.y == b->outlineColor.y &&
            a->outlineColor.z == b->outlineColor.z &&
            a->outlineColor.w == b->outlineColor.w &&
            a->outlineWidth == b->outlineWidth &&
            a->outlineFadeStart == b->outlineFadeStart &&
            a->outlineFadeEnd == b->outlineFadeEnd &&
            a->alphaMode == b->alphaMode &&
            a->alphaCutoff == b->alphaCutoff);
    }

    // Compares joint indices and inverse bind matrices to confirm two meshes share identical bone layouts
    [[nodiscard]] inline bool AreBonePalettesEqual(
        const std::vector<Model::Bone>& a,
        const std::vector<Model::Bone>& b) noexcept
    {
        if (&a == &b) return true;
        if (a.size() != b.size()) return false;
        if (a.empty()) return true;

        for (std::size_t i{ 0 }; i < a.size(); ++i)
        {
            if (a[i].nodeIndex != b[i].nodeIndex) return false;
            if (std::memcmp(&a[i].offsetTransform, &b[i].offsetTransform, sizeof(DirectX::XMFLOAT4X4)) != 0)
            {
                return false;
            }
        }
        return true;
    }
}

void ModelRenderer::SoftwareOcclusionBuffer::Clear() noexcept
{
    std::fill_n(depthBuffer, kTotalPixels, 1.0f);
}

bool ModelRenderer::SoftwareOcclusionBuffer::ProjectPoint(
    const DirectX::XMFLOAT3& worldPos,
    const DirectX::XMFLOAT4X4& viewProj,
    float& outX, float& outY, float& outZ) noexcept
{
    const float w{ worldPos.x * viewProj._14 + worldPos.y * viewProj._24 + worldPos.z * viewProj._34 + viewProj._44 };
    if (w <= 0.001f) return false;

    const float invW{ 1.0f / w };
    const float ndcX{ (worldPos.x * viewProj._11 + worldPos.y * viewProj._21 + worldPos.z * viewProj._31 + viewProj._41) * invW };
    const float ndcY{ (worldPos.x * viewProj._12 + worldPos.y * viewProj._22 + worldPos.z * viewProj._32 + viewProj._42) * invW };
    const float ndcZ{ (worldPos.x * viewProj._13 + worldPos.y * viewProj._23 + worldPos.z * viewProj._33 + viewProj._43) * invW };

    outX = (ndcX * 0.5f + 0.5f) * static_cast<float>(kWidth - 1);
    outY = (-ndcY * 0.5f + 0.5f) * static_cast<float>(kHeight - 1);
    outZ = ndcZ;
    return true;
}

void ModelRenderer::SoftwareOcclusionBuffer::RasterizeTriangle(
    float x0, float y0, float z0,
    float x1, float y1, float z1,
    float x2, float y2, float z2) noexcept
{
    float area{ (x1 - x0) * (y2 - y0) - (y1 - y0) * (x2 - x0) };
    if (std::abs(area) < 0.0001f)
    {
        return;
    }

    // Two-sided support for walls: ensure consistent clockwise rasterization winding
    if (area < 0.0f)
    {
        std::swap(x1, x2);
        std::swap(y1, y2);
        std::swap(z1, z2);
        area = -area;
    }

    int minX{ static_cast<int>(std::floor((std::min)({ x0, x1, x2 }))) };
    int maxX{ static_cast<int>(std::ceil((std::max)({ x0, x1, x2 }))) };
    int minY{ static_cast<int>(std::floor((std::min)({ y0, y1, y2 }))) };
    int maxY{ static_cast<int>(std::ceil((std::max)({ y0, y1, y2 }))) };

    minX = std::clamp(minX, 0, kWidth - 1);
    maxX = std::clamp(maxX, 0, kWidth - 1);
    minY = std::clamp(minY, 0, kHeight - 1);
    maxY = std::clamp(maxY, 0, kHeight - 1);

    if (minX > maxX || minY > maxY) return;

    const float invArea{ 1.0f / area };

    for (int y{ minY }; y <= maxY; ++y)
    {
        const float py{ static_cast<float>(y) + 0.5f };
        const int rowOffset{ y * kWidth };

        for (int x{ minX }; x <= maxX; ++x)
        {
            const float px{ static_cast<float>(x) + 0.5f };
            const float w0{ ((x1 - px) * (y2 - py) - (y1 - py) * (x2 - px)) * invArea };
            const float w1{ ((x2 - px) * (y0 - py) - (y2 - py) * (x0 - px)) * invArea };
            const float w2{ 1.0f - w0 - w1 };

            if (w0 >= 0.0f && w1 >= 0.0f && w2 >= 0.0f)
            {
                const float depth{ w0 * z0 + w1 * z1 + w2 * z2 };
                const int idx{ rowOffset + x };
                if (depth < depthBuffer[idx])
                {
                    depthBuffer[idx] = depth;
                }
            }
        }
    }
}

void ModelRenderer::SoftwareOcclusionBuffer::ClipAndRasterizeTriangle(
    const ClipVertex& v0,
    const ClipVertex& v1,
    const ClipVertex& v2) noexcept
{
    constexpr float kNearW{ 0.05f };

    auto projectAndDraw = [this](const ClipVertex& a, const ClipVertex& b, const ClipVertex& c) noexcept
        {
            const float invWa{ 1.0f / a.w };
            const float invWb{ 1.0f / b.w };
            const float invWc{ 1.0f / c.w };

            const float sx0{ (a.x * invWa * 0.5f + 0.5f) * static_cast<float>(kWidth - 1) };
            const float sy0{ (-a.y * invWa * 0.5f + 0.5f) * static_cast<float>(kHeight - 1) };
            const float sz0{ a.z * invWa };

            const float sx1{ (b.x * invWb * 0.5f + 0.5f) * static_cast<float>(kWidth - 1) };
            const float sy1{ (-b.y * invWb * 0.5f + 0.5f) * static_cast<float>(kHeight - 1) };
            const float sz1{ b.z * invWb };

            const float sx2{ (c.x * invWc * 0.5f + 0.5f) * static_cast<float>(kWidth - 1) };
            const float sy2{ (-c.y * invWc * 0.5f + 0.5f) * static_cast<float>(kHeight - 1) };
            const float sz2{ c.z * invWc };

            RasterizeTriangle(sx0, sy0, sz0, sx1, sy1, sz1, sx2, sy2, sz2);
        };

    auto lerpVertex = [](const ClipVertex& a, const ClipVertex& b, float t) noexcept -> ClipVertex
        {
            return {
                a.x + t * (b.x - a.x),
                a.y + t * (b.y - a.y),
                a.z + t * (b.z - a.z),
                a.w + t * (b.w - a.w)
            };
        };

    const bool in0{ v0.w >= kNearW };
    const bool in1{ v1.w >= kNearW };
    const bool in2{ v2.w >= kNearW };
    const int inCount{ (in0 ? 1 : 0) + (in1 ? 1 : 0) + (in2 ? 1 : 0) };

    if (inCount == 3)
    {
        projectAndDraw(v0, v1, v2);
    }
    else if (inCount == 1)
    {
        if (in0)
        {
            const float t1{ (kNearW - v0.w) / (v1.w - v0.w) };
            const float t2{ (kNearW - v0.w) / (v2.w - v0.w) };
            projectAndDraw(v0, lerpVertex(v0, v1, t1), lerpVertex(v0, v2, t2));
        }
        else if (in1)
        {
            const float t0{ (kNearW - v1.w) / (v0.w - v1.w) };
            const float t2{ (kNearW - v1.w) / (v2.w - v1.w) };
            projectAndDraw(lerpVertex(v1, v0, t0), v1, lerpVertex(v1, v2, t2));
        }
        else
        {
            const float t0{ (kNearW - v2.w) / (v0.w - v2.w) };
            const float t1{ (kNearW - v2.w) / (v1.w - v2.w) };
            projectAndDraw(lerpVertex(v2, v0, t0), lerpVertex(v2, v1, t1), v2);
        }
    }
    else if (inCount == 2)
    {
        if (!in0)
        {
            const float t1{ (kNearW - v0.w) / (v1.w - v0.w) };
            const float t2{ (kNearW - v0.w) / (v2.w - v0.w) };
            const ClipVertex v01{ lerpVertex(v0, v1, t1) };
            const ClipVertex v02{ lerpVertex(v0, v2, t2) };
            projectAndDraw(v01, v1, v2);
            projectAndDraw(v01, v2, v02);
        }
        else if (!in1)
        {
            const float t0{ (kNearW - v1.w) / (v0.w - v1.w) };
            const float t2{ (kNearW - v1.w) / (v2.w - v1.w) };
            const ClipVertex v10{ lerpVertex(v1, v0, t0) };
            const ClipVertex v12{ lerpVertex(v1, v2, t2) };
            projectAndDraw(v0, v10, v2);
            projectAndDraw(v10, v12, v2);
        }
        else
        {
            const float t0{ (kNearW - v2.w) / (v0.w - v2.w) };
            const float t1{ (kNearW - v2.w) / (v1.w - v2.w) };
            const ClipVertex v20{ lerpVertex(v2, v0, t0) };
            const ClipVertex v21{ lerpVertex(v2, v1, t1) };
            projectAndDraw(v0, v1, v21);
            projectAndDraw(v0, v21, v20);
        }
    }
}

void ModelRenderer::SoftwareOcclusionBuffer::RasterizeOccluderMesh(
    const Model::Mesh& mesh,
    const DirectX::XMFLOAT4X4& worldMatrix,
    const DirectX::XMFLOAT4X4& viewProj) noexcept
{
    if (mesh.vertices.empty() || mesh.indices.empty()) return;

    const DirectX::XMMATRIX W{ DirectX::XMLoadFloat4x4(&worldMatrix) };
    const DirectX::XMMATRIX VP{ DirectX::XMLoadFloat4x4(&viewProj) };
    const DirectX::XMMATRIX WVP{ W * VP };
    DirectX::XMFLOAT4X4 M{};
    DirectX::XMStoreFloat4x4(&M, WVP);

    const std::size_t indexCount{ mesh.indices.size() };
    for (std::size_t i{ 0 }; i + 2 < indexCount; i += 3)
    {
        const auto& p0{ mesh.vertices[mesh.indices[i]].position };
        const auto& p1{ mesh.vertices[mesh.indices[i + 1]].position };
        const auto& p2{ mesh.vertices[mesh.indices[i + 2]].position };

        const ClipVertex v0{
            p0.x * M._11 + p0.y * M._21 + p0.z * M._31 + M._41,
            p0.x * M._12 + p0.y * M._22 + p0.z * M._32 + M._42,
            p0.x * M._13 + p0.y * M._23 + p0.z * M._33 + M._43,
            p0.x * M._14 + p0.y * M._24 + p0.z * M._34 + M._44
        };

        const ClipVertex v1{
            p1.x * M._11 + p1.y * M._21 + p1.z * M._31 + M._41,
            p1.x * M._12 + p1.y * M._22 + p1.z * M._32 + M._42,
            p1.x * M._13 + p1.y * M._23 + p1.z * M._33 + M._43,
            p1.x * M._14 + p1.y * M._24 + p1.z * M._34 + M._44
        };

        const ClipVertex v2{
            p2.x * M._11 + p2.y * M._21 + p2.z * M._31 + M._41,
            p2.x * M._12 + p2.y * M._22 + p2.z * M._32 + M._42,
            p2.x * M._13 + p2.y * M._23 + p2.z * M._33 + M._43,
            p2.x * M._14 + p2.y * M._24 + p2.z * M._34 + M._44
        };

        ClipAndRasterizeTriangle(v0, v1, v2);
    }
}

bool ModelRenderer::SoftwareOcclusionBuffer::IsOccluded(
    const DirectX::XMFLOAT3& center,
    const DirectX::XMFLOAT3& extents,
    const DirectX::XMFLOAT4X4& viewProj) const noexcept
{
    const DirectX::XMFLOAT3 corners[8]{
        { center.x - extents.x, center.y - extents.y, center.z - extents.z },
        { center.x + extents.x, center.y - extents.y, center.z - extents.z },
        { center.x + extents.x, center.y + extents.y, center.z - extents.z },
        { center.x - extents.x, center.y + extents.y, center.z - extents.z },
        { center.x - extents.x, center.y - extents.y, center.z + extents.z },
        { center.x + extents.x, center.y - extents.y, center.z + extents.z },
        { center.x + extents.x, center.y + extents.y, center.z + extents.z },
        { center.x - extents.x, center.y + extents.y, center.z + extents.z }
    };

    float minX{ 1e9f }, maxX{ -1e9f };
    float minY{ 1e9f }, maxY{ -1e9f };
    float minZ{ 1e9f };

    for (int i{ 0 }; i < 8; ++i)
    {
        float sx{ 0.0f }, sy{ 0.0f }, sz{ 0.0f };
        if (!ProjectPoint(corners[i], viewProj, sx, sy, sz))
        {
            return false;
        }

        minX = (std::min)(minX, sx);
        maxX = (std::max)(maxX, sx);
        minY = (std::min)(minY, sy);
        maxY = (std::max)(maxY, sy);
        minZ = (std::min)(minZ, sz);
    }

    int startX{ std::clamp(static_cast<int>(std::floor(minX)), 0, kWidth - 1) };
    int endX{ std::clamp(static_cast<int>(std::ceil(maxX)), 0, kWidth - 1) };
    int startY{ std::clamp(static_cast<int>(std::floor(minY)), 0, kHeight - 1) };
    int endY{ std::clamp(static_cast<int>(std::ceil(maxY)), 0, kHeight - 1) };

    if (startX > endX || startY > endY) return false;

    // Strict conservative test: every pixel in the footprint must have occluder depth closer than the occludee's nearest depth
    for (int y{ startY }; y <= endY; ++y)
    {
        const int rowOffset{ y * kWidth };
        for (int x{ startX }; x <= endX; ++x)
        {
            if (depthBuffer[rowOffset + x] >= minZ)
            {
                return false;
            }
        }
    }

    return true;
}

ModelRenderer::ModelRenderer(ID3D11Device* device)
{
    GpuResourceUtils::CreateConstantBuffer(device, sizeof(CbScene), sceneConstantBuffer.GetAddressOf());

    D3D11_BUFFER_DESC idesc{};
    idesc.ByteWidth = sizeof(InstanceData) * static_cast<UINT>(m_instanceCapacity);
    idesc.Usage = D3D11_USAGE_DYNAMIC;
    idesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    idesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    device->CreateBuffer(&idesc, nullptr, m_instanceBuffer.GetAddressOf());

    drawInfos.reserve(2000);
    m_skeletonPool.reserve(16);

    shaders[static_cast<int>(ShaderId::Basic)] = std::make_unique<BasicShader>(device);
    shaders[static_cast<int>(ShaderId::Lambert)] = std::make_unique<LambertShader>(device);
    shaders[static_cast<int>(ShaderId::Phong)] = std::make_unique<PhongShader>(device);
    shaders[static_cast<int>(ShaderId::Pbr)] = std::make_unique<PbrShader>(device);
    shaders[static_cast<int>(ShaderId::Toon)] = std::make_unique<ToonShader>(device);

    m_outlineShader = std::make_unique<OutlineShader>(device);
    m_shadowCasterShader = std::make_unique<ShadowCasterShader>(device);
    m_velocityShader = std::make_unique<VelocityShader>(device);
}

std::size_t ModelRenderer::AcquireSkeletonSlot(ID3D11Device* device)
{
    const std::size_t index{ m_skeletonPoolUsed++ };
    if (index >= m_skeletonPool.size())
    {
        // Grows only the first time this many simultaneous meshes-with-transforms are drawn
        // in one frame. Every subsequent frame at or below that count allocates nothing.
        SkeletonSlot slot{};
        GpuResourceUtils::CreateConstantBuffer(device, sizeof(CbSkeleton), slot.currentBuffer.GetAddressOf());
        GpuResourceUtils::CreateConstantBuffer(device, sizeof(CbSkeleton), slot.previousBuffer.GetAddressOf());
        m_skeletonPool.push_back(std::move(slot));
    }
    return index;
}

void ModelRenderer::ComputeAndUploadSkeleton(
    ID3D11DeviceContext* dc, std::size_t slotIndex, const Model::Mesh& mesh, bool useManual,
    bool needsPrevious,
    const DirectX::XMFLOAT4X4& worldMatrix, const DirectX::XMFLOAT4X4& previousWorldMatrix,
    const std::vector<DirectX::XMFLOAT4X4>* currentNodeGlobals,
    const std::vector<DirectX::XMFLOAT4X4>* previousNodeGlobals)
{
    // Bypass buffer writes entirely if this slot is the shared global identity slot
    if (slotIndex == m_identitySkeletonSlot) return;

    SkeletonSlot& slot{ m_skeletonPool[slotIndex] };

    CbSkeleton cbCurrent{};
    CbSkeleton cbPrevious{}; // only populated and uploaded when needsPrevious is true

    const DirectX::XMMATRIX manualWorldMat{ DirectX::XMLoadFloat4x4(&worldMatrix) };
    const DirectX::XMMATRIX previousManualWorldMat{ DirectX::XMLoadFloat4x4(&previousWorldMatrix) };

    if (!mesh.bones.empty())
    {
        for (std::size_t i{ 0 }; i < mesh.bones.size(); ++i)
        {
            const Model::Bone& bone{ mesh.bones[i] };

            DirectX::XMMATRIX nodeGlobalMat;
            if (currentNodeGlobals && bone.nodeIndex >= 0 && static_cast<std::size_t>(bone.nodeIndex) < currentNodeGlobals->size())
            {
                nodeGlobalMat = DirectX::XMLoadFloat4x4(&(*currentNodeGlobals)[bone.nodeIndex]);
            }
            else
            {
                nodeGlobalMat = DirectX::XMLoadFloat4x4(&bone.node->globalTransform);
            }

            const DirectX::XMMATRIX offsetTransform{ DirectX::XMLoadFloat4x4(&bone.offsetTransform) };
            const DirectX::XMMATRIX worldTransform{ useManual
                ? (nodeGlobalMat * manualWorldMat)
                : DirectX::XMLoadFloat4x4(&bone.node->worldTransform) };
            DirectX::XMStoreFloat4x4(&cbCurrent.boneTransforms[i], offsetTransform * worldTransform);

            if (needsPrevious)
            {
                DirectX::XMMATRIX prevNodeGlobalMat;
                if (previousNodeGlobals && bone.nodeIndex >= 0 && static_cast<std::size_t>(bone.nodeIndex) < previousNodeGlobals->size())
                {
                    prevNodeGlobalMat = DirectX::XMLoadFloat4x4(&(*previousNodeGlobals)[bone.nodeIndex]);
                }
                else
                {
                    prevNodeGlobalMat = nodeGlobalMat;
                }

                const DirectX::XMMATRIX previousWorldTransform{ useManual
                    ? (prevNodeGlobalMat * previousManualWorldMat)
                    : DirectX::XMLoadFloat4x4(&bone.node->worldTransform) };
                DirectX::XMStoreFloat4x4(&cbPrevious.boneTransforms[i], offsetTransform * previousWorldTransform);
            }
        }
    }
    else
    {
        if (useManual)
        {
            // Unskinned meshes must track the animated node
            DirectX::XMMATRIX nodeGlobalMat;

            // If the AnimationComponent provided evaluated globals, use them
            if (currentNodeGlobals && mesh.nodeIndex >= 0 && static_cast<std::size_t>(mesh.nodeIndex) < currentNodeGlobals->size())
            {
                nodeGlobalMat = DirectX::XMLoadFloat4x4(&(*currentNodeGlobals)[mesh.nodeIndex]);
            }
            else
            {
                // Fallback to static bind pose if no AnimationComponent exists
                nodeGlobalMat = DirectX::XMLoadFloat4x4(&mesh.node->globalTransform);
            }

            DirectX::XMStoreFloat4x4(&cbCurrent.boneTransforms[0], nodeGlobalMat * manualWorldMat);

            if (needsPrevious)
            {
                DirectX::XMMATRIX prevNodeGlobalMat;
                if (previousNodeGlobals && mesh.nodeIndex >= 0 && static_cast<std::size_t>(mesh.nodeIndex) < previousNodeGlobals->size())
                {
                    prevNodeGlobalMat = DirectX::XMLoadFloat4x4(&(*previousNodeGlobals)[mesh.nodeIndex]);
                }
                else
                {
                    prevNodeGlobalMat = nodeGlobalMat;
                }

                DirectX::XMStoreFloat4x4(&cbPrevious.boneTransforms[0], prevNodeGlobalMat * previousManualWorldMat);
            }
        }
        else
        {
            cbCurrent.boneTransforms[0] = mesh.node->worldTransform;
            if (needsPrevious)
            {
                cbPrevious.boneTransforms[0] = mesh.node->worldTransform;
            }
        }
    }

    dc->UpdateSubresource(slot.currentBuffer.Get(), 0, nullptr, &cbCurrent, 0, 0);
    if (needsPrevious)
    {
        dc->UpdateSubresource(slot.previousBuffer.Get(), 0, nullptr, &cbPrevious, 0, 0);
    }
}

void ModelRenderer::BindSkeletonSlot(ID3D11DeviceContext* dc, std::size_t slotIndex) const noexcept
{
    const SkeletonSlot& slot{ m_skeletonPool[slotIndex] };
    ID3D11Buffer* const currentCb{ slot.currentBuffer.Get() };
    ID3D11Buffer* const previousCb{ slot.previousBuffer.Get() };
    dc->VSSetConstantBuffers(6, 1, &currentCb);  
    dc->VSSetConstantBuffers(9, 1, &previousCb); 
}

void ModelRenderer::Draw(std::shared_ptr<Model> model, const DirectX::XMFLOAT4& color, bool castShadows, const std::vector<Material>* materials)
{
    DrawInfo& drawInfo{ drawInfos.emplace_back() };
    drawInfo.model = std::move(model);
    drawInfo.color = color;
    drawInfo.useManualMatrix = false;
    drawInfo.castShadows = castShadows;
    drawInfo.materials = materials;
}

void ModelRenderer::Draw(std::shared_ptr<Model> model, DirectX::XMFLOAT4 color, const DirectX::XMFLOAT4X4& worldMatrix, bool castShadows, const std::vector<Material>* materials)
{
    DrawInfo& drawInfo{ drawInfos.emplace_back() };
    drawInfo.model = std::move(model);
    drawInfo.color = color;
    drawInfo.useManualMatrix = true;
    drawInfo.worldMatrix = worldMatrix;
    drawInfo.previousWorldMatrix = worldMatrix;
    drawInfo.castShadows = castShadows;
    drawInfo.materials = materials;
}

void ModelRenderer::Draw(std::shared_ptr<Model> model, DirectX::XMFLOAT4 color,
    const DirectX::XMFLOAT4X4& worldMatrix, const DirectX::XMFLOAT4X4& previousWorldMatrix,
    const std::vector<DirectX::XMFLOAT4X4>* currentNodeGlobals,
    const std::vector<DirectX::XMFLOAT4X4>* previousNodeGlobals, bool castShadows,
    const std::vector<Material>* materials)
{
    DrawInfo info{ std::move(model), currentNodeGlobals, previousNodeGlobals, color, true, worldMatrix, previousWorldMatrix, castShadows };
    info.hasVelocity = true;
    info.materials = materials;
    drawInfos.push_back(std::move(info));
}

void ModelRenderer::DrawMeshVelocity(ID3D11DeviceContext* dc, const Model::Mesh& mesh, std::size_t skeletonSlot)
{
    UINT stride{ sizeof(Model::Vertex) };
    UINT offset{ 0 };
    dc->IASetVertexBuffers(0, 1, mesh.vertexBuffer.GetAddressOf(), &stride, &offset);
    dc->IASetIndexBuffer(mesh.indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
    dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    BindSkeletonSlot(dc, skeletonSlot);

    dc->DrawIndexed(static_cast<UINT>(mesh.indices.size()), 0, 0);

    PROFILE_DRAW_CALL();
    PROFILE_TRIANGLES(mesh.indices.size() / 3);
}

void ModelRenderer::Render(const RenderContext& rc)
{
    if (drawInfos.empty()) return;

    ID3D11DeviceContext* const dc{ rc.deviceContext };
    Microsoft::WRL::ComPtr<ID3D11Device> device{};
    dc->GetDevice(device.GetAddressOf());

    m_skeletonPoolUsed = 0;
    m_frameArena.Reset();
    dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    // Create the global Identity Skeleton Slot once per frame for ALL static meshes to share
    m_identitySkeletonSlot = AcquireSkeletonSlot(device.Get());
    {
        SkeletonSlot& idSlot = m_skeletonPool[m_identitySkeletonSlot];
        CbSkeleton cbId{};
        const DirectX::XMMATRIX identity = DirectX::XMMatrixIdentity();
        for (int i = 0; i < 256; ++i)
        {
            DirectX::XMStoreFloat4x4(&cbId.boneTransforms[i], identity);
        }
        dc->UpdateSubresource(idSlot.currentBuffer.Get(), 0, nullptr, &cbId, 0, 0);
        dc->UpdateSubresource(idSlot.previousBuffer.Get(), 0, nullptr, &cbId, 0, 0);
    }

    for (DrawInfo& drawInfo : drawInfos)
    {
        if (!drawInfo.model) continue;

        const auto& meshes{ drawInfo.model->GetMeshes() };
        drawInfo.skeletonSlots.resize(meshes.size());

        for (std::size_t meshIdx{ 0 }; meshIdx < meshes.size(); ++meshIdx)
        {
            if (meshes[meshIdx].bones.empty())
            {
                drawInfo.skeletonSlots[meshIdx] = m_identitySkeletonSlot;
            }
            else
            {
                // Multi-mesh skeleton deduplication:
                // If an earlier mesh in this drawInfo has an identical bone palette, reuse its uploaded slot
                std::size_t sharedSlot{ kInvalidSkeletonSlot };
                for (std::size_t prevIdx{ 0 }; prevIdx < meshIdx; ++prevIdx)
                {
                    if (drawInfo.skeletonSlots[prevIdx] != m_identitySkeletonSlot &&
                        drawInfo.skeletonSlots[prevIdx] != kInvalidSkeletonSlot &&
                        AreBonePalettesEqual(meshes[meshIdx].bones, meshes[prevIdx].bones))
                    {
                        sharedSlot = drawInfo.skeletonSlots[prevIdx];
                        break;
                    }
                }

                if (sharedSlot != kInvalidSkeletonSlot)
                {
                    drawInfo.skeletonSlots[meshIdx] = sharedSlot;
                }
                else
                {
                    const std::size_t slot{ AcquireSkeletonSlot(device.Get()) };
                    ComputeAndUploadSkeleton(dc, slot, meshes[meshIdx], drawInfo.useManualMatrix,
                        drawInfo.hasVelocity, drawInfo.worldMatrix, drawInfo.previousWorldMatrix,
                        drawInfo.currentNodeGlobals, drawInfo.previousNodeGlobals);
                    drawInfo.skeletonSlots[meshIdx] = slot;
                }
            }
        }
    }

    std::size_t instanceOffset = 0;

    // THE PASS DISPATCHER: Single contiguous upload per pass, zero draw-loop mapping stalls
    auto executeBatches = [&](const DrawPacket* packets, std::size_t count, Shader* forcedShader = nullptr)
        {
            if (count == 0) return;

            // Dynamic Buffer Allocation & Wrap Guard
            if (instanceOffset + count > m_instanceCapacity)
            {
                if (count > m_instanceCapacity)
                {
                    m_instanceCapacity = (std::max)(m_instanceCapacity * 2, count + 1024);
                    D3D11_BUFFER_DESC idesc{};
                    idesc.ByteWidth = sizeof(InstanceData) * static_cast<UINT>(m_instanceCapacity);
                    idesc.Usage = D3D11_USAGE_DYNAMIC;
                    idesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
                    idesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
                    m_instanceBuffer.Reset();
                    device->CreateBuffer(&idesc, nullptr, m_instanceBuffer.GetAddressOf());
                }
                instanceOffset = 0;
            }

            // Stream all instance transforms for the entire pass in a single map operation
            const D3D11_MAP mapType{ (instanceOffset == 0) ? D3D11_MAP_WRITE_DISCARD : D3D11_MAP_WRITE_NO_OVERWRITE };
            D3D11_MAPPED_SUBRESOURCE mapped{};
            dc->Map(m_instanceBuffer.Get(), 0, mapType, 0, &mapped);
            InstanceData* const instData{ static_cast<InstanceData*>(mapped.pData) + instanceOffset };

            const DirectX::XMMATRIX identity{ DirectX::XMMatrixIdentity() };
            for (std::size_t i{ 0 }; i < count; ++i)
            {
                const DrawPacket& p{ packets[i] };
                if (p.mesh->bones.empty())
                {
                    DirectX::XMStoreFloat4x4(&instData[i].world, DirectX::XMMatrixTranspose(DirectX::XMLoadFloat4x4(&p.worldMatrix)));
                    DirectX::XMStoreFloat4x4(&instData[i].prevWorld, DirectX::XMMatrixTranspose(DirectX::XMLoadFloat4x4(&p.prevWorldMatrix)));
                }
                else
                {
                    DirectX::XMStoreFloat4x4(&instData[i].world, identity);
                    DirectX::XMStoreFloat4x4(&instData[i].prevWorld, identity);
                }

                // Stream combined instance tint and material baseColor directly via Slot 1 vertex buffer
                const DirectX::XMFLOAT4& baseCol{ p.material ? p.material->baseColor : DirectX::XMFLOAT4{ 1.0f, 1.0f, 1.0f, 1.0f } };
                instData[i].color = {
                    p.color.x * baseCol.x,
                    p.color.y * baseCol.y,
                    p.color.z * baseCol.z,
                    p.color.w * baseCol.w
                };
            }
            dc->Unmap(m_instanceBuffer.Get(), 0);

            // Bind Slot 1 Instance Buffer once for the entire pass
            ID3D11Buffer* const instBuf{ m_instanceBuffer.Get() };
            const UINT instStride{ sizeof(InstanceData) };
            const UINT zeroOffset{ 0 };
            dc->IASetVertexBuffers(1, 1, &instBuf, &instStride, &zeroOffset);

            // Hot Dispatch Loop: Zero Map/Unmap overhead, pure state caching & dispatches
            Shader* activeShader{ forcedShader };
            ShaderId activeShaderId{ ShaderId::EnumCount };
            const Material* currentMaterial{ nullptr };
            uint16_t currentMaterialKey{ 0xFFFF };
            ID3D11Buffer* currentVB{ nullptr };
            ID3D11Buffer* currentIB{ nullptr };
            std::size_t currentSlot{ kInvalidSkeletonSlot };

            const bool isDepthOrVelocityPass{ (forcedShader == m_shadowCasterShader.get() || forcedShader == m_velocityShader.get()) };

            for (std::size_t pIdx{ 0 }; pIdx < count; )
            {
                const DrawPacket& firstPacket{ packets[pIdx] };
                std::size_t batchCount{ 1 };
                const bool isStatic{ firstPacket.mesh->bones.empty() };

                if (isStatic)
                {
                    if (isDepthOrVelocityPass)
                    {
                        // Depth/Velocity: Dissimilar materials on identical geometry coalesced into single draw
                        while (pIdx + batchCount < count &&
                            packets[pIdx + batchCount].mesh == firstPacket.mesh &&
                            packets[pIdx + batchCount].mesh->bones.empty())
                        {
                            ++batchCount;
                        }
                    }
                    else
                    {
                        // Color/Outline: Coalesce identical geometry sharing the exact same 48-bit PSO & Material state key
                        const bool isTransparent{ (((firstPacket.key >> 60) & 0xFULL) == 6) };
                        const uint64_t firstStateKey{ firstPacket.key & 0xFFFFFFFFFFFF0000ULL };

                        while (pIdx + batchCount < count &&
                            packets[pIdx + batchCount].mesh == firstPacket.mesh &&
                            (!isTransparent ? ((packets[pIdx + batchCount].key & 0xFFFFFFFFFFFF0000ULL) == firstStateKey)
                                : (packets[pIdx + batchCount].key == firstPacket.key)) &&
                            packets[pIdx + batchCount].mesh->bones.empty())
                        {
                            ++batchCount;
                        }
                    }
                }

                if (!forcedShader)
                {
                    const ShaderId packetShaderId{ static_cast<ShaderId>((firstPacket.key >> 48) & 0xFF) };
                    if (packetShaderId != activeShaderId)
                    {
                        if (activeShader) activeShader->End(rc);
                        activeShaderId = packetShaderId;
                        activeShader = shaders[static_cast<int>(packetShaderId)].get();
                        if (activeShader) activeShader->Begin(rc);
                        currentMaterial = nullptr;
                        currentMaterialKey = 0xFFFF;
                        currentVB = nullptr;
                        currentIB = nullptr;
                        currentSlot = kInvalidSkeletonSlot;
                    }
                }

                if (firstPacket.mesh->vertexBuffer.Get() != currentVB)
                {
                    ID3D11Buffer* const vb{ firstPacket.mesh->vertexBuffer.Get() };
                    const UINT stride{ sizeof(Model::Vertex) };
                    dc->IASetVertexBuffers(0, 1, &vb, &stride, &zeroOffset);
                    currentVB = vb;
                }

                if (firstPacket.mesh->indexBuffer.Get() != currentIB)
                {
                    dc->IASetIndexBuffer(firstPacket.mesh->indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
                    currentIB = firstPacket.mesh->indexBuffer.Get();
                }

                if (firstPacket.skeletonSlot != currentSlot)
                {
                    BindSkeletonSlot(dc, firstPacket.skeletonSlot);
                    currentSlot = firstPacket.skeletonSlot;
                }

                if (!isDepthOrVelocityPass)
                {
                    const uint16_t packetMatKey{ static_cast<uint16_t>((firstPacket.key >> 32) & 0xFFFFULL) };
                    const bool isOutline{ (forcedShader == m_outlineShader.get()) };
                    const bool needsUpdate{
                        (currentMaterial == nullptr) ||
                        (firstPacket.material != currentMaterial &&
                         (packetMatKey != currentMaterialKey ||
                          (isOutline ? !AreOutlineMaterialsEquivalent(firstPacket.material, currentMaterial)
                                     : !AreMaterialsEquivalent(firstPacket.material, currentMaterial, activeShaderId))))
                    };

                    if (needsUpdate)
                    {
                        // Transiently inject component material into mesh for uniform upload, then restore
                        Model::Mesh* const nonConstMesh{ const_cast<Model::Mesh*>(firstPacket.mesh) };
                        Material* const originalMaterial{ nonConstMesh->material };
                        nonConstMesh->material = const_cast<Material*>(firstPacket.material);

                        if (activeShader) activeShader->Update(rc, *nonConstMesh);

                        nonConstMesh->material = originalMaterial;
                        currentMaterial = firstPacket.material;
                        currentMaterialKey = packetMatKey;
                    }
                }

                dc->DrawIndexedInstanced(
                    static_cast<UINT>(firstPacket.mesh->indices.size()),
                    static_cast<UINT>(batchCount),
                    0,
                    0,
                    static_cast<UINT>(instanceOffset + pIdx)
                );

                pIdx += batchCount;
                PROFILE_DRAW_CALL();
                PROFILE_TRIANGLES(firstPacket.mesh->indices.size() / 3 * batchCount);
            }

            if (activeShader && !forcedShader)
            {
                activeShader->End(rc);
            }

            instanceOffset += count;
        };

    // Fast bitwise pointer hash eliminating stack frame buffer hashing
    auto hashPointer = [](const void* ptr) noexcept -> uint16_t
        {
            const uintptr_t val{ reinterpret_cast<uintptr_t>(ptr) };
            return static_cast<uint16_t>((val >> 4) ^ (val >> 20));
        };

    if (rc.lightManager)
    {
        const_cast<LightManager*>(rc.lightManager)->Update();
        if (rc.lightManager->GetDirectionalLight().castShadows)
        {
            rc.lightManager->UpdateCascades(*rc.camera);
        }
    }

        std::size_t totalSceneMeshCount{ 0 };
        for (const auto& drawInfo : drawInfos) if (drawInfo.model) totalSceneMeshCount += drawInfo.model->GetMeshes().size();

        // Stream Extraction Arrays
        DrawPacket* const opaquePackets{ m_frameArena.AllocateArray<DrawPacket>(totalSceneMeshCount) };
        DrawPacket* const transparentPackets{ m_frameArena.AllocateArray<DrawPacket>(totalSceneMeshCount) };
        DrawPacket* const velocityPackets{ m_frameArena.AllocateArray<DrawPacket>(totalSceneMeshCount) };
        DrawPacket* const outlinePackets{ m_frameArena.AllocateArray<DrawPacket>(totalSceneMeshCount) };
        DrawPacket* const cascadePackets[SHADOW_CASCADE_COUNT]{
            m_frameArena.AllocateArray<DrawPacket>(totalSceneMeshCount),
            m_frameArena.AllocateArray<DrawPacket>(totalSceneMeshCount),
            m_frameArena.AllocateArray<DrawPacket>(totalSceneMeshCount)
        };
        std::size_t opaqueCount{ 0 }, transparentCount{ 0 }, velocityCount{ 0 }, outlineCount{ 0 };
        std::size_t cascadeCount[SHADOW_CASCADE_COUNT]{ 0, 0, 0 };

        DirectX::XMFLOAT4X4 matViewProj{};
        {
            const DirectX::XMMATRIX V{ DirectX::XMLoadFloat4x4(&rc.camera->GetView()) };
            const DirectX::XMMATRIX P{ DirectX::XMLoadFloat4x4(&rc.camera->GetProjection()) };
            DirectX::XMStoreFloat4x4(&matViewProj, V * P);
        }

        // Precompute AVX2 Camera Frustum once per frame
        AvxFrustum cameraFrustum{};
        {
            const CameraFrustumPlanes cp{ ExtractCameraFrustumPlanes(matViewProj) };
            cameraFrustum.Init(cp.planes, 6);
        }

        // Precompute all 4 AVX2 Shadow Cascade Frustums once per frame
        AvxFrustum cascadeFrustums[SHADOW_CASCADE_COUNT]{};
        const bool hasDirectionalShadows{ rc.lightManager != nullptr && rc.lightManager->GetDirectionalLight().castShadows };
        DirectX::XMFLOAT3 lightDir{ 0.0f, -1.0f, 0.0f };
        if (hasDirectionalShadows)
        {
            const auto& dirLight{ rc.lightManager->GetDirectionalLight() };
            const float lenSq{ dirLight.direction.x * dirLight.direction.x + dirLight.direction.y * dirLight.direction.y + dirLight.direction.z * dirLight.direction.z };
            if (lenSq > 0.0001f)
            {
                const float invLen{ 1.0f / std::sqrt(lenSq) };
                lightDir = { dirLight.direction.x * invLen, dirLight.direction.y * invLen, dirLight.direction.z * invLen };
            }

            for (int i{ 0 }; i < SHADOW_CASCADE_COUNT; ++i)
            {
                const CascadeFrustumPlanes cp{ ExtractCascadePlanes(rc.lightManager->GetCascadeMatrices()[i]) };
                cascadeFrustums[i].Init(cp.planes, 5);
            }
        }

        const DirectX::XMFLOAT3 cameraEyePos{ rc.camera->GetPosition() };

        // Scene Broadphase Flattening: Contiguous SoA spatial layout in L1 cache
        const std::size_t paddedCount{ (totalSceneMeshCount + 7) & ~std::size_t{ 7 } };
        float* const soaCenterX{ m_frameArena.AllocateArray<float>(paddedCount) };
        float* const soaCenterY{ m_frameArena.AllocateArray<float>(paddedCount) };
        float* const soaCenterZ{ m_frameArena.AllocateArray<float>(paddedCount) };
        float* const soaExtentX{ m_frameArena.AllocateArray<float>(paddedCount) };
        float* const soaExtentY{ m_frameArena.AllocateArray<float>(paddedCount) };
        float* const soaExtentZ{ m_frameArena.AllocateArray<float>(paddedCount) };

        struct MeshInstanceRef
        {
            const Model::Mesh* mesh{ nullptr };
            const Material* material{ nullptr };
            std::size_t         skeletonSlot{ 0 };
            DirectX::XMFLOAT4   color{ 1.0f, 1.0f, 1.0f, 1.0f };
            DirectX::XMFLOAT4X4 worldMatrix{};
            DirectX::XMFLOAT4X4 prevWorldMatrix{};
            bool                castShadows{ true };
            bool                hasVelocity{ false };
        };
        MeshInstanceRef* const meshRefs{ m_frameArena.AllocateArray<MeshInstanceRef>(paddedCount) };

        // Zero-fill padding lanes for clean AVX2 vector loads
        for (std::size_t i{ totalSceneMeshCount }; i < paddedCount; ++i)
        {
            soaCenterX[i] = 0.0f; soaCenterY[i] = 0.0f; soaCenterZ[i] = 0.0f;
            soaExtentX[i] = 0.0f; soaExtentY[i] = 0.0f; soaExtentZ[i] = 0.0f;
        }

        std::size_t flatIndex{ 0 };
        for (const DrawInfo& drawInfo : drawInfos)
        {
            if (!drawInfo.model) continue;

            for (std::size_t meshIdx{ 0 }; meshIdx < drawInfo.model->GetMeshes().size(); ++meshIdx)
            {
                const Model::Mesh& mesh{ drawInfo.model->GetMeshes()[meshIdx] };
                const Material* const mat{ (drawInfo.materials && mesh.materialIndex >= 0 &&
                    static_cast<std::size_t>(mesh.materialIndex) < drawInfo.materials->size())
                    ? &(*drawInfo.materials)[mesh.materialIndex]
                    : mesh.material };

                DirectX::XMMATRIX mWorld, mPrevWorld;
                if (drawInfo.useManualMatrix)
                {
                    DirectX::XMMATRIX nodeGlobalMat;
                    if (drawInfo.currentNodeGlobals && mesh.nodeIndex >= 0 && static_cast<std::size_t>(mesh.nodeIndex) < drawInfo.currentNodeGlobals->size())
                    {
                        nodeGlobalMat = DirectX::XMLoadFloat4x4(&(*drawInfo.currentNodeGlobals)[mesh.nodeIndex]);
                    }
                    else
                    {
                        nodeGlobalMat = DirectX::XMLoadFloat4x4(&mesh.node->globalTransform);
                    }

                    DirectX::XMMATRIX prevNodeGlobalMat;
                    if (drawInfo.previousNodeGlobals && mesh.nodeIndex >= 0 && static_cast<std::size_t>(mesh.nodeIndex) < drawInfo.previousNodeGlobals->size())
                    {
                        prevNodeGlobalMat = DirectX::XMLoadFloat4x4(&(*drawInfo.previousNodeGlobals)[mesh.nodeIndex]);
                    }
                    else
                    {
                        prevNodeGlobalMat = nodeGlobalMat;
                    }

                    mWorld = nodeGlobalMat * DirectX::XMLoadFloat4x4(&drawInfo.worldMatrix);
                    mPrevWorld = prevNodeGlobalMat * DirectX::XMLoadFloat4x4(&drawInfo.previousWorldMatrix);
                }
                else
                {
                    mWorld = DirectX::XMLoadFloat4x4(&mesh.node->worldTransform);
                    mPrevWorld = mWorld;
                }

                // Skinned AABB deduplication: If this mesh shares a skeleton slot with an earlier mesh in the same drawInfo,
                // and has matching boundsRadius, reuse the calculated AABB to skip redundant bone transforms
                BoundingBox aabb{};
                bool aabbReused{ false };
                if (!mesh.bones.empty())
                {
                    for (std::size_t prevIdx{ 0 }; prevIdx < meshIdx; ++prevIdx)
                    {
                        if (drawInfo.skeletonSlots[meshIdx] == drawInfo.skeletonSlots[prevIdx] &&
                            std::abs(mesh.boundsRadius - drawInfo.model->GetMeshes()[prevIdx].boundsRadius) <= 0.001f)
                        {
                            const std::size_t prevFlat{ flatIndex - (meshIdx - prevIdx) };
                            aabb.center = { soaCenterX[prevFlat], soaCenterY[prevFlat], soaCenterZ[prevFlat] };
                            aabb.extents = { soaExtentX[prevFlat], soaExtentY[prevFlat], soaExtentZ[prevFlat] };
                            aabbReused = true;
                            break;
                        }
                    }
                }

                if (!aabbReused)
                {
                    aabb = CalculateMeshAABB(mesh, drawInfo.useManualMatrix, drawInfo.worldMatrix, drawInfo.currentNodeGlobals);
                }

                soaCenterX[flatIndex] = aabb.center.x;
                soaCenterY[flatIndex] = aabb.center.y;
                soaCenterZ[flatIndex] = aabb.center.z;
                soaExtentX[flatIndex] = aabb.extents.x;
                soaExtentY[flatIndex] = aabb.extents.y;
                soaExtentZ[flatIndex] = aabb.extents.z;

                MeshInstanceRef& ref{ meshRefs[flatIndex] };
                ref.mesh = &mesh;
                ref.material = mat;
                ref.skeletonSlot = drawInfo.skeletonSlots[meshIdx];
                ref.color = drawInfo.color;
                DirectX::XMStoreFloat4x4(&ref.worldMatrix, mWorld);
                DirectX::XMStoreFloat4x4(&ref.prevWorldMatrix, mPrevWorld);
                ref.castShadows = drawInfo.castShadows;
                ref.hasVelocity = drawInfo.hasVelocity;

                ++flatIndex;
            }
        }

        // Front-to-Back Occluder Sorting: Closest occluders rasterize first to maximize early-depth rejections
        m_occlusionBuffer.Clear();

        struct OccluderCandidate
        {
            std::size_t refIndex{ 0 };
            float       distanceSq{ 0.0f };
        };
        OccluderCandidate* const occluders{ m_frameArena.AllocateArray<OccluderCandidate>(totalSceneMeshCount) };
        std::size_t occluderCount{ 0 };

        for (std::size_t i{ 0 }; i < totalSceneMeshCount; ++i)
        {
            const auto& ref{ meshRefs[i] };
            if (!ref.material || !ref.material->isOccluder || ref.material->alphaMode != AlphaMode::Opaque) continue;

            const DirectX::XMFLOAT3 center{ soaCenterX[i], soaCenterY[i], soaCenterZ[i] };
            const DirectX::XMFLOAT3 extents{ soaExtentX[i], soaExtentY[i], soaExtentZ[i] };

            if (!cameraFrustum.IsVisibleAABB(center, extents)) continue;

            const float dx{ center.x - cameraEyePos.x };
            const float dy{ center.y - cameraEyePos.y };
            const float dz{ center.z - cameraEyePos.z };
            occluders[occluderCount++] = { i, dx * dx + dy * dy + dz * dz };
        }

        std::sort(occluders, occluders + occluderCount, [](const OccluderCandidate& a, const OccluderCandidate& b) noexcept {
            return a.distanceSq < b.distanceSq;
            });

        for (std::size_t i{ 0 }; i < occluderCount; ++i)
        {
            const auto& cand{ occluders[i] };
            const auto& ref{ meshRefs[cand.refIndex] };
            const DirectX::XMFLOAT3 center{ soaCenterX[cand.refIndex], soaCenterY[cand.refIndex], soaCenterZ[cand.refIndex] };
            const DirectX::XMFLOAT3 extents{ soaExtentX[cand.refIndex], soaExtentY[cand.refIndex], soaExtentZ[cand.refIndex] };

            // Hierarchical occlusion: If a closer occluder already completely covers this occluder, skip rasterization
            if (ref.material->isOccludable && m_occlusionBuffer.IsOccluded(center, extents, matViewProj))
            {
                continue;
            }

            m_occlusionBuffer.RasterizeOccluderMesh(*ref.mesh, ref.worldMatrix, matViewProj);
        }

        // 8-Way AVX2 Parallel Culling Loop: Evaluates 8 objects across SIMD lanes from L1 cache
        const DirectX::XMFLOAT3 camFront{ rc.camera->GetFront() };

        for (std::size_t batch{ 0 }; batch < totalSceneMeshCount; batch += 8)
        {
            const uint32_t activeLanes{ (batch + 8 <= totalSceneMeshCount)
                ? 0xFFu
                : static_cast<uint32_t>((1u << (totalSceneMeshCount - batch)) - 1u) };

            const __m256 cX{ _mm256_loadu_ps(&soaCenterX[batch]) };
            const __m256 cY{ _mm256_loadu_ps(&soaCenterY[batch]) };
            const __m256 cZ{ _mm256_loadu_ps(&soaCenterZ[batch]) };
            const __m256 eX{ _mm256_loadu_ps(&soaExtentX[batch]) };
            const __m256 eY{ _mm256_loadu_ps(&soaExtentY[batch]) };
            const __m256 eZ{ _mm256_loadu_ps(&soaExtentZ[batch]) };

            const uint32_t camVisibleMask{ cameraFrustum.Cull8AABB(cX, cY, cZ, eX, eY, eZ) & activeLanes };

            for (std::size_t lane{ 0 }; lane < 8; ++lane)
            {
                if ((activeLanes & (1u << lane)) == 0) break;

                const std::size_t idx{ batch + lane };
                const auto& ref{ meshRefs[idx] };
                const auto* const mat{ ref.material };
                const auto& mesh{ *ref.mesh };

                const DirectX::XMFLOAT3 center{ soaCenterX[idx], soaCenterY[idx], soaCenterZ[idx] };
                const DirectX::XMFLOAT3 extents{ soaExtentX[idx], soaExtentY[idx], soaExtentZ[idx] };

                DrawPacket packet{};
                packet.mesh = &mesh;
                packet.material = mat;
                packet.skeletonSlot = ref.skeletonSlot;
                packet.color = ref.color;
                packet.worldMatrix = ref.worldMatrix;
                packet.prevWorldMatrix = ref.prevWorldMatrix;

                // Shadows: Extruded shadow volume culling against camera frustum and occlusion buffer
                if (ref.castShadows && mat->alphaMode != AlphaMode::Blend && hasDirectionalShadows)
                {
                    const float groundDist{ (std::max)(1.0f, center.y + extents.y) };
                    const float rayDist{ (std::abs(lightDir.y) > 0.05f) ? (groundDist / std::abs(lightDir.y)) : 30.0f };
                    const float shadowExtrudeLen{ std::clamp(rayDist, 10.0f, 40.0f) };
                    const float vx{ lightDir.x * shadowExtrudeLen };
                    const float vy{ lightDir.y * shadowExtrudeLen };
                    const float vz{ lightDir.z * shadowExtrudeLen };

                    const float sMinX{ (std::min)(center.x - extents.x, center.x - extents.x + vx) };
                    const float sMaxX{ (std::max)(center.x + extents.x, center.x + extents.x + vx) };
                    const float sMinY{ (std::min)(center.y - extents.y, center.y - extents.y + vy) };
                    const float sMaxY{ (std::max)(center.y + extents.y, center.y + extents.y + vy) };
                    const float sMinZ{ (std::min)(center.z - extents.z, center.z - extents.z + vz) };
                    const float sMaxZ{ (std::max)(center.z + extents.z, center.z + extents.z + vz) };

                    const DirectX::XMFLOAT3 shadowCenter{ (sMinX + sMaxX) * 0.5f, (sMinY + sMaxY) * 0.5f, (sMinZ + sMaxZ) * 0.5f };
                    const DirectX::XMFLOAT3 shadowExtents{ (sMaxX - sMinX) * 0.5f, (sMaxY - sMinY) * 0.5f, (sMaxZ - sMinZ) * 0.5f };

                    const bool shadowInViewFrustum{ cameraFrustum.IsVisibleAABB(shadowCenter, shadowExtents) };
                    const bool shadowOccluded{ mat->isOccludable && m_occlusionBuffer.IsOccluded(shadowCenter, shadowExtents, matViewProj) };

                    if (shadowInViewFrustum && !shadowOccluded)
                    {
                        for (int c{ 0 }; c < SHADOW_CASCADE_COUNT; ++c)
                        {
                            // Skinned meshes bypass Cascade 2 to eliminate distant bone evaluation
                            if (c == 2 && !mesh.bones.empty()) continue;

                            if (cascadeFrustums[c].IsVisibleAABB(center, extents))
                            {
                                DrawPacket shadowPacket{ packet };
                                shadowPacket.key = reinterpret_cast<uint64_t>(&mesh);
                                cascadePackets[c][cascadeCount[c]++] = shadowPacket;
                            }
                        }
                    }
                }

                // Camera visibility check using precomputed 8-way AVX2 mask
                if ((camVisibleMask & (1u << lane)) == 0) continue;

                const float dx{ center.x - cameraEyePos.x };
                const float dy{ center.y - cameraEyePos.y };
                const float dz{ center.z - cameraEyePos.z };
                const float distance{ dx * camFront.x + dy * camFront.y + dz * camFront.z };

                // Sub-pixel screen size culling
                const float maxExtent{ (std::max)({ extents.x, extents.y, extents.z }) };
                const float halfHeight{ (rc.psxEnabled ? rc.psxResHeight : 1080.0f) * 0.5f };
                const float projRadiusPixels{ (maxExtent / (std::max)(distance, 0.001f)) * matViewProj._22 * halfHeight };
                const bool isEmissive{ mat->emissiveColor.x > 0.001f || mat->emissiveColor.y > 0.001f || mat->emissiveColor.z > 0.001f };
                if (distance > 0.0f && projRadiusPixels < 0.5f && !isEmissive) continue;

                // Software Occlusion Culling
                if (mat->isOccludable && m_occlusionBuffer.IsOccluded(center, extents, matViewProj))
                {
                    continue;
                }

                const float clampedDist{ std::clamp(distance, 0.0f, 1000.0f) };
                const uint16_t quantizedDepth{ static_cast<uint16_t>((clampedDist / 1000.0f) * 65535.0f) };

                uint64_t passId{ 4 };
                uint64_t translucencyLayer{ 0 };
                uint16_t depthKey{ quantizedDepth };

                if (mat->alphaMode == AlphaMode::Blend || (mat->baseColor.w > 0.01f && mat->baseColor.w < 0.99f))
                {
                    passId = 6;
                    translucencyLayer = 1;
                    depthKey = static_cast<uint16_t>(65535 - quantizedDepth); // Back-to-front
                }
                else if (mat->alphaMode == AlphaMode::Mask)
                {
                    passId = 5;
                }

                const uint64_t shaderId{ static_cast<uint64_t>(mat->shaderId) };
                packet.key = BuildDrawKey(passId, translucencyLayer, shaderId, HashMaterial(mat), hashPointer(mesh.vertexBuffer.Get()), depthKey);

                if (passId == 6) transparentPackets[transparentCount++] = packet;
                else opaquePackets[opaqueCount++] = packet;

                if (ref.hasVelocity && passId < 6)
                {
                    DrawPacket velocityPacket{ packet };
                    velocityPacket.key = reinterpret_cast<uint64_t>(&mesh);
                    velocityPackets[velocityCount++] = velocityPacket;
                }

                if (shaderId == static_cast<uint64_t>(ShaderId::Toon) && mat->enableOutline && mat->outlineWidth > 0.0f)
                {
                    const float fadeEndSq{ mat->outlineFadeEnd * mat->outlineFadeEnd };
                    const float distSq{ dx * dx + dy * dy + dz * dz };
                    if (distSq <= (fadeEndSq + 25.0f))
                    {
                        DrawPacket outlinePacket{ packet };
                        outlinePacket.key = BuildDrawKey(4, 0, static_cast<uint64_t>(ShaderId::Toon), HashOutlineMaterial(mat), hashPointer(mesh.vertexBuffer.Get()), depthKey);
                        outlinePackets[outlineCount++] = outlinePacket;
                    }
                }
            }
        }
        drawInfos.clear();

        // Independent radix/stream sorting
        auto packetSort = [](const DrawPacket& a, const DrawPacket& b) noexcept
            {
                if (a.key != b.key) return a.key < b.key;
                return a.mesh < b.mesh; // Break key ties deterministically by geometry pointer
            };
        std::sort(opaquePackets, opaquePackets + opaqueCount, packetSort);
        std::sort(transparentPackets, transparentPackets + transparentCount, packetSort);
        std::sort(outlinePackets, outlinePackets + outlineCount, packetSort);
        std::sort(velocityPackets, velocityPackets + velocityCount, packetSort);
        for (int i{ 0 }; i < SHADOW_CASCADE_COUNT; ++i) std::sort(cascadePackets[i], cascadePackets[i] + cascadeCount[i], packetSort);

        // RENDER PASSES 
        if (rc.lightManager && rc.lightManager->GetDirectionalLight().castShadows)
        {
            ID3D11RenderTargetView* originalRTV{ nullptr };
            ID3D11DepthStencilView* originalDSV{ nullptr };
            dc->OMGetRenderTargets(1, &originalRTV, &originalDSV);

            UINT numViewports{ 1 };
            D3D11_VIEWPORT originalViewport{};
            dc->RSGetViewports(&numViewports, &originalViewport);

            D3D11_VIEWPORT shadowViewport{};
            shadowViewport.Width = static_cast<float>(SHADOW_MAP_SIZE);
            shadowViewport.Height = static_cast<float>(SHADOW_MAP_SIZE);
            shadowViewport.MaxDepth = 1.0f;

            dc->OMSetBlendState(rc.renderState->GetBlendState(BlendState::Opaque), nullptr, 0xFFFFFFFF);
            dc->OMSetDepthStencilState(rc.renderState->GetDepthStencilState(DepthState::TestAndWrite), 0);
            dc->RSSetState(rc.renderState->GetRasterizerState(RasterizerState::SolidCullBack));

            m_shadowCasterShader->Begin(rc);
            for (int i{ 0 }; i < SHADOW_CASCADE_COUNT; ++i)
            {
                ID3D11DepthStencilView* const cascadeDSV{ rc.lightManager->GetCascadeDSV(i) };
                // Pure D16 hardware depth clear
                dc->ClearDepthStencilView(cascadeDSV, D3D11_CLEAR_DEPTH, 1.0f, 0);
                dc->OMSetRenderTargets(0, nullptr, cascadeDSV);
                dc->RSSetViewports(1, &shadowViewport);

                m_shadowCasterShader->SetCascadeMatrix(dc, rc.lightManager->GetCascadeMatrices()[i]);
                executeBatches(cascadePackets[i], cascadeCount[i], m_shadowCasterShader.get());
            }
            m_shadowCasterShader->End(rc);

            dc->OMSetRenderTargets(1, &originalRTV, originalDSV);
            dc->RSSetViewports(1, &originalViewport);
            if (originalRTV) originalRTV->Release();
            if (originalDSV) originalDSV->Release();
        }

        {
            static LightManager defaultLightManager;
            const LightManager* const lightManager{ rc.lightManager ? rc.lightManager : &defaultLightManager };

            CbScene cbScene{};
            cbScene.viewProjection = matViewProj;
            const DirectionalLight& dirLight{ lightManager->GetDirectionalLight() };
            cbScene.lightDirection = { dirLight.direction.x, dirLight.direction.y, dirLight.direction.z, 0.0f };
            cbScene.lightColor = { dirLight.color.x * dirLight.intensity, dirLight.color.y * dirLight.intensity, dirLight.color.z * dirLight.intensity, 1.0f };
            const DirectX::XMFLOAT3& camPos{ rc.camera->GetPosition() };
            const DirectX::XMFLOAT3& camDir{ rc.camera->GetFront() };
            cbScene.cameraPosition = { camPos.x, camPos.y, camPos.z, 1.0f };
            cbScene.cameraDirection = { camDir.x, camDir.y, camDir.z, 0.0f };
            cbScene.ambientSkyColor = lightManager->GetEffectiveSkyColor();
            cbScene.ambientGroundColor = lightManager->GetEffectiveGroundColor();
            cbScene.packedParams = { rc.psxEnabled ? 1.0f : 0.0f, (std::max)(1.0f, rc.psxResWidth), (std::max)(1.0f, rc.psxResHeight), 0.0f };
            cbScene.lightCounts = { lightManager->GetPointLightCount(), lightManager->GetSpotLightCount(), 0, 0 };

            for (int i{ 0 }; i < 8; ++i) cbScene.pointLights[i] = lightManager->GetPointLights()[i];
            for (int i{ 0 }; i < 8; ++i) cbScene.spotLights[i] = lightManager->GetSpotLights()[i];

            if (dirLight.castShadows)
            {
                for (int i{ 0 }; i < SHADOW_CASCADE_COUNT; ++i) cbScene.cascadeMatrices[i] = lightManager->GetCascadeMatrices()[i];
                cbScene.cascadeSplits = { dirLight.splitDistances[1], dirLight.splitDistances[2], dirLight.splitDistances[3], 0.0f };
                cbScene.cascadeBias = { dirLight.shadowBias[0], dirLight.shadowBias[1], dirLight.shadowBias[2], 0.0f };
                cbScene.shadowSettings = { 1.0f, dirLight.shadowAttenuation, 0.0f, 0.0f };
            }
            else cbScene.shadowSettings = { 0.0f, 1.0f, 0.0f, 0.0f };

            dc->UpdateSubresource(sceneConstantBuffer.Get(), 0, 0, &cbScene, 0, 0);
        }

        ID3D11Buffer* const sceneCbs[] = { sceneConstantBuffer.Get() };
        dc->VSSetConstantBuffers(7, 1, sceneCbs);
        dc->PSSetConstantBuffers(7, 1, sceneCbs);

        ID3D11SamplerState* const samplers[] = { rc.renderState->GetSamplerState(SamplerState::AnisotropicWrap) };
        dc->PSSetSamplers(0, 1, samplers);

        ID3D11SamplerState* const shadowSampler = rc.renderState->GetSamplerState(SamplerState::ShadowMap);
        dc->PSSetSamplers(10, 1, &shadowSampler);

        if (rc.lightManager && rc.lightManager->GetDirectionalLight().castShadows)
        {
            ID3D11ShaderResourceView* const cascadeArraySRV{ rc.lightManager->GetCascadeArraySRV() };
            dc->PSSetShaderResources(10, 1, &cascadeArraySRV);
        }

        dc->OMSetDepthStencilState(rc.renderState->GetDepthStencilState(DepthState::TestAndWrite), 0);
        dc->RSSetState(rc.renderState->GetRasterizerState(RasterizerState::SolidCullBack));
        dc->OMSetBlendState(rc.renderState->GetBlendState(BlendState::Opaque), nullptr, 0xFFFFFFFF);

        // Main Opaque
        executeBatches(opaquePackets, opaqueCount);

        // Toon Outlines
        if (outlineCount > 0)
        {
            dc->RSSetState(rc.renderState->GetRasterizerState(RasterizerState::SolidCullFront));
            m_outlineShader->Begin(rc);
            executeBatches(outlinePackets, outlineCount, m_outlineShader.get());
            m_outlineShader->End(rc);
            dc->RSSetState(rc.renderState->GetRasterizerState(RasterizerState::SolidCullBack));
        }

        // Velocity Pass
        if (rc.velocityRenderTargetView && velocityCount > 0)
        {
            ID3D11RenderTargetView* originalRTV{ nullptr };
            ID3D11DepthStencilView* originalDSV{ nullptr };
            dc->OMGetRenderTargets(1, &originalRTV, &originalDSV);

            ID3D11RenderTargetView* const velocityRTV{ rc.velocityRenderTargetView };
            dc->OMSetRenderTargets(1, &velocityRTV, originalDSV);

            constexpr float s_zeroVelocity[4]{ 0.0f, 0.0f, 0.0f, 0.0f };
            dc->ClearRenderTargetView(velocityRTV, s_zeroVelocity);

            dc->OMSetDepthStencilState(rc.renderState->GetDepthStencilState(DepthState::TestOnly), 0);
            dc->OMSetBlendState(rc.renderState->GetBlendState(BlendState::Opaque), nullptr, 0xFFFFFFFF);

            m_velocityShader->Begin(rc);
            DirectX::XMFLOAT4X4 currentViewProjection{};
            DirectX::XMStoreFloat4x4(&currentViewProjection, DirectX::XMLoadFloat4x4(&rc.camera->GetView()) * DirectX::XMLoadFloat4x4(&rc.camera->GetProjection()));
            m_velocityShader->SetViewProjections(dc, currentViewProjection, rc.camera->GetPreviousViewProjection());

            executeBatches(velocityPackets, velocityCount, m_velocityShader.get());
            m_velocityShader->End(rc);

            dc->OMSetRenderTargets(1, &originalRTV, originalDSV);
            if (originalRTV) originalRTV->Release();
            if (originalDSV) originalDSV->Release();
        }

        // Transparent Pass
        dc->OMSetBlendState(rc.renderState->GetBlendState(BlendState::Transparency), nullptr, 0xFFFFFFFF);
        dc->OMSetDepthStencilState(rc.renderState->GetDepthStencilState(DepthState::TestOnly), 0);
        executeBatches(transparentPackets, transparentCount);

        // Unbind cleanly
        ID3D11Buffer* const nullBuffers[]{ nullptr, nullptr };
        UINT nullStrides[]{ 0, 0 };
        UINT nullOffsets[]{ 0, 0 };
        dc->IASetVertexBuffers(0, 2, nullBuffers, nullStrides, nullOffsets);

        dc->VSSetConstantBuffers(7, 1, nullBuffers);
        dc->PSSetConstantBuffers(7, 1, nullBuffers);
        ID3D11SamplerState* const nullSamplers[]{ nullptr };
        dc->PSSetSamplers(0, 1, nullSamplers);
        ID3D11ShaderResourceView* const nullShadowSRV{ nullptr };
        dc->PSSetShaderResources(10, 1, &nullShadowSRV);
}