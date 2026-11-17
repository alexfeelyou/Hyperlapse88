#include <algorithm>
#include <cmath>
#include "System/ShapeRenderer.h"
#include "AIControllerComponent.h"
#include "CombatDirector.h"
#include "GameObject.h"
#include "HealthComponent.h"
#include "IComponent.h"
#include "OrbitCameraDriverComponent.h"
#include "PlayerControllerComponent.h"

CombatDirector::CombatDirector() noexcept
{
    m_enemies.reserve(64);
}

void CombatDirector::RegisterEnemy(AIControllerComponent* enemy) noexcept
{
    if (!enemy) return;

    if (std::find(m_enemies.begin(), m_enemies.end(), enemy) == m_enemies.end())
    {
        m_enemies.push_back(enemy);
    }
}

void CombatDirector::UnregisterEnemy(AIControllerComponent* enemy) noexcept
{
    if (!enemy) return;

    ReleaseAttackToken(enemy);

    for (auto& slot : m_innerSlots)
    {
        if (slot.occupant == enemy)
        {
            slot.occupant = nullptr;
            slot.isOccupied = false;
        }
    }

    for (auto& slot : m_outerSlots)
    {
        if (slot.occupant == enemy)
        {
            slot.occupant = nullptr;
            slot.isOccupied = false;
        }
    }

    const auto it{ std::find(m_enemies.begin(), m_enemies.end(), enemy) };
    if (it != m_enemies.end())
    {
        m_enemies.erase(it);
    }
}

void CombatDirector::Clear() noexcept
{
    m_enemies.clear();
    m_tokenHolders.fill(nullptr);
    m_preferredAttacker = nullptr;
    m_tokenDispatchTimer = 0.0f;
    m_postHitGraceTimer = 0.0f;
    m_wakeUpGraceTimer = 0.0f;
    m_isPlayerActionLocked = false;
    m_wasPlayerKnockedDown = false;

    for (auto& slot : m_innerSlots)
    {
        slot.occupant = nullptr;
        slot.isOccupied = false;
    }
    for (auto& slot : m_outerSlots)
    {
        slot.occupant = nullptr;
        slot.isOccupied = false;
    }
}

void CombatDirector::UpdateSlotPositions(const GameObject* playerObject, const float /*dt*/) noexcept
{
    if (!playerObject) return;

    const DirectX::XMFLOAT4X4 pWorld{ playerObject->transform.GetWorldMatrix() };
    const DirectX::XMFLOAT3 playerPos{ pWorld._41, pWorld._42, pWorld._43 };

    // World-fixed orientation: combat slots translate with player position but do not rotate with player yaw.
    constexpr float innerStep{ DirectX::XM_2PI / static_cast<float>(s_innerSlotCount) };
    for (std::size_t i{ 0 }; i < s_innerSlotCount; ++i)
    {
        const float angle{ innerStep * static_cast<float>(i) };
        m_innerSlots[i].worldPosition = {
            playerPos.x + (std::sin(angle) * m_innerRadius),
            playerPos.y,
            playerPos.z + (std::cos(angle) * m_innerRadius)
        };
    }

    constexpr float outerStep{ DirectX::XM_2PI / static_cast<float>(s_outerSlotCount) };
    constexpr float outerPhase{ outerStep * 0.5f };
    for (std::size_t i{ 0 }; i < s_outerSlotCount; ++i)
    {
        const float angle{ outerPhase + (outerStep * static_cast<float>(i)) };
        m_outerSlots[i].worldPosition = {
            playerPos.x + (std::sin(angle) * m_outerRadius),
            playerPos.y,
            playerPos.z + (std::cos(angle) * m_outerRadius)
        };
    }
}

void CombatDirector::SetMaxAttackTokens(const std::uint32_t tokens) noexcept
{
    m_maxAttackTokens = std::clamp(tokens, 1u, static_cast<std::uint32_t>(s_maxSupportedTokens));
}

