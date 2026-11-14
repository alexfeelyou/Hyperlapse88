#pragma once

#include <cmath>
#include <cstdint>
#include <DirectXMath.h>
#include "GameObject.h"

namespace Engine::Combat
{
    // Extracts authoritative world-space translation from any GameObject regardless of parent nesting
    [[nodiscard]] inline DirectX::XMFLOAT3 GetWorldPosition(const GameObject* go) noexcept
    {
        if (!go)
        {
            return { 0.0f, 0.0f, 0.0f };
        }

        const DirectX::XMFLOAT4X4 wMat{ go->transform.GetWorldMatrix() };
        return { wMat._41, wMat._42, wMat._43 };
    }

    // 4-quadrant relative impact direction with kinematic blade slash-arc refinement.
    // Returns Node Index: 0 = Front, 1 = Back, 2 = Left, 3 = Right
    [[nodiscard]] inline std::uint8_t CalculateHitDirectionIndex(
        const GameObject* victim,
        const DirectX::XMFLOAT3& attackerPos,
        const DirectX::XMFLOAT3& slashDir = { 0.0f, 0.0f, 0.0f }) noexcept
    {
        if (!victim)
        {
            return 0;
        }

        const DirectX::XMFLOAT3 victimPos{ GetWorldPosition(victim) };
        const float dx{ attackerPos.x - victimPos.x };
        const float dz{ attackerPos.z - victimPos.z };
        const float distSq{ (dx * dx) + (dz * dz) };
        if (distSq < 0.0001f)
        {
            return 0;
        }

        const float invDist{ 1.0f / std::sqrt(distSq) };
        const float toAttackerX{ dx * invDist };
        const float toAttackerZ{ dz * invDist };

        // Derive victim's local horizontal basis vectors from world yaw
        const float yawRad{ DirectX::XMConvertToRadians(victim->transform.rotation.y) };
        const float fwdX{ std::sin(yawRad) };
        const float fwdZ{ std::cos(yawRad) };
        const float rightX{ fwdZ };
        const float rightZ{ -fwdX };

        const float fwdDot{ (toAttackerX * fwdX) + (toAttackerZ * fwdZ) };
        const float rightDot{ (toAttackerX * rightX) + (toAttackerZ * rightZ) };

        if (std::abs(fwdDot) >= std::abs(rightDot))
        {
            // Back impact takes absolute priority when attacker is behind victim
            if (fwdDot < 0.0f)
            {
                return 1; // 1 = Back
            }

            // Attacker is in front: resolve frontal cleave direction using blade sweep velocity
            const float slashLenSq{ (slashDir.x * slashDir.x) + (slashDir.z * slashDir.z) };
            constexpr float minSlashSpeedSq{ 0.02f * 0.02f }; // 2 cm/frame lateral sweep noise floor
            if (slashLenSq > minSlashSpeedSq)
            {
                const float invSlashLen{ 1.0f / std::sqrt(slashLenSq) };
                const float slashNormX{ slashDir.x * invSlashLen };
                const float slashNormZ{ slashDir.z * invSlashLen };

                // Project blade velocity onto victim's local Right axis
                const float slashRightDot{ (slashNormX * rightX) + (slashNormZ * rightZ) };
                constexpr float lateralDeadzone{ 0.42f }; // ~25 deg deadzone preserving clean frontal cuts
                if (slashRightDot < -lateralDeadzone)
                {
                    // Blade sweeps towards victim's Left -> weapon sliced in from victim's Right
                    return 3; // 3 = Right
                }
                if (slashRightDot > lateralDeadzone)
                {
                    // Blade sweeps towards victim's Right -> weapon sliced in from victim's Left
                    return 2; // 2 = Left
                }
            }

            return 0; // 0 = Front
        }

        return (rightDot >= 0.0f) ? 3 : 2; // 2 = Left, 3 = Right
    }
}