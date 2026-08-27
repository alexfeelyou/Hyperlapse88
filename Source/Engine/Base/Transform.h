#pragma once

#include <DirectXMath.h>

class GameObject; // Forward declaration 

// Represents the spatial data of a GameObject (Local and World)
struct Transform
{
    DirectX::XMFLOAT3 position{ 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT3 rotation{ 0.0f, 0.0f, 0.0f }; 
    DirectX::XMFLOAT3 scale{ 1.0f, 1.0f, 1.0f };

    GameObject* owner{ nullptr };  // Points back to the entity this transform belongs to
    Transform* parent{ nullptr };  // Used to walk up the hierarchy tree

    // Calculates the Local Matrix (Scale * Rotation * Translation)
    [[nodiscard]] DirectX::XMFLOAT4X4 GetLocalMatrix() const noexcept;

    // Calculates the World Matrix by recursively multiplying with the parent's World Matrix
    [[nodiscard]] DirectX::XMFLOAT4X4 GetWorldMatrix() const noexcept;
};