#include <algorithm>
#include <cmath>
#include "System/ShapeRenderer.h"
#include "AIControllerComponent.h"
#include "CombatDirector.h"
#include "GameObject.h"
#include "HealthComponent.h"
#include "IComponent.h"

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
    m_activeTokens = 0;
    m_tokenDispatchTimer = 0.0f;

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

void CombatDirector::Update(const float dt, GameObject* playerObject) noexcept
{
    if (!playerObject)
    {
        return;
    }

    // Advance token dispatch cadence timer
    if (m_tokenDispatchTimer > 0.0f)
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

    // Dynamic slot allocation: fill inner frontline first, promoting outer ring enemies into newly opened slots
    for (const auto& cand : s_candidates)
    {
        const SwarmRing currentRing{ GetAssignedRing(cand.enemy) };

        // Frontline enemies already secured in an inner slot preserve their spot
        if (currentRing == SwarmRing::inner)
        {
            continue;
        }

        const DirectX::XMFLOAT4X4 eWorld{ cand.enemy->GetOwner()->transform.GetWorldMatrix() };

        // Check for open inner slot vacancies (promotes outer enemies or assigns unslotted candidates)
        float bestInnerDistSq{ 999999.0f };
        int bestInnerSlotIdx{ -1 };

        for (std::size_t s{ 0 }; s < s_innerSlotCount; ++s)
        {
            if (m_innerSlots[s].isOccupied) continue;

            const float dsx{ m_innerSlots[s].worldPosition.x - eWorld._41 };
            const float dsz{ m_innerSlots[s].worldPosition.z - eWorld._43 };
            const float dSq{ (dsx * dsx) + (dsz * dsz) };

            if (dSq < bestInnerDistSq)
            {
                bestInnerDistSq = dSq;
                bestInnerSlotIdx = static_cast<int>(s);
            }
        }

        if (bestInnerSlotIdx >= 0)
        {
            // If promoted from outer ring, vacate previous outer slot so reinforcements can take it
            if (currentRing == SwarmRing::outer)
            {
                for (auto& outerSlot : m_outerSlots)
                {
                    if (outerSlot.occupant == cand.enemy)
                    {
                        outerSlot.occupant = nullptr;
                        outerSlot.isOccupied = false;
                        break;
                    }
                }
            }

            m_innerSlots[bestInnerSlotIdx].isOccupied = true;
            m_innerSlots[bestInnerSlotIdx].occupant = cand.enemy;
            continue;
        }

        // Inner ring is full: existing outer ring occupants preserve their slot
        if (currentRing == SwarmRing::outer)
        {
            continue;
        }

        // Candidate is completely unslotted: assign to nearest available outer ring slot
        float bestOuterDistSq{ 999999.0f };
        int bestOuterSlotIdx{ -1 };

        for (std::size_t s{ 0 }; s < s_outerSlotCount; ++s)
        {
            if (m_outerSlots[s].isOccupied) continue;

            const float dsx{ m_outerSlots[s].worldPosition.x - eWorld._41 };
            const float dsz{ m_outerSlots[s].worldPosition.z - eWorld._43 };
            const float dSq{ (dsx * dsx) + (dsz * dsz) };

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
}

bool CombatDirector::RequestAttackToken(AIControllerComponent* enemy) noexcept
{
    if (!enemy) return false;

    // Must be in the inner ring and cadence interval must be satisfied
    if (GetAssignedRing(enemy) != SwarmRing::inner)
    {
        return false;
    }

    if (m_activeTokens < m_maxAttackTokens && m_tokenDispatchTimer <= 0.0f)
    {
        ++m_activeTokens;
        m_tokenDispatchTimer = m_tokenCadenceInterval;
        return true;
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

    if (m_activeTokens > 0)
    {
        --m_activeTokens;
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