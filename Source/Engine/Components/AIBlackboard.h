#pragma once

#include <bitset>
#include <cstdint>
#include <DirectXMath.h>

class GameObject;

// Strongly typed archetype selector determining behavior profiles
enum class EnemyArchetype : std::uint8_t
{
    grunt = 0,      // Standard patrol, sight acquisition, and pursuit
    ground_ambush,  // Dormant proxy bursting forward on proximity
    wall_lurker,    // Suspended perch, drops when player walks below, then pursues
    count
};

// Strongly typed discrete behavioral states
enum class EnemyState : std::uint8_t
{
    idle = 0,
    patrol,
    ambush_dormant,
    ambush_burst,
    wall_hang,
    wall_scramble,
    wall_drop,
    wall_land,
    chase,
    attack_windup,
    attack_active,
    hit_react,
    airborne_juggle,
    dead,
    count
};

// Bitmask flags for sensory perception queries
enum class SensoryFlag : std::size_t
{
    has_line_of_sight = 0,
    is_target_in_fov,
    is_in_attack_range,
    is_ambush_triggered,
    is_leashed,
    is_target_beyond_leash, // Flagged when player is outside territory bounds
    count
};

// Flat Plain Old Data (POD) blackboard shared across sensory and state systems
struct AIBlackboard
{
    // Non-owning pointer to the acquired target entity
    GameObject* target{ nullptr };

    // World space anchors and targets
    DirectX::XMFLOAT3 spawnPosition{ 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT3 targetPosition{ 0.0f, 0.0f, 0.0f };    // Acquired combat target (Player)
    DirectX::XMFLOAT3 wanderDestination{ 0.0f, 0.0f, 0.0f }; // Unalerted wander/patrol point
    DirectX::XMFLOAT2 moveDirection{ 0.0f, 0.0f };

    // Spatial cache to prevent redundant vector math (Default to large distance to prevent false frame-1 proximity triggers)
    float targetDistanceSq{ 999999.0f };

    // Behavioral and sensory timers
    float stateTimer{ 0.0f };
    float perceptionTimer{ 0.0f };
    float attackCooldownTimer{ 0.0f };
    float deaggroCooldownTimer{ 0.0f }; 

    // Bit-packed sensory queries (8 flags in 1 byte)
    std::bitset<static_cast<std::size_t>(SensoryFlag::count)> flags{};

    [[nodiscard]] bool getFlag(SensoryFlag flag) const noexcept
    {
        return flags[static_cast<std::size_t>(flag)];
    }

    void setFlag(SensoryFlag flag, bool value) noexcept
    {
        flags[static_cast<std::size_t>(flag)] = value;
    }

    void resetTimers() noexcept
    {
        stateTimer = 0.0f;
        perceptionTimer = 0.0f;
        attackCooldownTimer = 0.0f;
        deaggroCooldownTimer = 0.0f;
        targetDistanceSq = 999999.0f;
    }
};