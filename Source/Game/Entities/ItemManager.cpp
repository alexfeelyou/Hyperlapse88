#include "ItemManager.h"
#include "LegacyCharacterComponent.h"

using namespace DirectX;

ItemManager::ItemManager() = default;
ItemManager::~ItemManager() { m_items.clear(); }

void ItemManager::Initialize(ID3D11Device* device, GameObject* parentNode)
{
    m_deviceRef = device;
    m_parentNode = parentNode;
    m_items.clear();
    m_spawnCounter = 0; // Reset counter on scene load
}

void ItemManager::SpawnItem(const ItemSpawnData& data)
{
    if (!m_deviceRef) return;

    auto newItem{ std::make_unique<Item>(m_deviceRef, data.Position, data.Type) };
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

    // Wrap the new item in a GameObject
    if (m_parentNode)
    {
        std::string nodeName{ (data.Type == ItemType::Heal) ? "Item_Heal" : "Item_Invincibility" };
        nodeName += "_" + std::to_string(++m_spawnCounter);

        auto itemNode{ std::make_unique<GameObject>(nodeName) };
        itemNode->AddComponent<LegacyCharacterComponent>(newItem.get());

        m_parentNode->AddChild(std::move(itemNode));
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
            boxSize.x = (item->scale.x * 0.5f) + 0.05f; 
            boxSize.y = (item->scale.y * 0.5f) + 0.05f;
            boxSize.z = (item->scale.z * 0.5f) + 0.05f;

            renderer->DrawBox(pos, item->GetRotation(), boxSize, { 1.0f, 1.0f, 0.0f, 1.0f });
        }
    }
}

void ItemManager::ResetAllAnimations()
{
    for (auto& item : m_items) { if (item) item->ResetAnimation(); }
}

void ItemManager::Serialize(nlohmann::json& outJson) const
{
    nlohmann::json itemsArray{ nlohmann::json::array() };

    for (const auto& item : m_items)
    {
        if (!item || !item->IsActive()) continue;

        nlohmann::json iJson{};
        const DirectX::XMFLOAT3 pos{ item->GetBasePosition() }; // Base pos avoids saving mid-bounce
        const DirectX::XMFLOAT3 rot{ item->GetRotation() };
        const DirectX::XMFLOAT3 scale{ item->scale };

        iJson["PosX"] = pos.x; iJson["PosY"] = pos.y; iJson["PosZ"] = pos.z;
        iJson["RotX"] = rot.x; iJson["RotY"] = rot.y; iJson["RotZ"] = rot.z;
        iJson["ScaleX"] = scale.x; iJson["ScaleY"] = scale.y; iJson["ScaleZ"] = scale.z;
        iJson["Type"] = static_cast<int>(item->GetType());

        itemsArray.push_back(iJson);
    }

    outJson["Items"] = itemsArray;
}

void ItemManager::Deserialize(const nlohmann::json& inJson)
{
    m_items.clear();

    if (!inJson.contains("Items")) return;

    for (const auto& iJson : inJson["Items"])
    {
        ItemSpawnData data{};
        data.Position = { iJson.value("PosX", 0.0f), iJson.value("PosY", 0.0f), iJson.value("PosZ", 0.0f) };
        data.Rotation = { iJson.value("RotX", 0.0f), iJson.value("RotY", 0.0f), iJson.value("RotZ", 0.0f) };
        data.Scale = { iJson.value("ScaleX", 1.0f), iJson.value("ScaleY", 1.0f), iJson.value("ScaleZ", 1.0f) };
        data.Type = static_cast<ItemType>(iJson.value("Type", 0));

        SpawnItem(data);
    }
}