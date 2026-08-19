#include "Enemy.h"

using namespace DirectX;

Enemy::Enemy(ID3D11Device* device, const char* filePath, XMFLOAT3 startPos, XMFLOAT3 startRot,
    XMFLOAT4 startColor, EnemyType type, AttackType attackType,
    float minX, float maxX, float minZ, float maxZ, MoveDir dir)
{
    // Delegate all setup to Reinitialize to keep logic in ONE place
    Reinitialize(device, filePath, startPos, startRot, startColor, type, attackType, minX, maxX, minZ, maxZ, dir);
}

float Enemy::GetRandomFloat(float min, float max)
{
    float random = ((float)rand()) / (float)RAND_MAX;
    float range = max - min;
    return (random * range) + min;
}

Enemy::~Enemy() {}

void Enemy::Update(float elapsedTime, Camera* camera)
{
    m_lifeTime += elapsedTime;

    if (m_blinkTimer > 0.0f)
    {
        m_blinkTimer = (std::max)(0.0f, m_blinkTimer - elapsedTime);
    }

    if (m_type == EnemyType::Pentagon)
    {
        XMFLOAT3 rot = movement->GetRotation();
        rot.y += 10.0f * elapsedTime;
        movement->SetRotation(rot);
    }

    if (m_attackType == AttackType::TrackingHorizontal)
    {
        XMFLOAT3 pos = movement->GetPosition();
        pos.x += m_currentSpeed * elapsedTime;

        if (pos.x >= m_patrolMaxX)
        {
            pos.x = m_patrolMaxX;
            m_currentSpeed = -std::abs(m_baseMoveSpeed);
        }
        else if (pos.x <= m_patrolMinX)
        {
            pos.x = m_patrolMinX;
            m_currentSpeed = std::abs(m_baseMoveSpeed);
        }
        movement->SetPosition(pos);
    }
    else if (m_attackType == AttackType::TrackingRandom)
    {
        XMFLOAT3 pos = movement->GetPosition();
        float dx = m_randomTargetPos.x - pos.x;
        float dz = m_randomTargetPos.z - pos.z;
        float distSq = dx * dx + dz * dz;

        if (distSq < 0.1f)
        {
            m_randomTargetPos.x = GetRandomFloat(m_patrolMinX, m_patrolMaxX);
            m_randomTargetPos.z = GetRandomFloat(m_patrolMinZ, m_patrolMaxZ);
            m_randomTargetPos.y = pos.y;
        }

        XMVECTOR vPos = XMLoadFloat3(&pos);
        XMVECTOR vTarget = XMLoadFloat3(&m_randomTargetPos);
        XMVECTOR vDir = XMVectorSubtract(vTarget, vPos);
        vDir = XMVector3Normalize(vDir);
        XMVECTOR vOffset = vDir * m_baseMoveSpeed * elapsedTime;
        XMVECTOR vNewPos = XMVectorAdd(vPos, vOffset);
        XMStoreFloat3(&pos, vNewPos);
        movement->SetPosition(pos);
    }

    UpdateProjectiles(elapsedTime, camera);

    XMFLOAT3 pos = movement->GetPosition();
    XMFLOAT3 rot = movement->GetRotation();

    XMMATRIX S = XMMatrixScaling(m_scale.x, m_scale.y, m_scale.z);
    XMMATRIX R = XMMatrixRotationRollPitchYaw(
        XMConvertToRadians(rot.x),
        XMConvertToRadians(rot.y),
        XMConvertToRadians(rot.z)
    );
    XMMATRIX T = XMMatrixTranslation(pos.x, pos.y, pos.z);

    XMFLOAT4X4 worldMatrix;
    XMStoreFloat4x4(&worldMatrix, S * R * T);

    if (m_model) m_model->UpdateTransform(worldMatrix);
}

void Enemy::UpdateTracking(float elapsedTime, Camera* camera, const DirectX::XMFLOAT3& playerPos, bool allowAttack)
{
    if (!camera) return;
    if (allowAttack)
    {
        UpdateAttackLogic(elapsedTime, camera, playerPos, allowAttack);
    }
}

