#pragma once

#include <array>
#include <cstdint>
#include <DirectXMath.h>
#include <vector>

class AIControllerComponent;
class GameObject;
struct GizmoContext;

// Ring tier classification for registered swarm enemies
enum class SwarmRing : std::uint8_t
{
    none = 0,
    inner,  // Active frontline melee combat ring
    outer   // Pressure and circling swarm mob ring
};

// Represents a spatial slot position on a concentric combat ring
struct CombatSlot
{
    DirectX::XMFLOAT3 worldPosition{ 0.0f, 0.0f, 0.0f };
    AIControllerComponent* occupant{ nullptr };
    bool isOccupied{ false };
};

// Centralized coordinator for swarm spacing, ring slotting, and attack token dispatching
class CombatDirector final
{
public:
    static constexpr std::size_t s_innerSlotCount{ 6 };  // Maximum simultaneous frontline attackers
    static constexpr std::size_t s_outerSlotCount{ 12 }; // Surrounding mob circle slots

    [[nodiscard]] static CombatDirector& Instance() noexcept
    {
        static CombatDirector s_instance{};
        return s_instance;
    }

    CombatDirector(const CombatDirector&) = delete;
    CombatDirector& operator=(const CombatDirector&) = delete;
    CombatDirector(CombatDirector&&) noexcept = delete;
    CombatDirector& operator=(CombatDirector&&) noexcept = delete;

    // Lifecycle registration
    void RegisterEnemy(AIControllerComponent* enemy) noexcept;
    void UnregisterEnemy(AIControllerComponent* enemy) noexcept;
    void Clear() noexcept;

    // Per-frame swarm orchestration
    void Update(float dt, GameObject* playerObject) noexcept;
    void UpdateSlotPositions(const GameObject* playerObject, float dt = 0.0f) noexcept;
    void DrawGizmos(const GizmoContext& ctx) noexcept;

    // Token arbitration
    [[nodiscard]] bool RequestAttackToken(AIControllerComponent* enemy) noexcept;
    void ReleaseAttackToken(AIControllerComponent* enemy) noexcept;

    // Spatial queries for AI steering & player combat arbitration
    [[nodiscard]] DirectX::XMFLOAT3 GetAssignedSlotPosition(const AIControllerComponent* enemy) const noexcept;
    [[nodiscard]] SwarmRing GetAssignedRing(const AIControllerComponent* enemy) const noexcept;
    [[nodiscard]] DirectX::XMFLOAT3 ComputeSeparationForce(const AIControllerComponent* enemy) const noexcept;
    [[nodiscard]] AIControllerComponent* FindNearestAirborneEnemy(const DirectX::XMFLOAT3& origin, float maxDistance = 4.0f) const noexcept;
    [[nodiscard]] bool TryConsumeParryOpportunity(const DirectX::XMFLOAT3& playerPos, DirectX::XMFLOAT3& outAttackerPos) noexcept;
    [[nodiscard]] const std::vector<AIControllerComponent*>& GetEnemies() const noexcept { return m_enemies; }

    // Tuning accessors
    [[nodiscard]] float GetInnerRadius() const noexcept { return m_innerRadius; }
    [[nodiscard]] float GetOuterRadius() const noexcept { return m_outerRadius; }
    void SetInnerRadius(float r) noexcept { m_innerRadius = r; }
    void SetOuterRadius(float r) noexcept { m_outerRadius = r; }

    [[nodiscard]] std::uint32_t GetMaxAttackTokens() const noexcept { return m_maxAttackTokens; }
    void SetMaxAttackTokens(std::uint32_t tokens) noexcept { m_maxAttackTokens = tokens; }
    [[nodiscard]] float GetCadenceInterval() const noexcept { return m_tokenCadenceInterval; }
    void SetCadenceInterval(float interval) noexcept { m_tokenCadenceInterval = interval; }

    [[nodiscard]] std::size_t GetRegisteredEnemyCount() const noexcept { return m_enemies.size(); }
    [[nodiscard]] std::uint32_t GetActiveTokenCount() const noexcept { return m_activeTokens; }
    [[nodiscard]] std::size_t GetOccupiedInnerSlotCount() const noexcept;
    [[nodiscard]] std::size_t GetOccupiedOuterSlotCount() const noexcept;

private:
    CombatDirector() noexcept;
    ~CombatDirector() = default;

    float m_innerRadius{ 2.8f }; // Frontline distance from player
    float m_outerRadius{ 5.5f }; // Swarm mob distance from player

    float m_tokenDispatchTimer{ 0.0f };
    float m_tokenCadenceInterval{ 0.25f }; // Stagger delay between attacks for readability

    std::uint32_t m_maxAttackTokens{ 3 }; // Configurable simultaneous attack token pool
    std::uint32_t m_activeTokens{ 0 };

    std::array<CombatSlot, s_innerSlotCount> m_innerSlots{};
    std::array<CombatSlot, s_outerSlotCount> m_outerSlots{};

    // Contiguous registration pool (zero allocation during gameplay loop)
    std::vector<AIControllerComponent*> m_enemies{};
};