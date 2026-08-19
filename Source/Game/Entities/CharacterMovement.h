#pragma once
#include <DirectXMath.h>

// Pure data container for character transform state.
// Physics simulation is handled externally by PhysX.
class CharacterMovement
{
public:
    CharacterMovement() = default;
    ~CharacterMovement() = default;

    // --- Position ---
    const DirectX::XMFLOAT3& GetPosition() const { return position; }
    void SetPosition(const DirectX::XMFLOAT3& pos) { position = pos; }

    // --- Rotation ---
    const DirectX::XMFLOAT3& GetRotation() const { return rotation; }
    void SetRotation(const DirectX::XMFLOAT3& rot) { rotation = rot; }
    float GetRotationY() const { return rotation.y; }
    void  SetRotationY(float rad) { rotation.y = rad; }

    // --- Velocity ---
    const DirectX::XMFLOAT3& GetVelocity() const { return velocity; }
    void SetVelocity(const DirectX::XMFLOAT3& vel) { velocity = vel; }
    void SetVelocityX(float x) { velocity.x = x; }
    void SetVelocityY(float y) { velocity.y = y; }
    void SetVelocityZ(float z) { velocity.z = z; }

    // Returns true if character has meaningful horizontal movement
    bool IsMoving() const;

private:
    DirectX::XMFLOAT3 position = { 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT3 velocity = { 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT3 rotation = { 0.0f, 0.0f, 0.0f };
};