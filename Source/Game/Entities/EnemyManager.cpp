#include "Enemy.h"
#include "EnemyManager.h"
#include "LegacyCharacterComponent.h"

using namespace DirectX;

namespace
{
    // Helper to generate readable names for the Hierarchy
    [[nodiscard]] constexpr std::string_view GetEnemyTypeName(EnemyType type) noexcept
    {
        switch (type)
        {
        case EnemyType::Paddle:           return "Paddle";
        case EnemyType::Ball:             return "Ball";
        case EnemyType::MushroomNone:     return "Mushroom_Idle";
        case EnemyType::MushroomStatic:   return "Mushroom_Turret";
        case EnemyType::MushroomTracking: return "Mushroom_Kamikaze";
        case EnemyType::FakeBoss:         return "FakeBoss";
        default:                          return "Unknown";
        }
    }
}

EnemyManager::EnemyManager() {}

EnemyManager::~EnemyManager() { m_enemies.clear(); }

void EnemyManager::Initialize(ID3D11Device* device, GameObject* parentNode)
{
    m_parentNode = parentNode;
    m_enemies.clear();
    m_enemyPool.clear();
    m_spawnCounter = 0;
}

void EnemyManager::SpawnEnemy(const EnemySpawnConfig& config)
{
    ID3D11Device* device{ Graphics::Instance().GetDevice() };
    const char* modelPath{ nullptr };
    DirectX::XMFLOAT3 finalScale{ config.Scale };

    // Behavior-Driven Model Selection 
    switch (config.Type)
    {
    case EnemyType::MushroomNone:     modelPath = "Data/Model/Character/ENEMY_mdl_EnemyNone.glb"; break;
    case EnemyType::MushroomStatic:   modelPath = "Data/Model/Character/ENEMY_mdl_EnemyStatic.glb"; break;
    case EnemyType::MushroomTracking: modelPath = "Data/Model/Character/ENEMY_mdl_EnemyTracking.glb"; break;
    case EnemyType::FakeBoss:
        modelPath = "Data/Model/Character/ENEMY_mdl_EnemyFakeBoss.glb";
        if (finalScale.x == 0.5f && finalScale.y == 0.5f && finalScale.z == 0.5f) { finalScale = { 2.0f, 2.0f, 2.0f }; }
        break;
    case EnemyType::Ball:             modelPath = "Data/Model/Character/PLACEHOLDER_mdl_Ball.glb"; break;
    case EnemyType::Paddle:
    default:                          modelPath = "Data/Model/Character/PLACEHOLDER_mdl_Paddle.glb"; break;
    }

    const int finalHP{ (config.AttackBehavior == AttackType::Tracking) ? 70 : config.MaxHP };

    Enemy* activeEnemyPtr{ nullptr };

    if (!m_enemyPool.empty())
    {
        // Re-use an existing memory allocation from the graveyard
        std::unique_ptr<Enemy> pooledEnemy{ std::move(m_enemyPool.back()) };
        m_enemyPool.pop_back();

        pooledEnemy->Reinitialize(
            device, modelPath, config.Position, config.Rotation, config.Color,
            config.Type, config.AttackBehavior, config.MinX, config.MaxX,
            config.MinZ, config.MaxZ, config.Direction
        );

        pooledEnemy->SetInvincible(config.Type == EnemyType::FakeBoss);
        pooledEnemy->SetScale(finalScale);
        pooledEnemy->SetBaseMoveSpeed(config.BaseSpeed);
        pooledEnemy->SetMaxHP(finalHP);

        activeEnemyPtr = pooledEnemy.get();
        m_enemies.push_back(std::move(pooledEnemy));
    }
    else
    {
        // Allocate a brand-new enemy
        auto newEnemy{ std::make_unique<Enemy>(
            device, modelPath, config.Position, config.Rotation, config.Color,
            config.Type, config.AttackBehavior, config.MinX, config.MaxX,
            config.MinZ, config.MaxZ, config.Direction
        ) };

        newEnemy->SetInvincible(config.Type == EnemyType::FakeBoss);
        newEnemy->SetScale(finalScale);
        newEnemy->SetBaseMoveSpeed(config.BaseSpeed);
        newEnemy->SetMaxHP(finalHP);

        activeEnemyPtr = newEnemy.get();
        m_enemies.push_back(std::move(newEnemy));
    }

    // Always create a GameObject wrapper, whether it's new or from the pool
    if (m_parentNode && activeEnemyPtr)
    {
        std::string nodeName{ GetEnemyTypeName(config.Type) };
        nodeName += "_" + std::to_string(++m_spawnCounter);

        auto enemyNode{ std::make_unique<GameObject>(nodeName) };
        enemyNode->AddComponent<LegacyCharacterComponent>(activeEnemyPtr);

        enemyNode->transform.position = config.Position;
        enemyNode->transform.rotation = config.Rotation;
        enemyNode->transform.scale = finalScale;

        m_parentNode->AddChild(std::move(enemyNode));
    }
}

