#include <cmath>
#include <imgui.h>
#include "System/GpuResourceUtils.h"
#include "System/Graphics.h"
#include "Camera.h"
#include "GameObject.h"
#include "Light.h"
#include "LightComponent.h"

void LightManager::Initialize(ID3D11Device* device) noexcept
{
    // Single contiguous Texture2DArray
    D3D11_TEXTURE2D_DESC texDesc{};
    texDesc.Width = SHADOW_MAP_SIZE;
    texDesc.Height = SHADOW_MAP_SIZE;
    texDesc.MipLevels = 1;
    texDesc.ArraySize = SHADOW_CASCADE_COUNT;
    texDesc.Format = DXGI_FORMAT_R16_TYPELESS;
    texDesc.SampleDesc.Count = 1;
    texDesc.SampleDesc.Quality = 0;
    texDesc.Usage = D3D11_USAGE_DEFAULT;
    texDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
    texDesc.CPUAccessFlags = 0;
    texDesc.MiscFlags = 0;

    HRESULT hr = device->CreateTexture2D(&texDesc, nullptr, m_cascadeTextureArray.GetAddressOf());
    _ASSERT_EXPR(SUCCEEDED(hr), "Failed to create CSM Texture2DArray");

    // Unified SRV covering all cascade slices
    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = DXGI_FORMAT_R16_UNORM;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
    srvDesc.Texture2DArray.MostDetailedMip = 0;
    srvDesc.Texture2DArray.MipLevels = 1;
    srvDesc.Texture2DArray.FirstArraySlice = 0;
    srvDesc.Texture2DArray.ArraySize = SHADOW_CASCADE_COUNT;

    hr = device->CreateShaderResourceView(m_cascadeTextureArray.Get(), &srvDesc, m_cascadeArraySRV.GetAddressOf());
    _ASSERT_EXPR(SUCCEEDED(hr), "Failed to create CSM Array SRV");

    // Per-slice DSVs for single-pass hardware rasterization
    D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
    dsvDesc.Format = DXGI_FORMAT_D16_UNORM;
    dsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
    dsvDesc.Texture2DArray.MipSlice = 0;
    dsvDesc.Texture2DArray.ArraySize = 1;

    for (int i = 0; i < SHADOW_CASCADE_COUNT; ++i)
    {
        dsvDesc.Texture2DArray.FirstArraySlice = i;
        hr = device->CreateDepthStencilView(m_cascadeTextureArray.Get(), &dsvDesc, m_cascadeDSVs[i].GetAddressOf());
        _ASSERT_EXPR(SUCCEEDED(hr), "Failed to create CSM Slice DSV");
    }
}

void LightManager::Update() noexcept
{
    // Default sun direction if no active directional light exists
    bool hasDirectionalLight{ false };

    m_pointLightCount = 0;
    m_spotLightCount = 0;

    for (const auto* const light : m_lights)
    {
        if (!light || !light->GetOwner() || !light->GetOwner()->IsActive())
        {
            continue;
        }

        const DirectX::XMFLOAT3 pos{ light->GetWorldPosition() };
        const DirectX::XMFLOAT3 col{ light->GetColor() };
        const float intensity{ light->GetIntensity() };
        const float range{ light->GetRange() };

        switch (light->GetLightType())
        {
        case LightType::Directional:
            if (!hasDirectionalLight)
            {
                m_directionalLight.direction = light->GetDirection();
                m_directionalLight.color = col;
                m_directionalLight.intensity = intensity;

                const auto* const dirLightComp{ static_cast<const DirectionalLightComponent*>(light) };
                m_directionalLight.castShadows = dirLightComp->CastsShadows();
                m_directionalLight.shadowAttenuation = dirLightComp->GetShadowAttenuation();
                m_directionalLight.shadowBias = dirLightComp->GetShadowBias();
                m_directionalLight.splitDistances = dirLightComp->GetSplitDistances();

                hasDirectionalLight = true;
            }
            break;

        case LightType::Point:
            if (m_pointLightCount < 8)
            {
                auto& p{ m_pointLights[m_pointLightCount++] };
                p.positionAndRange = { pos.x, pos.y, pos.z, range };
                p.colorAndIntensity = { col.x, col.y, col.z, intensity };
            }
            break;

        case LightType::Spot:
            if (m_spotLightCount < 8)
            {
                auto& s{ m_spotLights[m_spotLightCount++] };
                const DirectX::XMFLOAT3 dir{ light->GetDirection() };
                const float angleCos{ std::cos(DirectX::XMConvertToRadians(light->GetSpotAngle() * 0.5f)) };

                s.positionAndRange = { pos.x, pos.y, pos.z, range };
                s.directionAndAngle = { dir.x, dir.y, dir.z, angleCos };
                s.colorAndIntensity = { col.x, col.y, col.z, intensity };
            }
            break;
        }
    }

    if (!hasDirectionalLight)
    {
        m_directionalLight.direction = { 0.0f, -0.707f, -0.707f };
        m_directionalLight.color = { 1.0f, 1.0f, 1.0f };
        m_directionalLight.intensity = 1.0f;
    }
}

