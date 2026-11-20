#include <cmath>
#include <imgui.h>
#include "System/RespawnManager.h"
#include "System/ShapeRenderer.h"
#include "ComponentRegistry.h"
#include "EditorManager.h"
#include "GameObject.h"
#include "RespawnPointComponent.h"
#include "StringHash.h"

RespawnPointComponent::~RespawnPointComponent()
{
    if (m_owner)
    {
        RespawnManager::Instance().UnregisterAnchor(m_owner);
    }
}

void RespawnPointComponent::OnAttach(GameObject* owner) noexcept
{
    IComponent::OnAttach(owner);
    SyncWithManager();
}

void RespawnPointComponent::OnEnable() noexcept
{
    SyncWithManager();
}

void RespawnPointComponent::OnDisable() noexcept
{
    if (m_owner)
    {
        RespawnManager::Instance().UnregisterAnchor(m_owner);
    }
}

DirectX::XMFLOAT3 RespawnPointComponent::GetResolvedSpawnPosition() const noexcept
{
    if (!m_owner) return { 0.0f, 0.0f, 0.0f };

    const DirectX::XMFLOAT4X4 worldMat{ m_owner->transform.GetWorldMatrix() };
    return {
        worldMat._41,
        worldMat._42 + m_groundSnapOffset,
        worldMat._43
    };
}

void RespawnPointComponent::SyncWithManager() noexcept
{
    if (!m_owner) return;

    m_anchorId = Core::RuntimeHash(m_owner->GetName());

    if (!m_owner->IsActive())
    {
        RespawnManager::Instance().UnregisterAnchor(m_owner);
        return;
    }

    RespawnAnchorData pod{};
    pod.id = m_anchorId;
    pod.position = GetResolvedSpawnPosition();
    pod.yawDegrees = m_owner->GetRotation().y;
    pod.owner = m_owner;

    RespawnManager::Instance().RegisterAnchor(pod);
}

void RespawnPointComponent::DrawGizmo(const GizmoContext& ctx) noexcept
{
    if (!m_owner || !ctx.shapes) return;

    // Keep POD transform synchronized while designer drags the anchor in Edit Mode
    SyncWithManager();

    const bool isSelected{ EditorManager::Instance().IsObjectSelected(m_owner) };
    const bool categoryVisible{ (ctx.categoryMask & static_cast<std::uint32_t>(GizmoCategory::Triggers)) != 0 };

    if (!isSelected && (!m_showGizmo || !categoryVisible))
    {
        return;
    }

    const DirectX::XMFLOAT3 spawnPos{ GetResolvedSpawnPosition() };
    const float yawRad{ DirectX::XMConvertToRadians(m_owner->GetRotation().y) };

    const DirectX::XMFLOAT4 primaryColor{ isSelected
        ? DirectX::XMFLOAT4{ 1.0f, 0.90f, 0.15f, 1.0f }   // Bright Gold when selected
        : DirectX::XMFLOAT4{ 0.15f, 0.95f, 0.85f, 0.85f } // Cyan-Teal in scene overview
    };

    // Draw character-sized reference capsule so designers immediately see clearance & floor snap
    constexpr float previewRadius{ 0.37f };
    constexpr float previewHeight{ 1.03f };
    const float centerOffsetY{ (previewHeight * 0.5f) + previewRadius };

    DirectX::XMFLOAT4X4 capsuleWorld{};
    DirectX::XMStoreFloat4x4(
        &capsuleWorld,
        DirectX::XMMatrixRotationY(yawRad) *
        DirectX::XMMatrixTranslation(spawnPos.x, spawnPos.y + centerOffsetY, spawnPos.z)
    );
    ctx.shapes->DrawCapsule(capsuleWorld, previewRadius, previewHeight, primaryColor);

    // Draw forward facing arrow (1.6m) so spawn orientation is unmistakable
    const float fwdX{ std::sin(yawRad) };
    const float fwdZ{ std::cos(yawRad) };
    const DirectX::XMFLOAT3 arrowStart{ spawnPos.x, spawnPos.y + 0.9f, spawnPos.z };
    const DirectX::XMFLOAT3 arrowTip{ spawnPos.x + (fwdX * 1.6f), spawnPos.y + 0.9f, spawnPos.z + (fwdZ * 1.6f) };
    ctx.shapes->DrawLine(arrowStart, arrowTip, primaryColor);
}

void RespawnPointComponent::DrawInspector()
{
    ImGui::TextDisabled("Respawn Anchor Node");
    ImGui::Separator();

    // Re-sync in case the designer renamed the GameObject in the Hierarchy/Inspector
    SyncWithManager();

    ImGui::Text("Anchor Name: %s", m_owner ? m_owner->GetName().c_str() : "None");
    ImGui::TextDisabled("Hash ID: 0x%016llX", static_cast<unsigned long long>(m_anchorId));

    if (ImGui::DragFloat("Ground Snap Offset", &m_groundSnapOffset, 0.01f, 0.0f, 2.0f, "%.2f m"))
    {
        SyncWithManager();
    }

    ImGui::Checkbox("Show Editor Gizmo", &m_showGizmo);
}

void RespawnPointComponent::Serialize(nlohmann::json& outJson) const
{
    outJson["GroundSnapOffset"] = m_groundSnapOffset;
    outJson["ShowGizmo"] = m_showGizmo;
}

void RespawnPointComponent::Deserialize(const nlohmann::json& inJson)
{
    m_groundSnapOffset = inJson.value("GroundSnapOffset", 0.05f);
    m_showGizmo = inJson.value("ShowGizmo", true);
    SyncWithManager();
}

REGISTER_COMPONENT(RespawnPointComponent)