#include <algorithm>
#include <cmath>
#include "System/Input.h"
#include "CameraInput.h"

namespace
{
    // Mouse (Keyboard device)
    // Radians of rotation per pixel of raw mouse movement. Mouse::Update() reports whole-pixel
    // deltas every frame (not DPI-normalized), so this stays a small, hand-tuned constant.
    constexpr float s_mouseYawSensitivity{ 0.0025f };
    constexpr float s_mousePitchSensitivity{ 0.0025f };

    // Gamepad (right stick) 
    // Radians per second at full stick deflection. Multiplied by dt below since the stick
    // reports a held analog value every frame, not a one-shot delta like the mouse does.
    constexpr float s_stickYawSensitivity{ 2.5f };
    constexpr float s_stickPitchSensitivity{ 2.0f };
    constexpr float s_stickDeadzone{ 0.15f }; 

    [[nodiscard]] DirectX::XMFLOAT2 ResolveMouseDelta() noexcept
    {
        const Mouse& mouse{ Input::Instance().GetMouse() };

        return {
            mouse.GetDeltaX() * s_mouseYawSensitivity,
            mouse.GetDeltaY() * s_mousePitchSensitivity
        };
    }

    [[nodiscard]] DirectX::XMFLOAT2 ResolveStickDelta(float dt) noexcept
    {
        const GamePad& pad{ Input::Instance().GetGamePad() };

        float rx{ pad.GetAxisRX() };
        float ry{ pad.GetAxisRY() };

        // GamePad::Update() only zeros the right stick when BOTH axes are simultaneously
        // inside its deadzone — holding the stick along one axis can leave a small nonzero
        // value on the other. Re-apply a per-axis deadzone so that residual jitter doesn't
        // leak into camera rotation.
        if (std::abs(rx) < s_stickDeadzone) rx = 0.0f;
        if (std::abs(ry) < s_stickDeadzone) ry = 0.0f;

        // Stick Y is physically inverted relative to screen-space mouse Y (pushing the stick
        // UP returns a positive value, but "up" should decrease pitch the same way moving the
        // mouse up does) — negate so both devices feel identical to the player.
        return {
            rx * s_stickYawSensitivity * dt,
            -ry * s_stickPitchSensitivity * dt
        };
    }
}

namespace CameraInput
{
    DirectX::XMFLOAT2 ResolveOrbitDelta(float dt) noexcept
    {
        switch (Input::Instance().GetLastUsedDevice())
        {
        case InputDevice::Gamepad:
            return ResolveStickDelta(dt);

        case InputDevice::Keyboard:
        default:
            return ResolveMouseDelta();
        }
    }
}