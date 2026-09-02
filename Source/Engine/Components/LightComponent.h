#pragma once

#include <DirectXMath.h>
#include "ComponentRegistry.h"
#include "IComponent.h"
#include "Light.h"

// Component enabling a GameObject to act as a physical light source
class LightComponent final : public IComponent
{
public:
    LightComponent() = default;
    ~LightComponent() override;

    LightComponent(const LightComponent&) = delete;
    LightComponent& operator=(const LightComponent&) = delete;
    LightComponent(LightComponent&&) noexcept = default;
    LightComponent& operator=(LightComponent&&) noexcept = default;

    void OnAttach(GameObject* owner) noexcept override;
    void DrawInspector() override;

    [[nodiscard]] const char* GetTypeName() const noexcept override { return "LightComponent"; }

    // World transformation accessors
    [[nodiscard]] DirectX::XMFLOAT3 GetDirection() const noexcept;
    [[nodiscard]] DirectX::XMFLOAT3 GetWorldPosition() const noexcept;

    // Property accessors
    [[nodiscard]] LightType GetLightType() const noexcept { return m_type; }
    void setLightType(LightType type) noexcept { m_type = type; }

    [[nodiscard]] DirectX::XMFLOAT3 GetColor() const noexcept { return m_color; }
    void setColor(const DirectX::XMFLOAT3& color) noexcept { m_color = color; }

    [[nodiscard]] float GetIntensity() const noexcept { return m_intensity; }
    void setIntensity(float intensity) noexcept { m_intensity = intensity; }

    [[nodiscard]] float GetRange() const noexcept { return m_range; }
    void setRange(float range) noexcept { m_range = range; }

    [[nodiscard]] float GetSpotAngle() const noexcept { return m_spotAngle; }
    void setSpotAngle(float angle) noexcept { m_spotAngle = angle; }

private:
    LightType         m_type{ LightType::Directional };
    DirectX::XMFLOAT3 m_color{ 1.0f, 1.0f, 1.0f };
    float             m_intensity{ 1.0f };
    float             m_range{ 10.0f };
    float             m_spotAngle{ 45.0f }; // Degrees
};