#include <algorithm>
#include <cmath>
#include "FacingResolver.h"
#include "GameObject.h"

namespace
{
    constexpr float s_minDirectionLengthSq{ 0.0001f };

    // X/Z world direction -> yaw in degrees
    [[nodiscard]] float DirectionToYawDegrees(const DirectX::XMFLOAT2& dirXZ) noexcept
    {
        return DirectX::XMConvertToDegrees(std::atan2(dirXZ.x, dirXZ.y));
    }

    // Wraps a degree delta to the shortest signed path in [-180, 180] — turning from
    // 179 to -179 degrees should rotate 2 degrees, not 358
    [[nodiscard]] float WrapDegrees(float degrees) noexcept
    {
        while (degrees > 180.0f) degrees -= 360.0f;
        while (degrees < -180.0f) degrees += 360.0f;
        return degrees;
    }

    [[nodiscard]] bool IsDirectionMeaningful(const DirectX::XMFLOAT2& dirXZ) noexcept
    {
        return ((dirXZ.x * dirXZ.x) + (dirXZ.y * dirXZ.y)) > s_minDirectionLengthSq;
    }
}

namespace FacingResolver
{
    void SmoothFaceDirection(GameObject* character, const DirectX::XMFLOAT2& worldDirectionXZ,
        float turnRateDegPerSec, float dt) noexcept
    {
        if (!character || !IsDirectionMeaningful(worldDirectionXZ)) return;

        const DirectX::XMFLOAT3 currentRotation{ character->GetRotation() };
        const float targetYaw{ DirectionToYawDegrees(worldDirectionXZ) };
        const float yawDelta{ WrapDegrees(targetYaw - currentRotation.y) };

        // Clamp the per-frame step to the configured turn rate so a large yawDelta
        // (e.g. player reverses direction instantly) doesn't overshoot or snap at low
        const float maxStep{ turnRateDegPerSec * dt };
        const float step{ std::clamp(yawDelta, -maxStep, maxStep) };

        character->SetRotation({ currentRotation.x, currentRotation.y + step, currentRotation.z });
    }

    void SnapFaceDirection(GameObject* character, const DirectX::XMFLOAT2& worldDirectionXZ) noexcept
    {
        if (!character || !IsDirectionMeaningful(worldDirectionXZ)) return;

        const DirectX::XMFLOAT3 currentRotation{ character->GetRotation() };
        const float targetYaw{ DirectionToYawDegrees(worldDirectionXZ) };

        character->SetRotation({ currentRotation.x, targetYaw, currentRotation.z });
    }

    DirectX::XMFLOAT2 ResolveDirectionOrCurrentFacing(const GameObject* character,
        const DirectX::XMFLOAT2& rawWorldDirectionXZ) noexcept
    {
        if (IsDirectionMeaningful(rawWorldDirectionXZ)) return rawWorldDirectionXZ;
        if (!character) return { 0.0f, 1.0f };

        const float yawRad{ DirectX::XMConvertToRadians(character->GetRotation().y) };
        return { std::sin(yawRad), std::cos(yawRad) };
    }
}