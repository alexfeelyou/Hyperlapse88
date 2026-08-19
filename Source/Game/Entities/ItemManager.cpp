#include "ItemManager.h"

using namespace DirectX;

ItemManager::ItemManager() {}
ItemManager::~ItemManager() { m_items.clear(); }

void ItemManager::Initialize(ID3D11Device* device)
{
    m_deviceRef = device;
    m_items.clear();

    for (const auto& data : ItemLevelData::Spawns)
    {
        SpawnItem(data);
    }
}

void ItemManager::SpawnItem(const ItemSpawnData& data)
{
    if (!m_deviceRef) return;

    auto newItem = std::make_unique<Item>(m_deviceRef, data.Position, data.Type);
    newItem->SetRotation(data.Rotation);
    newItem->scale = data.Scale;
    if (data.Type == ItemType::Heal) 
    {
        newItem->color = { 1.0f, 0.89f, 0.58f, 1.0f }; 
    }
    else if (data.Type == ItemType::Invincible) 
    {
        newItem->color = { 0.5f, 0.5f, 0.5f, 1.0f };   
    }
    m_items.push_back(std::move(newItem));
}

void ItemManager::SpawnHealAt(const DirectX::XMFLOAT3& position)
{
    ItemSpawnData data;
    data.Type = ItemType::Heal;

    data.Position = position;
    data.Position.y = 0.4f; 

    data.Rotation = { 0.0f, 0.0f, 0.0f };
    data.Scale = { 2.0f, 2.0f, 2.0f }; 

    SpawnItem(data);
}

void ItemManager::AddItem(ItemType type)
{
    ItemSpawnData defaultData;
    defaultData.Type = type;

    if (!m_items.empty()) 
    {
        defaultData.Position = m_items.back()->GetBasePosition(); 
        defaultData.Rotation = { 0, 0, 0 };
        defaultData.Scale = m_items.back()->scale;
    }
    else 
    {
        defaultData.Position = { 0, 0.4f, 0 };
        defaultData.Rotation = { 0, 0, 0 };
        defaultData.Scale = { 2.0f, 2.0f, 2.0f };
    }
    SpawnItem(defaultData);
}

void ItemManager::Update(float elapsedTime, Camera* camera)
{
    for (auto& item : m_items)
    {
        item->Update(elapsedTime, camera);
    }
}

void ItemManager::Render(ModelRenderer* renderer)
{
    for (auto& item : m_items)
    {
        item->Render(renderer);
    }
}

void ItemManager::RenderDebug(ShapeRenderer* renderer)
{
    if (m_debugHighlightIndex >= 0 && m_debugHighlightIndex < m_items.size())
    {
        auto& item = m_items[m_debugHighlightIndex];
        if (item && item->IsActive())
        {
            XMFLOAT3 pos = item->GetPosition();
            XMFLOAT3 boxSize;
            boxSize.x = (item->scale.x * 0.5f) + 0.05f; // Half scale + Tiny Padding
            boxSize.y = (item->scale.y * 0.5f) + 0.05f;
            boxSize.z = (item->scale.z * 0.5f) + 0.05f;

            // [FIXED] Pass 'boxSize', not 'scale'
            renderer->DrawBox(pos, item->GetRotation(), boxSize, { 1.0f, 1.0f, 0.0f, 1.0f });
        }
    }
}

void ItemManager::ResetAllAnimations()
{
    for (auto& item : m_items) { if (item) item->ResetAnimation(); }
}