#include "Weapon.h"
#include "System/AssetManager.h"

using namespace DirectX;

Weapon::Weapon(ID3D11Device* device, const char* modelPath)
{
    // Route through AssetManager
    m_model = Engine::System::AssetManager::Instance().GetOrLoadModel(device, modelPath);
    XMStoreFloat4x4(&m_finalWorldMatrix, XMMatrixIdentity());

    // If the weapon is still too large, adjust your m_offsetScale either here
    // or wherever SetLocalOffset is called.
}

void Weapon::SetLocalOffset(const DirectX::XMFLOAT3& pos, const DirectX::XMFLOAT3& rotEuler, const DirectX::XMFLOAT3& scale)
{
    m_offsetPos = pos;
    m_offsetRot = rotEuler;
    m_offsetScale = scale;
}

void Weapon::UpdateTransform(const XMFLOAT4X4& parentBoneMatrix)
{
    if (!m_model) return;

    // Calculate Local Matrix 
    XMMATRIX S = XMMatrixScaling(m_offsetScale.x, m_offsetScale.y, m_offsetScale.z);

    XMMATRIX R = XMMatrixRotationRollPitchYaw(
        XMConvertToRadians(m_offsetRot.x),
        XMConvertToRadians(m_offsetRot.y),
        XMConvertToRadians(m_offsetRot.z)
    );

    XMMATRIX T = XMMatrixTranslation(m_offsetPos.x, m_offsetPos.y, m_offsetPos.z);

    XMMATRIX localOffset = S * R * T;

    // Multiply by Parent Bone (Local * Parent)
    XMMATRIX parentBone = XMLoadFloat4x4(&parentBoneMatrix);
    XMMATRIX finalTransform = localOffset * parentBone;

    // Store result
    XMStoreFloat4x4(&m_finalWorldMatrix, finalTransform);

    m_model->UpdateTransform(m_finalWorldMatrix);
}

void Weapon::Render(ModelRenderer* renderer)
{
    if (!m_model) return;

    renderer->Draw(ShaderId::Phong, m_model, { 1.0f, 1.0f, 1.0f, 1.0f }, m_finalWorldMatrix);
}