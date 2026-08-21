#include "CollisionManager.h"

using namespace DirectX;

// Helper function to transform a world position into the local space of a wall
static XMVECTOR TransformToLocal(const XMFLOAT3& worldPos, const DebugWallData& wall)
{
    XMVECTOR vWorldPos = XMLoadFloat3(&worldPos);
    XMVECTOR vWallPos = XMLoadFloat3(&wall.Position);
    XMVECTOR vRelative = XMVectorSubtract(vWorldPos, vWallPos);
    XMMATRIX matRot = XMMatrixRotationRollPitchYaw(
        XMConvertToRadians(wall.Rotation.x),
        XMConvertToRadians(wall.Rotation.y),
        XMConvertToRadians(wall.Rotation.z)
    );
    XMMATRIX matInvRot = XMMatrixTranspose(matRot);
    return XMVector3TransformNormal(vRelative, matInvRot);
}

static XMVECTOR TransformNormalToWorld(const XMVECTOR& localNorm, const DebugWallData& wall)
{
    XMMATRIX matRot = XMMatrixRotationRollPitchYaw(
        XMConvertToRadians(wall.Rotation.x),
        XMConvertToRadians(wall.Rotation.y),
        XMConvertToRadians(wall.Rotation.z)
    );
    return XMVector3TransformNormal(localNorm, matRot);
}

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

static XMVECTOR TransformToLocalLine(const XMFLOAT3& worldPos, const DebugLineData& line)
{
    XMVECTOR vWorldPos = XMLoadFloat3(&worldPos);
    XMVECTOR vLinePos = XMLoadFloat3(&line.Position);
    XMVECTOR vRelPos = XMVectorSubtract(vWorldPos, vLinePos);
    XMMATRIX matRot = XMMatrixRotationRollPitchYaw(
        XMConvertToRadians(line.Rotation.x),
        XMConvertToRadians(line.Rotation.y),
        XMConvertToRadians(line.Rotation.z)
    );
    XMMATRIX matInvRot = XMMatrixTranspose(matRot);
    return XMVector3TransformNormal(vRelPos, matInvRot);
}