void LightManager::UpdateCascades(const Camera& camera) noexcept
{
    if (!m_directionalLight.castShadows) return;

    const float fovY{ camera.GetFovY() };
    const float aspect{ camera.GetAspectRatio() };
    const DirectX::XMVECTOR camPos{ DirectX::XMLoadFloat3(&camera.GetPosition()) };
    const DirectX::XMVECTOR camFront{ DirectX::XMLoadFloat3(&camera.GetFront()) };
    const DirectX::XMVECTOR camUp{ DirectX::XMLoadFloat3(&camera.GetUp()) };
    const DirectX::XMVECTOR camRight{ DirectX::XMLoadFloat3(&camera.GetRight()) };

    DirectX::XMVECTOR lightDir{ DirectX::XMVector3Normalize(DirectX::XMLoadFloat3(&m_directionalLight.direction)) };

    DirectX::XMVECTOR upVec{ DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f) };
    if (std::abs(m_directionalLight.direction.y) > 0.999f)
    {
        upVec = DirectX::XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);
    }

    const float tanHalfFov{ std::tan(fovY * 0.5f) };

    for (int i{ 0 }; i < SHADOW_CASCADE_COUNT; ++i)
    {
        const float nearZ{ m_directionalLight.splitDistances[i] };
        const float farZ{ m_directionalLight.splitDistances[i + 1] };

        const float nearY{ tanHalfFov * nearZ };
        const float nearX{ nearY * aspect };
        const float farY{ tanHalfFov * farZ };
        const float farX{ farY * aspect };

        const DirectX::XMVECTOR nearCenter{ DirectX::XMVectorAdd(camPos, DirectX::XMVectorScale(camFront, nearZ)) };
        const DirectX::XMVECTOR farCenter{ DirectX::XMVectorAdd(camPos, DirectX::XMVectorScale(camFront, farZ)) };

        const std::array<DirectX::XMVECTOR, 8> frustumCorners{
            DirectX::XMVectorAdd(nearCenter, DirectX::XMVectorAdd(DirectX::XMVectorScale(camUp,  nearY), DirectX::XMVectorScale(camRight,  nearX))),
            DirectX::XMVectorAdd(nearCenter, DirectX::XMVectorAdd(DirectX::XMVectorScale(camUp,  nearY), DirectX::XMVectorScale(camRight, -nearX))),
            DirectX::XMVectorAdd(nearCenter, DirectX::XMVectorAdd(DirectX::XMVectorScale(camUp, -nearY), DirectX::XMVectorScale(camRight,  nearX))),
            DirectX::XMVectorAdd(nearCenter, DirectX::XMVectorAdd(DirectX::XMVectorScale(camUp, -nearY), DirectX::XMVectorScale(camRight, -nearX))),
            DirectX::XMVectorAdd(farCenter,  DirectX::XMVectorAdd(DirectX::XMVectorScale(camUp,  farY),  DirectX::XMVectorScale(camRight,  farX))),
            DirectX::XMVectorAdd(farCenter,  DirectX::XMVectorAdd(DirectX::XMVectorScale(camUp,  farY),  DirectX::XMVectorScale(camRight, -farX))),
            DirectX::XMVectorAdd(farCenter,  DirectX::XMVectorAdd(DirectX::XMVectorScale(camUp, -farY),  DirectX::XMVectorScale(camRight,  farX))),
            DirectX::XMVectorAdd(farCenter,  DirectX::XMVectorAdd(DirectX::XMVectorScale(camUp, -farY),  DirectX::XMVectorScale(camRight, -farX)))
        };

        // Rotation-Invariant Bounding Sphere calculation
        DirectX::XMVECTOR sphereCenter{ DirectX::XMVectorZero() };
        for (const auto& corner : frustumCorners)
        {
            sphereCenter = DirectX::XMVectorAdd(sphereCenter, corner);
        }
        sphereCenter = DirectX::XMVectorScale(sphereCenter, 1.0f / 8.0f);

        float sphereRadius{ 0.0f };
        for (const auto& corner : frustumCorners)
        {
            const float dist{ DirectX::XMVectorGetX(DirectX::XMVector3Length(DirectX::XMVectorSubtract(corner, sphereCenter))) };
            sphereRadius = (std::max)(sphereRadius, dist);
        }
        sphereRadius = std::ceil(sphereRadius);

        // Stabilized light view matrix centered on the bounding sphere
        const DirectX::XMVECTOR lightPos{ DirectX::XMVectorSubtract(sphereCenter, DirectX::XMVectorScale(lightDir, sphereRadius * 2.0f)) };
        DirectX::XMMATRIX lightView{ DirectX::XMMatrixLookAtLH(lightPos, sphereCenter, upVec) };

        // Metric World-Space Texel Grid Snapping
        const float worldUnitsPerTexel{ (sphereRadius * 2.0f) / static_cast<float>(SHADOW_MAP_SIZE) };
        const DirectX::XMVECTOR centerLightSpace{ DirectX::XMVector3TransformCoord(sphereCenter, lightView) };

        const float cx{ DirectX::XMVectorGetX(centerLightSpace) };
        const float cy{ DirectX::XMVectorGetY(centerLightSpace) };
        const float snappedX{ std::floor(cx / worldUnitsPerTexel) * worldUnitsPerTexel };
        const float snappedY{ std::floor(cy / worldUnitsPerTexel) * worldUnitsPerTexel };

        const float deltaX{ snappedX - cx };
        const float deltaY{ snappedY - cy };

        // Apply metric delta to light view translation directly to anchor world texels
        DirectX::XMFLOAT4X4 viewFloat;
        DirectX::XMStoreFloat4x4(&viewFloat, lightView);
        viewFloat._41 += deltaX;
        viewFloat._42 += deltaY;
        lightView = DirectX::XMLoadFloat4x4(&viewFloat);

        const float minZ{ 0.0f };
        const float maxZ{ sphereRadius * 4.0f };
        const DirectX::XMMATRIX lightProj{ DirectX::XMMatrixOrthographicOffCenterLH(-sphereRadius, sphereRadius, -sphereRadius, sphereRadius, minZ, maxZ) };

        DirectX::XMStoreFloat4x4(&m_cascadeMatrices[i], lightView * lightProj);
    }
}

