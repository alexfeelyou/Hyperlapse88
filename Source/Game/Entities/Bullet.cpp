#include "System/AssetManager.h"
#include "System/Graphics.h"
#include "Bullet.h"

Bullet::Bullet() noexcept
{
    ID3D11Device* device{ Graphics::Instance().GetDevice() };
    m_model = Engine::System::AssetManager::Instance().GetOrLoadModel(device, "Data/Model/Character/PLACEHOLDER_mdl_Ball.glb");
    scale = { 0.01f, 0.01f, 0.01f };
    m_isActive = false;
    m_velocity = { 0.0f, 0.0f, 0.0f };
}

void Bullet::Fire(const DirectX::XMFLOAT3& startPos, const DirectX::XMFLOAT3& direction, float projectileSpeed) noexcept
{
    m_isActive = true;
    m_lifeTime = 0.0f;
    m_position = startPos;

    DirectX::XMVECTOR vDir{ DirectX::XMLoadFloat3(&direction) };
    vDir = DirectX::XMVector3Normalize(vDir);
    const DirectX::XMVECTOR vVel{ DirectX::XMVectorScale(vDir, projectileSpeed) };
    DirectX::XMStoreFloat3(&m_velocity, vVel);
}

void Bullet::Update(float elapsedTime, Camera* /*camera*/) noexcept
{
    if (!m_isActive) return;

    m_lifeTime += elapsedTime;

    m_position.x += m_velocity.x * elapsedTime;
    m_position.y += m_velocity.y * elapsedTime;
    m_position.z += m_velocity.z * elapsedTime;
}

void Bullet::ApplyMovement(const DirectX::XMFLOAT3& newPos, const DirectX::XMFLOAT3& newVel) noexcept
{
    m_position = newPos;
    m_velocity = newVel;
}