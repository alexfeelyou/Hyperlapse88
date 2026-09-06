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
    D3D11_TEXTURE2D_DESC texDesc{};
    texDesc.Width = SHADOW_MAP_SIZE;
    texDesc.Height = SHADOW_MAP_SIZE;
    texDesc.MipLevels = 1;
    texDesc.ArraySize = 1;
    texDesc.Format = DXGI_FORMAT_R32_TYPELESS;
    texDesc.SampleDesc.Count = 1;
    texDesc.Usage = D3D11_USAGE_DEFAULT;
    texDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;

    D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
    dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
    dsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;

    for (int i = 0; i < SHADOW_CASCADE_COUNT; ++i)
    {
        Microsoft::WRL::ComPtr<ID3D11Texture2D> depthBuffer{};
        HRESULT hr = device->CreateTexture2D(&texDesc, nullptr, depthBuffer.GetAddressOf());
        _ASSERT_EXPR(SUCCEEDED(hr), "Failed to create CSM Texture");

        hr = device->CreateDepthStencilView(depthBuffer.Get(), &dsvDesc, m_cascadeDSVs[i].GetAddressOf());
        _ASSERT_EXPR(SUCCEEDED(hr), "Failed to create CSM DSV");

        hr = device->CreateShaderResourceView(depthBuffer.Get(), &srvDesc, m_cascadeSRVs[i].GetAddressOf());
        _ASSERT_EXPR(SUCCEEDED(hr), "Failed to create CSM SRV");

        m_cascadeSRVptrs[i] = m_cascadeSRVs[i].Get();
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

    const float fovY = camera.GetFovY();
    const float aspect = camera.GetAspectRatio();
    const DirectX::XMVECTOR camPos = DirectX::XMLoadFloat3(&camera.GetPosition());
    const DirectX::XMVECTOR camFront = DirectX::XMLoadFloat3(&camera.GetFront());
    const DirectX::XMVECTOR camUp = DirectX::XMLoadFloat3(&camera.GetUp());
    const DirectX::XMVECTOR camRight = DirectX::XMLoadFloat3(&camera.GetRight());

    // Establish the base Light View-Projection bounding the entire scene
    const DirectX::XMVECTOR lightDir = DirectX::XMLoadFloat3(&m_directionalLight.direction);
    
    // Anchor the shadow projection around the camera's current X/Z position
    DirectX::XMVECTOR camTarget = DirectX::XMVectorSet(camera.GetPosition().x, 0.0f, camera.GetPosition().z, 0.0f);
    DirectX::XMVECTOR lightPos = DirectX::XMVectorAdd(camTarget, DirectX::XMVectorScale(lightDir, -500.0f));

    // Prevent Gimbal Lock crash if the light looks straight down
    DirectX::XMVECTOR upVec = DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    if (std::abs(m_directionalLight.direction.y) > 0.999f) {
        upVec = DirectX::XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);
    }

    const DirectX::XMMATRIX V = DirectX::XMMatrixLookAtLH(lightPos, camTarget, upVec);
    const DirectX::XMMATRIX P = DirectX::XMMatrixOrthographicLH(10000.0f, 10000.0f, 0.1f, 1000.0f);
    const DirectX::XMMATRIX LVP_Base = V * P;

    // Crop Matrix Generation for each cascade
    for (int i = 0; i < SHADOW_CASCADE_COUNT; ++i)
    {
        const float nearZ = m_directionalLight.splitDistances[i];
        const float farZ = m_directionalLight.splitDistances[i + 1];

        const float nearY = std::tan(fovY * 0.5f) * nearZ;
        const float nearX = nearY * aspect;
        const float farY = std::tan(fovY * 0.5f) * farZ;
        const float farX = farY * aspect;

        const DirectX::XMVECTOR nearCenter = DirectX::XMVectorAdd(camPos, DirectX::XMVectorScale(camFront, nearZ));
        const DirectX::XMVECTOR farCenter = DirectX::XMVectorAdd(camPos, DirectX::XMVectorScale(camFront, farZ));

        // 8 Corners of the sub-frustum
        const std::array<DirectX::XMVECTOR, 8> frustumCorners = {
            DirectX::XMVectorAdd(nearCenter, DirectX::XMVectorAdd(DirectX::XMVectorScale(camUp, nearY), DirectX::XMVectorScale(camRight, nearX))),
            DirectX::XMVectorAdd(nearCenter, DirectX::XMVectorAdd(DirectX::XMVectorScale(camUp, nearY), DirectX::XMVectorScale(camRight, -nearX))),
            DirectX::XMVectorAdd(nearCenter, DirectX::XMVectorAdd(DirectX::XMVectorScale(camUp, -nearY), DirectX::XMVectorScale(camRight, nearX))),
            DirectX::XMVectorAdd(nearCenter, DirectX::XMVectorAdd(DirectX::XMVectorScale(camUp, -nearY), DirectX::XMVectorScale(camRight, -nearX))),
            DirectX::XMVectorAdd(farCenter,  DirectX::XMVectorAdd(DirectX::XMVectorScale(camUp, farY),  DirectX::XMVectorScale(camRight, farX))),
            DirectX::XMVectorAdd(farCenter,  DirectX::XMVectorAdd(DirectX::XMVectorScale(camUp, farY),  DirectX::XMVectorScale(camRight, -farX))),
            DirectX::XMVectorAdd(farCenter,  DirectX::XMVectorAdd(DirectX::XMVectorScale(camUp, -farY), DirectX::XMVectorScale(camRight, farX))),
            DirectX::XMVectorAdd(farCenter,  DirectX::XMVectorAdd(DirectX::XMVectorScale(camUp, -farY), DirectX::XMVectorScale(camRight, -farX)))
        };

        // Find min/max bounds in Light NDC space
        DirectX::XMFLOAT2 vMin{ FLT_MAX, FLT_MAX };
        DirectX::XMFLOAT2 vMax{ -FLT_MAX, -FLT_MAX };

        for (const auto& corner : frustumCorners)
        {
            DirectX::XMFLOAT3 p;
            DirectX::XMStoreFloat3(&p, DirectX::XMVector3TransformCoord(corner, LVP_Base));
            vMin.x = (std::min)(p.x, vMin.x);
            vMin.y = (std::min)(p.y, vMin.y);
            vMax.x = (std::max)(p.x, vMax.x);
            vMax.y = (std::max)(p.y, vMax.y);
        }

        // Build 2D scale/translate Crop Matrix
        const float xScale = 2.0f / (vMax.x - vMin.x);
        const float yScale = 2.0f / (vMax.y - vMin.y);
        const float xOff = -0.5f * (vMax.x + vMin.x) * xScale;
        const float yOff = -0.5f * (vMax.y + vMin.y) * yScale;

        DirectX::XMFLOAT4X4 cropMat;
        DirectX::XMStoreFloat4x4(&cropMat, DirectX::XMMatrixIdentity());
        cropMat._11 = xScale;
        cropMat._22 = yScale;
        cropMat._41 = xOff;
        cropMat._42 = yOff;

        DirectX::XMStoreFloat4x4(&m_cascadeMatrices[i], LVP_Base * DirectX::XMLoadFloat4x4(&cropMat));
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