void CombatDirector::EvaluatePlayerCombatWindow(const float dt, GameObject* playerObject) noexcept
{
    if (m_postHitGraceTimer > 0.0f)
    {
        m_postHitGraceTimer = (std::max)(0.0f, m_postHitGraceTimer - dt);
    }
    if (m_wakeUpGraceTimer > 0.0f)
    {
        m_wakeUpGraceTimer = (std::max)(0.0f, m_wakeUpGraceTimer - dt);
    }

    const auto* playerCtrl{ playerObject ? playerObject->GetComponent<PlayerControllerComponent>() : nullptr };
    const bool isKnockedDown{ playerCtrl && playerCtrl->IsInKnockdownRecovery() };
    const bool isInHitReact{ playerCtrl && playerCtrl->IsInHitReact() };
    const bool isParrying{ playerCtrl && playerCtrl->IsInParryState() };

    if (isKnockedDown)
    {
        // Freeze new attacks and cancel unstarted enemy windups while player is knocked down or getting up
        m_isPlayerActionLocked = true;
        RevokePendingWindups();
    }
    else
    {
        // Transition out of Knockdown/GetUp: grant guaranteed wake-up protection window
        if (m_wasPlayerKnockedDown)
        {
            m_wakeUpGraceTimer = (std::max)(m_wakeUpGraceTimer, m_wakeUpGraceDuration);
        }

        if (isInHitReact)
        {
            // Light stagger: prevent immediate follow-up dogpile from surrounding enemies
            m_isPlayerActionLocked = true;
            m_postHitGraceTimer = (std::max)(m_postHitGraceTimer, m_postHitGraceDuration);
            RevokePendingWindups();
        }
        else if (isParrying)
        {
            // Active parry/counter: abort unstarted windups from other enemies so parry isn't interrupted
            m_isPlayerActionLocked = true;
            RevokePendingWindups();
        }
        else
        {
            m_isPlayerActionLocked = false;
        }
    }

    m_wasPlayerKnockedDown = isKnockedDown;
}

void CombatDirector::RevokePendingWindups() noexcept
{
    for (auto* holder : m_tokenHolders)
    {
        if (holder && holder->GetCurrentState() == EnemyState::attack_windup)
        {
            holder->CancelWindupToChase();
        }
    }
}

void CombatDirector::SelectPreferredAttacker(const DirectX::XMFLOAT3& playerPos) noexcept
{
    m_preferredAttacker = nullptr;

    const float camYawRad{ OrbitCameraDriverComponent::GetActiveYawRadians() };
    const float camFwdX{ std::sin(camYawRad) };
    const float camFwdZ{ std::cos(camYawRad) };

    float bestScore{ -999999.0f };

    for (const auto& slot : m_innerSlots)
    {
        AIControllerComponent* enemy{ slot.occupant };
        if (!enemy || !enemy->GetOwner() || enemy->GetOwner()->IsDestroyed())
        {
            continue;
        }
        if (enemy->GetCurrentState() != EnemyState::chase)
        {
            continue;
        }
        if (enemy->GetBlackboard().attackCooldownTimer > 0.0f)
        {
            continue;
        }

        // Consider enemies at or approaching attack range
        const float readyRange{ enemy->GetConfig().attackRange + 0.65f };
        const float distSq{ enemy->GetBlackboard().targetDistanceSq };
        if (distSq > (readyRange * readyRange))
        {
            continue;
        }

        const DirectX::XMFLOAT4X4 eWorld{ enemy->GetOwner()->transform.GetWorldMatrix() };
        const float dx{ eWorld._41 - playerPos.x };
        const float dz{ eWorld._43 - playerPos.z };
        const float lenSq{ (dx * dx) + (dz * dz) };

        float camDot{ 1.0f };
        if (lenSq > 0.0001f)
        {
            const float invLen{ 1.0f / std::sqrt(lenSq) };
            camDot = ((dx * invLen) * camFwdX) + ((dz * invLen) * camFwdZ);
        }

        // Strongly prioritize on-screen enemies in front of the camera over off-screen blindsides
        const float visibilityScore{ (camDot >= 0.0f) ? (2.0f + camDot) : (camDot * 2.0f) };
        const float proximityScore{ 1.0f - (std::sqrt(distSq) / (std::max)(0.1f, readyRange)) };
        const float totalScore{ visibilityScore + proximityScore };

        if (totalScore > bestScore)
        {
            bestScore = totalScore;
            m_preferredAttacker = enemy;
        }
    }
}

