#include "Item.h"
#include "System/ModelRenderer.h"
#include <cmath>

Item::Item(ID3D11Device* device, const DirectX::XMFLOAT3& position, ItemType type) noexcept
    : m_position{ position }
    , m_originalY{ position.y }
    , m_type{ type }
{
    m_model = std::make_shared<Model>(device, "Data/Model/Character/PLACEHOLDER_mdl_Block.glb");
    scale = { 2.0f, 2.0f, 2.0f };
    SetType(type);
}

void Item::SetType(ItemType type) noexcept
{
    m_type = type;
    color = (m_type == ItemType::Heal) ? kHealColor : kInvincibleColor;
}

void Item::Update(float elapsedTime, Camera* /*camera*/) noexcept
{
    if (!m_isActive) return;

    m_animTime += elapsedTime;

    // Floating bobbing animation on the Y-axis
    m_position.y = m_originalY + std::sin(m_animTime * kFloatSpeed) * kFloatAmp;

    // Continuous yaw spinning
    m_rotation.y = m_animTime * kSpinSpeed;
}

void Item::Render(ModelRenderer* renderer) const noexcept
{
    if (!m_isActive || !m_model || !renderer) return;

    // Compute TRS matrix directly for submission
    const DirectX::XMMATRIX S{ DirectX::XMMatrixScaling(scale.x, scale.y, scale.z) };
    const DirectX::XMMATRIX R{ DirectX::XMMatrixRotationRollPitchYaw(
        DirectX::XMConvertToRadians(m_rotation.x),
        DirectX::XMConvertToRadians(m_rotation.y),
        DirectX::XMConvertToRadians(m_rotation.z)) };
    const DirectX::XMMATRIX T{ DirectX::XMMatrixTranslation(m_position.x, m_position.y, m_position.z) };

    DirectX::XMFLOAT4X4 worldMatrix{};
    DirectX::XMStoreFloat4x4(&worldMatrix, S * R * T);

    renderer->Draw(m_model, color, worldMatrix);
}