void Enemy::UpdateAttackLogic(float elapsedTime, Camera* camera, const DirectX::XMFLOAT3& playerPos, bool allowAttack)
{
    // 1. FAST FAIL: Guard clauses prevent deep nesting and dangling pointer crashes.
    if (m_attackType == AttackType::None || !camera || !allowAttack) return;

    const DirectX::XMFLOAT3 myPos{ movement->GetPosition() };
    const DirectX::XMFLOAT3 targetPos{ playerPos };

    // 2. CPU OPTIMIZATION: Calculate Squared Distance to skip expensive std::sqrt calculations.
    const float dx{ targetPos.x - myPos.x };
    const float dz{ targetPos.z - myPos.z };
    const float distSq{ (dx * dx) + (dz * dz) };
    const float activationDistSq{ m_activationDistance * m_activationDistance };

    // Out of range? Exit early. Zero CPU wasted.
    if (distSq > activationDistSq)
    {
        // ---> MOMENTUM RESET GUARD <---
        // If the player escapes the radius, reset the timer. 
        // This forces the enemy to do the slow "creep" phase again next time!
        if (m_attackType == AttackType::Tracking) m_aggroTimer = 0.0f;
        return;
    }

    // 3. PHYSICAL ROTATION & MOVEMENT
    const bool isTrackingType{
        m_attackType == AttackType::Tracking ||
        m_attackType == AttackType::TrackingHorizontal ||
        m_attackType == AttackType::TrackingRandom
    };

    if (isTrackingType)
    {
        // 3a. ROTATE TO PLAYER
        const float targetYawRad{ std::atan2(dx, dz) };
        const float targetYawDeg{ DirectX::XMConvertToDegrees(targetYawRad) };
        movement->SetRotation({ 0.0f, targetYawDeg, 0.0f });

        // 3b. MOVE TO PLAYER (The "Trees Hate You" Mechanic)
        if (m_attackType == AttackType::Tracking && m_baseMoveSpeed > 0.0f)
        {
            constexpr float MELEE_STOPPING_DIST_SQ{ 0.1f * 0.1f };

            if (distSq > MELEE_STOPPING_DIST_SQ)
            {
                // Tick the Aggro Timer up every frame it chases the player
                m_aggroTimer += elapsedTime;

                // --- HORROR TUNING VARIABLES ---
                constexpr float CREEP_TIME{ 3.0f };          // How many seconds it stays slow
                constexpr float CREEP_SPEED{ 1.5f };         // Slow, scary walking speed
                constexpr float HILARIOUS_MAX_SPEED{ 14.5f };// Player is 10.0, this is TERRIFYING
                constexpr float RAMP_UP_RATE{ 18.0f };       // Acceleration multiplier

                float currentSpeed{ 0.0f };

                if (m_aggroTimer < CREEP_TIME)
                {
                    // PHASE 1: The Creeping Doom
                    currentSpeed = CREEP_SPEED;
                }
                else
                {
                    // PHASE 2: The Explosive Sprint
                    // Calculate how many seconds have passed since it got mad
                    const float timeSprint{ m_aggroTimer - CREEP_TIME };

                    // Linear Ramp: Speed = Base + (Time * Acceleration)
                    currentSpeed = CREEP_SPEED + (timeSprint * RAMP_UP_RATE);

                    // Cap the speed so it doesn't break the physics engine
                    currentSpeed = (std::min)(currentSpeed, HILARIOUS_MAX_SPEED);
                }

                // Apply the terrifying speed to movement
                const float dist{ std::sqrt(distSq) };
                DirectX::XMFLOAT3 pos{ movement->GetPosition() };

                pos.x += (dx / dist) * currentSpeed * elapsedTime;
                pos.z += (dz / dist) * currentSpeed * elapsedTime;

                movement->SetPosition(pos);
            }
            else
            {
                // BUG PREVENTION: DO NOT RESET THE TIMER HERE!
                // If the enemy catches the player, they stop to shoot. 
                // If the player backs up 1 step, we want the enemy to STILL be sprinting.
                // We only reset the timer if the player fully escapes the 'activationDistSq'.
            }
        }
    }

    // 4. FIRING LOGIC
    if (m_attackType != AttackType::Tracking)
    {
        m_attackTimer += elapsedTime;

        if (m_attackTimer >= m_fireRate)
        {
            m_attackTimer = 0.0f;

            if (m_attackType == AttackType::RadialBurst)
            {
                constexpr int projectileCount{ 8 };
                const float angleStep{ DirectX::XM_2PI / static_cast<float>(projectileCount) };

                for (int i{ 0 }; i < projectileCount; ++i)
                {
                    const float currentAngle{ i * angleStep };
                    const float dirX{ std::sin(currentAngle) };
                    const float dirZ{ std::cos(currentAngle) };
                    const DirectX::XMFLOAT3 burstDir{ dirX, 0.0f, dirZ };

                    const DirectX::XMFLOAT3 spawnPos{
                        myPos.x + (dirX * 1.0f),
                        myPos.y,
                        myPos.z + (dirZ * 1.0f)
                    };

                    // Implement zero-cost Object Pool for Burst
                    bool bulletRecycled{ false };
                    for (auto& bullet : m_projectiles)
                    {
                        if (!bullet->IsActive())
                        {
                            bullet->Fire(spawnPos, burstDir, m_projectileSpeed);
                            bulletRecycled = true;
                            break;
                        }
                    }

                    if (!bulletRecycled)
                    {
                        auto newBullet{ std::make_unique<Bullet>() };
                        newBullet->Fire(spawnPos, burstDir, m_projectileSpeed);
                        m_projectiles.push_back(std::move(newBullet));
                    }
                }
            }
            else
            {
                // ---> THE FIX: Aim EXACTLY at the player for both Tracking AND Static <---
                DirectX::XMFLOAT3 fwd{ 0.0f, 0.0f, 1.0f }; // Safe default

                // Calculate exact 3D trajectory
                const float aimDx{ targetPos.x - myPos.x };
                const float aimDy{ targetPos.y - myPos.y };
                const float aimDz{ targetPos.z - myPos.z };
                const float aimDistSq{ (aimDx * aimDx) + (aimDy * aimDy) + (aimDz * aimDz) };

                // BUG PREVENTION: "Divide-By-Zero" Guard (NaN Propagation)
                // If the player and enemy perfectly overlap, math divides by zero and crashes the engine.
                if (aimDistSq > 0.0001f)
                {
                    const float aimDist{ std::sqrt(aimDistSq) };
                    fwd = { aimDx / aimDist, aimDy / aimDist, aimDz / aimDist };
                }
                else
                {
                    fwd = GetForwardVector(); // Safe fallback if they are perfectly overlapping
                }

                // Calculate Spawn Position safely
                const DirectX::XMFLOAT3 spawnPos{
                    myPos.x + (fwd.x * SPAWN_OFFSET_FWD),
                    myPos.y + SPAWN_OFFSET_Y,
                    myPos.z + (fwd.z * SPAWN_OFFSET_FWD)
                };

                // ---> OPTIMIZATION: ZERO RUNTIME ALLOCATION POOL <---
                // Recycle old bullets instead of 'new/delete' thrashing the CPU memory heap.
                bool bulletRecycled{ false };
                for (auto& bullet : m_projectiles)
                {
                    if (!bullet->IsActive())
                    {
                        bullet->Fire(spawnPos, fwd, m_projectileSpeed);
                        bulletRecycled = true;
                        break; // Instant exit
                    }
                }

                if (!bulletRecycled)
                {
                    auto newBullet{ std::make_unique<Bullet>() };
                    newBullet->Fire(spawnPos, fwd, m_projectileSpeed);
                    m_projectiles.push_back(std::move(newBullet));

                    if (m_projectiles.size() > static_cast<size_t>(MAX_PROJECTILES))
                    {
                        m_projectiles.pop_front();
                    }
                }
            }
        }
    }
    // 5. UPDATE IN-FLIGHT PROJECTILES (Zero-cost Despawn)
    const float despawnDistSq{ m_despawnDistance * m_despawnDistance };

    for (auto& bullet : m_projectiles)
    {
        if (!bullet->IsActive()) continue;

        bullet->Update(elapsedTime, camera);

        const DirectX::XMFLOAT3 bPos{ bullet->GetMovement()->GetPosition() };
        const float bDx{ myPos.x - bPos.x };
        const float bDz{ myPos.z - bPos.z };

        // BUG PREVENTION: INFINITE FLIGHT GUARD
        // Instead of erasing the bullet from memory (which is slow), we just turn it off!
        if ((bDx * bDx + bDz * bDz) > despawnDistSq)
        {
            bullet->SetActive(false);
        }
    }
}