void CombatDirector::Update(const float dt, GameObject* playerObject) noexcept
{
    if (!playerObject)
    {
        return;
    }

    // Evaluate player vulnerability / parry / wake-up grace before advancing attack cadence
    EvaluatePlayerCombatWindow(dt, playerObject);

    // Advance token dispatch cadence timer only while player is actionable
    if (!IsPlayerSafeFromNewAttacks() && m_tokenDispatchTimer > 0.0f)
    {
        m_tokenDispatchTimer -= dt;
    }

    UpdateSlotPositions(playerObject, dt);

    // Predicate evaluating if an entity is deceased, destroyed, or pending removal
    const auto isEnemyDeadOrInvalid = [](const AIControllerComponent* e) noexcept {
        if (!e || !e->GetOwner() || e->GetOwner()->IsDestroyed()) return true;
        if (e->GetCurrentState() == EnemyState::dead) return true;
        const auto* health{ e->GetOwner()->GetComponent<HealthComponent>() };
        return health && health->IsDead();
        };

    // Immediately evict dead or destroyed enemies from director pool
    const auto deadIt{ std::remove_if(m_enemies.begin(), m_enemies.end(), isEnemyDeadOrInvalid) };
    if (deadIt != m_enemies.end())
    {
        m_enemies.erase(deadIt, m_enemies.end());
    }

    // Helper predicate: verifies if an enemy is actively in combat pursuit or attacking
    const auto isEnemyInCombat = [](const AIControllerComponent* e) noexcept {
        if (!e) return false;
        const EnemyState st{ e->GetCurrentState() };
        return st == EnemyState::chase ||
            st == EnemyState::attack_windup ||
            st == EnemyState::attack_active ||
            st == EnemyState::hit_react ||
            st == EnemyState::airborne_juggle ||
            st == EnemyState::ambush_burst;
        };

    // Clean up vacated slots: evict dead enemies or enemies that returned to idle/patrol
    for (auto& slot : m_innerSlots)
    {
        if (slot.isOccupied && (isEnemyDeadOrInvalid(slot.occupant) || !isEnemyInCombat(slot.occupant)))
        {
            ReleaseAttackToken(slot.occupant);
            slot.isOccupied = false;
            slot.occupant = nullptr;
        }
    }
    for (auto& slot : m_outerSlots)
    {
        if (slot.isOccupied && (isEnemyDeadOrInvalid(slot.occupant) || !isEnemyInCombat(slot.occupant)))
        {
            ReleaseAttackToken(slot.occupant);
            slot.isOccupied = false;
            slot.occupant = nullptr;
        }
    }

    const DirectX::XMFLOAT4X4 pWorld{ playerObject->transform.GetWorldMatrix() };
    const DirectX::XMFLOAT3 playerPos{ pWorld._41, pWorld._42, pWorld._43 };

    struct Candidate
    {
        AIControllerComponent* enemy{ nullptr };
        float distSq{ 0.0f };
    };

    static std::vector<Candidate> s_candidates{};
    s_candidates.clear();

    for (auto* enemy : m_enemies)
    {
        // Strictly filter candidates: only actively engaged combatants can claim ring slots
        if (!isEnemyInCombat(enemy))
        {
            continue;
        }

        const DirectX::XMFLOAT4X4 eWorld{ enemy->GetOwner()->transform.GetWorldMatrix() };
        const float dx{ eWorld._41 - playerPos.x };
        const float dz{ eWorld._43 - playerPos.z };
        s_candidates.push_back({ enemy, (dx * dx) + (dz * dz) });
    }

    std::sort(s_candidates.begin(), s_candidates.end(), [](const Candidate& a, const Candidate& b) noexcept {
        return a.distSq < b.distSq;
        });

    // Snapshot previous occupants for hysteresis to avoid slot ping-ponging when equidistant
    std::array<AIControllerComponent*, s_innerSlotCount> prevInnerOccupants{};
    for (std::size_t i{ 0 }; i < s_innerSlotCount; ++i)
    {
        prevInnerOccupants[i] = m_innerSlots[i].occupant;
        m_innerSlots[i].isOccupied = false;
        m_innerSlots[i].occupant = nullptr;
    }

    std::array<AIControllerComponent*, s_outerSlotCount> prevOuterOccupants{};
    for (std::size_t i{ 0 }; i < s_outerSlotCount; ++i)
    {
        prevOuterOccupants[i] = m_outerSlots[i].occupant;
        m_outerSlots[i].isOccupied = false;
        m_outerSlots[i].occupant = nullptr;
    }

    // Dynamic slot allocation: assign closest available slot to each candidate's current position
    for (const auto& cand : s_candidates)
    {
        const DirectX::XMFLOAT4X4 eWorld{ cand.enemy->GetOwner()->transform.GetWorldMatrix() };

        float bestInnerDistSq{ 999999.0f };
        int bestInnerSlotIdx{ -1 };

        for (std::size_t s{ 0 }; s < s_innerSlotCount; ++s)
        {
            if (m_innerSlots[s].isOccupied) continue;

            const float dsx{ m_innerSlots[s].worldPosition.x - eWorld._41 };
            const float dsz{ m_innerSlots[s].worldPosition.z - eWorld._43 };
            float dSq{ (dsx * dsx) + (dsz * dsz) };

            // Hysteresis bias: favor retaining the slot held on the prior frame to eliminate boundary jitter
            if (prevInnerOccupants[s] == cand.enemy)
            {
                dSq -= 1.0f;
            }

            if (dSq < bestInnerDistSq)
            {
                bestInnerDistSq = dSq;
                bestInnerSlotIdx = static_cast<int>(s);
            }
        }

        if (bestInnerSlotIdx >= 0)
        {
            m_innerSlots[bestInnerSlotIdx].isOccupied = true;
            m_innerSlots[bestInnerSlotIdx].occupant = cand.enemy;
            continue;
        }

        // Inner ring is full: assign to nearest available outer ring slot
        float bestOuterDistSq{ 999999.0f };
        int bestOuterSlotIdx{ -1 };

        for (std::size_t s{ 0 }; s < s_outerSlotCount; ++s)
        {
            if (m_outerSlots[s].isOccupied) continue;

            const float dsx{ m_outerSlots[s].worldPosition.x - eWorld._41 };
            const float dsz{ m_outerSlots[s].worldPosition.z - eWorld._43 };
            float dSq{ (dsx * dsx) + (dsz * dsz) };

            if (prevOuterOccupants[s] == cand.enemy)
            {
                dSq -= 1.0f;
            }

            if (dSq < bestOuterDistSq)
            {
                bestOuterDistSq = dSq;
                bestOuterSlotIdx = static_cast<int>(s);
            }
        }

        if (bestOuterSlotIdx >= 0)
        {
            m_outerSlots[bestOuterSlotIdx].isOccupied = true;
            m_outerSlots[bestOuterSlotIdx].occupant = cand.enemy;
        }
    }

    // Sanitize stale token holders that are no longer actively attacking
    for (auto*& holder : m_tokenHolders)
    {
        if (!holder) continue;
        const EnemyState st{ holder->GetCurrentState() };
        if (isEnemyDeadOrInvalid(holder) || (st != EnemyState::attack_windup && st != EnemyState::attack_active))
        {
            holder = nullptr;
        }
    }

    // Select the highest-priority visible frontline attacker for deterministic token arbitration
    SelectPreferredAttacker(playerPos);
}

