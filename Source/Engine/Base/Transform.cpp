#include "Transform.h"

// Calculate local Scale * Rotation * Translation (SRT) matrix
[[nodiscard]] DirectX::XMFLOAT4X4 Transform::GetLocalMatrix() const noexcept
{
    // Convert euler angles to radians for DirectXMath
    const DirectX::XMMATRIX matScale{ DirectX::XMMatrixScaling(scale.x, scale.y, scale.z) };
    const DirectX::XMMATRIX matRot{ DirectX::XMMatrixRotationRollPitchYaw(
        DirectX::XMConvertToRadians(rotation.x),
        DirectX::XMConvertToRadians(rotation.y),
        DirectX::XMConvertToRadians(rotation.z)
    ) };
    const DirectX::XMMATRIX matTrans{ DirectX::XMMatrixTranslation(position.x, position.y, position.z) };

    const DirectX::XMMATRIX matLocal{ matScale * matRot * matTrans };

    DirectX::XMFLOAT4X4 result{};
    DirectX::XMStoreFloat4x4(&result, matLocal);

    return result;
}

// Calculate true world position by inheriting the parent's transformations
[[nodiscard]] DirectX::XMFLOAT4X4 Transform::GetWorldMatrix() const noexcept
{
    const DirectX::XMFLOAT4X4 localFloat4x4{ GetLocalMatrix() };
    const DirectX::XMMATRIX matLocal{ DirectX::XMLoadFloat4x4(&localFloat4x4) };

    // If there is no parent, local space IS world space
    if (!parent)
    {
        return localFloat4x4;
    }

    // If there is a parent, World = Local * ParentWorld
    const DirectX::XMFLOAT4X4 parentWorldFloat4x4{ parent->GetWorldMatrix() };
    const DirectX::XMMATRIX matParentWorld{ DirectX::XMLoadFloat4x4(&parentWorldFloat4x4) };

    const DirectX::XMMATRIX matWorld{ matLocal * matParentWorld };

    DirectX::XMFLOAT4X4 result{};
    DirectX::XMStoreFloat4x4(&result, matWorld);

    return result;
}