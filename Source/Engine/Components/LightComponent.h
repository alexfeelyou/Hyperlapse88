#pragma once

#include <DirectXMath.h>
#include "ComponentRegistry.h"
#include "IComponent.h"
#include "Light.h"

// Abstract base class for all light types
class LightComponent : public IComponent
{
public:
    LightComponent() = default;
    ~LightComponent() override; // Virtual by inheritance

    LightComponent(const LightComponent&) = delete;
    LightComponent& operator=(const LightComponent&) = delete;
    LightComponent(LightComponent&&) noexcept = default;
    LightComponent& operator=(LightComponent&&) noexcept = default;

    void OnAttach(GameObject* owner) noexcept override;

    [[nodiscard]] virtual LightType GetLightType() const noexcept = 0;

    // World transformation accessors
    [[nodiscard]] DirectX::XMFLOAT3 GetDirection() const noexcept;
    [[nodiscard]] DirectX::XMFLOAT3 GetWorldPosition() const noexcept;

    // Shared property accessors
    [[nodiscard]] DirectX::XMFLOAT3 GetColor() const noexcept { return m_color; }
    void SetColor(const DirectX::XMFLOAT3& color) noexcept { m_color = color; }

    [[nodiscard]] float GetIntensity() const noexcept { return m_intensity; }
    void SetIntensity(float intensity) noexcept { m_intensity = intensity; }

    // Virtual getters with safe defaults so LightManager can query blindly without dynamic_cast
    [[nodiscard]] virtual float GetRange() const noexcept { return 0.0f; }
    [[nodiscard]] virtual float GetSpotAngle() const noexcept { return 0.0f; }

protected:
    DirectX::XMFLOAT3 m_color{ 1.0f, 1.0f, 1.0f };
    float             m_intensity{ 1.0f };
};

class DirectionalLightComponent final : public LightComponent
{
public:
    void DrawInspector() override;
    void Serialize(nlohmann::json& outJson) const override;
    void Deserialize(const nlohmann::json& inJson) override;

    [[nodiscard]] const char* GetTypeName() const noexcept override { return "DirectionalLightComponent"; }
    [[nodiscard]] LightType GetLightType() const noexcept override { return LightType::Directional; }

    // Shadow mapping properties
    [[nodiscard]] bool CastsShadows() const noexcept { return m_castShadows; }
    void SetCastShadows(bool cast) noexcept { m_castShadows = cast; }
    [[nodiscard]] float GetShadowAttenuation() const noexcept { return m_shadowAttenuation; }
    [[nodiscard]] float GetShadowDistance() const noexcept { return m_shadowDistance; }
    void SetShadowDistance(float dist) noexcept { m_shadowDistance = dist; CalculateSplitDistances(m_splitDistances[0]); }
    [[nodiscard]] float GetSplitLambda() const noexcept { return m_splitLambda; }
    void SetSplitLambda(float lambda) noexcept { m_splitLambda = lambda; CalculateSplitDistances(m_splitDistances[0]); }
    [[nodiscard]] const std::array<float, SHADOW_CASCADE_COUNT>& GetShadowBias() const noexcept { return m_shadowBias; }
    [[nodiscard]] const std::array<float, SHADOW_CASCADE_COUNT + 1>& GetSplitDistances() const noexcept { return m_splitDistances; }

    void CalculateSplitDistances(float nearZ) noexcept;

private:
    bool  m_castShadows{ true };
    float m_shadowAttenuation{ 0.5f };
    float m_shadowDistance{ 150.0f };
    float m_splitLambda{ 0.85f };
    std::array<float, SHADOW_CASCADE_COUNT> m_shadowBias{ 0.0005f, 0.0012f, 0.0025f };
    std::array<float, SHADOW_CASCADE_COUNT + 1> m_splitDistances{ 0.1f, 8.5f, 26.0f, 150.0f };
};

class PointLightComponent final : public LightComponent
{
public:
    void DrawInspector() override;
    void Serialize(nlohmann::json& outJson) const override;
    void Deserialize(const nlohmann::json& inJson) override;

    [[nodiscard]] const char* GetTypeName() const noexcept override { return "PointLightComponent"; }
    [[nodiscard]] LightType GetLightType() const noexcept override { return LightType::Point; }

    [[nodiscard]] float GetRange() const noexcept override { return m_range; }
    void SetRange(float range) noexcept { m_range = range; }

private:
    float m_range{ 10.0f };
};

class SpotLightComponent final : public LightComponent
{
public:
    void DrawInspector() override;
    void Serialize(nlohmann::json& outJson) const override;
    void Deserialize(const nlohmann::json& inJson) override;

    [[nodiscard]] const char* GetTypeName() const noexcept override { return "SpotLightComponent"; }
    [[nodiscard]] LightType GetLightType() const noexcept override { return LightType::Spot; }

    [[nodiscard]] float GetRange() const noexcept override { return m_range; }
    void SetRange(float range) noexcept { m_range = range; }

    [[nodiscard]] float GetSpotAngle() const noexcept override { return m_spotAngle; }
    void SetSpotAngle(float angle) noexcept { m_spotAngle = angle; }

private:
    float m_range{ 10.0f };
    float m_spotAngle{ 45.0f }; // Degrees
};