#pragma once

#include <algorithm>
#include <cmath>
#include <DirectXMath.h>
#include <functional>
#include <vector>
#include "System/AudioManager.h"
#include "System/Collision.h"
#include "System/Input.h"
#include "EffectManager.h"
#include <CameraController.h>
#include "Enemy.h"
#include "Player.h"
#include "EnemyManager.h"
#include "ItemManager.h"
#include "StateMachine.h"
#include "PlayerStates.h"
#include "NaviAlly.h"

class ItemManager;
class NaviAlly;

// Axis-Aligned Bounding Box for rapid broad-phase rejection
struct AABB
{
    DirectX::XMFLOAT3 minPoint{};
    DirectX::XMFLOAT3 maxPoint{};
};

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

    void Initialize(Player* p, EnemyManager* em, ItemManager* im);

    void Update(float elapsedTime);
    void SetOnPlayerDeathCallback(std::function<void()> callback) { m_onPlayerDeathCallback = callback; }

    [[nodiscard]] float GetEnemyPushRadius(const Enemy* enemy) const;
    [[nodiscard]] Enemy* GetTargetInSlashCone(const DirectX::XMFLOAT3& playerPos, const DirectX::XMFLOAT3& aimDir, float reach, float minDotProduct) const;

    void SetNavi(NaviAlly* navi) { m_navi = navi; }
    NaviAlly* GetNavi() const { return m_navi; }

private:
    void CheckPlayerVsEnemies();
    void CheckPlayerVsItems();
    void CheckPlayerProjectilesVsEnemies(float elapsedTime);
    void CheckPlayerProjectilesVsNavi(float elapsedTime);
    bool CheckSphereCollision(const DirectX::XMFLOAT3& posA, const DirectX::XMFLOAT3& posB, float threshold);
    void CheckEnemyProjectilesFull(float elapsedTime);
    void CheckNaviProjectilesVsEnemies(float elapsedTime);
    void CheckNaviAllyProjectilesVsPlayer(float elapsedTime);

    Player* m_player = nullptr;
    EnemyManager* m_enemyManager = nullptr;
    ItemManager* m_itemManager = nullptr;
    NaviAlly* m_navi = nullptr;

    std::function<void()> m_onPlayerDeathCallback;
};