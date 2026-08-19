#pragma once
#include <vector>
#include <memory>
#include "Item.h"
#include "System/Graphics.h"
#include "System/ShapeRenderer.h"

struct ItemSpawnData {
    DirectX::XMFLOAT3 Position;
    DirectX::XMFLOAT3 Rotation;
    DirectX::XMFLOAT3 Scale;
    ItemType Type;
};

// ==========================================
// ITEM CONFIGURATION (EDIT HERE)
// ==========================================
namespace ItemLevelData
{
    static const std::vector<ItemSpawnData> Spawns =
    {
        // 1. Heal Item
        { { -4.0f, 0.4f, 20.0f }, { 0.0f, 0.0f, 0.0f }, { 2.0f, 2.0f, 2.0f }, ItemType::Heal },

        // 2. Invincible Item
        { { 4.0f, 0.4f, 20.0f }, { 0.0f, 0.0f, 0.0f }, { 2.0f, 2.0f, 2.0f }, ItemType::Invincible }
    };
}

class ItemManager
{
public:
    ItemManager();
    ~ItemManager();

    void Initialize(ID3D11Device* device);
    void Update(float elapsedTime, Camera* camera);
    void Render(ModelRenderer* renderer);
    void RenderDebug(ShapeRenderer* renderer);

    // Spawning
    void AddItem(ItemType type); // For GUI Button
    void ResetAllAnimations();
    void SpawnItem(const ItemSpawnData& data);
    void SpawnHealAt(const DirectX::XMFLOAT3& position);

    std::vector<std::unique_ptr<Item>>& GetItems() { return m_items; }

    // GUI Tools
    void SetHighlight(int index) { m_debugHighlightIndex = index; }
    int GetHighlight() const { return m_debugHighlightIndex; }

private:
    std::vector<std::unique_ptr<Item>> m_items;
    ID3D11Device* m_deviceRef = nullptr;
    int m_debugHighlightIndex = -1;
};