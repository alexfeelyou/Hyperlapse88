#include "CollisionManager.h"

using namespace DirectX;

static XMVECTOR TransformToEnemyLocal(const XMFLOAT3& worldPos, const Enemy* enemy)
{
    XMVECTOR vWorldPos = XMLoadFloat3(&worldPos);
    XMVECTOR vEnemyPos = XMLoadFloat3(&enemy->GetPosition());
    XMVECTOR vRelative = XMVectorSubtract(vWorldPos, vEnemyPos);
    XMFLOAT3 rot = enemy->GetRotation();
    XMMATRIX matRot = XMMatrixRotationRollPitchYaw(rot.x, rot.y, rot.z);
    XMMATRIX matInvRot = XMMatrixTranspose(matRot);
    return XMVector3TransformNormal(vRelative, matInvRot);
}

static float DistancePointToLineSegment2D(const DirectX::XMFLOAT3& A, const DirectX::XMFLOAT3& B, const DirectX::XMFLOAT3& P)
{
    float lineX = B.x - A.x;
    float lineZ = B.z - A.z;
    float px = P.x - A.x;
    float pz = P.z - A.z;

    float lineLengthSq = (lineX * lineX) + (lineZ * lineZ);

    // If the bullet didn't move this frame, just do a normal sphere check
    if (lineLengthSq == 0.0f) return std::sqrt((px * px) + (pz * pz));

    // Dot product to project the target's position onto the bullet's path line
    float t = ((px * lineX) + (pz * lineZ)) / lineLengthSq;

    // Clamp 't' between 0.0f and 1.0f 
    t = (std::max)(0.0f, (std::min)(1.0f, t));

    // Find the closest point on the line
    float closestX = A.x + t * lineX;
    float closestZ = A.z + t * lineZ;

    float dx = P.x - closestX;
    float dz = P.z - closestZ;

    return std::sqrt((dx * dx) + (dz * dz));
}

[[nodiscard]] inline AABB CreateSweptAABB(const DirectX::XMFLOAT3& startPos,
    const DirectX::XMFLOAT3& endPos,
    const float radius) noexcept
{
    return AABB{
        { (std::min)(startPos.x, endPos.x) - radius,
          (std::min)(startPos.y, endPos.y) - radius,
          (std::min)(startPos.z, endPos.z) - radius },

        { (std::max)(startPos.x, endPos.x) + radius,
          (std::max)(startPos.y, endPos.y) + radius,
          (std::max)(startPos.z, endPos.z) + radius }
    };
}

void CollisionManager::Initialize(Player* p, EnemyManager* em, ItemManager* im)
{
    m_player = p;
    m_enemyManager = em;
    m_itemManager = im;
}

void CollisionManager::Update(float elapsedTime)
{
    CheckEnemyProjectilesFull(elapsedTime);
    CheckPlayerProjectilesVsEnemies(elapsedTime);
    CheckPlayerProjectilesVsNavi(elapsedTime);
    CheckNaviAllyProjectilesVsPlayer(elapsedTime);
    CheckPlayerVsEnemies();

    if (m_itemManager)
    {
        CheckPlayerVsItems();
    }
    
    CheckNaviProjectilesVsEnemies(elapsedTime);
}

