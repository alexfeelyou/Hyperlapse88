#pragma once

#include <DirectXMath.h>
#include <json.hpp>
#include <memory>
#include <vector>
#include <array>
#include "System/Graphics.h"
#include "GameObject.h"

class Enemy;
class ShapeRenderer;

enum class EnemyType
{
    Paddle,
    Ball,
    MushroomNone,
    MushroomStatic,
    MushroomTracking,
    FakeBoss
};

enum class AttackType
{
    None,
    Static,
    TrackingHorizontal,
    TrackingRandom,
    RadialBurst
};

enum class MoveDir
{
    None,
    Left,
    Right
};

struct EnemySpawnConfig
{
    DirectX::XMFLOAT3 Position{ 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT3 Rotation{ 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT4 Color{ 1.0f, 1.0f, 1.0f, 1.0f };
    EnemyType Type{ EnemyType::MushroomNone };
    AttackType AttackBehavior{ AttackType::None };
    MoveDir Direction{ MoveDir::None };
    float MinX{ 0.0f }; float MaxX{ 0.0f };
    float MinZ{ 0.0f }; float MaxZ{ 0.0f };

    DirectX::XMFLOAT3 Scale{ 0.5f, 0.5f, 0.5f };
    float BaseSpeed{ 2.0f };
    int MaxHP{ 50 };
};

namespace EnemyLevelData
{
    // ==========================================
    // COLOR PRESETS 
    // ==========================================
    static const DirectX::XMFLOAT4 None = { 1.0f, 1.0f, 1.0f, 1.0f };
    static const DirectX::XMFLOAT4 Blue = { 0.0f, 0.0f, 0.8f, 1.0f };
    static const DirectX::XMFLOAT4 PaleYellow = { 0.76f, 0.74f, 0.56f, 1.0f };
    static const DirectX::XMFLOAT4 Potioned = { -1.0f, -1.0f, -1.0f, 1.0f };
    static const DirectX::XMFLOAT4 ArcanePurple = { 0.8f, 0.2f, 0.9f, 1.0f };
    static const DirectX::XMFLOAT4 ToxicGreen = { 0.4f, 0.9f, 0.2f, 1.0f };

    // ==========================================
    // ROTATION PRESETS
    // ==========================================
    namespace Rot
    {
        static const DirectX::XMFLOAT3 Backward = { 0.0f, 180.0f, 0.0f };
        static const DirectX::XMFLOAT3 Forward = { 0.0f, 0.0f, 0.0f };
        static const DirectX::XMFLOAT3 Left = { 0.0f, -90.0f, 0.0f };
        static const DirectX::XMFLOAT3 Right = { 0.0f, 90.0f, 0.0f };
    }
}

class EnemyManager
{
public:
    EnemyManager();
    ~EnemyManager();

    void Initialize(ID3D11Device* device, GameObject* parentNode = nullptr);

    void Update(float elapsedTime, Camera* camera, const DirectX::XMFLOAT3& playerPos, bool allowAttack = true);
    void Render(ModelRenderer* renderer, Camera* camera = nullptr);
    void RenderDebug(ShapeRenderer* renderer);

    void RespawnEnemyAs(size_t index, AttackType attack, MoveDir dir = MoveDir::None, float minX = 0, float maxX = 0, float minZ = 0, float maxZ = 0);
    void SpawnEnemy(const EnemySpawnConfig& config);

    void Serialize(nlohmann::json& outJson) const;
    void Deserialize(const nlohmann::json& inJson);

    [[nodiscard]] std::vector<std::unique_ptr<Enemy>>& GetEnemies() noexcept { return m_enemies; }

private:
    std::vector<std::unique_ptr<Enemy>> m_enemies{};
    std::vector<std::unique_ptr<Enemy>> m_enemyPool{};

    GameObject* m_parentNode{ nullptr }; // Tracks the Hierarchy folder
    uint32_t m_spawnCounter{ 0 }; // Tracks spawns per scene reload
};