DirectX::XMFLOAT3 Enemy::GetForwardVector() const
{
    XMFLOAT3 rot = movement->GetRotation();
    float yawRad = XMConvertToRadians(rot.y);
    float pitchRad = XMConvertToRadians(rot.x);
    float x = sinf(yawRad) * cosf(pitchRad);
    float y = -sinf(pitchRad);
    float z = cosf(yawRad) * cosf(pitchRad);
    return { x, y, z };
}

void Enemy::Reinitialize(ID3D11Device* device, const char* filePath, const DirectX::XMFLOAT3& startPos,
    const DirectX::XMFLOAT3& startRot, const DirectX::XMFLOAT4& startColor,
    EnemyType type, AttackType attackType, const float minX, const float maxX,
    const float minZ, const float maxZ, const MoveDir dir)
{
    // Reassign the model (std::shared_ptr handles cleanup of the old model automatically)
    m_model = std::make_shared<Model>(device, filePath);
    model = m_model;

    // Reset Core Identity
    m_type = type;
    m_attackType = attackType;
    m_baseColor = startColor;
    m_scale = (m_type == EnemyType::Pentagon) ? DirectX::XMFLOAT3{ 150.0f, 150.0f, 150.0f } : DirectX::XMFLOAT3{ 1.0f, 1.0f, 1.0f };

    // Reset Transforms & Patrols
    movement->SetPosition(startPos);
    movement->SetRotation(startRot);
    originalPosition = startPos;
    originalRotation = startRot;

    m_patrolMinX = startPos.x + minX;
    m_patrolMaxX = startPos.x + maxX;
    m_patrolMinZ = startPos.z + minZ;
    m_patrolMaxZ = startPos.z + maxZ;
    m_randomTargetPos = startPos;
    m_moveDir = dir;

    if (dir == MoveDir::Right)      m_currentSpeed = -m_baseMoveSpeed;
    else if (dir == MoveDir::Left)  m_currentSpeed = m_baseMoveSpeed;
    else                            m_currentSpeed = 0.0f;

    // 4. Clean up the "Zombie" state from its previous life
    m_projectiles.clear(); // Destroy old bullets
    m_attackTimer = 0.0f;
    m_aggroTimer = 0.0f;
    m_blinkTimer = 0.0f;
    m_lifeTime = 0.0f;
    m_hp = 30; // Or whatever default/config HP you want
    m_isHighlighted = false;
    m_isActive = true;
}

