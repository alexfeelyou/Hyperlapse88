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
    [[nodiscard]] const char* GetTypeName() const noexcept override { return "DirectionalLightComponent"; }
    [[nodiscard]] LightType GetLightType() const noexcept override { return LightType::Directional; }
};

class PointLightComponent final : public LightComponent
{
public:
    void DrawInspector() override;
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