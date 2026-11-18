#include <algorithm>
#include <cmath>
#include <limits>
#include "AnimationComponent.h"
#include "CapsuleColliderComponent.h"
#include "CharacterMovementComponent.h"
#include "GameObject.h"
#include "HealthComponent.h"
#include "OrbitCameraDriverComponent.h"
#include "PlayerControllerComponent.h"
#include "RespawnManager.h"
#include "VirtualCameraComponent.h"

void RespawnManager::ResetRuntimeState() noexcept
{
    m_hasQueuedRequest = false;
    m_activeRequest = PendingRespawnEvent{};
    m_phase = TransitionPhase::Idle;
    m_phaseTimer = 0.0f;
    m_fadeAlpha = 0.0f;
}

void RespawnManager::SetFallbackSpawn(const DirectX::XMFLOAT3& position, const float yawDegrees) noexcept
{
    m_fallbackPos = position;
    m_fallbackYaw = yawDegrees;
}

void RespawnManager::RegisterAnchor(const RespawnAnchorData& anchor) noexcept
{
    if (!anchor.owner) return;

    // Update in-place if already registered to prevent duplicate entries on reload
    for (auto& existing : m_anchors)
    {
        if (existing.owner == anchor.owner)
        {
            existing = anchor;
            return;
        }
    }

    m_anchors.push_back(anchor);
}

void RespawnManager::UnregisterAnchor(const GameObject* owner) noexcept
{
    if (!owner) return;

    const auto eraseIt{ std::remove_if(
        m_anchors.begin(),
        m_anchors.end(),
        [owner](const RespawnAnchorData& entry) noexcept
        {
            return entry.owner == owner;
        }
    ) };

    if (eraseIt != m_anchors.end())
    {
        m_anchors.erase(eraseIt, m_anchors.end());
    }
}

void RespawnManager::UpdateAnchorTransform(
    const GameObject* owner,
    const DirectX::XMFLOAT3& position,
    const float yawDegrees) noexcept
{
    if (!owner) return;

    for (auto& entry : m_anchors)
    {
        if (entry.owner == owner)
        {
            entry.position = position;
            entry.yawDegrees = yawDegrees;
            return;
        }
    }
}

const RespawnAnchorData* RespawnManager::FindAnchorById(const std::uint64_t id) const noexcept
{
    if (id == 0) return nullptr;

    for (const auto& entry : m_anchors)
    {
        if (entry.id == id)
        {
            return &entry;
        }
    }
    return nullptr;
}

const RespawnAnchorData* RespawnManager::FindNearestAnchor(const DirectX::XMFLOAT3& worldPos) const noexcept
{
    const RespawnAnchorData* bestAnchor{ nullptr };
    float bestDistSq{ (std::numeric_limits<float>::max)() };

    for (const auto& entry : m_anchors)
    {
        const float dx{ entry.position.x - worldPos.x };
        const float dy{ entry.position.y - worldPos.y };
        const float dz{ entry.position.z - worldPos.z };
        const float distSq{ (dx * dx) + (dy * dy) + (dz * dz) };

        if (distSq < bestDistSq)
        {
            bestDistSq = distSq;
            bestAnchor = &entry;
        }
    }

    return bestAnchor;
}

void RespawnManager::EnqueueRespawn(const PendingRespawnEvent& request) noexcept
{
    // Ignore overlapping triggers while a respawn sequence is already executing
    if (!request.playerObject || m_phase != TransitionPhase::Idle || m_hasQueuedRequest)
    {
        return;
    }

    m_activeRequest = request;
    m_hasQueuedRequest = true;
}

