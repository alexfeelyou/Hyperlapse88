#pragma once
#include "Character.h"
#include <DirectXMath.h>

enum class ItemType { Heal, Invincible };

class Item : public Character
{
public:
    Item(ID3D11Device* device, const DirectX::XMFLOAT3& position, ItemType type);
    ~Item() override = default;

    void Update(float elapsedTime, Camera* camera) override;
    void Render(ModelRenderer* renderer);

    // --- Getters / Setters ---
    bool     IsActive()  const { return isActive; }
    ItemType GetType()   const { return m_type; }

    void SetActive(bool active) { isActive = active; }
    void ResetAnimation() { animTime = 0.0f; }

    void SetType(ItemType type)
    {
        m_type = type;
        color = (m_type == ItemType::Heal) ? kHealColor : kInvincibleColor;
    }

    DirectX::XMFLOAT3 GetPosition() const { return movement->GetPosition(); }
    DirectX::XMFLOAT3 GetRotation() const { return movement->GetRotation(); }

    DirectX::XMFLOAT3 GetBasePosition() const
    {
        DirectX::XMFLOAT3 pos = movement->GetPosition();
        pos.y = originalY;
        return pos;
    }

    void SetPosition(const DirectX::XMFLOAT3& pos) { movement->SetPosition(pos); originalY = pos.y; }
    void SetRotation(const DirectX::XMFLOAT3& rot) { movement->SetRotation(rot); }

    // Public for renderer
    DirectX::XMFLOAT4 color = kHealColor;
    DirectX::XMFLOAT3 scale = { 2.0f, 2.0f, 2.0f };

private:
    ItemType m_type;
    bool     isActive = true;

    float originalY = 0.0f;
    float animTime = 0.0f;

    // Animation config — static so they're shared, not duplicated per instance
    static constexpr float kFloatSpeed = 2.0f;
    static constexpr float kFloatAmp = 0.5f;
    static constexpr float kSpinSpeed = 1.5f;

    // Color presets
    static constexpr DirectX::XMFLOAT4 kHealColor = { 1.0f, 0.89f, 0.58f, 1.0f }; // Pale yellow
    static constexpr DirectX::XMFLOAT4 kInvincibleColor = { 0.5f, 0.5f,  0.5f,  1.0f }; // Grey
};