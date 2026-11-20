#include <algorithm>
#include <cstring>
#include <imgui.h>
#include "System/CollisionLayer.h"
#include "System/RespawnManager.h"
#include "System/ShapeRenderer.h"
#include "ComponentRegistry.h"
#include "EditorManager.h"
#include "GameObject.h"
#include "RespawnPointComponent.h"
#include "RespawnTriggerComponent.h"
#include "StaticMeshColliderComponent.h"
#include "StringHash.h"

void RespawnTriggerComponent::OnAttach(GameObject* owner) noexcept
{
    IComponent::OnAttach(owner);
    ConfigureTriggerCollider();
    RefreshTargetHash();
}

void RespawnTriggerComponent::ConfigureTriggerCollider() noexcept
{
    if (!m_owner) return;

    auto* collider{ m_owner->GetComponent<StaticMeshColliderComponent>() };
    if (!collider)
    {
        StaticColliderConfig cfg{};
        cfg.shapeType = ColliderShapeType::Box;
        cfg.layer = CollisionLayer::TriggerVolume;
        cfg.collidesWith = CollisionLayer::Player;
        cfg.proxyExtents = { 10.0f, 2.0f, 10.0f };
        cfg.isTrigger = true;
        m_owner->AddComponent<StaticMeshColliderComponent>(cfg);
        return;
    }

    auto& cfg{ collider->GetConfig() };
    cfg.layer = CollisionLayer::TriggerVolume;
    cfg.collidesWith = CollisionLayer::Player;
    cfg.isTrigger = true;
    collider->MarkDirty();
}

void RespawnTriggerComponent::RefreshTargetHash() noexcept
{
    m_targetSpawnHash = m_targetSpawnName.empty() ? 0 : Core::RuntimeHash(m_targetSpawnName);
}

void RespawnTriggerComponent::OnPlayerEntered(GameObject* playerObject) const noexcept
{
    if (!playerObject) return;

    PendingRespawnEvent evt{};
    evt.playerObject = playerObject;
    evt.targetAnchorId = m_targetSpawnHash;
    evt.fadeOutDuration = m_fadeOutDuration;
    evt.fadeInDuration = m_fadeInDuration;
    evt.hazardDamage = m_hazardDamage;
    evt.applyDamage = m_applyHazardDamage;

    RespawnManager::Instance().EnqueueRespawn(evt);
}

void RespawnTriggerComponent::DrawGizmo(const GizmoContext& ctx) noexcept
{
    if (!m_owner || !ctx.shapes) return;

    const RespawnAnchorData* linkedAnchor{ RespawnManager::Instance().FindAnchorById(m_targetSpawnHash) };
    const bool isTriggerSelected{ EditorManager::Instance().IsObjectSelected(m_owner) };
    const bool isAnchorSelected{ linkedAnchor && EditorManager::Instance().IsObjectSelected(linkedAnchor->owner) };
    const bool isHighlighted{ isTriggerSelected || isAnchorSelected };

    const bool categoryVisible{ (ctx.categoryMask & static_cast<std::uint32_t>(GizmoCategory::Triggers)) != 0 };
    if (!isHighlighted && (!m_showGizmo || !categoryVisible))
    {
        return;
    }

    // Compute world-space center of the trigger box
    DirectX::XMFLOAT3 boxCenter{ m_owner->GetPosition() };
    DirectX::XMFLOAT3 boxRotRad{
        DirectX::XMConvertToRadians(m_owner->GetRotation().x),
        DirectX::XMConvertToRadians(m_owner->GetRotation().y),
        DirectX::XMConvertToRadians(m_owner->GetRotation().z)
    };
    DirectX::XMFLOAT3 halfExtents{ 1.0f, 1.0f, 1.0f };

    if (auto* collider{ m_owner->GetComponent<StaticMeshColliderComponent>() })
    {
        const auto& cfg{ collider->GetConfig() };
        const DirectX::XMFLOAT4X4 worldMat{ m_owner->transform.GetWorldMatrix() };
        const DirectX::XMMATRIX objWorld{ DirectX::XMLoadFloat4x4(&worldMat) };

        DirectX::XMVECTOR vScale{}, vRotQuat{}, vTrans{};
        DirectX::XMMatrixDecompose(&vScale, &vRotQuat, &vTrans, objWorld);

        DirectX::XMFLOAT3 wScale{};
        DirectX::XMStoreFloat3(&wScale, vScale);
        halfExtents = {
            (std::max)(0.001f, cfg.proxyExtents.x * std::abs(wScale.x) * 0.5f),
            (std::max)(0.001f, cfg.proxyExtents.y * std::abs(wScale.y) * 0.5f),
            (std::max)(0.001f, cfg.proxyExtents.z * std::abs(wScale.z) * 0.5f)
        };

        const DirectX::XMVECTOR localOff{ DirectX::XMLoadFloat3(&cfg.localOffset) };
        DirectX::XMStoreFloat3(&boxCenter, DirectX::XMVector3TransformCoord(localOff, objWorld));
    }

    const DirectX::XMFLOAT4 volumeColor{ isHighlighted
        ? DirectX::XMFLOAT4{ 1.0f, 0.35f, 0.15f, 1.0f }   // Bright Hazard Orange when selected
        : DirectX::XMFLOAT4{ 0.95f, 0.20f, 0.25f, 0.70f } // Crimson OOB wireframe
    };
    ctx.shapes->DrawBox(boxCenter, boxRotRad, halfExtents, volumeColor);

    // Draw tether line to the linked RespawnPoint so level flow is visually verifiable
    if (linkedAnchor)
    {
        const DirectX::XMFLOAT4 tetherColor{ isHighlighted
            ? DirectX::XMFLOAT4{ 1.0f, 0.90f, 0.15f, 1.0f }
            : DirectX::XMFLOAT4{ 0.15f, 0.95f, 0.85f, 0.55f }
        };
        ctx.shapes->DrawLine(boxCenter, linkedAnchor->position, tetherColor);
    }
}

