#include "NaviAlly.h"
#include "Camera.h"
#include "Enemy.h"
#include "EnemyManager.h"
#include "Player.h"
#include "System/AudioManager.h"
#include "System/Graphics.h"
#include <algorithm>
#include <cmath>

using namespace DirectX;

float NaviAlly::GetRandomFloat(float min, float max) noexcept
{
    const float random{ static_cast<float>(rand()) / static_cast<float>(RAND_MAX) };
    return min + random * (max - min);
}

NaviAlly::NaviAlly(ID3D11Device* device, Player* targetPlayer, EnemyManager* enemyManager)
    : m_targetPlayer{ targetPlayer }
    , m_enemyManager{ enemyManager }
{
    model = std::make_shared<Model>(device, "Data/Model/Character/MDL_Navi.glb");
    scale = { 0.4f, 0.4f, 0.4f };

    if (movement)
    {
        movement->SetRotation({ 0.0f, 0.0f, 0.0f });
    }

    m_animator = std::make_unique<AnimationController>();
    m_animator->Initialize(model);
    m_animator->Play("Armature|Flying", true, 0.2f);

    if (m_targetPlayer && movement)
    {
        XMFLOAT3 startPos{ m_targetPlayer->GetPosition() };
        startPos.x += 1.0f;
        startPos.y += HOVER_HEIGHT;
        startPos.z += 0.5f;
        movement->SetPosition(startPos);
    }

    SyncData();
}

void NaviAlly::Reset() noexcept
{
    m_hp = MAX_HP;
    m_projectiles.clear();
    m_pulseTimer = 0.0f;
}

void NaviAlly::UpdateAttackDelay(float dt) noexcept
{
    if (m_attackDelayTimer > 0.0f)
    {
        m_attackDelayTimer = (std::max)(0.0f, m_attackDelayTimer - dt);
    }
}

void NaviAlly::Update(float elapsedTime, Camera* camera)
{
    if (!IsAlive()) return;

    UpdateAttackDelay(elapsedTime);

    if (m_animator)
    {
        m_animator->Update(elapsedTime);
    }

    if (m_isPotioned)
    {
        m_pulseTimer += elapsedTime;
    }

    UpdateHoverLogic(elapsedTime);
    UpdateShootingLogic(elapsedTime, camera);
    UpdateProjectiles(elapsedTime, camera);
}

