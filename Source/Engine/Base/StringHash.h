#pragma once

#include <cstdint>
#include <string_view>
#include "rapidhash.h"

namespace Core
{
    // Compile-time FNV-1a hash for static string literals 
    [[nodiscard]] consteval std::uint64_t Hash(std::string_view str) noexcept
    {
        std::uint64_t hash{ 0xCBF29CE484222325ULL };
        for (const char c : str)
        {
            hash ^= static_cast<std::uint64_t>(c);
            hash *= 0x100000001B3ULL;
        }
        return hash;
    }

    // Ultra-fast 64-bit runtime hash using rapidhash for dynamic strings (e.g., loaded JSON)
    [[nodiscard]] inline std::uint64_t RuntimeHash(std::string_view str) noexcept
    {
        return rapidhash(str.data(), str.size());
    }
}