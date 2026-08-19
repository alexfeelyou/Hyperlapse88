#pragma once
#include <algorithm>

namespace MathUtils
{
    // Linear Interpolation
    template <typename T>
    constexpr T Lerp(T a, T b, float t)
    {
        if (t < 0.0f) return a;
        if (t > 1.0f) return b;
        return a + (b - a) * t;
    }
}