void LightManager::LoadSkybox(ID3D11Device* device, const std::array<std::string, 6>& filepaths) noexcept
{
    m_skyboxPaths = filepaths;

    if (filepaths[0].empty())
    {
        ClearSkybox();
        return;
    }

    if (FAILED(GpuResourceUtils::LoadCubemap(device, m_skyboxPaths, m_skyboxSRV.ReleaseAndGetAddressOf())))
    {
        ClearSkybox();
    }
}

void LightManager::ClearSkybox() noexcept {
    m_skyboxSRV.Reset();
    m_skyboxPaths = {};
}

void LightManager::DrawEnvironmentGUI() noexcept
{
    ImGui::TextDisabled("ENVIRONMENT ILLUMINATION");
    ImGui::Separator();

    ImGui::ColorEdit3("Sky Ambient Color", &m_skyColor.x);
    ImGui::SliderFloat("Sky Intensity", &m_skyIntensity, 0.0f, 2.0f);

    ImGui::ColorEdit3("Ground Ambient Color", &m_groundColor.x);
    ImGui::SliderFloat("Ground Intensity", &m_groundIntensity, 0.0f, 2.0f);

    ImGui::Spacing();
    ImGui::Text("Active Registered Lights: %zu", m_lights.size());
    ImGui::TextDisabled("Points: %d/8 | Spots: %d/8", m_pointLightCount, m_spotLightCount);
}

void LightManager::Serialize(nlohmann::json& outJson) const
{
    outJson["SkyColor"] = { m_skyColor.x, m_skyColor.y, m_skyColor.z };
    outJson["SkyIntensity"] = m_skyIntensity;

    outJson["GroundColor"] = { m_groundColor.x, m_groundColor.y, m_groundColor.z };
    outJson["GroundIntensity"] = m_groundIntensity;

    outJson["SkyboxPaths"] = m_skyboxPaths;
}

void LightManager::Deserialize(const nlohmann::json& inJson)
{
    if (inJson.contains("SkyColor"))
    {
        m_skyColor = { inJson["SkyColor"][0], inJson["SkyColor"][1], inJson["SkyColor"][2] };
    }
    m_skyIntensity = inJson.value("SkyIntensity", 0.5f);

    if (inJson.contains("GroundColor"))
    {
        m_groundColor = { inJson["GroundColor"][0], inJson["GroundColor"][1], inJson["GroundColor"][2] };
    }
    m_groundIntensity = inJson.value("GroundIntensity", 0.5f);

    if (inJson.contains("SkyboxPaths"))
    {
        m_skyboxPaths = inJson["SkyboxPaths"].get<std::array<std::string, 6>>();
        LoadSkybox(Graphics::Instance().GetDevice(), m_skyboxPaths);
    }
}