void RespawnTriggerComponent::DrawInspector()
{
    ImGui::TextDisabled("Out-of-Bounds / Restricted Area Trigger");
    ImGui::Separator();

    // Quick-pick dropdown listing all active RespawnPoints in the scene
    const auto& anchors{ RespawnManager::Instance().GetAnchors() };
    const char* previewLabel{ m_targetSpawnName.empty() ? "<Nearest Anchor Fallback>" : m_targetSpawnName.c_str() };

    if (ImGui::BeginCombo("Target Respawn Point", previewLabel))
    {
        if (ImGui::Selectable("<Nearest Anchor Fallback>", m_targetSpawnName.empty()))
        {
            m_targetSpawnName.clear();
            RefreshTargetHash();
        }

        for (const auto& anchor : anchors)
        {
            if (!anchor.owner) continue;
            const std::string& anchorName{ anchor.owner->GetName() };
            const bool isSelected{ anchorName == m_targetSpawnName };
            if (ImGui::Selectable(anchorName.c_str(), isSelected))
            {
                m_targetSpawnName = anchorName;
                RefreshTargetHash();
            }
        }
        ImGui::EndCombo();
    }

    // Manual text input override (also useful before the target anchor is instantiated)
    char nameBuf[128]{};
    strncpy_s(nameBuf, sizeof(nameBuf), m_targetSpawnName.c_str(), _TRUNCATE);
    if (ImGui::InputText("Target Name (Exact)", nameBuf, sizeof(nameBuf)))
    {
        m_targetSpawnName = nameBuf;
        RefreshTargetHash();
    }

    // Live link status indicator
    if (m_targetSpawnName.empty())
    {
        ImGui::TextColored(ImVec4{ 0.3f, 0.85f, 1.0f, 1.0f }, "Mode: Auto-selects nearest RespawnPoint");
    }
    else if (RespawnManager::Instance().FindAnchorById(m_targetSpawnHash) != nullptr)
    {
        ImGui::TextColored(ImVec4{ 0.2f, 0.95f, 0.3f, 1.0f }, "Status: Linked to '%s'", m_targetSpawnName.c_str());
    }
    else
    {
        ImGui::TextColored(ImVec4{ 1.0f, 0.65f, 0.1f, 1.0f }, "Warning: '%s' not found (will use nearest)", m_targetSpawnName.c_str());
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextDisabled("TRANSITION & PENALTY");
    ImGui::DragFloat("Fade Out (s)", &m_fadeOutDuration, 0.02f, 0.05f, 3.0f, "%.2f s");
    ImGui::DragFloat("Fade In (s)", &m_fadeInDuration, 0.02f, 0.05f, 3.0f, "%.2f s");
    ImGui::Checkbox("Apply Hazard Damage", &m_applyHazardDamage);
    if (m_applyHazardDamage)
    {
        ImGui::DragFloat("Hazard Damage", &m_hazardDamage, 1.0f, 0.0f, 500.0f, "%.0f HP");
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Checkbox("Show Editor Gizmo", &m_showGizmo);

    if (auto* collider{ m_owner ? m_owner->GetComponent<StaticMeshColliderComponent>() : nullptr })
    {
        IComponent* selectedComp{ EditorManager::Instance().GetSelectedComponent() };
        const bool isEditingBounds{ selectedComp == collider };
        if (ImGui::Button(isEditingBounds ? "Stop Editing Trigger Box" : "Edit Trigger Box Bounds", ImVec2{ -1.0f, 28.0f }))
        {
            EditorManager::Instance().SetSelectedComponent(isEditingBounds ? nullptr : collider);
        }
    }
}

void RespawnTriggerComponent::Serialize(nlohmann::json& outJson) const
{
    outJson["TargetSpawnName"] = m_targetSpawnName;
    outJson["FadeOutDuration"] = m_fadeOutDuration;
    outJson["FadeInDuration"] = m_fadeInDuration;
    outJson["ApplyHazardDamage"] = m_applyHazardDamage;
    outJson["HazardDamage"] = m_hazardDamage;
    outJson["ShowGizmo"] = m_showGizmo;
}

void RespawnTriggerComponent::Deserialize(const nlohmann::json& inJson)
{
    m_targetSpawnName = inJson.value("TargetSpawnName", "");
    m_fadeOutDuration = inJson.value("FadeOutDuration", 0.35f);
    m_fadeInDuration = inJson.value("FadeInDuration", 0.35f);
    m_applyHazardDamage = inJson.value("ApplyHazardDamage", false);
    m_hazardDamage = inJson.value("HazardDamage", 15.0f);
    m_showGizmo = inJson.value("ShowGizmo", true);

    ConfigureTriggerCollider();
    RefreshTargetHash();
}

REGISTER_COMPONENT(RespawnTriggerComponent)