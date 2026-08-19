#pragma once

#include <memory>
#include <DirectXMath.h>
#include "System/Model.h"
#include "System/ModelRenderer.h"

class Weapon
{
public:
    // Pass the device so we can initialize the 3D model inside this class
    Weapon(ID3D11Device* device, const char* modelPath);
    ~Weapon() = default;

    // Disallow copying to prevent heavy GPU resource duplication
    Weapon(const Weapon&) = delete;
    Weapon& operator=(const Weapon&) = delete;

    void UpdateTransform(const DirectX::XMFLOAT4X4& parentBoneMatrix);
    void Render(ModelRenderer* renderer);

    // Getters for ImGui to directly manipulate the memory addresses
    DirectX::XMFLOAT3* GetOffsetPos() { return &m_offsetPos; }
    DirectX::XMFLOAT3* GetOffsetRot() { return &m_offsetRot; }
    DirectX::XMFLOAT3* GetOffsetScale() { return &m_offsetScale; }

    void SetLocalOffset(const DirectX::XMFLOAT3& pos, const DirectX::XMFLOAT3& rotEuler, const DirectX::XMFLOAT3& scale);
    void ResetOffsets();

private:
    std::shared_ptr<Model> m_model{};

    // Local adjustments so the weapon perfectly fits the palm of the hand
    DirectX::XMFLOAT3 m_offsetPos{ 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT3 m_offsetRot{ 0.0f, 0.0f, 0.0f }; 
    DirectX::XMFLOAT3 m_offsetScale{ 1.0f, 1.0f, 1.0f };

    DirectX::XMFLOAT4X4 m_finalWorldMatrix{};
};