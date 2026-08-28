#pragma once

#include <json.hpp>
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

    void Serialize(nlohmann::json& outJson) const;
    void Deserialize(const nlohmann::json& inJson);

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