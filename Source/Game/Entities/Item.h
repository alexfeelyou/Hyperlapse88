#pragma once

#include <cstdint>
#include <DirectXMath.h>
#include <memory>

class Camera;
class GameObject;
class Model;
class ModelRenderer;
struct ID3D11Device;

enum class ItemType : std::uint8_t { Heal, Invincible };

class Item final
{
public:
    Item(ID3D11Device* device, const DirectX::XMFLOAT3& position, ItemType type) noexcept;
    ~Item() = default;

    Item(const Item&) = delete;
    Item& operator=(const Item&) = delete;
    Item(Item&&) noexcept = default;
    Item& operator=(Item&&) noexcept = default;

    void Update(float elapsedTime, Camera* camera = nullptr) noexcept;
    void Render(ModelRenderer* renderer) const noexcept;

    [[nodiscard]] bool IsActive() const noexcept { return m_isActive; }
    void SetActive(bool active) noexcept { m_isActive = active; }

    [[nodiscard]] ItemType GetType() const noexcept { return m_type; }
    void SetType(ItemType type) noexcept;

    [[nodiscard]] const DirectX::XMFLOAT3& GetPosition() const noexcept { return m_position; }
    void SetPosition(const DirectX::XMFLOAT3& pos) noexcept
    {
        m_position = pos;
        m_originalY = pos.y;
    }

    [[nodiscard]] const DirectX::XMFLOAT3& GetRotation() const noexcept { return m_rotation; }
    void SetRotation(const DirectX::XMFLOAT3& rot) noexcept { m_rotation = rot; }

    [[nodiscard]] DirectX::XMFLOAT3 GetBasePosition() const noexcept
    {
        return { m_position.x, m_originalY, m_position.z };
    }

    void ResetAnimation() noexcept { m_animTime = 0.0f; }

    // Hierarchy node link for Inspector active checks
    void SetOwnerNode(GameObject* node) noexcept { m_ownerNode = node; }
    [[nodiscard]] GameObject* GetOwnerNode() const noexcept { return m_ownerNode; }

    DirectX::XMFLOAT3 scale{ 2.0f, 2.0f, 2.0f };
    DirectX::XMFLOAT4 color{ 1.0f, 0.89f, 0.58f, 1.0f };

private:
    std::shared_ptr<Model> m_model{ nullptr };
    GameObject* m_ownerNode{ nullptr };

    DirectX::XMFLOAT3 m_position{ 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT3 m_rotation{ 0.0f, 0.0f, 0.0f };

    ItemType m_type{ ItemType::Heal };
    bool m_isActive{ true };

    float m_originalY{ 0.0f };
    float m_animTime{ 0.0f };

    static constexpr float kFloatSpeed{ 2.0f };
    static constexpr float kFloatAmp{ 0.5f };
    static constexpr float kSpinSpeed{ 1.5f };

    static constexpr DirectX::XMFLOAT4 kHealColor{ 1.0f, 0.89f, 0.58f, 1.0f };
    static constexpr DirectX::XMFLOAT4 kInvincibleColor{ 0.5f, 0.5f, 0.5f, 1.0f };
};