#include "Item.h"
#include "System/Graphics.h"

using namespace DirectX;

Item::Item(ID3D11Device* device, const DirectX::XMFLOAT3& position, ItemType type)
{
    // Reuse Block model for memory optimization
    model = std::make_shared<Model>(device, "Data/Model/Character/PLACEHOLDER_mdl_Block.glb");

    m_type = type;
    movement->SetPosition(position);
    originalY = position.y;
    animTime = 0.0f;

    // Initialize color from type
    SetType(type);
}

void Item::Update(float elapsedTime, Camera* camera)
{
    if (!isActive) return;

    animTime += elapsedTime;

    // Floating animation (sine wave on Y axis)
    XMFLOAT3 pos = movement->GetPosition();
    pos.y = originalY + sinf(animTime * kFloatSpeed) * kFloatAmp;
    movement->SetPosition(pos);

    // Spinning animation
    movement->SetRotation({ 0.0f, animTime * kSpinSpeed, 0.0f });

    SyncData();
}

void Item::Render(ModelRenderer* renderer)
{
    if (!isActive) return;

    Character::scale = scale;
    renderer->Draw(ShaderId::Phong, model, color);
}