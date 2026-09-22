#pragma once

#include <DirectXMath.h>

// Resolves per-frame camera rotation input (yaw/pitch deltas, in radians) from whichever
// hardware device the player last used
namespace CameraInput
{
    // x = yaw delta (radians), y = pitch delta (radians). Feed these straight into
    // VirtualCameraComponent::AddOrbitYaw()/AddOrbitPitch().
    [[nodiscard]] DirectX::XMFLOAT2 ResolveOrbitDelta(float dt) noexcept;
}