#pragma once

#include <bitset>
#include <cstdint>

namespace Engine::Animation
{
    // Strongly typed enum mapping
    // Explicit std::uint8_t saves memory
    enum class AnimSlot : std::uint8_t
    {
        None = 0,
        Locomotion,
        Locomotion_Start, 
        Locomotion_Stop,  
        PivotTurn,
        Slide,
        AirTraversal,
        Jump_Acrobatic, 
        Landing,        
        ParkourWall,
        DashEvade,
        Attack_Primary,
        Attack_Contextual,
        Attack_Directional,
        Attack_Charged,
        Attack_Aerial,
        Attack_Plunge,
        Parry_Counter,
        HitReact,
        Count // Used strictly for array sizing
    };

    enum class AnimFlag : std::size_t
    {
        none = 0,
        is_grounded,
        is_strafing,
        is_combat_active,
        is_wall_running,
        is_charging,
        has_air_dashed,
        Count
    };

    // Pure POD shared memory contract
    struct AnimBlackboard
    {
        float groundSpeed{ 0.0f };
        float verticalVelocity{ 0.0f };
        float chargeTimer{ 0.0f };

        int actionIndex{ 0 };
        int currentJumps{ 0 }; 

        std::bitset<static_cast<std::size_t>(AnimFlag::Count)> flags{};

        [[nodiscard]] constexpr bool getFlag(AnimFlag flag) const noexcept
        {
            if (flag == AnimFlag::none) return false;
            return flags[static_cast<std::size_t>(flag)];
        }

        constexpr void setFlag(AnimFlag flag, bool value) noexcept
        {
            if (flag == AnimFlag::none) return;
            flags[static_cast<std::size_t>(flag)] = value;
        }
    };
}