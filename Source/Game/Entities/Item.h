#pragma once

#include <DirectXMath.h>
#include "Character.h"

enum class ItemType { Heal, Invincible };

class Item final : public Character
{
public:
    Item(ID3D11Device* device, const DirectX::XMFLOAT3& position, ItemType type);
    ~Item() override = default;

    void Update(float elapsedTime, Camera* camera) override;
    void Render(ModelRenderer* renderer);

    // Getters / Setters 
    [[nodiscard]] bool IsActive() const noexcept override { return isActive; }
    [[nodiscard]] ItemType GetType() const noexcept { return m_type; }

    void SetActive(bool active) noexcept { isActive = active; }
    void ResetAnimation() noexcept { animTime = 0.0f; }

    void SetType(ItemType type) noexcept
    {
        m_type = type;
        color = (m_type == ItemType::Heal) ? kHealColor : kInvincibleColor;
    }

    [[nodiscard]] DirectX::XMFLOAT3 GetPosition() const noexcept { return movement->GetPosition(); }
    [[nodiscard]] DirectX::XMFLOAT3 GetRotation() const noexcept { return movement->GetRotation(); }

    [[nodiscard]] DirectX::XMFLOAT3 GetBasePosition() const noexcept
    {
        DirectX::XMFLOAT3 pos{ movement->GetPosition() };
        pos.y = originalY;
        return pos;
    }

    // Override the virtual setters
    void SetPosition(const DirectX::XMFLOAT3& pos) noexcept override
    {
        if (movement) movement->SetPosition(pos);
        originalY = pos.y;
    }

    void SetRotation(const DirectX::XMFLOAT3& rot) noexcept override
    {
        if (movement) movement->SetRotation(rot);
    }

    // Public for renderer
    DirectX::XMFLOAT4 color{ kHealColor };

private:
    ItemType m_type;
    bool isActive{ true };

    float originalY{ 0.0f };
    float animTime{ 0.0f };

    static constexpr float kFloatSpeed{ 2.0f };
    static constexpr float kFloatAmp{ 0.5f };
    static constexpr float kSpinSpeed{ 1.5f };

    // Color presets
    static constexpr DirectX::XMFLOAT4 kHealColor{ 1.0f, 0.89f, 0.58f, 1.0f }; // Pale yellow
    static constexpr DirectX::XMFLOAT4 kInvincibleColor{ 0.5f, 0.5f, 0.5f, 1.0f }; // Grey
};