void Enemy::UpdateProjectiles(float elapsedTime, Camera* camera)
{
    // UPDATE IN-FLIGHT PROJECTILES (Zero-cost Despawn)
    const float despawnDistSq{ m_despawnDistance * m_despawnDistance };

    for (auto& bullet : m_projectiles)
    {
        if (!bullet->IsActive()) continue;

        bullet->Update(elapsedTime, camera);

        const DirectX::XMFLOAT3 myPos{ movement->GetPosition() };
        const DirectX::XMFLOAT3 bPos{ bullet->GetMovement()->GetPosition() };
        const float bDx{ myPos.x - bPos.x };
        const float bDz{ myPos.z - bPos.z };

        if ((bDx * bDx + bDz * bDz) > despawnDistSq)
        {
            bullet->SetActive(false);
        }
    }
}

void Enemy::RenderProjectiles(ModelRenderer* renderer)
{
    // 1. Color Constants (Static for Zero-Allocation)
    static constexpr DirectX::XMFLOAT4 TOXIC_GREEN{ 0.4f, 0.9f, 0.2f, 1.0f };
    static constexpr DirectX::XMFLOAT4 ELECTRIC_PINK{ 1.0f, 0.0f, 1.0f, 1.0f };
    static constexpr float PULSE_SPEED{ 15.0f };

    for (auto& bullet : m_projectiles)
    {
        if (bullet && bullet->IsActive())
        {
            // 2. Calculate the sine pulse (Result: 0.0 to 1.0)
            // Using bullet->GetLifeTime() ensures each bullet pulses independently
            const float pulse{ (std::sin(bullet->GetLifeTime() * PULSE_SPEED) + 1.0f) * 0.5f };

            // 3. Interpolate (LERP) between Toxic Green and Electric Pink
            const DirectX::XMFLOAT4 pulseColor{
                TOXIC_GREEN.x + (ELECTRIC_PINK.x - TOXIC_GREEN.x) * pulse,
                TOXIC_GREEN.y + (ELECTRIC_PINK.y - TOXIC_GREEN.y) * pulse,
                TOXIC_GREEN.z + (ELECTRIC_PINK.z - TOXIC_GREEN.z) * pulse,
                1.0f
            };

            // 4. Render using Phong to ensure the Bloom/HDR glow activates
            renderer->Draw(ShaderId::Phong, bullet->GetModel(), pulseColor);
        }
    }
}

