#pragma once

#include <chrono>
#include <random>
#include <type_traits>

// Random Helper (Mersenne Twister Engine)
namespace Random
{
    [[nodiscard]] inline std::mt19937 GenerateEngine() noexcept
    {
        std::random_device rd{};
        std::seed_seq ss{
            static_cast<std::seed_seq::result_type>(std::chrono::steady_clock::now().time_since_epoch().count()),
            rd(), rd(), rd(), rd(), rd(), rd(), rd()
        };
        return std::mt19937{ ss };
    }

    // Shared global PRNG instance
    inline std::mt19937 s_prng{ GenerateEngine() };

    // Primary function template: supports BOTH integers and floating-point numbers
    template <typename T>
    [[nodiscard]] T Get(T min, T max)
    {
        if constexpr (std::is_floating_point_v<T>)
        {
            return std::uniform_real_distribution<T>{ min, max }(s_prng);
        }
        else if constexpr (std::is_integral_v<T>)
        {
            return std::uniform_int_distribution<T>{ min, max }(s_prng);
        }
        else
        {
            static_assert(std::is_arithmetic_v<T>, "Random::Get only supports arithmetic types (ints and floats).");
        }
    }

    // Overload for mixed-type calls (e.g. Random::Get<float>(-1, 1))
    template <typename R, typename S, typename T>
    [[nodiscard]] R Get(S min, T max)
    {
        return Get<R>(static_cast<R>(min), static_cast<R>(max));
    }
}