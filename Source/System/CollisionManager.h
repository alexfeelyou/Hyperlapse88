#pragma once

#include <algorithm>
#include <cmath>
#include <DirectXMath.h>
#include <functional>
#include <vector>
#include "System/AudioManager.h"
#include "System/Collision.h"
#include "System/Input.h"
#include "Enemy.h"
#include "Player.h"
#include "Stage.h"
#include "EnemyManager.h"
#include "ItemManager.h"
#include "StateMachine.h"
#include "PlayerStates.h"
#include "NaviAlly.h"

class ItemManager;
class NaviAlly;
class Boss;

// Axis-Aligned Bounding Box for rapid broad-phase rejection
struct AABB
{
    DirectX::XMFLOAT3 minPoint{};
    DirectX::XMFLOAT3 maxPoint{};
};

// inline and noexcept allow the compiler to heavily optimize this (zero-cost abstraction).
[[nodiscard]] inline bool CheckAABBIntersection(const AABB& a, const AABB& b) noexcept
{
    return (a.minPoint.x <= b.maxPoint.x && a.maxPoint.x >= b.minPoint.x) &&
        (a.minPoint.y <= b.maxPoint.y && a.maxPoint.y >= b.minPoint.y) &&
        (a.minPoint.z <= b.maxPoint.z && a.maxPoint.z >= b.minPoint.z);
}

class CollisionManager
{
public:
    CollisionManager() = default;
    ~CollisionManager() = default;

    // OVERLOAD 1: Untuk SceneGameBreaker (Tidak butuh Boss)
    void Initialize(Player* p, Stage* s, EnemyManager* em, ItemManager* im);

    void Update(float elapsedTime);
    void SetOnCheckpointReachCallback(std::function<void(DirectX::XMFLOAT3)> callback) { m_onCheckpointReachCallback = callback; }
    void SetOnLevelCompleteCallback(std::function<void()> callback) { m_onLevelCompleteCallback = callback; }
    void SetOnPlayerDeathCallback(std::function<void()> callback) { m_onPlayerDeathCallback = callback; }
    void SetOnPlayerHitCallback(std::function<void()> callback) { m_onPlayerHitCallback = callback; }
    void SetOnEnableLineReachCallback(std::function<void(int)> callback) { m_onEnableLineReachCallback = callback; }
    [[nodiscard]] float GetEnemyPushRadius(const Enemy* enemy) const;
    [[nodiscard]] Enemy* GetTargetInSlashCone(const DirectX::XMFLOAT3& playerPos, const DirectX::XMFLOAT3& aimDir, float reach, float minDotProduct) const;
    bool GetParryableProjectile(const DirectX::XMFLOAT3& playerPos, float threshold, class Bullet** outBullet, Enemy** outNearestEnemy);
    void SetNavi(NaviAlly* navi) { m_navi = navi; }
    NaviAlly* GetNavi() const { return m_navi; }
    void SetBoss(Boss* Boss) { m_Boss = Boss; }
    Boss* GetBoss() const { return m_Boss; }

private:
    void CheckPlayerVsCheckpointLines();
    void CheckPlayerVsEnemies();
    void CheckPlayerVsItems();
    void CheckPlayerProjectilesVsEnemies(float elapsedTime);
    void CheckPlayerProjectilesVsNavi(float elapsedTime);
    void CheckPlayerVsTriggerLines();
    void CheckPlayerVsVoidLines();
    bool CheckSphereCollision(const DirectX::XMFLOAT3& posA, const DirectX::XMFLOAT3& posB, float threshold);
    void CheckEnemyProjectilesFull(float elapsedTime);
    void CheckNaviProjectilesVsEnemies(float elapsedTime);
    void CheckNaviAllyProjectilesVsPlayer(float elapsedTime);
    void CheckBossProjectilesVsPlayer(float elapsedTime);
    void CheckBossProjectilesVsBoss(float elapsedTime); // Fungsi pantulan

    Player* m_player = nullptr;
    Stage* m_stage = nullptr;

    EnemyManager* m_enemyManager = nullptr;
    ItemManager* m_itemManager = nullptr;
    NaviAlly* m_navi = nullptr;
    Boss* m_Boss = nullptr;

    std::function<void(DirectX::XMFLOAT3)> m_onCheckpointReachCallback;
    std::function<void(int)> m_onEnableLineReachCallback = nullptr;
    std::function<void()> m_onLevelCompleteCallback = nullptr;
    std::function<void()> m_onPlayerDeathCallback;
    std::function<void()> m_onPlayerHitCallback = nullptr;
};