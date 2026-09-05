#pragma once

#include <algorithm>
#include <array>
#include <d3d11.h>
#include <DirectXMath.h>
#include <json.hpp>
#include <string>
#include <vector>
#include <wrl/client.h>

// Forward declaration
class LightComponent;

// Categorizes lighting calculation algorithms
enum class LightType : std::uint8_t
{
    Directional = 0,
    Point,
    Spot
};

// Represents direct sun/moon illumination
struct DirectionalLight
{
    DirectX::XMFLOAT3 direction{ 0.0f, -0.707f, -0.707f };
    DirectX::XMFLOAT3 color{ 1.0f, 1.0f, 1.0f };
    float             intensity{ 1.0f };
};

struct PointLightData
{
    DirectX::XMFLOAT4 positionAndRange{};  
    DirectX::XMFLOAT4 colorAndIntensity{}; 
};

struct SpotLightData
{
    DirectX::XMFLOAT4 positionAndRange{};  
    DirectX::XMFLOAT4 directionAndAngle{}; 
    DirectX::XMFLOAT4 colorAndIntensity{}; 
};

// Manages global scene illumination and collects active light components
class LightManager
{
public:
    LightManager() = default;
    ~LightManager() = default;

    LightManager(const LightManager&) = delete;
    LightManager& operator=(const LightManager&) = delete;
    LightManager(LightManager&&) noexcept = default;
    LightManager& operator=(LightManager&&) noexcept = default;

    // Registers a light component into the tracking pool
    void RegisterLight(LightComponent* const light) noexcept
    {
        if (!light) return;
        m_lights.push_back(light);
    }

    // Removes a light component upon destruction or deactivation
    void UnregisterLight(LightComponent* const light) noexcept
    {
        const auto it = std::remove(m_lights.begin(), m_lights.end(), light);
        m_lights.erase(it, m_lights.end());
    }

    // Aggregates active light components and updates lighting states
    void Update() noexcept;

	// Loads a skybox texture 
    void LoadSkybox(ID3D11Device* device, std::string_view filepath) noexcept;
    void ClearSkybox() noexcept;

    // Renders ImGui controls for ambient sky and ground illumination
    void DrawEnvironmentGUI() noexcept;

	// Serialization 
    void Serialize(nlohmann::json& outJson) const;
    void Deserialize(const nlohmann::json& inJson);

    // Direct lighting accessors
    [[nodiscard]] const DirectionalLight& GetDirectionalLight() const noexcept { return m_directionalLight; }
    [[nodiscard]] const std::array<PointLightData, 8>& GetPointLights() const noexcept { return m_pointLights; }
    [[nodiscard]] int GetPointLightCount() const noexcept { return m_pointLightCount; }

    [[nodiscard]] const std::array<SpotLightData, 8>& GetSpotLights() const noexcept { return m_spotLights; }
    [[nodiscard]] int GetSpotLightCount() const noexcept { return m_spotLightCount; }

    [[nodiscard]] DirectX::XMFLOAT4 GetEffectiveSkyColor() const noexcept
    {
        return { m_skyColor.x * m_skyIntensity, m_skyColor.y * m_skyIntensity, m_skyColor.z * m_skyIntensity, 1.0f };
    }

    [[nodiscard]] DirectX::XMFLOAT4 GetEffectiveGroundColor() const noexcept
    {
        return { m_groundColor.x * m_groundIntensity, m_groundColor.y * m_groundIntensity, m_groundColor.z * m_groundIntensity, 1.0f };
    }

    [[nodiscard]] ID3D11ShaderResourceView* GetSkyboxSRV() const noexcept { return m_skyboxSRV.Get(); }
    [[nodiscard]] bool HasSkybox() const noexcept { return m_skyboxSRV != nullptr; }

private:
    std::vector<LightComponent*>  m_lights{};

    DirectionalLight              m_directionalLight{};
    std::array<PointLightData, 8> m_pointLights{};
    std::array<SpotLightData, 8>  m_spotLights{};

    int m_pointLightCount{ 0 };
    int m_spotLightCount{ 0 };

    DirectX::XMFLOAT3 m_skyColor{ 0.6f, 0.6f, 0.65f };
    float             m_skyIntensity{ 0.5f };

    DirectX::XMFLOAT3 m_groundColor{ 0.2f, 0.2f, 0.2f };
    float             m_groundIntensity{ 0.5f };

    std::string m_skyboxPath{};
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_skyboxSRV{};
};