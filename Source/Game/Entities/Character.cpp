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

    XMFLOAT3 pos = movement->GetPosition();
    XMMATRIX T = XMMatrixTranslation(pos.x, pos.y, pos.z);

    XMFLOAT3 rot = movement->GetRotation();
    XMMATRIX R = XMMatrixRotationRollPitchYaw(rot.x, rot.y, rot.z);

    XMFLOAT4X4 transform;
    XMStoreFloat4x4(&transform, R * T);

    renderer->DrawCapsule(transform, 0.5f, 1.6f, { 0, 1, 0, 1 });
}

void Character::SyncData()
{
    if (!model || model->GetNodes().empty()) return;

    Model::Node& rootNode = model->GetNodes().at(0);

    // Sync position, rotation, and scale from movement state to model root node
    rootNode.position = movement->GetPosition();

    XMFLOAT3 rot = movement->GetRotation();
    XMVECTOR qRot = XMQuaternionRotationRollPitchYaw(rot.x, rot.y, rot.z);
    XMStoreFloat4(&rootNode.rotation, qRot);

    rootNode.scale = scale;

    XMFLOAT4X4 identity;
    XMStoreFloat4x4(&identity, XMMatrixIdentity());
    model->UpdateTransform(identity);
}