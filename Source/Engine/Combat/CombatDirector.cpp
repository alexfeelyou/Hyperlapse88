#include <algorithm>
#include <cmath>
#include "System/ShapeRenderer.h"
#include "AIControllerComponent.h"
#include "CombatDirector.h"
#include "GameObject.h"
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

void CombatDirector::UpdateSlotPositions(const GameObject* playerObject) noexcept
{
    if (!playerObject) return;

    const DirectX::XMFLOAT4X4 pWorld{ playerObject->transform.GetWorldMatrix() };
    const DirectX::XMFLOAT3 playerPos{ pWorld._41, pWorld._42, pWorld._43 };
    const float playerYawRad{ DirectX::XMConvertToRadians(playerObject->transform.rotation.y) };

    constexpr float innerStep{ DirectX::XM_2PI / static_cast<float>(s_innerSlotCount) };
    for (std::size_t i{ 0 }; i < s_innerSlotCount; ++i)
    {
        const float angle{ playerYawRad + (innerStep * static_cast<float>(i)) };
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
        const float angle{ playerYawRad + outerPhase + (outerStep * static_cast<float>(i)) };
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

    UpdateSlotPositions(playerObject);

    for (auto& slot : m_innerSlots) { slot.isOccupied = false; slot.occupant = nullptr; }
    for (auto& slot : m_outerSlots) { slot.isOccupied = false; slot.occupant = nullptr; }

    const DirectX::XMFLOAT4X4 pWorld{ playerObject->transform.GetWorldMatrix() };
    const DirectX::XMFLOAT3 playerPos{ pWorld._41, pWorld._42, pWorld._43 };

    // Filter active candidates and assign slots starting with closest enemies
    struct Candidate
    {
        AIControllerComponent* enemy{ nullptr };
        float distSq{ 0.0f };
    };

    static std::vector<Candidate> s_candidates{};
    s_candidates.clear();

    // Prune destroyed or dead enemy registrations to prevent dangling pointer access
    const auto deadIt{ std::remove_if(m_enemies.begin(), m_enemies.end(), [](const AIControllerComponent* e) noexcept {
        return !e || !e->GetOwner() || e->GetCurrentState() == EnemyState::dead;
    }) };
    if (deadIt != m_enemies.end())
    {
        m_enemies.erase(deadIt, m_enemies.end());
    }

    for (auto* enemy : m_enemies)
    {
        // Only slot engaged or chasing enemies (ignore dormant ambushers on walls or props)
        const EnemyState st{ enemy->GetCurrentState() };
        if (st == EnemyState::ambush_dormant || st == EnemyState::wall_hang)
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

    // Assign inner ring slots to the closest candidates
    std::size_t assignedCount{ 0 };
    for (const auto& cand : s_candidates)
    {
        if (assignedCount < s_innerSlotCount)
        {
            // Find closest free inner slot to this enemy
            const DirectX::XMFLOAT4X4 eWorld{ cand.enemy->GetOwner()->transform.GetWorldMatrix() };
            float bestDistSq{ 999999.0f };
            int bestSlotIdx{ -1 };

            for (std::size_t s{ 0 }; s < s_innerSlotCount; ++s)
            {
                if (m_innerSlots[s].isOccupied) continue;

                const float dsx{ m_innerSlots[s].worldPosition.x - eWorld._41 };
                const float dsz{ m_innerSlots[s].worldPosition.z - eWorld._43 };
                const float dSq{ (dsx * dsx) + (dsz * dsz) };

                if (dSq < bestDistSq)
                {
                    bestDistSq = dSq;
                    bestSlotIdx = static_cast<int>(s);
                }
            }

            if (bestSlotIdx >= 0)
            {
                m_innerSlots[bestSlotIdx].isOccupied = true;
                m_innerSlots[bestSlotIdx].occupant = cand.enemy;
                ++assignedCount;
                continue;
            }
        }

        // Assign to outer ring slots
        const DirectX::XMFLOAT4X4 eWorld{ cand.enemy->GetOwner()->transform.GetWorldMatrix() };
        float bestDistSq{ 999999.0f };
        int bestSlotIdx{ -1 };

        for (std::size_t s{ 0 }; s < s_outerSlotCount; ++s)
        {
            if (m_outerSlots[s].isOccupied) continue;

            const float dsx{ m_outerSlots[s].worldPosition.x - eWorld._41 };
            const float dsz{ m_outerSlots[s].worldPosition.z - eWorld._43 };
            const float dSq{ (dsx * dsx) + (dsz * dsz) };

            if (dSq < bestDistSq)
            {
                bestDistSq = dSq;
                bestSlotIdx = static_cast<int>(s);
            }
        }

        if (bestSlotIdx >= 0)
        {
            m_outerSlots[bestSlotIdx].isOccupied = true;
            m_outerSlots[bestSlotIdx].occupant = cand.enemy;
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
    constexpr float separationRadiusSq{ 1.0f * 1.0f }; // 1.0m personal bubble

    for (const auto* other : m_enemies)
    {
        if (!other || other == enemy || !other->GetOwner() || other->GetCurrentState() == EnemyState::dead) continue;

        const DirectX::XMFLOAT4X4 otherMat{ other->GetOwner()->transform.GetWorldMatrix() };
        const float dx{ myPos.x - otherMat._41 };
        const float dz{ myPos.z - otherMat._43 };
        const float distSq{ (dx * dx) + (dz * dz) };

        if (distSq > 0.001f && distSq < separationRadiusSq)
        {
            const float invDist{ 1.0f / std::sqrt(distSq) };
            separation.x += (dx * invDist) * (1.0f - (std::sqrt(distSq) / 1.0f));
            separation.z += (dz * invDist) * (1.0f - (std::sqrt(distSq) / 1.0f));
        }
    }

    return separation;
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