#pragma once

#include <cstdint>
#include <DirectXMath.h>
#include <json.hpp>
#include "IComponent.h"

// Marker component defining a safe designer-placed respawn location & facing direction
class RespawnPointComponent final : public IComponent
{
public:
    RespawnPointComponent() noexcept = default;
    ~RespawnPointComponent() override;

    RespawnPointComponent(const RespawnPointComponent&) = delete;
    RespawnPointComponent& operator=(const RespawnPointComponent&) = delete;

    void OnAttach(GameObject* owner) noexcept override;
    void OnEnable() noexcept override;
    void OnDisable() noexcept override;

    void DrawGizmo(const GizmoContext& ctx) noexcept override;
    void DrawInspector() override;

    void Serialize(nlohmann::json& outJson) const override;
    void Deserialize(const nlohmann::json& inJson) override;

    [[nodiscard]] const char* GetTypeName() const noexcept override { return "RespawnPointComponent"; }
    [[nodiscard]] std::uint64_t GetAnchorId() const noexcept { return m_anchorId; }
    [[nodiscard]] DirectX::XMFLOAT3 GetResolvedSpawnPosition() const noexcept;

private:
    void SyncWithManager() noexcept;

    std::uint64_t m_anchorId{ 0 };
    float         m_groundSnapOffset{ 0.05f }; // Prevents capsule floor clipping on teleport
    bool          m_showGizmo{ true };
};