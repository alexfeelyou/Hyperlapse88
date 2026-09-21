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
        float groundSpeed{ 0.0f };       // Horizontal magnitude of velocity (m/s)
        float verticalVelocity{ 0.0f };  // Vertical speed, positive = ascending (m/s)

        // Packs up to 8 boolean conditions into a single byte for minimal cache footprint.
        std::bitset<8> flags{};

        // Retrieves a boolean flag's state safely.
        // Note: bitset::operator[] is bounds-safe here due to the enum cast.
        [[nodiscard]] constexpr bool getFlag(AnimFlag flag) const noexcept
        {
            if (flag == AnimFlag::none) return false;
            return flags[static_cast<std::size_t>(flag)];
        }

        // Sets a boolean flag to true or false.
        inline void setFlag(AnimFlag flag, bool value) noexcept
        {
            if (flag == AnimFlag::none) return;
            flags[static_cast<std::size_t>(flag)] = value;
        }
    };
}