void EnemyManager::Update(const float elapsedTime, Camera* camera, const DirectX::XMFLOAT3& playerPos, const bool allowAttack)
{
    for (size_t i{ 0 }; i < m_enemies.size(); )
    {
        auto& currentEnemy{ m_enemies[i] };

        if (!currentEnemy->IsActive())
        {
            // Move the dead enemy to the graveyard pool
            m_enemyPool.push_back(std::move(currentEnemy));

            if (i != m_enemies.size() - 1)
            {
                m_enemies[i] = std::move(m_enemies.back());
            }

            // Destroy the now-duplicate last element
            m_enemies.pop_back();
        }
        else
        {
            // Only update enemy logic if the player is alive
            if (allowAttack)
            {
                currentEnemy->Update(elapsedTime, camera);
                currentEnemy->UpdateTracking(elapsedTime, camera, playerPos, allowAttack);
            }

            // Move to the next element
            ++i;
        }
    }
}

void EnemyManager::Render(ModelRenderer* renderer, Camera* camera)
{
    for (auto& enemy : m_enemies)
    {
        bool isBodyVisible{ true };

        if (camera)
        {
            const DirectX::XMFLOAT3 pos{ enemy->GetPosition() };
            const DirectX::XMFLOAT3 scale{ enemy->GetScale() };

            const float maxScale{ (std::max)(scale.x, (std::max)(scale.y, scale.z)) };
            const float cullingRadius{ 150.0f * maxScale };

            if (!camera->CheckSphere(pos.x, pos.y, pos.z, cullingRadius))
            {
                isBodyVisible = false;
            }
        }

        if (isBodyVisible)
        {
            renderer->Draw(ShaderId::Phong, enemy->GetModel(), enemy->GetRenderColor());
        }

        enemy->RenderProjectiles(renderer);
    }
}

void EnemyManager::RenderDebug(ShapeRenderer* renderer)
{
    for (auto& enemy : m_enemies)
    {
        enemy->RenderDebugProjectiles(renderer);
    }
}

void EnemyManager::RespawnEnemyAs(size_t index, AttackType attack, MoveDir dir, float minX, float maxX, float minZ, float maxZ)
{
    if (index >= m_enemies.size()) return;

    auto& e{ m_enemies[index] };

    EnemySpawnConfig config;
    config.Position = e->GetPosition();
    config.Rotation = e->GetRotation();
    config.Color = e->GetBaseColor();
    config.Type = e->GetType();
    config.AttackBehavior = attack;
    config.Direction = dir;
    config.MinX = minX; config.MaxX = maxX;
    config.MinZ = minZ; config.MaxZ = maxZ;

    SpawnEnemy(config);

    m_enemyPool.push_back(std::move(m_enemies[index]));

    m_enemies[index] = std::move(m_enemies.back());
    m_enemies.pop_back();
}

