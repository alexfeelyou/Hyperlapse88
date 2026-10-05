#pragma once

#include <cstdint>
#include <string_view>
#include "rapidhash.h"

namespace Core
{
    // STRING ID DOMAIN (For Names, States, Bones)
    constexpr std::uint64_t FNV_OFFSET_BASIS{ 0xCBF29CE484222325ULL };
    constexpr std::uint64_t FNV_PRIME{ 0x100000001B3ULL };

    // Compile-time FNV-1a hash for static string literals
    [[nodiscard]] consteval std::uint64_t Hash(const std::string_view str) noexcept
    {
        std::uint64_t hash{ FNV_OFFSET_BASIS };
        for (const char c : str)
        {
            hash ^= static_cast<std::uint64_t>(c);
            hash *= FNV_PRIME;
        }
        return hash;
    }

    // Runtime FNV-1a hash
    [[nodiscard]] constexpr std::uint64_t RuntimeHash(const std::string_view str) noexcept
    {
        std::uint64_t hash{ FNV_OFFSET_BASIS };
        for (const char c : str)
        {
            hash ^= static_cast<std::uint64_t>(c);
            hash *= FNV_PRIME;
        }
        return hash;
    }

    // BUFFER DOMAIN (For large binary data caches
    // Ultra-fast 64-bit runtime hash using rapidhash for large continuous memory blocks
    [[nodiscard]] inline std::uint64_t BufferHash(const void* data, const std::size_t size) noexcept
    {
        return rapidhash(data, size);
    }
}