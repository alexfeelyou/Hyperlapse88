#pragma once

namespace UI
{
    inline constexpr float s_baseWidth = 1920.0f;
    inline constexpr float s_baseHeight = 1080.0f;

    struct Bounds
    {
        float x;
        float y;
        float w;
        float h;
    };

    [[nodiscard]] inline Bounds GetScaled(float x, float y, float w, float h, float screenW, float screenH) noexcept
    {
        const float scaleX = screenW / s_baseWidth;
        const float scaleY = screenH / s_baseHeight;
        return { x * scaleX, y * scaleY, w * scaleX, h * scaleY };
    }
}