void NaviAlly::UpdateHoverLogic(float elapsedTime)
{
    if (!m_targetPlayer || !movement) return;
    m_animTime += elapsedTime;

    if (m_isPotioned && !m_hasCapturedAnchor)
    {
        m_potionAnchorPos = movement->GetPosition();
        m_hasCapturedAnchor = true;
    }
    else if (!m_isPotioned)
    {
        m_hasCapturedAnchor = false;
    }

    XMFLOAT3 currentPos{ movement->GetPosition() };
    XMFLOAT3 targetPos{ 0.0f, 0.0f, 0.0f };

    if (m_isPotioned)
    {
        m_randomMoveTimer += elapsedTime;
        if (m_randomMoveTimer >= MOVE_SWITCH_TIME)
        {
            m_randomTargetOffset = {
                GetRandomFloat(-TETHER_RADIUS, TETHER_RADIUS),
                0.0f,
                GetRandomFloat(-TETHER_RADIUS, TETHER_RADIUS)
            };
            m_randomMoveTimer = 0.0f;
        }

        targetPos = {
            m_potionAnchorPos.x + m_randomTargetOffset.x,
            m_potionAnchorPos.y,
            m_potionAnchorPos.z + m_randomTargetOffset.z
        };
    }
    else
    {
        const CharacterMovement* const pMovement{ m_targetPlayer->GetMovement() };
        const std::shared_ptr<Model> pModel{ m_targetPlayer->GetModel() };
        if (!pMovement || !pModel) return;

        DirectX::XMFLOAT3 anchorPos{ pMovement->GetPosition() };
        const int bodyIndex{ pModel->GetNodeIndex("body") };

        if (bodyIndex != -1)
        {
            const auto& nodes{ pModel->GetNodes() };
            if (static_cast<size_t>(bodyIndex) < nodes.size())
            {
                const DirectX::XMFLOAT4X4& bodyMatrix{ nodes[bodyIndex].worldTransform };
                anchorPos = { bodyMatrix._41, bodyMatrix._42, bodyMatrix._43 };
            }
        }

        const DirectX::XMFLOAT3 aimPos{ m_targetPlayer->GetAimTarget() };
        const float aimDx{ aimPos.x - anchorPos.x };
        const float aimDz{ aimPos.z - anchorPos.z };

        float targetYaw{ DirectX::XMConvertToRadians(pMovement->GetRotation().y) };
        if ((aimDx * aimDx + aimDz * aimDz) > 0.0001f)
        {
            targetYaw = std::atan2(aimDx, aimDz);
        }

        float angleDiff{ targetYaw - m_lazyHoverYaw };
        while (angleDiff > DirectX::XM_PI)  angleDiff -= DirectX::XM_2PI;
        while (angleDiff < -DirectX::XM_PI) angleDiff += DirectX::XM_2PI;

        const float lazyLerp{ 1.0f - std::exp(-LAZY_ROTATION_SPEED * elapsedTime) };
        m_lazyHoverYaw += angleDiff * lazyLerp;

        while (m_lazyHoverYaw > DirectX::XM_PI)  m_lazyHoverYaw -= DirectX::XM_2PI;
        while (m_lazyHoverYaw < -DirectX::XM_PI) m_lazyHoverYaw += DirectX::XM_2PI;

        const float sinYaw{ std::sin(m_lazyHoverYaw) };
        const float cosYaw{ std::cos(m_lazyHoverYaw) };

        const float offsetX{ (cosYaw * HOVER_RIGHT_OFFSET) - (sinYaw * HOVER_BACK_OFFSET) };
        const float offsetZ{ (-sinYaw * HOVER_RIGHT_OFFSET) - (cosYaw * HOVER_BACK_OFFSET) };

        targetPos = {
            anchorPos.x + offsetX,
            anchorPos.y + (HOVER_HEIGHT * 0.25f),
            anchorPos.z + offsetZ
        };
    }

    const float lerpFactor{ 1.0f - std::exp(-FOLLOW_SPEED * elapsedTime) };
    currentPos.x += (targetPos.x - currentPos.x) * lerpFactor;
    currentPos.z += (targetPos.z - currentPos.z) * lerpFactor;

    const float hoverBob{ std::sin(m_animTime * FLOAT_SPEED) * FLOAT_AMP };
    currentPos.y += (targetPos.y - currentPos.y) * lerpFactor;
    currentPos.y += hoverBob;

    movement->SetPosition(currentPos);
    SyncData();
}

void NaviAlly::SpawnBullet(const DirectX::XMFLOAT3& pos, const DirectX::XMFLOAT3& fwd, float speed) noexcept
{
    for (auto& bullet : m_projectiles)
    {
        if (!bullet->IsActive())
        {
            bullet->Fire(pos, fwd, speed);
            return;
        }
    }

    auto newBullet{ std::make_unique<Bullet>() };
    newBullet->scale = { 0.7f, 0.7f, 0.7f };
    newBullet->Fire(pos, fwd, speed);
    m_projectiles.push_back(std::move(newBullet));

    if (m_projectiles.size() > MAX_BULLETS)
    {
        m_projectiles.pop_front();
    }
}

void NaviAlly::FireAtTarget(const DirectX::XMFLOAT3& targetPos)
{
    if (!movement) return;

    const XMFLOAT3 myPos{ movement->GetPosition() };
    const float dx{ targetPos.x - myPos.x };
    const float dy{ targetPos.y - myPos.y };
    const float dz{ targetPos.z - myPos.z };
    const float distSq{ (dx * dx) + (dy * dy) + (dz * dz) };

    XMFLOAT3 fwd{ 0.0f, 0.0f, 1.0f };
    if (distSq > 0.0001f)
    {
        const float dist{ std::sqrt(distSq) };
        fwd = { dx / dist, dy / dist, dz / dist };
    }

    SpawnBullet(myPos, fwd, BULLET_SPEED);
}

