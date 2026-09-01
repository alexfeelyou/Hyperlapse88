#include <cmath>
#include "System/Graphics.h"
#include "Item.h"

using namespace DirectX;

Item::Item(ID3D11Device* device, const DirectX::XMFLOAT3& position, ItemType type)
{
    model = std::make_shared<Model>(device, "Data/Model/Character/PLACEHOLDER_mdl_Block.glb");

    m_type = type;
    if (movement) movement->SetPosition(position);
    originalY = position.y;
    animTime = 0.0f;

    // Assign directly to inherited Character::scale
    scale = { 2.0f, 2.0f, 2.0f };

    SetType(type);
}

void Item::Update(float elapsedTime, Camera* camera)
{
    if (!isActive || !movement) return;

    animTime += elapsedTime;

    // Floating animation (sine wave on Y axis)
    XMFLOAT3 pos{ movement->GetPosition() };
    pos.y = originalY + std::sin(animTime * kFloatSpeed) * kFloatAmp;
    movement->SetPosition(pos);

    // Spinning animation
    movement->SetRotation({ 0.0f, animTime * kSpinSpeed, 0.0f });

    // Sync physical data up to the visual model root node
    SyncData();
}

void Item::Render(ModelRenderer* renderer)
{
    if (!isActive) return;

    renderer->Draw(model, color);
}