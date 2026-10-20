#include "Transform.h"

// Calculate local Scale * Rotation * Translation (SRT) matrix with dirty state checking
[[nodiscard]] DirectX::XMFLOAT4X4 Transform::GetLocalMatrix() const noexcept
{
    const bool isClean{
        position.x == m_cachedPosition.x && position.y == m_cachedPosition.y && position.z == m_cachedPosition.z &&
        rotation.x == m_cachedRotation.x && rotation.y == m_cachedRotation.y && rotation.z == m_cachedRotation.z &&
        scale.x == m_cachedScale.x && scale.y == m_cachedScale.y && scale.z == m_cachedScale.z
    };

    if (isClean)
    {
        return m_cachedLocalMatrix;
    }

    const DirectX::XMMATRIX matScale{ DirectX::XMMatrixScaling(scale.x, scale.y, scale.z) };
    const DirectX::XMMATRIX matRot{ DirectX::XMMatrixRotationRollPitchYaw(
        DirectX::XMConvertToRadians(rotation.x),
        DirectX::XMConvertToRadians(rotation.y),
        DirectX::XMConvertToRadians(rotation.z)
    ) };
    const DirectX::XMMATRIX matTrans{ DirectX::XMMatrixTranslation(position.x, position.y, position.z) };

    const DirectX::XMMATRIX matLocal{ matScale * matRot * matTrans };

    DirectX::XMStoreFloat4x4(&m_cachedLocalMatrix, matLocal);
    m_cachedPosition = position;
    m_cachedRotation = rotation;
    m_cachedScale = scale;
    ++m_version;

    return m_cachedLocalMatrix;
}

// Calculate true world position with parent version checking
[[nodiscard]] DirectX::XMFLOAT4X4 Transform::GetWorldMatrix() const noexcept
{
    const std::uint32_t currentVersion{ m_version };
    const DirectX::XMFLOAT4X4 localFloat4x4{ GetLocalMatrix() };
    const bool localChanged{ m_version != currentVersion };

    if (!parent)
    {
        return localFloat4x4;
    }

    // Force parent to evaluate its world hierarchy first so parentCurrentVersion is accurate
    const DirectX::XMFLOAT4X4 parentWorldFloat4x4{ parent->GetWorldMatrix() };
    const std::uint32_t parentCurrentVersion{ parent->GetVersion() };
    const bool parentChanged{ m_cachedParentVersion != parentCurrentVersion };

    if (!localChanged && !parentChanged)
    {
        return m_cachedWorldMatrix;
    }

    const DirectX::XMMATRIX matLocal{ DirectX::XMLoadFloat4x4(&localFloat4x4) };
    const DirectX::XMMATRIX matParentWorld{ DirectX::XMLoadFloat4x4(&parentWorldFloat4x4) };

    const DirectX::XMMATRIX matWorld{ matLocal * matParentWorld };

    DirectX::XMStoreFloat4x4(&m_cachedWorldMatrix, matWorld);
    m_cachedParentVersion = parentCurrentVersion;
    ++m_version;

    return m_cachedWorldMatrix;
}