void NaviAlly::FireRadialBurst() noexcept
{
    if (!movement) return;

    const XMFLOAT3 myPos{ movement->GetPosition() };
    constexpr int bulletCount{ 24 };
    constexpr float angleStep{ DirectX::XM_2PI / static_cast<float>(bulletCount) };

    for (int i{ 0 }; i < bulletCount; ++i)
    {
        const float angle{ static_cast<float>(i) * angleStep };
        const XMFLOAT3 fwd{ std::sin(angle), 0.0f, std::cos(angle) };
        SpawnBullet(myPos, fwd, BOSS_BULLET_SPEED);
    }
}

void NaviAlly::FireFanBurst(const DirectX::XMFLOAT3& targetPos) noexcept
{
    if (!movement) return;

    const XMFLOAT3 myPos{ movement->GetPosition() };
    const float dx{ targetPos.x - myPos.x };
    const float dz{ targetPos.z - myPos.z };

    if ((dx * dx + dz * dz) < 0.0001f) return;

    const float baseAngle{ std::atan2(dx, dz) };
    constexpr int bulletCount{ 5 };
    constexpr float spreadAngle{ 0.25f };
    const float startAngle{ baseAngle - (spreadAngle * (static_cast<float>(bulletCount) / 2.0f)) };

    for (int i{ 0 }; i < bulletCount; ++i)
    {
        const float angle{ startAngle + (static_cast<float>(i) * spreadAngle) };
        const XMFLOAT3 fwd{ std::sin(angle), 0.0f, std::cos(angle) };
        SpawnBullet(myPos, fwd, BOSS_BULLET_SPEED * 1.5f);
    }
}

void NaviAlly::UpdateShootingLogic(float elapsedTime, Camera* camera)
{
    if (m_targetPlayer && m_targetPlayer->GetHP() <= 0) return;
    if (m_attackDelayTimer > 0.0f) return;

    if (m_isPotioned)
    {
        if (!m_targetPlayer) return;

        m_bossAttackTimer += elapsedTime;
        if (m_bossAttackTimer >= BOSS_ATTACK_COOLDOWN)
        {
            m_bossAttackTimer = 0.0f;

            if (m_currentPattern == PotionAttackPattern::Radial)
            {
                FireRadialBurst();
                m_currentPattern = PotionAttackPattern::Fan;
            }
            else
            {
                XMFLOAT3 aimPos{ m_targetPlayer->GetPosition() };
                aimPos.y += 1.0f;
                FireFanBurst(aimPos);
                m_currentPattern = PotionAttackPattern::Radial;
            }
        }
        return;
    }

    if (!m_enemyManager || !camera || !movement) return;

    const XMFLOAT3 myPos{ movement->GetPosition() };
    Enemy* bestTarget{ nullptr };
    float closestDistSq{ ATTACK_RANGE_SQ };

    for (const auto& enemy : m_enemyManager->GetEnemies())
    {
        if (!enemy || !enemy->IsActive()) continue;
        if (enemy->GetAttackType() == AttackType::None) continue;

        const XMFLOAT3 ePos{ enemy->GetPosition() };
        const float dx{ ePos.x - myPos.x };
        const float dz{ ePos.z - myPos.z };
        const float distSq{ (dx * dx) + (dz * dz) };

        if (distSq < closestDistSq)
        {
            const float dynamicRadius{ 1.5f * enemy->GetScale().x };
            if (camera->CheckSphere(ePos.x, ePos.y, ePos.z, dynamicRadius))
            {
                closestDistSq = distSq;
                bestTarget = enemy.get();
            }
        }
    }

    if (bestTarget != m_currentTarget)
    {
        m_currentTarget = bestTarget;
        m_reactionTimer = 0.0f;
        m_fireTimer = 0.0f;
    }

    if (m_currentTarget)
    {
        m_reactionTimer += elapsedTime;

        if (m_reactionTimer >= REACTION_DELAY)
        {
            m_fireTimer += elapsedTime;
            if (m_fireTimer >= FIRE_RATE)
            {
                m_fireTimer = 0.0f;
                FireAtTarget(m_currentTarget->GetPosition());
            }
        }
    }
}