void CollisionManager::CheckEnemyProjectilesFull(float elapsedTime)
{
    if (!m_enemyManager) return;

    const bool isPlayerActive = m_player && (!m_player->GetOwnerNode() || m_player->GetOwnerNode()->IsActive());

    for (auto& enemy : m_enemyManager->GetEnemies())
    {
        if (!enemy || !enemy->IsActive()) continue;
        if (enemy->GetOwnerNode() && !enemy->GetOwnerNode()->IsActive()) continue;

        auto& projectiles = enemy->GetProjectiles();

        for (auto it = projectiles.begin(); it != projectiles.end(); )
        {
            auto& bullet = *it;
            if (!bullet) { ++it; continue; }

            XMFLOAT3 currentPos = bullet->GetMovement()->GetPosition();
            XMFLOAT3 currentVel = bullet->GetVelocity();
            XMVECTOR vPos = XMLoadFloat3(&currentPos);
            XMVECTOR vVel = XMLoadFloat3(&currentVel);
            XMVECTOR vNextPos = vPos + (vVel * elapsedTime);

            float bulletRadius = bullet->GetRadius();

            if (bullet->GetHomingTarget() != nullptr)
            {
                Enemy* targetEnemy = static_cast<Enemy*>(bullet->GetHomingTarget());

                bool isTargetAlive = false;
                for (auto& activeEnemy : m_enemyManager->GetEnemies()) {
                    if (activeEnemy.get() == targetEnemy && activeEnemy->IsActive()) {
                        isTargetAlive = true;
                        break;
                    }
                }

                if (!isTargetAlive) {
                    it = projectiles.erase(it);
                    continue;
                }

                DirectX::XMFLOAT3 bPos = bullet->GetMovement()->GetPosition();
                DirectX::XMFLOAT3 targetPos = targetEnemy->GetPosition();

                float dx = bPos.x - targetPos.x;
                float dz = bPos.z - targetPos.z;
                float distSq = dx * dx + dz * dz;

                float enemyScale = targetEnemy->GetScale().x;
                float hitRadius = 1.0f * enemyScale;

                if (targetEnemy->GetType() == EnemyType::Paddle) hitRadius = 0.8f * enemyScale;

                float combinedHitRadius = hitRadius + bullet->GetRadius();

                if (distSq < (combinedHitRadius * combinedHitRadius))
                {
                    constexpr int PARRY_DAMAGE = 30;
                    targetEnemy->TakeDamage(PARRY_DAMAGE);
                    it = projectiles.erase(it);
                    continue;
                }
            }

            XMFLOAT3 nextPosFloat;
            XMStoreFloat3(&nextPosFloat, vNextPos);

            if (isPlayerActive && bullet->GetHomingTarget() == nullptr && m_player->GetHP() > 0 && !m_player->IsInvincible())
            {
                DirectX::XMFLOAT3 playerPos = m_player->GetMovement()->GetPosition();

                constexpr int ENEMY_BULLET_DAMAGE = 10;
                constexpr float PLAYER_HITBOX_RADIUS = 0.3f;

                float combinedRadius = PLAYER_HITBOX_RADIUS + bulletRadius;
                float distToPath = DistancePointToLineSegment2D(currentPos, nextPosFloat, playerPos);

                if (distToPath <= combinedRadius)
                {
                    m_player->TakeDamage(ENEMY_BULLET_DAMAGE);

                    if (m_player->GetHP() <= 0)
                    {
                        m_player->scale = { 0.0f, 0.0f, 0.0f };
                        m_player->SetInputEnabled(false);
                        m_player->GetMovement()->SetVelocity({ 0,0,0 });
                        m_player->GetStateMachine()->ChangeState(m_player, std::make_unique<PlayerDead>());
                    }

                    it = projectiles.erase(it);
                    continue;
                }
            }

            bullet->ApplyMovement(nextPosFloat, currentVel);
            ++it;
        }
    }
}

float CollisionManager::GetEnemyPushRadius(const Enemy* enemy) const
{
    float scale = enemy->GetScale().x;

    switch (enemy->GetType())
    {
    case EnemyType::Paddle:     return 0.8f * scale;
    case EnemyType::FakeBoss:   return 1.9f * scale;
    default:                    return 1.2f * scale; 
    }
}

