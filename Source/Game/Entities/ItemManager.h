#pragma once

#include <memory>
#include <vector>
#include "System/Graphics.h"
#include "System/ShapeRenderer.h"
#include "Item.h"
#include "GameObject.h"

struct ItemSpawnData {
    DirectX::XMFLOAT3 Position;
    DirectX::XMFLOAT3 Rotation;
    DirectX::XMFLOAT3 Scale;
    ItemType Type;
};

// ==========================================
// ITEM CONFIGURATION 
// ==========================================
namespace ItemLevelData
{
    static const std::vector<ItemSpawnData> Spawns =
    {
        // Heal Items
        { { -3.0f, 0.4f, 5.0f }, { 0.0f, 0.0f, 0.0f }, { 2.0f, 2.0f, 2.0f }, ItemType::Heal }
    };
}

class ItemManager
{
public:
    ItemManager();
    ~ItemManager();

    void Initialize(ID3D11Device* device, GameObject* parentNode = nullptr);

    void Update(float elapsedTime, Camera* camera);
    void Render(ModelRenderer* renderer);
    void RenderDebug(ShapeRenderer* renderer);

    // Spawning
    void AddItem(ItemType type);
    void ResetAllAnimations();
    void SpawnItem(const ItemSpawnData& data);
    void SpawnHealAt(const DirectX::XMFLOAT3& position);

    [[nodiscard]] std::vector<std::unique_ptr<Item>>& GetItems() noexcept { return m_items; }

    // GUI Tools
    void SetHighlight(int index) noexcept { m_debugHighlightIndex = index; }
    [[nodiscard]] int GetHighlight() const noexcept { return m_debugHighlightIndex; }

private:
    std::vector<std::unique_ptr<Item>> m_items{};
    ID3D11Device* m_deviceRef{ nullptr };
    int m_debugHighlightIndex{ -1 };

    GameObject* m_parentNode{ nullptr }; // Tracks the Hierarchy folder
};