bool CombatDirector::HasAttackToken(const AIControllerComponent* enemy) const noexcept
{
    if (!enemy) return false;

    for (const auto* holder : m_tokenHolders)
    {
        if (holder == enemy)
        {
            return true;
        }
    }
    return false;
}

std::uint32_t CombatDirector::GetActiveTokenCount() const noexcept
{
    std::uint32_t count{ 0 };
    for (const auto* holder : m_tokenHolders)
    {
        if (holder != nullptr)
        {
            ++count;
        }
    }
    return count;
}

bool CombatDirector::IsPlayerSafeFromNewAttacks() const noexcept
{
    return m_isPlayerActionLocked || (m_postHitGraceTimer > 0.0f) || (m_wakeUpGraceTimer > 0.0f);
}

bool CombatDirector::CanBeginActiveSwing(const AIControllerComponent* enemy) const noexcept
{
    if (!enemy || IsPlayerSafeFromNewAttacks() || !HasAttackToken(enemy))
    {
        return false;
    }

    // Single-Active-Hitbox Invariant: prevent two enemies from entering active slash frames simultaneously
    for (const auto* holder : m_tokenHolders)
    {
        if (holder && holder != enemy && holder->GetCurrentState() == EnemyState::attack_active)
        {
            return false;
        }
    }
    return true;
}