void CollisionManager::CheckPlayerVsEnemies()
{
    if (!m_player || !m_enemyManager) return;

    // If player is already dead, skip all enemy physics
    if (m_player->GetHP() <= 0) return;

    // Fast-fail if Player is deactivated in the Editor
    if (m_player->GetOwnerNode() && !m_player->GetOwnerNode()->IsActive()) return;

    auto& enemies{ m_enemyManager->GetEnemies() };
    DirectX::XMFLOAT3 playerPos{ m_player->GetMovement()->GetPosition() };
    DirectX::XMFLOAT3 playerVel{ m_player->GetMovement()->GetVelocity() };

    constexpr float PLAYER_RADIUS{ 0.25f };
    bool collidedAny{ false };

    for (const auto& enemy : enemies)
    {
        if (!enemy || !enemy->IsActive()) continue;

        // Skip deactivated enemies
        if (enemy->GetOwnerNode() && !enemy->GetOwnerNode()->IsActive()) continue;

        const DirectX::XMFLOAT3 ePos{ enemy->GetPosition() };

        const float enemyRadius{ GetEnemyPushRadius(enemy.get()) };
        const float combinedRadius{ PLAYER_RADIUS + enemyRadius };

        float dx{ playerPos.x - ePos.x };
        float dz{ playerPos.z - ePos.z };
        const float distSq{ (dx * dx) + (dz * dz) };

        if (distSq < (combinedRadius * combinedRadius))
        {
            // Normal Enemy Collision Logic: Push the player away from the enemy to prevent overlap
            float dist{ std::sqrt(distSq) };

            if (dist < 0.0001f)
            {
                dx = 1.0f; dz = 0.0f; dist = 1.0f;
            }

            const float overlap{ combinedRadius - dist };
            playerPos.x += (dx / dist) * overlap;
            playerPos.z += (dz / dist) * overlap;

            collidedAny = true;

            DirectX::XMVECTOR vVel{ DirectX::XMLoadFloat3(&playerVel) };
            const DirectX::XMVECTOR vNormal{ DirectX::XMVectorSet(dx / dist, 0.0f, dz / dist, 0.0f) };

            const float dot{ DirectX::XMVectorGetX(DirectX::XMVector3Dot(vVel, vNormal)) };
            if (dot < 0.0f)
            {
                vVel = DirectX::XMVectorSubtract(vVel, DirectX::XMVectorScale(vNormal, dot));
                DirectX::XMStoreFloat3(&playerVel, vVel);
            }
        }
    }

    // Only update memory if a collision actually happened
    if (collidedAny)
    {
        m_player->SetPosition(playerPos);
        m_player->GetMovement()->SetVelocity(playerVel);
    }
}

void CollisionManager::CheckPlayerVsItems()
{
    if (!m_player || !m_itemManager) return;

    // Ignore if player is disabled
    if (m_player->GetOwnerNode() && !m_player->GetOwnerNode()->IsActive()) return;

    XMFLOAT3 pPos = m_player->GetMovement()->GetPosition();
    float pRadius = 0.5f;

    for (auto& item : m_itemManager->GetItems())
    {
        if (!item->IsActive()) continue;

        // Ignore disabled items
        if (item->GetOwnerNode() && !item->GetOwnerNode()->IsActive()) continue;

        XMFLOAT3 iPos = item->GetPosition();
        float iRadius = item->scale.x * 0.5f;

        float distSq = (pPos.x - iPos.x) * (pPos.x - iPos.x) +
            (pPos.z - iPos.z) * (pPos.z - iPos.z);

        float combinedRadius = pRadius + iRadius;

        if (distSq < combinedRadius * combinedRadius)
        {
            //AudioManager::Instance().PlaySFX("Data/Sound/SE_Pop.wav", 0.4f);

            if (item->GetType() == ItemType::Invincible)
            {
            }

            item->SetActive(false);
        }
    }
}

void CollisionManager::CheckPlayerProjectilesVsEnemies(const float elapsedTime)
{
    if (!m_player) return;

    auto& projectiles{ m_player->GetProjectiles() };

    constexpr float BULLET_HITBOX_RADIUS = 1.0f;

    for (auto& bullet : projectiles)
    {
        if (!bullet || !bullet->IsActive()) continue;

        const DirectX::XMFLOAT3 currentPos{ bullet->GetMovement()->GetPosition() };

        if (!m_enemyManager) continue; // Cek musuh di sini agar aman
        const auto& enemies{ m_enemyManager->GetEnemies() };

        const DirectX::XMFLOAT3 velocity{ bullet->GetVelocity() };

        // Calculate where the bullet was last frame
        const DirectX::XMFLOAT3 prevPos{
            currentPos.x - (velocity.x * elapsedTime),
            currentPos.y - (velocity.y * elapsedTime),
            currentPos.z - (velocity.z * elapsedTime)
        };

        // Generate the Swept AABB for the bullet
        const AABB bulletAABB{ CreateSweptAABB(prevPos, currentPos, BULLET_HITBOX_RADIUS) };

        for (const auto& enemy : enemies)
        {
            if (!enemy || !enemy->IsActive()) continue;

            // Prevent player bullets from hitting deactivated enemies
            if (enemy->GetOwnerNode() && !enemy->GetOwnerNode()->IsActive()) continue;

            const DirectX::XMFLOAT3 ePos{ enemy->GetPosition() };
            const float enemyRadius{ GetEnemyPushRadius(enemy.get()) };

            // Generate the static AABB for the enemy
            const AABB enemyAABB{
                { ePos.x - enemyRadius, ePos.y - enemyRadius, ePos.z - enemyRadius },
                { ePos.x + enemyRadius, ePos.y + enemyRadius, ePos.z + enemyRadius }
            };

            // Are they even close? 
            if (!CheckAABBIntersection(bulletAABB, enemyAABB))
            {
                continue; 
            }

            if (CheckSphereCollision(currentPos, ePos, BULLET_HITBOX_RADIUS + enemyRadius))
            {
                enemy->TakeDamage(bullet->GetDamage());
                bullet->SetActive(false);
                break; // Stop checking this bullet against other enemies
            }
        }
    }
}

