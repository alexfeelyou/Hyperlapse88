#include "Bullet.h"
#include "EffectManager.h"
#include <cmath>

using namespace DirectX;

Bullet::Bullet()
{
    ID3D11Device* device = Graphics::Instance().GetDevice();
    model = std::make_shared<Model>(device, "Data/Model/Character/PLACEHOLDER_mdl_Ball.glb");

    isActive = false;
    velocity = { 0, 0, 0 };
    movement->SetRotationY(0.0f);
    SyncData();
}

void Bullet::Fire(const DirectX::XMFLOAT3& startPos, const DirectX::XMFLOAT3& direction, float projectileSpeed)
{
    isActive = true;
    m_lifeTime = 0.0f;
    movement->SetPosition(startPos);

    XMVECTOR vDir = XMLoadFloat3(&direction);
    vDir = XMVector3Normalize(vDir);
    XMVECTOR vVel = vDir * projectileSpeed;
    XMStoreFloat3(&velocity, vVel);

    SyncData();
}

void Bullet::Update(float elapsedTime, Camera* camera)
{
    if (!isActive) return;

    m_lifeTime += elapsedTime;

    XMFLOAT3 pos = movement->GetPosition();

    // Apply full 3D velocity
    pos.x += velocity.x * elapsedTime;
    pos.y += velocity.y * elapsedTime;
    pos.z += velocity.z * elapsedTime;

    movement->SetPosition(pos);
    SyncData();

}

void Bullet::ApplyMovement(const DirectX::XMFLOAT3& newPos, const DirectX::XMFLOAT3& newVel)
{
    movement->SetPosition(newPos);
    velocity = newVel;
    SyncData();
}

void Bullet::SetActive(bool active)
{
    isActive = active;
}