void Enemy::RenderDebugProjectiles(ShapeRenderer* renderer)
{
    /*for (const auto& bullet : m_projectiles)
    {
        if (bullet && bullet->IsActive())
        {
            DirectX::XMFLOAT3 pos = bullet->GetMovement()->GetPosition();
            float radius = bullet->GetRadius();
            renderer->DrawSphere(pos, radius, { 1.0f, 0.0f, 0.0f, 1.0f });
        }
    }

    if (m_isHighlighted)
    {
        DirectX::XMFLOAT3 pos = movement->GetPosition();
        renderer->DrawBox(pos, { 0.0f, 0.0f, 0.0f }, { 2.0f, 1.0f, 1.0f }, { 0.0f, 1.0f, 0.0f, 1.0f });
    }*/
}

DirectX::XMFLOAT4 Enemy::GetRenderColor() const
{
    // PRIORITY 1: DAMAGE FLASH
    // If the enemy just got hit, flash HDR White instantly. 
    if (m_blinkTimer > 0.0f)
    {
        return { 5.0f, 5.0f, 5.0f, 1.0f };
    }

    // PRIORITY 2: PERMANENT POTIONED STATE
    // If it spawned as Potioned (Sentinel Value triggered), do the smooth purple/green pulse.
    if (m_baseColor.x < 0.0f)
    {
        // Use continuous lifespan for a smooth, endless pulse (Speed: 15.0f)
        const float wave{ (std::sin(m_lifeTime * 15.0f) + 1.0f) * 0.5f };

        // Zero-copy const references
        const auto& c1{ EnemyLevelData::ArcanePurple };
        const auto& c2{ EnemyLevelData::ToxicGreen };

        // LERP Math
        return {
            c1.x + (c2.x - c1.x) * wave,
            c1.y + (c2.y - c1.y) * wave,
            c1.z + (c2.z - c1.z) * wave,
            1.0f
        };
    }

    // PRIORITY 3: NORMAL STATE
    // If it's not taking damage and not potioned, just return its normal color.
    return m_baseColor;
}

void Enemy::SetPatrolLimitsX(float minOffset, float maxOffset)
{
    m_patrolMinX = originalPosition.x + minOffset;
    m_patrolMaxX = originalPosition.x + maxOffset;
}

void Enemy::SetPatrolLimitsZ(float minOffset, float maxOffset)
{
    m_patrolMinZ = originalPosition.z + minOffset;
    m_patrolMaxZ = originalPosition.z + maxOffset;
}

void Enemy::UpdateOriginalTransform(const DirectX::XMFLOAT3& pos, const DirectX::XMFLOAT3& rot)
{
    originalPosition = pos;
    originalRotation = rot;

    if (!m_isActive)
    {
        m_patrolMinX = pos.x; m_patrolMaxX = pos.x;
        m_patrolMinZ = pos.z; m_patrolMaxZ = pos.z;
    }
}

void Enemy::TakeDamage(int damage)
{
    // Guard Clause: If invincible OR inactive, ignore the hit entirely.
    if (m_isInvincible || !m_isActive || m_hp <= 0) return;

    m_hp -= damage;
    m_blinkTimer = BLINK_DURATION; // Trigger blink effect

    static const std::string HIT_SFX_PATH{ "Data/Sound/SE_Enemy_Hit.wav" };
    AudioManager::Instance().PlaySFX(HIT_SFX_PATH, 0.6f);

	// Play visual effect
    EffectManager::Instance().Play("Data/Effect/Hit.efk", GetPosition(), 1.0f);

    if (m_hp <= 0)
    {
        m_hp = 0; // Clamp to 0 to prevent negative HP logic bugs
        SetActive(false); // Kill the enemy
    }
}

void Enemy::SetPosition(const DirectX::XMFLOAT3& pos) { movement->SetPosition(pos); }
void Enemy::SetRotation(const DirectX::XMFLOAT3& rot) { movement->SetRotation(rot); }

DirectX::XMFLOAT3 Enemy::GetPosition() const { return movement->GetPosition(); }
DirectX::XMFLOAT3 Enemy::GetRotation() const { return movement->GetRotation(); }