void CollisionManager::CheckNaviProjectilesVsEnemies(float elapsedTime)
{
    if (!m_navi || !m_enemyManager) return;

    auto& projectiles = m_navi->GetProjectiles();
    auto& enemies = m_enemyManager->GetEnemies();

    constexpr int NAVI_BULLET_DAMAGE = 1;
    constexpr float BULLET_HITBOX_RADIUS = 0.1f;

    for (auto& bullet : projectiles)
    {
        if (!bullet || !bullet->IsActive()) continue;

        DirectX::XMFLOAT3 currentPos = bullet->GetMovement()->GetPosition();
        DirectX::XMFLOAT3 vel = bullet->GetVelocity();

        // Calculate exactly where the bullet was last frame
        DirectX::XMFLOAT3 prevPos = {
            currentPos.x - (vel.x * elapsedTime),
            currentPos.y - (vel.y * elapsedTime), 
            currentPos.z - (vel.z * elapsedTime)
        };

        for (auto& enemy : enemies)
        {
            if (!enemy || !enemy->IsActive()) continue;

            DirectX::XMFLOAT3 ePos = enemy->GetPosition();

			// Dynamic Hitbox Calculation: The Navi's bullets are small, so we need to adjust the hitbox based on the enemy's size
            float enemyScale = enemy->GetScale().x;
            float enemyRadius = 1.0f * enemyScale; // Default Ball

            if (enemy->GetType() == EnemyType::Paddle) enemyRadius = 0.6f * enemyScale;

            // Combine the enemy's size with the bullet's size
            float exactHitDistance = enemyRadius + BULLET_HITBOX_RADIUS;

			// Anti-Tunneling CCD (Continuous Collision Detection) Logic:
            // Draws an invisible math line from prevPos to currentPos.
            // If the enemy touches any part of that line, it's a guaranteed hit
            float distToPath = DistancePointToLineSegment2D(prevPos, currentPos, ePos);

            if (distToPath <= exactHitDistance)
            {
                enemy->TakeDamage(NAVI_BULLET_DAMAGE);
                bullet->SetActive(false); // Send back to Object Pool instantly
                break; // Stop checking this bullet against other enemies
            }
        }
    }
}

void CollisionManager::CheckPlayerProjectilesVsNavi(const float elapsedTime)
{
    if (!m_player || !m_navi || !m_navi->IsAlive() || !m_navi->IsPotioned()) return;

    // Reference binding to avoid copying the container
    const auto& projectiles{ m_player->GetProjectiles() };
    const DirectX::XMFLOAT3 naviPos{ m_navi->GetMovement()->GetPosition() };

    constexpr float NAVI_HITBOX_RADIUS_XZ{ 0.8f };
    constexpr int PLAYER_BULLET_DAMAGE{ 10 };

    for (const auto& bullet : projectiles)
    {
        // Null and active state guard
        if (!bullet || !bullet->IsActive()) continue;

        const DirectX::XMFLOAT3 currentPos{ bullet->GetMovement()->GetPosition() };
        const DirectX::XMFLOAT3 vel{ bullet->GetVelocity() };

        const DirectX::XMFLOAT3 prevPos{
            currentPos.x - (vel.x * elapsedTime),
            currentPos.y - (vel.y * elapsedTime),
            currentPos.z - (vel.z * elapsedTime)
        };

        // Anti-tunneling CCD (Continuous Collision Detection) 
        const float distToPath{ DistancePointToLineSegment2D(prevPos, currentPos, naviPos) };

        // 2D Cylinder Collision completely ignore the Y-axis vertical distance
        if (distToPath <= NAVI_HITBOX_RADIUS_XZ)
        {
            m_navi->TakeDamage(PLAYER_BULLET_DAMAGE);
            EffectManager::Instance().Play("Data/Effect/Hit.efk", naviPos, 1.0f);
            bullet->SetActive(false); // Instantly recycle the bullet into the object pool
        }
    }
}

