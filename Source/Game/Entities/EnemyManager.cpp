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

    for (const auto& config : EnemyLevelData::Spawns)
    {
        SpawnEnemy(config);
    }
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

    if (!m_enemyPool.empty())
    {
        // Re-use an existing memory allocation. Its existing GameObject will snap to the new position
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

        // Wrap the new memory in a GameObject
        if (m_parentNode)
        {
            // Construct a unique name 
            std::string nodeName{ GetEnemyTypeName(config.Type) };
            nodeName += "_" + std::to_string(m_enemies.size() + m_enemyPool.size());

            auto enemyNode{ std::make_unique<GameObject>(nodeName) };
            enemyNode->AddComponent<LegacyCharacterComponent>(newEnemy.get());

            m_parentNode->AddChild(std::move(enemyNode));
        }

        m_enemies.push_back(std::move(newEnemy));
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
        bool isBodyVisible = true;

        if (camera)
        {
            DirectX::XMFLOAT3 pos = enemy->GetPosition();
            DirectX::XMFLOAT3 scale = enemy->GetScale();

            float maxScale = max(scale.x, max(scale.y, scale.z));
            float cullingRadius = 150.0f * maxScale;

            if (!camera->CheckSphere(pos.x, pos.y, pos.z, cullingRadius))
            {
                isBodyVisible = false;
            }
        }

        if (isBodyVisible)
        {
            renderer->Draw(ShaderId::Phong, enemy->GetModel(), enemy->GetRenderColor());
        }

        // Projectiles tetap dirender terpisah (selalu render)
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
    for (auto it = m_enemyPool.begin(); it != m_enemyPool.end(); )
    {
        if ((*it)->HasKilledPlayer())
        {
            // Reset the killer tag
            (*it)->SetKilledPlayer(false);

            // Fully heal it and revive it
            (*it)->SetMaxHP(70);
            (*it)->SetActive(true);

            // Snap it back to its original spawn position
            (*it)->SetPosition((*it)->GetOriginalPosition());
            (*it)->SetRotation((*it)->GetOriginalRotation());
            (*it)->GetProjectiles().clear(); // Wipe any old bullets

            // Move it from the graveyard back to the active enemies list
            m_enemies.push_back(std::move(*it));

            // Erase the empty shell from the pool
            it = m_enemyPool.erase(it);
        }
        else
        {
            ++it;
        }
    }
}