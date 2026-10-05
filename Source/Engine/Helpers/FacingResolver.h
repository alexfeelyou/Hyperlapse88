#pragma once

#include <DirectXMath.h>

class GameObject;

// Shared player-facing utilities. Both functions rotate only a GameObject's yaw
// (Transform::rotation.y) — never pitch/roll, never position. 
namespace FacingResolver
{
    // Turns toward worldDirectionXZ at turnRateDegPerSec, using the shortest angular path.
    // A (near-)zero-length direction is a no-op: "no input" means "hold last facing", not
    // "snap to some default direction".
    void SmoothFaceDirection(GameObject* character, const DirectX::XMFLOAT2& worldDirectionXZ,
        float turnRateDegPerSec, float dt) noexcept;

    // Immediately sets yaw to face worldDirectionXZ. Same zero-length guard as
    // SmoothFaceDirection.
    void SnapFaceDirection(GameObject* character, const DirectX::XMFLOAT2& worldDirectionXZ) noexcept;

    // Returns rawWorldDirectionXZ unchanged if non-zero-length; otherwise returns the
    // character's current facing as a world-space XZ direction.
    [[nodiscard]] DirectX::XMFLOAT2 ResolveDirectionOrCurrentFacing(const GameObject* character,
        const DirectX::XMFLOAT2& rawWorldDirectionXZ) noexcept;
}