void CollisionManager::CheckNaviAllyProjectilesVsPlayer(const float elapsedTime)
{
    // If the player is dead, Navi isn't potioned, or the player is currently Dashing (Invincible)
    if (!m_player || !m_navi || m_player->GetHP() <= 0 || !m_navi->IsPotioned() || m_player->IsInvincible()) return;

    const auto& projectiles{ m_navi->GetProjectiles() };
    const DirectX::XMFLOAT3 playerPos{ m_player->GetMovement()->GetPosition() };

    constexpr float PLAYER_HURTBOX_RADIUS{ 0.3f };
    constexpr int NAVI_BULLET_DAMAGE{ 10 };

    for (const auto& bullet : projectiles)
    {
        if (!bullet || !bullet->IsActive()) continue;

        const DirectX::XMFLOAT3 currentPos{ bullet->GetMovement()->GetPosition() };
        const DirectX::XMFLOAT3 vel{ bullet->GetVelocity() };

        const DirectX::XMFLOAT3 prevPos{
            currentPos.x - (vel.x * elapsedTime),
            currentPos.y - (vel.y * elapsedTime),
            currentPos.z - (vel.z * elapsedTime)
        };

        const float distToPath{ DistancePointToLineSegment2D(prevPos, currentPos, playerPos) };
        const float combinedRadius{ PLAYER_HURTBOX_RADIUS + bullet->GetRadius() };

        if (distToPath <= combinedRadius)
        {
            bullet->SetActive(false); // Destroy the bullet

            // Apply Damage 
            m_player->TakeDamage(NAVI_BULLET_DAMAGE);

            // Death Sequence Logic 
            if (m_player->GetHP() <= 0)
            {
                // Visual & State Reset
                m_player->scale = { 0.0f, 0.0f, 0.0f };
                m_player->SetInputEnabled(false);
                m_player->GetMovement()->SetVelocity({ 0.0f, 0.0f, 0.0f });
                m_player->GetStateMachine()->ChangeState(m_player, std::make_unique<PlayerDead>());

                // Trigger Fade via Callback
                if (m_onPlayerDeathCallback)
                {
                    m_onPlayerDeathCallback();
                }
            }
        }
    }
}

bool CollisionManager::CheckSphereCollision(const DirectX::XMFLOAT3& posA, const DirectX::XMFLOAT3& posB, float threshold)
{
    float dx = posA.x - posB.x;
    float dz = posA.z - posB.z;
    float distSq = (dx * dx) + (dz * dz);
    float thresholdSq = threshold * threshold;

    return distSq < thresholdSq;
}

Enemy* CollisionManager::GetTargetInSlashCone(const DirectX::XMFLOAT3& playerPos, const DirectX::XMFLOAT3& aimDir, float baseReach, float minDotProduct) const
{
    if (!m_enemyManager) return nullptr;

    Enemy* bestTarget{ nullptr };
    float closestDistSq{ (std::numeric_limits<float>::max)() };

    for (const auto& enemy : m_enemyManager->GetEnemies())
    {
        if (!enemy || !enemy->IsActive()) continue;

        // Ignore deactivated enemies in the hierarchy
        if (enemy->GetOwnerNode() && !enemy->GetOwnerNode()->IsActive()) continue;

        const DirectX::XMFLOAT3 ePos{ enemy->GetPosition() };
        const float enemyScale{ enemy->GetScale().x };
        float enemyRadius{ 1.0f * enemyScale };

        if (enemy->GetType() == EnemyType::Paddle) enemyRadius = 1.2f * enemyScale;

        const float exactSlashDistance{ 0.5f + enemyRadius + baseReach };
        const float exactSlashDistSq{ exactSlashDistance * exactSlashDistance };

        const float dx{ ePos.x - playerPos.x };
        const float dz{ ePos.z - playerPos.z };
        const float distSq{ (dx * dx) + (dz * dz) };

        // Fast fail distance check before expensive sqrt
        if (distSq < exactSlashDistSq && distSq > 0.0001f)
        {
            const float dist{ std::sqrt(distSq) };
            const float dirX{ dx / dist };
            const float dirZ{ dz / dist };

            const float dot{ (dirX * aimDir.x) + (dirZ * aimDir.z) };

            if (dot >= minDotProduct)
            {
                if (distSq < closestDistSq) // Prioritization logic removed
                {
                    closestDistSq = distSq;
                    bestTarget = enemy.get();
                }
            }
        }
    }

    return bestTarget;
}