bool CombatDirector::RequestAttackToken(AIControllerComponent* enemy) noexcept
{
    if (!enemy || IsPlayerSafeFromNewAttacks())
    {
        return false;
    }

    if (HasAttackToken(enemy))
    {
        return true;
    }

    // Frontline validation: enemy must be assigned to the inner ring or physically within inner radius combat space
    const SwarmRing ring{ GetAssignedRing(enemy) };
    if (ring != SwarmRing::inner)
    {
        const float innerRadiusSq{ m_innerRadius * m_innerRadius };
        if (enemy->GetBlackboard().targetDistanceSq > innerRadiusSq)
        {
            return false;
        }
    }

    // Camera-frustum fairness: if a visible preferred attacker is ready, defer off-screen challengers
    if (m_preferredAttacker && m_preferredAttacker != enemy)
    {
        return false;
    }

    const std::uint32_t activeCount{ GetActiveTokenCount() };
    if (activeCount < m_maxAttackTokens && m_tokenDispatchTimer <= 0.0f)
    {
        for (std::size_t i{ 0 }; i < m_maxAttackTokens && i < s_maxSupportedTokens; ++i)
        {
            if (!m_tokenHolders[i])
            {
                m_tokenHolders[i] = enemy;
                m_tokenDispatchTimer = m_tokenCadenceInterval;
                return true;
            }
        }
    }

    return false;
}

std::size_t CombatDirector::GetOccupiedInnerSlotCount() const noexcept
{
    std::size_t count{ 0 };
    for (const auto& slot : m_innerSlots)
    {
        if (slot.isOccupied) ++count;
    }
    return count;
}

std::size_t CombatDirector::GetOccupiedOuterSlotCount() const noexcept
{
    std::size_t count{ 0 };
    for (const auto& slot : m_outerSlots)
    {
        if (slot.isOccupied) ++count;
    }
    return count;
}

void CombatDirector::ReleaseAttackToken(AIControllerComponent* enemy) noexcept
{
    if (!enemy) return;

    bool released{ false };
    for (auto*& holder : m_tokenHolders)
    {
        if (holder == enemy)
        {
            holder = nullptr;
            released = true;
        }
    }

    // Start post-attack cadence timer upon token release so the next attacker waits after the combo finishes
    if (released)
    {
        m_tokenDispatchTimer = (std::max)(m_tokenDispatchTimer, m_tokenCadenceInterval);
    }
}

DirectX::XMFLOAT3 CombatDirector::GetAssignedSlotPosition(const AIControllerComponent* enemy) const noexcept
{
    if (!enemy) return { 0.0f, 0.0f, 0.0f };

    for (const auto& slot : m_innerSlots)
    {
        if (slot.occupant == enemy) return slot.worldPosition;
    }
    for (const auto& slot : m_outerSlots)
    {
        if (slot.occupant == enemy) return slot.worldPosition;
    }

    return { 0.0f, 0.0f, 0.0f };
}

SwarmRing CombatDirector::GetAssignedRing(const AIControllerComponent* enemy) const noexcept
{
    if (!enemy) return SwarmRing::none;

    for (const auto& slot : m_innerSlots)
    {
        if (slot.occupant == enemy) return SwarmRing::inner;
    }
    for (const auto& slot : m_outerSlots)
    {
        if (slot.occupant == enemy) return SwarmRing::outer;
    }

    return SwarmRing::none;
}

DirectX::XMFLOAT3 CombatDirector::ComputeSeparationForce(const AIControllerComponent* enemy) const noexcept
{
    if (!enemy || !enemy->GetOwner()) return { 0.0f, 0.0f, 0.0f };

    const DirectX::XMFLOAT4X4 myMat{ enemy->GetOwner()->transform.GetWorldMatrix() };
    const DirectX::XMFLOAT3 myPos{ myMat._41, myMat._42, myMat._43 };

    DirectX::XMFLOAT3 separation{ 0.0f, 0.0f, 0.0f };
    constexpr float separationRadius{ 1.35f }; // Expanded bubble for clean spacing between attackers
    constexpr float separationRadiusSq{ separationRadius * separationRadius };

    for (const auto* other : m_enemies)
    {
        if (!other || other == enemy || !other->GetOwner() || other->GetCurrentState() == EnemyState::dead || other->GetCurrentState() == EnemyState::wall_hang || other->GetCurrentState() == EnemyState::wall_scramble) continue;

        const DirectX::XMFLOAT4X4 otherMat{ other->GetOwner()->transform.GetWorldMatrix() };
        const float dx{ myPos.x - otherMat._41 };
        const float dz{ myPos.z - otherMat._43 };
        const float distSq{ (dx * dx) + (dz * dz) };

        if (distSq > 0.001f && distSq < separationRadiusSq)
        {
            const float dist{ std::sqrt(distSq) };
            const float invDist{ 1.0f / dist };
            const float weight{ 1.0f - (dist / separationRadius) };
            separation.x += (dx * invDist) * weight;
            separation.z += (dz * invDist) * weight;
        }
    }

    return separation;
}