void NaviAlly::UpdateProjectiles(float elapsedTime, Camera* camera)
{
    if (!movement) return;
    const XMFLOAT3 myPos{ movement->GetPosition() };

    for (auto& bullet : m_projectiles)
    {
        if (!bullet->IsActive()) continue;

        bullet->Update(elapsedTime, camera);

        const XMFLOAT3 bPos{ bullet->GetMovement()->GetPosition() };
        const float dx{ myPos.x - bPos.x };
        const float dz{ myPos.z - bPos.z };

        if ((dx * dx + dz * dz) > DESPAWN_DIST_SQ)
        {
            bullet->SetActive(false);
        }
    }
}

void NaviAlly::TakeDamage(int damage) noexcept
{
    if (!IsAlive()) return;

    m_hp = (std::max)(0, m_hp - damage);

    static const std::string s_hitSfxPath{ "Data/Sound/SE_Enemy_Hit.wav" };
    AudioManager::Instance().PlaySFX(s_hitSfxPath, 0.6f);

    if (m_hp <= 0)
    {
        m_projectiles.clear();
    }
}

void NaviAlly::SetPotionedState(bool isPotioned) noexcept
{
    if (m_isPotioned == isPotioned) return;

    m_isPotioned = isPotioned;

    if (m_isPotioned && m_targetPlayer)
    {
        m_targetPlayer->SetMaxHP(150.0f);
    }
}

void NaviAlly::SetPosition(const DirectX::XMFLOAT3& pos) noexcept
{
    if (movement) movement->SetPosition(pos);
    SyncData();
}

void NaviAlly::SetRotation(const DirectX::XMFLOAT3& rot) noexcept
{
    if (movement) movement->SetRotation(rot);
    SyncData();
}

void NaviAlly::Render(ModelRenderer* renderer)
{
    if (!IsAlive() || !model || !renderer) return;

    DirectX::XMFLOAT4 renderColor{ m_color };

    if (m_isPotioned)
    {
        const float wave{ (std::sin(m_pulseTimer * 15.0f) + 1.0f) * 0.5f };
        const auto& c1{ EnemyLevelData::ArcanePurple };
        const auto& c2{ EnemyLevelData::ToxicGreen };

        renderColor = {
            c1.x + (c2.x - c1.x) * wave,
            c1.y + (c2.y - c1.y) * wave,
            c1.z + (c2.z - c1.z) * wave,
            1.0f
        };
    }
    renderer->Draw(model, renderColor);
}

void NaviAlly::RenderProjectiles(ModelRenderer* renderer)
{
    if (!IsAlive() || !renderer) return;

    static constexpr DirectX::XMFLOAT4 s_naviBulletColor{ 6.0f, 6.0f, 6.0f, 6.0f };

    for (auto& bullet : m_projectiles)
    {
        if (bullet && bullet->IsActive())
        {
            renderer->Draw(bullet->GetModel(), s_naviBulletColor);
        }
    }
}

void NaviAlly::RenderDebug(ShapeRenderer* shapeRenderer)
{
    if (!IsAlive() || !shapeRenderer || !movement) return;

    static constexpr DirectX::XMFLOAT4 s_debugColor{ 0.0f, 1.0f, 1.0f, 0.5f };
    shapeRenderer->DrawSphere(movement->GetPosition(), HITBOX_RADIUS, s_debugColor);
}