void EnemyManager::ReviveKamikazes()
{
    // Search the graveyard pool for the Kamikaze that killed the player
    for (auto it{ m_enemyPool.begin() }; it != m_enemyPool.end(); )
    {
        if ((*it)->HasKilledPlayer())
        {
            // Reset state and position
            (*it)->SetKilledPlayer(false);
            (*it)->SetMaxHP(70);
            (*it)->SetActive(true);
            (*it)->SetPosition((*it)->GetOriginalPosition());
            (*it)->SetRotation((*it)->GetOriginalRotation());
            (*it)->GetProjectiles().clear();

            // Recreate the GameObject Hierarchy wrapper before moving the pointer
            if (m_parentNode)
            {
                static int s_reviveCount{ 0 };
                std::string nodeName{ "Mushroom_Kamikaze_Revived_" + std::to_string(++s_reviveCount) };

                auto enemyNode{ std::make_unique<GameObject>(nodeName) };
                enemyNode->AddComponent<LegacyCharacterComponent>(it->get());
                m_parentNode->AddChild(std::move(enemyNode));
            }

            // Move from the pool back to active list
            m_enemies.push_back(std::move(*it));
            it = m_enemyPool.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

void EnemyManager::Serialize(nlohmann::json& outJson) const
{
    nlohmann::json enemiesArray{ nlohmann::json::array() };

    for (const auto& enemy : m_enemies)
    {
        if (!enemy || !enemy->IsActive()) continue;

        nlohmann::json eJson{};
        const DirectX::XMFLOAT3 pos{ enemy->GetPosition() };
        const DirectX::XMFLOAT3 rot{ enemy->GetRotation() };
        const DirectX::XMFLOAT3 scale{ enemy->GetScale() };
        const DirectX::XMFLOAT4 color{ enemy->GetBaseColor() };

        eJson["PosX"] = pos.x; eJson["PosY"] = pos.y; eJson["PosZ"] = pos.z;
        eJson["RotX"] = rot.x; eJson["RotY"] = rot.y; eJson["RotZ"] = rot.z;
        eJson["ScaleX"] = scale.x; eJson["ScaleY"] = scale.y; eJson["ScaleZ"] = scale.z;
        eJson["ColR"] = color.x; eJson["ColG"] = color.y; eJson["ColB"] = color.z; eJson["ColA"] = color.w;

        eJson["Type"] = static_cast<int>(enemy->GetType());
        eJson["AttackBehavior"] = static_cast<int>(enemy->GetAttackType());
        eJson["Direction"] = static_cast<int>(enemy->GetMoveDir());

        eJson["MinX"] = enemy->GetMinX(); eJson["MaxX"] = enemy->GetMaxX();
        eJson["MinZ"] = enemy->GetMinZ(); eJson["MaxZ"] = enemy->GetMaxZ();

        eJson["MaxHP"] = enemy->GetHP(); 

        enemiesArray.push_back(eJson);
    }

    outJson["Enemies"] = enemiesArray;
}

void EnemyManager::Deserialize(const nlohmann::json& inJson)
{
    // Clear live memory and the GameObject Hierarchy folder
    m_enemies.clear();
    m_enemyPool.clear();

    // Reset the naming counter so reloaded enemies start at 1
    m_spawnCounter = 0;

    if (!inJson.contains("Enemies")) return;

    for (const auto& eJson : inJson["Enemies"])
    {
        EnemySpawnConfig config{};

        // .value() safely provides a default if the JSON key is missing
        config.Position = { eJson.value("PosX", 0.0f), eJson.value("PosY", 0.0f), eJson.value("PosZ", 0.0f) };
        config.Rotation = { eJson.value("RotX", 0.0f), eJson.value("RotY", 0.0f), eJson.value("RotZ", 0.0f) };
        config.Scale = { eJson.value("ScaleX", 1.0f), eJson.value("ScaleY", 1.0f), eJson.value("ScaleZ", 1.0f) };
        config.Color = { eJson.value("ColR", 1.0f), eJson.value("ColG", 1.0f), eJson.value("ColB", 1.0f), eJson.value("ColA", 1.0f) };

        config.Type = static_cast<EnemyType>(eJson.value("Type", 0));
        config.AttackBehavior = static_cast<AttackType>(eJson.value("AttackBehavior", 0));
        config.Direction = static_cast<MoveDir>(eJson.value("Direction", 0));

        config.MinX = eJson.value("MinX", 0.0f); config.MaxX = eJson.value("MaxX", 0.0f);
        config.MinZ = eJson.value("MinZ", 0.0f); config.MaxZ = eJson.value("MaxZ", 0.0f);

        config.MaxHP = eJson.value("MaxHP", 50);

        SpawnEnemy(config); // Spawn automatically handles GameObject creation
    }
}