void RespawnManager::Update(const float unscaledDt) noexcept
{
    // Consume newly queued trigger event immediately after physics step
    if (m_hasQueuedRequest && m_phase == TransitionPhase::Idle)
    {
        m_hasQueuedRequest = false;

        if (!m_activeRequest.playerObject || m_activeRequest.playerObject->IsDestroyed())
        {
            return;
        }

        // Lock player controls immediately so they cannot dash/attack while falling into the void
        if (auto* playerCtrl{ m_activeRequest.playerObject->GetComponent<PlayerControllerComponent>() })
        {
            playerCtrl->SetInputEnabled(false);
        }

        // Optional hazard damage; if lethal, hand off cleanly to SceneGame's full death pipeline
        if (m_activeRequest.applyDamage && m_activeRequest.hazardDamage > 0.0f)
        {
            if (auto* health{ m_activeRequest.playerObject->GetComponent<HealthComponent>() })
            {
                health->ApplyDamage(m_activeRequest.hazardDamage, 0.0f, { 0.0f, 0.0f, 0.0f }, 0.0f);
                if (health->IsDead())
                {
                    ResetRuntimeState();
                    return;
                }
            }
        }

        m_phase = TransitionPhase::FadingOut;
        m_phaseTimer = 0.0f;
    }

    if (m_phase == TransitionPhase::Idle)
    {
        return;
    }

    switch (m_phase)
    {
    case TransitionPhase::FadingOut:
    {
        const float duration{ (std::max)(0.01f, m_activeRequest.fadeOutDuration) };
        m_phaseTimer += unscaledDt;
        m_fadeAlpha = std::clamp(m_phaseTimer / duration, 0.0f, 1.0f);

        if (m_fadeAlpha >= 1.0f)
        {
            ExecuteTeleportAndReset();
            m_phase = TransitionPhase::FadingIn;
            m_phaseTimer = 0.0f;
        }
        break;
    }

    case TransitionPhase::FadingIn:
    {
        const float duration{ (std::max)(0.01f, m_activeRequest.fadeInDuration) };
        m_phaseTimer += unscaledDt;
        const float t{ std::clamp(m_phaseTimer / duration, 0.0f, 1.0f) };
        m_fadeAlpha = 1.0f - t;

        if (t >= 1.0f)
        {
            if (m_activeRequest.playerObject && !m_activeRequest.playerObject->IsDestroyed())
            {
                if (auto* playerCtrl{ m_activeRequest.playerObject->GetComponent<PlayerControllerComponent>() })
                {
                    playerCtrl->SetInputEnabled(true);
                }
            }

            ResetRuntimeState();
        }
        break;
    }

    default:
        ResetRuntimeState();
        break;
    }
}

void RespawnManager::ExecuteTeleportAndReset() noexcept
{
    GameObject* player{ m_activeRequest.playerObject };
    if (!player || player->IsDestroyed()) return;

    // Resolve target anchor: Exact hash match -> Nearest anchor -> Play-mode boot fallback
    DirectX::XMFLOAT3 targetPos{ m_fallbackPos };
    float targetYawDeg{ m_fallbackYaw };

    if (const RespawnAnchorData * exactAnchor{ FindAnchorById(m_activeRequest.targetAnchorId) })
    {
        targetPos = exactAnchor->position;
        targetYawDeg = exactAnchor->yawDegrees;
    }
    else if (const RespawnAnchorData * nearestAnchor{ FindNearestAnchor(player->GetPosition()) })
    {
        targetPos = nearestAnchor->position;
        targetYawDeg = nearestAnchor->yawDegrees;
    }

    // Zero all residual fall velocity, dashes, and impulses before teleporting
    if (auto* movement{ player->GetComponent<CharacterMovementComponent>() })
    {
        movement->ClearKinematicOverride();
        movement->HaltMomentum(1.0f);
        movement->SetVerticalVelocity(0.0f);
    }

    // Atomically relocate PhysX character controller and GameObject transform
    player->SetPosition(targetPos);
    player->SetRotation({ 0.0f, targetYawDeg, 0.0f });

    if (auto* capsule{ player->GetComponent<CapsuleColliderComponent>() })
    {
        capsule->Teleport(targetPos);
    }

    // Return player state machine cleanly to grounded Locomotion
    if (auto* playerCtrl{ player->GetComponent<PlayerControllerComponent>() })
    {
        auto& bb{ playerCtrl->getAnimBlackboard() };
        bb.actionIndex = 0;

        if (auto* sm{ playerCtrl->GetStateMachine() })
        {
            sm->ChangeState(playerCtrl, playerCtrl->GetState(PlayerStateType::Locomotion));
        }
    }

    // Align orbit camera behind the player's new facing direction without cross-map whipping
    if (auto* vcam{ OrbitCameraDriverComponent::GetActiveVirtualCamera() })
    {
        vcam->SetOrbitYaw(DirectX::XMConvertToRadians(targetYawDeg + 180.0f));
        vcam->ForceSync();
    }
}