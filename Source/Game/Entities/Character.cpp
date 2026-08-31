#include "Character.h"

using namespace DirectX;

Character::Character()
    : movement(std::make_unique<CharacterMovement>())
{
}

// ~Character() is compiler-generated — unique_ptr cleans up movement automatically

void Character::Render(ModelRenderer* renderer)
{
    if (model) renderer->Draw(ShaderId::Lambert, model);
}

void Character::RenderDebug(const RenderContext& rc, ShapeRenderer* renderer)
{
    if (!movement) return;

    const XMFLOAT3 pos{ movement->GetPosition() };
    const XMMATRIX T{ XMMatrixTranslation(pos.x, pos.y, pos.z) };

    const XMFLOAT3 rot{ movement->GetRotation() };
    const XMMATRIX R{ XMMatrixRotationRollPitchYaw(rot.x, rot.y, rot.z) };

    XMFLOAT4X4 transform{};
    XMStoreFloat4x4(&transform, R * T);

    renderer->DrawCapsule(transform, 0.5f, 1.6f, { 0.0f, 1.0f, 0.0f, 1.0f });
}

void Character::SyncData() noexcept
{
    if (!model || model->GetNodes().empty()) return;

    Model::Node& rootNode{ model->GetNodes().front() };

    rootNode.position = movement->GetPosition();

    const XMFLOAT3 rot{ movement->GetRotation() };
    const XMVECTOR qRot{ XMQuaternionRotationRollPitchYaw(rot.x, rot.y, rot.z) };
    XMStoreFloat4(&rootNode.rotation, qRot);

    rootNode.scale = scale;

    XMFLOAT4X4 identity{};
    XMStoreFloat4x4(&identity, XMMatrixIdentity());
    model->UpdateTransform(identity);
}