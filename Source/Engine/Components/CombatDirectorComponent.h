#pragma once

#include <cstdint>
#include <json.hpp>
#include "IComponent.h"

// Scene-graph component interface bridging CombatDirector to the engine lifecycle
class CombatDirectorComponent final : public IComponent
{
public:
    CombatDirectorComponent() noexcept = default;
    ~CombatDirectorComponent() override;

    CombatDirectorComponent(const CombatDirectorComponent&) = delete;
    CombatDirectorComponent& operator=(const CombatDirectorComponent&) = delete;
    CombatDirectorComponent(CombatDirectorComponent&&) noexcept = delete;
    CombatDirectorComponent& operator=(CombatDirectorComponent&&) noexcept = delete;

    void OnAttach(GameObject* owner) noexcept override;
    void OnEnable() noexcept override;
    void OnDisable() noexcept override;
    void Update(float dt) override;
    void DrawGizmo(const GizmoContext& ctx) noexcept override;
    void DrawInspector() override;

    void Serialize(nlohmann::json& outJson) const override;
    void Deserialize(const nlohmann::json& inJson) override;

    [[nodiscard]] const char* GetTypeName() const noexcept override { return "CombatDirectorComponent"; }

private:
    float m_innerRadius{ 2.8f };
    float m_outerRadius{ 5.5f };
    std::uint32_t m_maxAttackTokens{ 1 };
    float m_cadenceInterval{ 0.85f };
    float m_postHitGraceDuration{ 0.70f };
    float m_wakeUpGraceDuration{ 0.85f };

    void PushSettingsToDirector() const noexcept;
};