#pragma once
#include <DirectXMath.h> 
#include <memory>
#include <vector>
#include <array>
#include "System/Graphics.h"

class Enemy;
class ShapeRenderer;

enum class EnemyType
{
    Paddle,
    Ball,
    Pentagon,
    MushroomNone,
    MushroomStatic,
    MushroomTracking,
    FakeBoss
};

enum class AttackType
{
    None,
    Static,
    Tracking,
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
    DirectX::XMFLOAT3 Position;
    DirectX::XMFLOAT3 Rotation;
    DirectX::XMFLOAT4 Color;
    EnemyType Type;
    AttackType AttackBehavior;
    MoveDir Direction = MoveDir::None;
    float MinX = 0.0f;
    float MaxX = 0.0f;
    float MinZ = 0.0f;
    float MaxZ = 0.0f;

    DirectX::XMFLOAT3 Scale = { 0.5f, 0.5f, 0.5f };
    float BaseSpeed = 2.0f;

    int MaxHP = 50;
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

    // ==========================================
    // MASTER SPAWN LIST
    // ==========================================
    static const std::array<EnemySpawnConfig, 20> Spawns =
    { {
            // NONE: Just stands there, doesn't shoot or move
            { { 8.6f, 1.1f, 39.9f }, Rot::Backward, None, EnemyType::MushroomNone, AttackType::None },
            { { -6.2f, 1.1f, 100.6f }, Rot::Backward, None, EnemyType::MushroomNone, AttackType::None },
            { { 20.0f, 1.1f, 113.8f }, Rot::Backward, None, EnemyType::MushroomNone, AttackType::None },
            { { 54.7f, 1.1f, 98.9f }, Rot::Backward, None, EnemyType::MushroomNone, AttackType::None },
            { { 11.6f, 1.1f, 77.0f }, Rot::Forward, None, EnemyType::MushroomStatic, AttackType::None },
            { { 11.3f, 1.1f, 90.2f }, Rot::Forward, None, EnemyType::MushroomStatic, AttackType::None },
            { { 25.6f, 1.1f, 84.6f }, Rot::Forward, None, EnemyType::MushroomStatic, AttackType::None },
            { { 28.1f, 1.1f, 77.0f }, Rot::Forward, None, EnemyType::MushroomStatic, AttackType::None },
            { { -6.1f, 1.1f, 72.4f }, Rot::Forward, None, EnemyType::MushroomTracking, AttackType::None },
            { { -1.3f, 1.1f, 115.3f }, Rot::Forward, None, EnemyType::MushroomTracking, AttackType::None },
            { { 51.7f, 1.1f, 113.7f }, Rot::Forward, None, EnemyType::MushroomTracking, AttackType::None },
            { { 35.9f, 1.1f, 67.0f }, Rot::Backward, None, EnemyType::FakeBoss, AttackType::None },

            // STATIC: Stands completely still, but shoots at the player
            { { -6.4f, 1.1f, 83.0f }, Rot::Right, Potioned, EnemyType::MushroomNone, AttackType::Static },
            { { -6.2f, 1.1f, 109.3f }, Rot::Right, Potioned, EnemyType::MushroomStatic, AttackType::Static },
            { { 24.8f, 1.1f, 113.8f }, Rot::Backward, Potioned, EnemyType::MushroomTracking, AttackType::Static },
            { { 55.614f, 1.1f, 109.4f }, Rot::Left, Potioned, EnemyType::MushroomStatic, AttackType::Static },

            // TRACKING: Slowly chases the player around the map
            { { 31.514f, 1.1f, 99.7f }, Rot::Left, Potioned, EnemyType::MushroomTracking, AttackType::Tracking },
            { { 55.814f, 1.1f, 92.4f }, Rot::Left, Potioned, EnemyType::MushroomStatic, AttackType::Tracking },
            { { 53.514f, 1.1f, 75.2f }, Rot::Left, Potioned, EnemyType::MushroomTracking, AttackType::Tracking },
            { { 26.714f, 1.1f, 73.4f }, Rot::Right, Potioned, EnemyType::MushroomStatic, AttackType::Tracking }
    } };
}

class EnemyManager
{
public:
    EnemyManager();
    ~EnemyManager();

    void Initialize(ID3D11Device* device);
    void Update(float elapsedTime, Camera* camera, const DirectX::XMFLOAT3& playerPos, bool allowAttack = true);
    void Render(ModelRenderer* renderer, Camera* camera = nullptr);
    void RenderDebug(ShapeRenderer* renderer);
    void RespawnEnemyAs(size_t index, AttackType attack, MoveDir dir = MoveDir::None, float minX = 0, float maxX = 0, float minZ = 0, float maxZ = 0);
    void ReviveKamikazes();
    void SpawnEnemy(const EnemySpawnConfig& config);

    std::vector<std::unique_ptr<Enemy>>& GetEnemies() { return m_enemies; }
private:
    std::vector<std::unique_ptr<Enemy>> m_enemies{};
    std::vector<std::unique_ptr<Enemy>> m_enemyPool{};
};