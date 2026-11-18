#pragma once

#include <cstdint>
#include <DirectXMath.h>
#include <json.hpp>
#include <string>
#include "IComponent.h"

// Out-of-Bounds / Restricted Area trigger volume that routes the player to a named RespawnPoint.
// Automatically configures a sibling StaticMeshColliderComponent as a TriggerVolume on attach.
class RespawnTriggerComponent final : public IComponent
{
public:
    RespawnTriggerComponent() noexcept = default;
    ~RespawnTriggerComponent() override = default;

    RespawnTriggerComponent(const RespawnTriggerComponent&) = delete;
    RespawnTriggerComponent& operator=(const RespawnTriggerComponent&) = delete;

    void OnAttach(GameObject* owner) noexcept override;
    void DrawGizmo(const GizmoContext& ctx) noexcept override;
    void DrawInspector() override;

    void Serialize(nlohmann::json& outJson) const override;
    void Deserialize(const nlohmann::json& inJson) override;

    [[nodiscard]] const char* GetTypeName() const noexcept override { return "RespawnTriggerComponent"; }

    // Called by PhysicsManager's PxSimulationEventCallback when Player touches this trigger
    void OnPlayerEntered(GameObject* playerObject) const noexcept;

private:
    void ConfigureTriggerCollider() noexcept;
    void RefreshTargetHash() noexcept;

    std::string   m_targetSpawnName{};
    std::uint64_t m_targetSpawnHash{ 0 };

    float         m_fadeOutDuration{ 0.35f };
    float         m_fadeInDuration{ 0.35f };
    bool          m_applyHazardDamage{ false };
    float         m_hazardDamage{ 15.0f };
    bool          m_showGizmo{ true };
};