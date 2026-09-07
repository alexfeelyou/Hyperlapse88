#pragma once

#include <cstdint>

// Defines bitmask layers for PhysX collision filtering
namespace CollisionLayer
{
    inline constexpr std::uint32_t None{ 0 };
    inline constexpr std::uint32_t WorldStatic{ 1 << 0 };
    inline constexpr std::uint32_t Player{ 1 << 1 };
    inline constexpr std::uint32_t Enemy{ 1 << 2 };
    inline constexpr std::uint32_t Projectile{ 1 << 3 };
    inline constexpr std::uint32_t All{ ~0u };
}