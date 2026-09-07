#pragma once

#include <cstdint>

// Defines bitmask layers and interaction presets for PhysX collision filtering
namespace CollisionLayer
{
    // Discrete layer channels (Single-bit flags)
    inline constexpr std::uint32_t None{ 0 };
    inline constexpr std::uint32_t WorldStatic{ 1 << 0 }; // Level terrain, greybox walls, immovable props
    inline constexpr std::uint32_t WorldDynamic{ 1 << 1 }; // Destructibles, movable crates, physics props
    inline constexpr std::uint32_t Player{ 1 << 2 }; // Player locomotion capsule
    inline constexpr std::uint32_t Enemy{ 1 << 3 }; // Enemy / Boss locomotion capsules
    inline constexpr std::uint32_t PlayerProjectile{ 1 << 4 }; // Player arrows, bullets, slashes
    inline constexpr std::uint32_t EnemyProjectile{ 1 << 5 }; // Boss spells, enemy ranged attacks
    inline constexpr std::uint32_t TriggerVolume{ 1 << 6 }; // Dialogue zones, checkpoints, event boxes
    inline constexpr std::uint32_t All{ ~0u };    // Raycasts, editor tools, broad line-of-sight

    // Convenience masks defining what each channel collides with by default
    namespace Mask
    {
        // Static environment blocks movement and projectiles, but ignores triggers
        inline constexpr std::uint32_t WorldStatic =
            Player | Enemy | PlayerProjectile | EnemyProjectile;

        // Player collides with the world, props, and enemies (cannot walk through them)
        inline constexpr std::uint32_t Player =
            WorldStatic | WorldDynamic | Enemy | TriggerVolume;

        // Enemy collides with the world, props, and the player
        inline constexpr std::uint32_t Enemy =
            WorldStatic | WorldDynamic | Player;

        // Player attacks hit the environment and enemies; they pass cleanly through the player
        inline constexpr std::uint32_t PlayerProjectile =
            WorldStatic | WorldDynamic | Enemy;

        // Enemy attacks hit the environment and the player; they pass cleanly through other enemies
        inline constexpr std::uint32_t EnemyProjectile =
            WorldStatic | WorldDynamic | Player;
    }
}