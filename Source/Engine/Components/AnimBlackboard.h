#pragma once

#include <bitset>
#include <cstdint>

namespace Engine::Animation
{
    // Scoped enum mapping boolean flags for the Blackboard.
    // The 0-valued enumerator 'none' acts as a safe default/invalid state.
    enum class AnimFlag : std::size_t
    {
        none = 0,
        is_grounded,
        is_strafing,
        is_combat_active
    };

    // The Parameter Blackboard: A pure Plain Old Data (POD) struct.
    // Decouples gameplay logic from animation definitions by acting as a shared data contract.
    struct AnimBlackboard
    {
        float groundSpeed{ 0.0f };
        float verticalVelocity{ 0.0f };

        // Selects the combo step or directional action 
        int actionIndex{ 0 };

        std::bitset<8> flags{};

        [[nodiscard]] constexpr bool getFlag(AnimFlag flag) const noexcept
        {
            if (flag == AnimFlag::none) return false;
            return flags[static_cast<std::size_t>(flag)];
        }

        inline void setFlag(AnimFlag flag, bool value) noexcept
        {
            if (flag == AnimFlag::none) return;
            flags[static_cast<std::size_t>(flag)] = value;
        }
    };
}