static float RayCastOBB(XMVECTOR rayOrigin, XMVECTOR rayDir, float rayLength, float radius, const DebugWallData& wall, XMVECTOR& outNormal)
{
    XMVECTOR vWallPos = XMLoadFloat3(&wall.Position);
    XMMATRIX matRot = XMMatrixRotationRollPitchYaw(
        XMConvertToRadians(wall.Rotation.x),
        XMConvertToRadians(wall.Rotation.y),
        XMConvertToRadians(wall.Rotation.z)
    );
    XMMATRIX matInvRot = XMMatrixTranspose(matRot);

    XMVECTOR vRelOrigin = XMVectorSubtract(rayOrigin, vWallPos);
    XMVECTOR vLocalOrigin = XMVector3TransformNormal(vRelOrigin, matInvRot);
    XMVECTOR vLocalDir = XMVector3TransformNormal(rayDir, matInvRot);

    float r = radius;
    float minX = -wall.Scale.x - r; float maxX = wall.Scale.x + r;
    float minZ = -wall.Scale.z - r; float maxZ = wall.Scale.z + r;

    float tMin = 0.0f;
    float tMax = rayLength;

    XMFLOAT3 start, dir;
    XMStoreFloat3(&start, vLocalOrigin);
    XMStoreFloat3(&dir, vLocalDir);

    if (abs(dir.x) < 1e-6f) {
        if (start.x < minX || start.x > maxX) return -1.0f;
    }
    else {
        float invD = 1.0f / dir.x;
        float t1 = (minX - start.x) * invD;
        float t2 = (maxX - start.x) * invD;
        if (t1 > t2) std::swap(t1, t2);
        tMin = (std::max)(tMin, t1);
        tMax = (std::min)(tMax, t2);
        if (tMin > tMax) return -1.0f;
    }

    if (abs(dir.z) < 1e-6f) {
        if (start.z < minZ || start.z > maxZ) return -1.0f;
    }
    else {
        float invD = 1.0f / dir.z;
        float t1 = (minZ - start.z) * invD;
        float t2 = (maxZ - start.z) * invD;
        if (t1 > t2) std::swap(t1, t2);
        tMin = (std::max)(tMin, t1);
        tMax = (std::min)(tMax, t2);
        if (tMin > tMax) return -1.0f;
    }

    XMFLOAT3 hitPoint;
    XMStoreFloat3(&hitPoint, vLocalOrigin + vLocalDir * tMin);

    float distMinX = abs(hitPoint.x - minX);
    float distMaxX = abs(hitPoint.x - maxX);
    float distMinZ = abs(hitPoint.z - minZ);
    float distMaxZ = abs(hitPoint.z - maxZ);

    float bestDist = distMinX;
    XMVECTOR localNormal = XMVectorSet(-1, 0, 0, 0);

    if (distMaxX < bestDist) { bestDist = distMaxX; localNormal = XMVectorSet(1, 0, 0, 0); }
    if (distMinZ < bestDist) { bestDist = distMinZ; localNormal = XMVectorSet(0, 0, -1, 0); }
    if (distMaxZ < bestDist) { bestDist = distMaxZ; localNormal = XMVectorSet(0, 0, 1, 0); }

    outNormal = XMVector3TransformNormal(localNormal, matRot);

    return tMin;
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

void CollisionManager::Initialize(Player* p, Stage* s, EnemyManager* em, ItemManager* im)
{
    m_player = p;
    m_stage = s;
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
    CheckPlayerVsCheckpointLines();
    CheckPlayerVsTriggerLines();
    CheckPlayerVsVoidLines();

    if (m_itemManager)
    {
        CheckPlayerVsItems();
    }
    
    CheckNaviProjectilesVsEnemies(elapsedTime);
}

void CollisionManager::CheckEnemyProjectilesFull(float elapsedTime)
{
    if (!m_enemyManager) return;

    for (auto& enemy : m_enemyManager->GetEnemies())
    {
        auto& projectiles = enemy->GetProjectiles();
        AttackType type = enemy->GetAttackType();

        for (auto it = projectiles.begin(); it != projectiles.end(); )
        {
            auto& bullet = *it;
            if (!bullet) { ++it; continue; }

            XMFLOAT3 currentPos = bullet->GetMovement()->GetPosition();
            XMFLOAT3 currentVel = bullet->GetVelocity();
            XMVECTOR vPos = XMLoadFloat3(&currentPos);
            XMVECTOR vVel = XMLoadFloat3(&currentVel);
            XMVECTOR vNextPos = vPos + (vVel * elapsedTime);
            XMVECTOR vDir = XMVector3Normalize(vVel);

            float speed = XMVectorGetX(XMVector3Length(vVel));
            float frameDist = speed * elapsedTime;
            float bulletRadius = bullet->GetRadius();

            bool hitWall = false;
            float closestT = frameDist;
            XMVECTOR hitNormal = XMVectorZero();

            if (m_stage)
            {
                for (const auto& wall : m_stage->m_debugWalls)
                {
                    float dx = currentPos.x - wall.Position.x;
                    float dz = currentPos.z - wall.Position.z;
                    float wallMax = (std::max)(wall.Scale.x, wall.Scale.z);
                    if ((dx * dx + dz * dz) > pow(wallMax + frameDist + 10.0f, 2)) continue;

                    XMVECTOR tempNormal;
                    float t = RayCastOBB(vPos, vDir, frameDist, bulletRadius, wall, tempNormal);

                    if (t >= 0.0f && t < closestT)
                    {
                        closestT = t;
                        hitNormal = tempNormal;
                        hitWall = true;
                    }
                }
            }

            if (hitWall)
            {
                if (type == AttackType::Static) {
                    it = projectiles.erase(it);
                    continue;
                }

                float safeDist = (std::max)(0.0f, closestT - 0.01f);
                XMVECTOR vSafePos = vPos + (vDir * safeDist);
                XMVECTOR vReflectedVel = XMVector3Reflect(vVel, hitNormal);
                float remainingDist = frameDist - closestT;
                vSafePos += (XMVector3Normalize(vReflectedVel) * remainingDist);
                vSafePos += hitNormal * 0.05f;

                XMFLOAT3 finalPos, finalVel;
                XMStoreFloat3(&finalPos, vSafePos);
                XMStoreFloat3(&finalVel, vReflectedVel);
                finalPos.y = 0.0f; finalVel.y = 0.0f;

                bullet->ApplyMovement(finalPos, finalVel);
                ++it;
                continue;
            }

            if (bullet->GetHomingTarget() != nullptr)
            {
                Enemy* targetEnemy = static_cast<Enemy*>(bullet->GetHomingTarget());

                // Verify the target wasn't deleted from memory by a different attack (like a Slash)
                bool isTargetAlive = false;
                for (auto& activeEnemy : m_enemyManager->GetEnemies()) {
                    if (activeEnemy.get() == targetEnemy && activeEnemy->IsActive()) {
                        isTargetAlive = true;
                        break;
                    }
                }

                // If the target died while the bullet was mid-air, destroy the bullet to prevent a crash
                if (!isTargetAlive) {
                    it = projectiles.erase(it);
                    continue;
                }

                DirectX::XMFLOAT3 bPos = bullet->GetMovement()->GetPosition();
                DirectX::XMFLOAT3 targetPos = targetEnemy->GetPosition();

                float dx = bPos.x - targetPos.x;
                float dz = bPos.z - targetPos.z;
                float distSq = dx * dx + dz * dz;

				// Party Damage Logic: If the bullet is close enough to the target, apply damage and destroy the bullet
                float enemyScale = targetEnemy->GetScale().x;
                float hitRadius = 1.0f * enemyScale;

                if (targetEnemy->GetType() == EnemyType::Pentagon) hitRadius = 4.0f * enemyScale;
                else if (targetEnemy->GetType() == EnemyType::Paddle) hitRadius = 0.8f * enemyScale;

                float combinedHitRadius = hitRadius + bullet->GetRadius();

                if (distSq < (combinedHitRadius * combinedHitRadius))
                {
					// Apply damage to the enemy and destroy the bullet
                    constexpr int PARRY_DAMAGE = 30;
                    targetEnemy->TakeDamage(PARRY_DAMAGE);

                    // Destroy the bullet
                    it = projectiles.erase(it);
                    continue;
                }
            }

            bool hitPlayer = false;

			// Check for collision with the player using Continuous Collision Detection (CCD)
            XMFLOAT3 nextPosFloat;
            XMStoreFloat3(&nextPosFloat, vNextPos);

            if (m_player && bullet->GetHomingTarget() == nullptr && m_player->GetHP() > 0 && !m_player->IsInvincible())
            {
                DirectX::XMFLOAT3 playerPos = m_player->GetMovement()->GetPosition();

                constexpr int ENEMY_BULLET_DAMAGE = 10;
                constexpr float PLAYER_HITBOX_RADIUS = 0.3f; 

                float combinedRadius = PLAYER_HITBOX_RADIUS + bulletRadius;

                // Mathematical CCD (Prevents Tunneling)
                float distToPath = DistancePointToLineSegment2D(currentPos, nextPosFloat, playerPos);

                if (distToPath <= combinedRadius)
                {
                    bool wasAlive = (m_player->GetHP() > 0);
                    m_player->TakeDamage(ENEMY_BULLET_DAMAGE);

                    if (m_player->GetHP() <= 0)
                    {
                        m_player->scale = { 0.0f, 0.0f, 0.0f }; // Make the 3D model vanish
                        m_player->SetInputEnabled(false);       // Stop WASD and Spacebar input
                        m_player->GetMovement()->SetVelocity({ 0,0,0 }); // Stop sliding
                        m_player->GetStateMachine()->ChangeState(m_player, std::make_unique<PlayerDead>());
                    }

                    // Destroy the bullet and prevent crashes
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
    case EnemyType::Pentagon:   return 4.0f * scale;
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

    auto& enemies{ m_enemyManager->GetEnemies() };
    DirectX::XMFLOAT3 playerPos{ m_player->GetMovement()->GetPosition() };
    DirectX::XMFLOAT3 playerVel{ m_player->GetMovement()->GetVelocity() };

    constexpr float PLAYER_RADIUS{ 0.25f };
    bool collidedAny{ false };

    for (const auto& enemy : enemies)
    {
        if (!enemy || !enemy->IsActive()) continue;

        const DirectX::XMFLOAT3 ePos{ enemy->GetPosition() };

        const float enemyRadius{ GetEnemyPushRadius(enemy.get()) };
        const float combinedRadius{ PLAYER_RADIUS + enemyRadius };

        float dx{ playerPos.x - ePos.x };
        float dz{ playerPos.z - ePos.z };
        const float distSq{ (dx * dx) + (dz * dz) };

        if (distSq < (combinedRadius * combinedRadius))
        {
			// Kamikaze Enemy Logic: If the enemy is a tracking type and the player is not invincible, instantly kill the player
            if (enemy->GetAttackType() == AttackType::Tracking && !m_player->IsInvincible())
            {
                // Instantly nuke player HP
                m_player->TakeDamage(9999);

                // Trigger standard death sequence
                m_player->scale = { 0.0f, 0.0f, 0.0f }; // Hide 3D model
                m_player->SetInputEnabled(false);       // Lock controls
                m_player->GetMovement()->SetVelocity({ 0.0f, 0.0f, 0.0f }); // Stop sliding
                m_player->GetStateMachine()->ChangeState(m_player, std::make_unique<PlayerDead>());

                // Kill the kamikaze enemy so it doesn't survive the explosion
                enemy->TakeDamage(9999);

                enemy->SetKilledPlayer(true);

                // Player is dead, no need to check other enemies
                return;
            }

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

void CollisionManager::CheckPlayerVsCheckpointLines()
{
    if (!m_player || !m_stage || m_player->GetHP() <= 0) return;

    const float TRIGGER_RANGE_Z = 2.0f;

    for (const auto& line : m_stage->m_linesCheckpoint)
    {
        DirectX::XMVECTOR vLocalPos = TransformToLocalLine(m_player->GetMovement()->GetPosition(), line);
        DirectX::XMFLOAT3 localPos;
        DirectX::XMStoreFloat3(&localPos, vLocalPos);
        float lineHalfLength = line.Scale.x * 0.5f;

        // Check if player is between the left/right ends of the line
        if (localPos.x < -lineHalfLength || localPos.x > lineHalfLength) continue;

        // Check if player crosses the Z-depth of the line
        if (localPos.z > -TRIGGER_RANGE_Z && localPos.z < TRIGGER_RANGE_Z)
        {
            if (m_onCheckpointReachCallback)
            {
                // line.Position is the exact center 
                m_onCheckpointReachCallback(line.Position);
            }
        }
    }
}

void CollisionManager::CheckPlayerVsTriggerLines()
{
    // Guard against missing data or dead player
    if (!m_player || !m_stage || m_player->GetHP() <= 0) return;

    const float TRIGGER_RANGE_Z = 2.0f;

    for (int i = 0; i < m_stage->m_linesEnable.size(); ++i)
    {
        const auto& line = m_stage->m_linesEnable[i];

        // Transform player pos into the line's local space
        DirectX::XMVECTOR vLocalPos = TransformToLocalLine(m_player->GetMovement()->GetPosition(), line);
        DirectX::XMFLOAT3 localPos;
        DirectX::XMStoreFloat3(&localPos, vLocalPos);

        float lineHalfLength = line.Scale.x * 0.5f;

        // Check if player is standing on the line
        if (localPos.x >= -lineHalfLength && localPos.x <= lineHalfLength &&
            localPos.z > -TRIGGER_RANGE_Z && localPos.z < TRIGGER_RANGE_Z)
        {
            if (m_onEnableLineReachCallback)
            {
                m_onEnableLineReachCallback(i);
            }
        }
    }
}

void CollisionManager::CheckPlayerVsVoidLines()
{
    //if (!m_player || !m_stage) return;
    //if (m_player->IsFalling()) return;

    //const float FALL_THRESHOLD = 0.1f;
    //const float TRIGGER_RANGE = 2.0f;

    //for (const auto& line : m_stage->m_linesVoid)
    //{
    //    XMVECTOR vLocalPos = TransformToLocalLine(m_player->GetMovement()->GetPosition(), line);
    //    XMFLOAT3 localPos;
    //    XMStoreFloat3(&localPos, vLocalPos);
    //    float lineHalfLength = line.Scale.x * 0.5f;

    //    if (localPos.x < -lineHalfLength - 0.5f || localPos.x > lineHalfLength + 0.5f) continue;
    //    if (localPos.z < -FALL_THRESHOLD && localPos.z > -TRIGGER_RANGE)
    //    {
    //        m_player->SetFalling(true);
    //    }
    //}
}

void CollisionManager::CheckPlayerVsItems()
{
    if (!m_player || !m_itemManager) return;

    XMFLOAT3 pPos = m_player->GetMovement()->GetPosition();
    float pRadius = 0.5f;

    for (auto& item : m_itemManager->GetItems())
    {
        if (!item->IsActive()) continue;

        XMFLOAT3 iPos = item->GetPosition();
        float iRadius = item->scale.x * 0.5f;

        float distSq = (pPos.x - iPos.x) * (pPos.x - iPos.x) +
            (pPos.z - iPos.z) * (pPos.z - iPos.z);

        float combinedRadius = pRadius + iRadius;

        if (distSq < combinedRadius * combinedRadius)
        {
            AudioManager::Instance().PlaySFX("Data/Sound/SE_Pop.wav", 0.4f);

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

            if (enemy->GetType() == EnemyType::Pentagon) enemyRadius = 4.0f * enemyScale;
            else if (enemy->GetType() == EnemyType::Paddle) enemyRadius = 0.6f * enemyScale;

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

        const DirectX::XMFLOAT3 ePos{ enemy->GetPosition() };
        const float enemyScale{ enemy->GetScale().x };
        float enemyRadius{ 1.0f * enemyScale };

        if (enemy->GetType() == EnemyType::Pentagon) enemyRadius = 4.0f * enemyScale;
        else if (enemy->GetType() == EnemyType::Paddle) enemyRadius = 1.2f * enemyScale;

        const bool isKamikaze{ enemy->GetAttackType() == AttackType::Tracking };
        const float dynamicReach{ isKamikaze ? (baseReach * 3.5f) : baseReach };

        const float exactSlashDistance{ 0.5f + enemyRadius + dynamicReach };
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

            // If the dot product is less than the threshold 
            // (e.g. Kamikaze is behind the player), this fails entirely. Player dies.
            if (dot >= minDotProduct)
            {
                const float prioritizationDistSq{ isKamikaze ? (distSq * 0.1f) : distSq };

                if (prioritizationDistSq < closestDistSq)
                {
                    closestDistSq = prioritizationDistSq;
                    bestTarget = enemy.get();
                }
            }
        }
    }

    return bestTarget;
}