AIControllerComponent* CombatDirector::FindNearestAirborneEnemy(const DirectX::XMFLOAT3& origin, const float maxDistance) const noexcept
{
    AIControllerComponent* bestEnemy{ nullptr };
    float bestDistSq{ maxDistance * maxDistance };

    for (auto* enemy : m_enemies)
    {
        if (!enemy || !enemy->GetOwner() || enemy->GetOwner()->IsDestroyed()) continue;
        if (enemy->GetCurrentState() != EnemyState::airborne_juggle) continue;

        const DirectX::XMFLOAT4X4 eWorld{ enemy->GetOwner()->transform.GetWorldMatrix() };
        const float dx{ eWorld._41 - origin.x };
        const float dy{ eWorld._42 - origin.y };
        const float dz{ eWorld._43 - origin.z };
        const float distSq{ (dx * dx) + (dy * dy) + (dz * dz) };

        if (distSq < bestDistSq)
        {
            bestDistSq = distSq;
            bestEnemy = enemy;
        }
    }
    return bestEnemy;
}

bool CombatDirector::TryConsumeParryOpportunity(const DirectX::XMFLOAT3& playerPos, DirectX::XMFLOAT3& outAttackerPos) noexcept
{
    AIControllerComponent* bestEnemy{ nullptr };
    DirectX::XMFLOAT3 bestPos{ 0.0f, 0.0f, 0.0f };
    float bestDistSq{ 999999.0f };

    for (auto* enemy : m_enemies)
    {
        if (!enemy || !enemy->HasOpenParryWindow())
        {
            continue;
        }

        const auto* health{ enemy->GetOwner()->GetComponent<HealthComponent>() };
        if (health && health->IsDead())
        {
            continue;
        }

        const DirectX::XMFLOAT4X4 eWorld{ enemy->GetOwner()->transform.GetWorldMatrix() };
        const float dy{ std::abs(eWorld._42 - playerPos.y) };
        if (dy > 2.0f)
        {
            continue;
        }

        const float dx{ eWorld._41 - playerPos.x };
        const float dz{ eWorld._43 - playerPos.z };
        const float distSq{ (dx * dx) + (dz * dz) };

        // Scale parry reach to the enemy's authored melee attack range plus blade lunge margin
        const float maxParryRange{ enemy->GetConfig().attackRange + 1.2f };
        const float maxParryRangeSq{ maxParryRange * maxParryRange };
        if (distSq > maxParryRangeSq)
        {
            continue;
        }

        if (distSq < bestDistSq)
        {
            bestDistSq = distSq;
            bestEnemy = enemy;
            bestPos = { eWorld._41, eWorld._42, eWorld._43 };
        }
    }

    if (!bestEnemy)
    {
        return false;
    }

    // Atomically lock this enemy's current swing window so F cannot be spammed even after locomotion/evade cancels
    bestEnemy->ConsumeParryOpportunity();
    outAttackerPos = bestPos;
    return true;
}

void CombatDirector::DrawGizmos(const GizmoContext& ctx) noexcept
{
    if (!(ctx.categoryMask & static_cast<std::uint32_t>(GizmoCategory::Navigation)) || !ctx.shapes)
    {
        return;
    }

    // Draw inner slot indicators (Bright Yellow if occupied, Pale Yellow if vacant)
    for (const auto& slot : m_innerSlots)
    {
        const DirectX::XMFLOAT4 col = slot.isOccupied
            ? DirectX::XMFLOAT4{ 1.0f, 0.85f, 0.1f, 0.9f }
        : DirectX::XMFLOAT4{ 1.0f, 0.85f, 0.1f, 0.35f };

        ctx.shapes->DrawBox(slot.worldPosition, { 0.0f, 0.0f, 0.0f }, { 0.2f, 0.2f, 0.2f }, col);
    }

    // Draw outer slot indicators (Bright Cyan if occupied, Pale Cyan if vacant)
    for (const auto& slot : m_outerSlots)
    {
        const DirectX::XMFLOAT4 col = slot.isOccupied
            ? DirectX::XMFLOAT4{ 0.1f, 0.8f, 1.0f, 0.8f }
        : DirectX::XMFLOAT4{ 0.1f, 0.8f, 1.0f, 0.25f };

        ctx.shapes->DrawBox(slot.worldPosition, { 0.0f, 0.0f, 0.0f }, { 0.15f, 0.15f, 0.15f }, col);
    }
}