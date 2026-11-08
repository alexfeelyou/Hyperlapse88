#include <algorithm>
#include <cmath>
#include <imgui.h>
#include "System/CollisionLayer.h"
#include "System/PhysicsManager.h"
#include "System/ShapeRenderer.h"
#include "AIControllerComponent.h"
#include "AnimationComponent.h"
#include "CapsuleColliderComponent.h"
#include "CharacterMovementComponent.h"
#include "CombatDirector.h"
#include "ComponentRegistry.h"
#include "EditorManager.h"
#include "GameObject.h"
#include "HealthComponent.h"
#include "PlayerControllerComponent.h"
#include "Random.h"

namespace
{
    // Extracts authoritative world position from any GameObject regardless of hierarchy nesting
    [[nodiscard]] DirectX::XMFLOAT3 GetWorldPosition(const GameObject* go) noexcept
    {
        if (!go) return { 0.0f, 0.0f, 0.0f };
        const DirectX::XMFLOAT4X4 wMat{ go->transform.GetWorldMatrix() };
        return { wMat._41, wMat._42, wMat._43 };
    }

    // 4-quadrant relative impact direction 
    // Returns Node Index: 0 = Front, 1 = Back, 2 = Left, 3 = Right
    [[nodiscard]] std::uint8_t CalculateHitDirectionIndex(const GameObject* victim, const DirectX::XMFLOAT3& attackerPos) noexcept
    {
        if (!victim) return 0;

        const DirectX::XMFLOAT3 victimPos{ GetWorldPosition(victim) };
        const float dx{ attackerPos.x - victimPos.x };
        const float dz{ attackerPos.z - victimPos.z };
        const float distSq{ (dx * dx) + (dz * dz) };
        if (distSq < 0.0001f) return 0;

        const float invDist{ 1.0f / std::sqrt(distSq) };
        const float toAttackerX{ dx * invDist };
        const float toAttackerZ{ dz * invDist };

        const float yawRad{ DirectX::XMConvertToRadians(victim->transform.rotation.y) };
        const float fwdX{ std::sin(yawRad) };
        const float fwdZ{ std::cos(yawRad) };
        const float rightX{ fwdZ };
        const float rightZ{ -fwdX };

        const float fwdDot{ (toAttackerX * fwdX) + (toAttackerZ * fwdZ) };
        const float rightDot{ (toAttackerX * rightX) + (toAttackerZ * rightZ) };

        if (std::abs(fwdDot) >= std::abs(rightDot))
        {
            return (fwdDot >= 0.0f) ? 0 : 1; // 0 = Front, 1 = Back
        }
        else
        {
            return (rightDot >= 0.0f) ? 3 : 2; // 2 = Left, 3 = Right
        }
    }
}

AIControllerComponent::AIControllerComponent(const AIControllerConfig& config) noexcept
    : m_config{ config }
{}

AIControllerComponent::~AIControllerComponent()
{
    CombatDirector::Instance().UnregisterEnemy(this);
}

void AIControllerComponent::OnAttach(GameObject* owner) noexcept
{
    IComponent::OnAttach(owner);

    if (m_owner)
    {
        m_capsule = m_owner->GetComponent<CapsuleColliderComponent>();
        if (m_capsule)
        {
            // 敵のカプセルを CollisionLayer::Enemy に自動設定
            m_capsule->SetCollisionLayer(CollisionLayer::Enemy, CollisionLayer::Mask::Enemy);
        }
        m_movement = m_owner->GetComponent<CharacterMovementComponent>();
        m_health = m_owner->GetComponent<HealthComponent>();
        m_animation = m_owner->GetComponent<AnimationComponent>();
        if (m_animation)
        {
            m_animation->SetBlackboard(&m_animBlackboard);
        }

        // Cache initial spawn position in world coordinates for leash and wander constraints
        m_blackboard.spawnPosition = GetWorldPosition(m_owner);
        m_blackboard.wanderDestination = m_blackboard.spawnPosition;

        if (m_movement)
        {
            m_initialGravity = m_movement->GetConfig().useGravity;
        }

        FindCoverProps();

        // Configure initial state aligned to archetype
        switch (m_config.archetype)
        {
        case EnemyArchetype::grunt:
            ChangeState(EnemyState::idle);
            break;
        case EnemyArchetype::ground_ambush:
            ChangeState(EnemyState::ambush_dormant);
            break;
        case EnemyArchetype::wall_lurker:
            ChangeState(EnemyState::wall_hang);
            break;
        default:
            ChangeState(EnemyState::idle);
            break;
        }

        CombatDirector::Instance().RegisterEnemy(this);
    }
}

void AIControllerComponent::OnEnable() noexcept
{
    if (m_owner)
    {
        m_blackboard.spawnPosition = GetWorldPosition(m_owner);
        CombatDirector::Instance().RegisterEnemy(this);
        if (m_capsule)
        {
            m_capsule->SetCollisionLayer(CollisionLayer::Enemy, CollisionLayer::Mask::Enemy);
        }
        if (!m_animation)
        {
            m_animation = m_owner->GetComponent<AnimationComponent>();
        }
        if (m_animation)
        {
            m_animation->SetBlackboard(&m_animBlackboard);
        }
    }
}

void AIControllerComponent::OnDisable() noexcept
{
    StopMovement();
    CombatDirector::Instance().UnregisterEnemy(this);
}

void AIControllerComponent::ChangeState(const EnemyState newState) noexcept
{
    if (m_currentState == newState)
    {
        return;
    }

    // State exit logic
    if (m_currentState == EnemyState::wall_hang && m_movement)
    {
        // Restore standard gravity when detaching from wall perch
        m_movement->GetConfig().useGravity = m_initialGravity;
    }

    if (m_currentState == EnemyState::hit_react && m_health)
    {
        // Replenish poise gauge back to max upon recovering from stagger
        m_health->ResetPoise();
    }

    if (m_currentState == EnemyState::airborne_juggle)
    {
        if (m_movement)
        {
            m_movement->SetGravityScale(1.0f);
        }
        if (m_capsule)
        {
            m_capsule->SetCollisionLayer(CollisionLayer::Enemy, CollisionLayer::Mask::Enemy);
        }
        m_jugglePhase = JugglePhase::None;
        m_juggleHoldTimer = 0.0f;
    }

    // Release combat token when exiting attack states
    if (m_currentState == EnemyState::attack_windup || m_currentState == EnemyState::attack_active)
    {
        CombatDirector::Instance().ReleaseAttackToken(this);
    }

    // Immediately evict dead enemies from director slots and spatial buffers
    if (newState == EnemyState::dead)
    {
        CombatDirector::Instance().UnregisterEnemy(this);
    }

    m_currentState = newState;
    m_blackboard.stateTimer = 0.0f;

    // State entry logic
    switch (m_currentState)
    {
    case EnemyState::wall_hang:
        if (m_movement)
        {
            // Suspend gravity and halt momentum to cling securely to wall
            m_movement->GetConfig().useGravity = false;
            m_movement->HaltMomentum(1.0f);
        }
        break;

    case EnemyState::wall_drop:
        StopMovement();
        if (m_movement)
        {
            // Kill all forward scramble sprint momentum so the enemy drops straight down
            m_movement->HaltMomentum(1.0f);
            m_movement->GetConfig().useGravity = true;
            // Inject negative downward impulse for a punchy drop
            m_movement->SetVerticalVelocity(-10.0f);

            // Wall Clearance Ejection: Raycast backward to detect wall surface normal
            DirectX::XMFLOAT3 ejectDir{ 0.0f, 0.0f, 0.0f };
            auto* scene{ PhysicsManager::Instance().GetScene() };
            const DirectX::XMFLOAT3 myPos{ GetWorldPosition(m_owner) };
            const float yawRad{ DirectX::XMConvertToRadians(m_owner->transform.rotation.y) };
            const physx::PxVec3 backwardDir{ -std::sin(yawRad), 0.0f, -std::cos(yawRad) };

            if (scene)
            {
                physx::PxRaycastBuffer wallHit{};
                physx::PxQueryFilterData filterData{};
                filterData.data.word0 = CollisionLayer::WorldStatic;
                filterData.flags = physx::PxQueryFlag::eSTATIC;

                if (scene->raycast(physx::PxVec3{ myPos.x, myPos.y, myPos.z }, backwardDir, 2.5f, wallHit, physx::PxHitFlag::eNORMAL, filterData))
                {
                    ejectDir = { wallHit.block.normal.x, 0.0f, wallHit.block.normal.z };
                }
            }

            const float ejectLenSq{ (ejectDir.x * ejectDir.x) + (ejectDir.z * ejectDir.z) };
            if (ejectLenSq > 0.001f)
            {
                const float invLen{ 1.0f / std::sqrt(ejectLenSq) };
                ejectDir.x *= invLen * m_config.wallDropEjectSpeed;
                ejectDir.z *= invLen * m_config.wallDropEjectSpeed;
            }
            else
            {
                // Fallback: Eject along forward facing vector
                ejectDir = { std::sin(yawRad) * m_config.wallDropEjectSpeed, 0.0f, std::cos(yawRad) * m_config.wallDropEjectSpeed };
            }

            m_movement->AddImpulse(ejectDir);
        }
        break;

    case EnemyState::wall_scramble:
        if (m_movement)
        {
            // Enable gravity and allow movement so the capsule moves across the roof to the ledge
            m_movement->GetConfig().useGravity = true;
        }
        break;

    case EnemyState::wall_land:
        StopMovement();
        if (m_movement)
        {
            m_movement->HaltMomentum(1.0f);
            m_movement->GetConfig().useGravity = true;
        }
        if (m_animation)
        {
            m_animation->PlaySlot(Engine::Animation::AnimSlot::Landing, true);
            const float animDuration{ m_animation->GetSlotDuration(Engine::Animation::AnimSlot::Landing) };
            if (animDuration > 0.05f)
            {
                m_config.wallLandDuration = animDuration;
            }
        }
        // Re-Anchor: Relocate spawn & combat territory anchor to the landed floor coordinates
        m_blackboard.spawnPosition = GetWorldPosition(m_owner);
        m_blackboard.wanderDestination = m_blackboard.spawnPosition;
        m_blackboard.setFlag(SensoryFlag::is_leashed, false);
        m_blackboard.setFlag(SensoryFlag::is_target_beyond_leash, false);
        m_blackboard.deaggroCooldownTimer = 0.0f;
        m_blackboard.perceptionTimer = m_config.perceptionInterval; // Force immediate perception refresh
        break;

    case EnemyState::ambush_burst:
        if (m_animation)
        {
            m_animation->PlaySlot(Engine::Animation::AnimSlot::Attack_Contextual, true);
        }
        if (m_movement && m_blackboard.target)
        {
            // Inject high-speed burst impulse directly toward target in world space
            const DirectX::XMFLOAT3 currentPos{ GetWorldPosition(m_owner) };
            const DirectX::XMFLOAT3 targetPos{ GetWorldPosition(m_blackboard.target) };

            const DirectX::XMVECTOR vCurrent{ DirectX::XMLoadFloat3(&currentPos) };
            const DirectX::XMVECTOR vTarget{ DirectX::XMLoadFloat3(&targetPos) };
            DirectX::XMVECTOR vDir{ DirectX::XMVectorSubtract(vTarget, vCurrent) };
            vDir = DirectX::XMVectorSetY(vDir, 0.0f); // Ground plane only
            vDir = DirectX::XMVector3Normalize(vDir);

            DirectX::XMFLOAT3 launchDir{};
            DirectX::XMStoreFloat3(&launchDir, DirectX::XMVectorScale(vDir, 16.0f));

            // Instantly snap orientation toward player so burst lunges directly into combat
            const float targetYawDeg{ DirectX::XMConvertToDegrees(std::atan2(launchDir.x, launchDir.z)) };
            m_owner->transform.rotation.y = targetYawDeg;

            m_movement->AddImpulse(launchDir);
        }
        break;

    case EnemyState::airborne_juggle:
        StopMovement();
        if (m_capsule)
        {
            // Temporarily ignore player capsule collisions so aerial attacks don't jitter against victim
            m_capsule->SetCollisionLayer(CollisionLayer::Enemy, CollisionLayer::Mask::Enemy & ~CollisionLayer::Player);
        }
        break;

    case EnemyState::idle:
    case EnemyState::patrol:
    case EnemyState::chase:
        if (m_currentState == EnemyState::idle)
        {
            StopMovement();
        }
        if (m_animation)
        {
            m_animation->PlaySlot(Engine::Animation::AnimSlot::Locomotion);
        }
        break;

    case EnemyState::attack_windup:
        StopMovement();
        break;

    case EnemyState::attack_active:
        StopMovement();
        if (m_animation)
        {
            m_animation->PlaySlot(Engine::Animation::AnimSlot::Attack_Primary, true);
            const float animDuration{ m_animation->GetSlotDuration(Engine::Animation::AnimSlot::Attack_Primary) };
            if (animDuration > 0.05f)
            {
                m_config.attackActiveDuration = animDuration;
            }
        }
        break;

    case EnemyState::hit_react:
        StopMovement();
        if (m_movement)
        {
            m_movement->HaltMomentum(1.0f);
            m_movement->GetConfig().useGravity = true;
        }
        if (m_animation)
        {
            m_animBlackboard.actionIndex = static_cast<int>(m_hitDirectionIndex);
            m_animation->PlaySlot(Engine::Animation::AnimSlot::HitReact, true, static_cast<int>(m_hitDirectionIndex));
            const float animDuration{ m_animation->GetSlotDuration(Engine::Animation::AnimSlot::HitReact) };
            if (animDuration > 0.05f)
            {
                m_config.hitReactDuration = animDuration;
            }
        }
        // Frontal stagger faces attacker; back and lateral hits preserve orientation so flinches do not flip 180 degrees
        if (m_hitDirectionIndex == 0 && m_blackboard.target)
        {
            const DirectX::XMFLOAT3 currentPos{ GetWorldPosition(m_owner) };
            const DirectX::XMFLOAT3 targetPos{ GetWorldPosition(m_blackboard.target) };
            const float dx{ targetPos.x - currentPos.x };
            const float dz{ targetPos.z - currentPos.z };
            if ((dx * dx) + (dz * dz) > 0.01f)
            {
                m_owner->transform.rotation.y = DirectX::XMConvertToDegrees(std::atan2(dx, dz));
            }
        }
        break;

    case EnemyState::dead:
        StopMovement();
        break;

    default:
        break;
    }
}

void AIControllerComponent::AcquirePlayerTarget() noexcept
{
    if (m_blackboard.target)
    {
        return; // Already acquired
    }

    if (!m_owner)
    {
        return;
    }

    // Ascend to scene root to guarantee locating the player wherever enemies are grouped in the hierarchy
    GameObject* root{ m_owner };
    while (root->GetParent() != nullptr)
    {
        root = root->GetParent();
    }

    for (const auto& child : root->GetChildren())
    {
        if (child && child->GetComponent<PlayerControllerComponent>())
        {
            m_blackboard.target = child.get();
            break;
        }
    }
}

void AIControllerComponent::UpdatePerception(const float dt) noexcept
{
    m_blackboard.perceptionTimer += dt;
    if (m_blackboard.perceptionTimer < m_config.perceptionInterval)
    {
        return;
    }
    m_blackboard.perceptionTimer = 0.0f;

    if (!m_blackboard.target)
    {
        AcquirePlayerTarget();
        if (!m_blackboard.target)
        {
            m_blackboard.setFlag(SensoryFlag::has_line_of_sight, false);
            return;
        }
    }

    const DirectX::XMFLOAT3 myPos{ GetWorldPosition(m_owner) };
    const DirectX::XMFLOAT3 targetPos{ GetWorldPosition(m_blackboard.target) };
    m_blackboard.targetPosition = targetPos;

    const DirectX::XMVECTOR vMyPos{ DirectX::XMLoadFloat3(&myPos) };
    const DirectX::XMVECTOR vTargetPos{ DirectX::XMLoadFloat3(&targetPos) };
    const DirectX::XMVECTOR vDiff{ DirectX::XMVectorSubtract(vTargetPos, vMyPos) };

    // Compute squared distance (Tier 1 SIMD without std::sqrt)
    const DirectX::XMVECTOR vDistSq{ DirectX::XMVector3LengthSq(vDiff) };
    DirectX::XMStoreFloat(&m_blackboard.targetDistanceSq, vDistSq);

    // AAA Territory Invariant: Check if player has escaped beyond the leash boundary
    const DirectX::XMVECTOR vSpawn{ DirectX::XMLoadFloat3(&m_blackboard.spawnPosition) };
    const DirectX::XMVECTOR vTargetFromSpawn{ DirectX::XMVectorSubtract(vTargetPos, vSpawn) };
    float targetDistFromSpawnSq{ 0.0f };
    DirectX::XMStoreFloat(&targetDistFromSpawnSq, DirectX::XMVector3LengthSq(vTargetFromSpawn));
    const float leashRangeSq{ m_config.leashRange * m_config.leashRange };
    m_blackboard.setFlag(SensoryFlag::is_target_beyond_leash, targetDistFromSpawnSq > leashRangeSq);

    // Evaluate Field of View (FOV) via dot product against owner forward vector
    const float yawRad{ DirectX::XMConvertToRadians(m_owner->transform.rotation.y) };
    const DirectX::XMVECTOR vForward{ DirectX::XMVectorSet(std::sin(yawRad), 0.0f, std::cos(yawRad), 0.0f) };
    DirectX::XMVECTOR vDirFlat{ DirectX::XMVectorSetY(vDiff, 0.0f) };
    vDirFlat = DirectX::XMVector3Normalize(vDirFlat);

    float dotVal{ 0.0f };
    DirectX::XMStoreFloat(&dotVal, DirectX::XMVector3Dot(vForward, vDirFlat));

    const float cosHalfFov{ std::cos(DirectX::XMConvertToRadians(m_config.sightFovDeg * 0.5f)) };
    const bool withinSightRange{ m_blackboard.targetDistanceSq <= (m_config.sightRange * m_config.sightRange) };
    m_blackboard.setFlag(SensoryFlag::is_target_in_fov, withinSightRange && (dotVal >= cosHalfFov));

    // PhysX raycast to verify unobstructed line of sight against static environment
    auto* scene{ PhysicsManager::Instance().GetScene() };
    if (!scene)
    {
        return;
    }

    const float eyeHeight{ m_capsule ? (m_capsule->GetTotalHalfHeight() * 1.5f) : 1.6f };
    const physx::PxVec3 rayOrigin{ myPos.x, myPos.y + eyeHeight, myPos.z };

    DirectX::XMFLOAT3 dirNorm{};
    DirectX::XMStoreFloat3(&dirNorm, DirectX::XMVector3Normalize(vDiff));
    const physx::PxVec3 rayDir{ dirNorm.x, dirNorm.y, dirNorm.z };

    const float distance{ std::sqrt(m_blackboard.targetDistanceSq) };

    physx::PxRaycastBuffer hitBuffer{};
    physx::PxQueryFilterData filterData{};
    filterData.data.word0 = CollisionLayer::WorldStatic;
    filterData.flags = physx::PxQueryFlag::eSTATIC;

    const bool isBlocked{ scene->raycast(rayOrigin, rayDir, distance, hitBuffer, physx::PxHitFlag::eDEFAULT, filterData) };

    // Line-of-sight is purely geometric (unobstructed raycast), allowing wide cover detection across territory
    m_blackboard.setFlag(SensoryFlag::has_line_of_sight, !isBlocked);

    // Dynamic Blind-Spot Occlusion Update (Runs whenever dormant, even if player is across the arena)
    if (m_config.archetype == EnemyArchetype::ground_ambush && m_currentState == EnemyState::ambush_dormant)
    {
        UpdateAmbushCover(dt);
    }
}

void AIControllerComponent::SteerTowards(const DirectX::XMFLOAT3& targetWorldPos, const float dt, const DirectX::XMFLOAT3* lookAtWorldPos) noexcept
{
    if (!m_movement || !m_owner)
    {
        return;
    }

    const DirectX::XMFLOAT3 currentPos{ GetWorldPosition(m_owner) };
    const DirectX::XMVECTOR vCurrent{ DirectX::XMLoadFloat3(&currentPos) };
    const DirectX::XMVECTOR vTarget{ DirectX::XMLoadFloat3(&targetWorldPos) };

    DirectX::XMVECTOR vDir{ DirectX::XMVectorSubtract(vTarget, vCurrent) };
    vDir = DirectX::XMVectorSetY(vDir, 0.0f);

    const DirectX::XMVECTOR vLengthSq{ DirectX::XMVector3LengthSq(vDir) };
    float lenSq{ 0.0f };
    DirectX::XMStoreFloat(&lenSq, vLengthSq);

    // Arrival threshold: once within 40cm of the designated slot, settle into slot
    constexpr float arrivalThresholdSq{ 0.40f * 0.40f };

    float targetYawDeg{ m_owner->transform.rotation.y };
    bool hasTargetYaw{ false };

    if (lenSq > arrivalThresholdSq)
    {
        const float invLen{ 1.0f / std::sqrt(lenSq) };
        DirectX::XMFLOAT3 normDir{
            (targetWorldPos.x - currentPos.x) * invLen,
            0.0f,
            (targetWorldPos.z - currentPos.z) * invLen
        };

        // Smooth orbital steering: prevent capsule pathing from piercing through player space
        if (lookAtWorldPos)
        {
            const float toPlayerX{ lookAtWorldPos->x - currentPos.x };
            const float toPlayerZ{ lookAtWorldPos->z - currentPos.z };
            const float distToPlayerSq{ (toPlayerX * toPlayerX) + (toPlayerZ * toPlayerZ) };

            // Clearance boundary: keeps attackers from pushing inside 1.85m of player center
            constexpr float playerAvoidRadius{ 1.85f };
            constexpr float playerAvoidRadiusSq{ playerAvoidRadius * playerAvoidRadius };

            const float toSlotX{ targetWorldPos.x - currentPos.x };
            const float toSlotZ{ targetWorldPos.z - currentPos.z };
            const float projectionT{ ((toPlayerX * toSlotX) + (toPlayerZ * toSlotZ)) / lenSq };

            // Evaluates if the direct navigation vector slices through the player
            if (projectionT > 0.0f)
            {
                const float clampedT{ (std::min)(projectionT, 1.0f) };
                const float closestPointX{ currentPos.x + (toSlotX * clampedT) };
                const float closestPointZ{ currentPos.z + (toSlotZ * clampedT) };
                const float dToPlayerX{ closestPointX - lookAtWorldPos->x };
                const float dToPlayerZ{ closestPointZ - lookAtWorldPos->z };
                const float closestDistSq{ (dToPlayerX * dToPlayerX) + (dToPlayerZ * dToPlayerZ) };

                if (closestDistSq < playerAvoidRadiusSq && distToPlayerSq > 0.001f)
                {
                    const float distToPlayer{ std::sqrt(distToPlayerSq) };
                    const float radialX{ -toPlayerX / distToPlayer };
                    const float radialZ{ -toPlayerZ / distToPlayer };

                    // Perpendicular tangential vectors around player
                    const float tang1X{ -radialZ };
                    const float tang1Z{ radialX };
                    const float tang2X{ radialZ };
                    const float tang2Z{ -radialX };

                    // Select the tangent advancing shortest toward the target slot
                    const float dot1{ (tang1X * toSlotX) + (tang1Z * toSlotZ) };
                    const float dot2{ (tang2X * toSlotX) + (tang2Z * toSlotZ) };
                    const float chosenTangX{ (dot1 >= dot2) ? tang1X : tang2X };
                    const float chosenTangZ{ (dot1 >= dot2) ? tang1Z : tang2Z };

                    const float closestDist{ std::sqrt(closestDistSq) };
                    const float avoidBlend{ std::clamp(1.0f - (closestDist / playerAvoidRadius), 0.45f, 1.0f) };
                    const float radialPush{ (distToPlayer < playerAvoidRadius) ? (1.0f - (distToPlayer / playerAvoidRadius)) : 0.0f };

                    normDir.x = (normDir.x * (1.0f - avoidBlend)) + (chosenTangX * avoidBlend) + (radialX * radialPush * 0.8f);
                    normDir.z = (normDir.z * (1.0f - avoidBlend)) + (chosenTangZ * avoidBlend) + (radialZ * radialPush * 0.8f);
                }
            }
        }

        // Swarm Boid Anti-Stacking: Blend lateral repulsion away from immediate neighbors
        const DirectX::XMFLOAT3 repForce{ CombatDirector::Instance().ComputeSeparationForce(this) };
        normDir.x += repForce.x * 0.75f;
        normDir.z += repForce.z * 0.75f;

        const float finalLenSq{ (normDir.x * normDir.x) + (normDir.z * normDir.z) };
        if (finalLenSq > 0.001f)
        {
            const float invLenFinal{ 1.0f / std::sqrt(finalLenSq) };
            normDir.x *= invLenFinal;
            normDir.z *= invLenFinal;
        }

        // Drive kinematic motor in world space (strafing occurs naturally when facing differs from normDir)
        m_movement->SetDesiredDirection({ normDir.x, normDir.z });

        // Active locomotion to slot: body faces the actual travel heading
        targetYawDeg = DirectX::XMConvertToDegrees(std::atan2(normDir.x, normDir.z));
        hasTargetYaw = true;
    }
    else
    {
        // At slot position: check if neighboring enemies crowd the personal bubble or player encroaches
        const DirectX::XMFLOAT3 repForce{ CombatDirector::Instance().ComputeSeparationForce(this) };
        float repX{ repForce.x * 0.5f };
        float repZ{ repForce.z * 0.5f };

        // Gently yield backward if player moves directly into slot occupant
        if (lookAtWorldPos)
        {
            const float dxP{ currentPos.x - lookAtWorldPos->x };
            const float dzP{ currentPos.z - lookAtWorldPos->z };
            const float dPlayerSq{ (dxP * dxP) + (dzP * dzP) };
            constexpr float minPlayerBufferSq{ 1.6f * 1.6f };
            if (dPlayerSq < minPlayerBufferSq && dPlayerSq > 0.001f)
            {
                const float invD{ 1.0f / std::sqrt(dPlayerSq) };
                repX += dxP * invD * 0.6f;
                repZ += dzP * invD * 0.6f;
            }
        }

        const float repLenSq{ (repX * repX) + (repZ * repZ) };
        if (repLenSq > 0.01f)
        {
            m_movement->SetDesiredDirection({ repX, repZ });
        }
        else
        {
            StopMovement();
        }

        // Settled in slot: orient body to face combat target
        if (lookAtWorldPos)
        {
            const float dxLook{ lookAtWorldPos->x - currentPos.x };
            const float dzLook{ lookAtWorldPos->z - currentPos.z };
            if ((dxLook * dxLook) + (dzLook * dzLook) > 0.01f)
            {
                targetYawDeg = DirectX::XMConvertToDegrees(std::atan2(dxLook, dzLook));
                hasTargetYaw = true;
            }
        }
    }

    if (hasTargetYaw)
    {
        float deltaYaw{ targetYawDeg - m_owner->transform.rotation.y };
        while (deltaYaw > 180.0f) deltaYaw -= 360.0f;
        while (deltaYaw < -180.0f) deltaYaw += 360.0f;

        const float maxStep{ m_config.turnSpeedDeg * dt };
        const float step{ std::clamp(deltaYaw, -maxStep, maxStep) };
        m_owner->transform.rotation.y += step;

        while (m_owner->transform.rotation.y > 360.0f) m_owner->transform.rotation.y -= 360.0f;
        while (m_owner->transform.rotation.y < -360.0f) m_owner->transform.rotation.y += 360.0f;
    }
}

void AIControllerComponent::StopMovement() noexcept
{
    if (m_movement)
    {
        m_movement->SetDesiredDirection({ 0.0f, 0.0f });
    }
}

void AIControllerComponent::NotifyHitReaction() noexcept
{
    // Fast-fail: immediately transition lethal blows to dead, freeing director slots
    if (m_currentState == EnemyState::dead || (m_health && m_health->IsDead()))
    {
        ChangeState(EnemyState::dead);
        return;
    }

    // If airborne, maintain juggle instead of snapping to grounded hit_react
    if (m_currentState == EnemyState::airborne_juggle)
    {
        NotifyAirJuggle(0.06f);
        return;
    }

    if (!m_blackboard.target)
    {
        AcquirePlayerTarget();
    }
    const DirectX::XMFLOAT3 attackerPos{ m_blackboard.target ? GetWorldPosition(m_blackboard.target) : DirectX::XMFLOAT3{ 0.0f, 0.0f, 0.0f } };
    m_hitDirectionIndex = CalculateHitDirectionIndex(m_owner, attackerPos);

    // Re-trigger stagger and reset cooldowns if already in hit reaction (ensures multi-hit combo responsiveness)
    if (m_currentState == EnemyState::hit_react)
    {
        m_blackboard.stateTimer = 0.0f;
        StopMovement();
        if (m_movement)
        {
            m_movement->HaltMomentum(1.0f);
        }
        if (m_health)
        {
            m_health->ResetPoise();
        }
        if (m_animation)
        {
            m_animBlackboard.actionIndex = static_cast<int>(m_hitDirectionIndex);
            m_animation->PlaySlot(Engine::Animation::AnimSlot::HitReact, true, static_cast<int>(m_hitDirectionIndex));
            const float animDuration{ m_animation->GetSlotDuration(Engine::Animation::AnimSlot::HitReact) };
            if (animDuration > 0.05f)
            {
                m_config.hitReactDuration = animDuration;
            }
        }
        if (m_hitDirectionIndex == 0 && m_blackboard.target)
        {
            const DirectX::XMFLOAT3 currentPos{ GetWorldPosition(m_owner) };
            const float dx{ attackerPos.x - currentPos.x };
            const float dz{ attackerPos.z - currentPos.z };
            if ((dx * dx) + (dz * dz) > 0.01f)
            {
                m_owner->transform.rotation.y = DirectX::XMConvertToDegrees(std::atan2(dx, dz));
            }
        }
        return;
    }

    ChangeState(EnemyState::hit_react);
}

void AIControllerComponent::NotifyLaunch(const float launchVelocityY) noexcept
{
    if (m_currentState == EnemyState::dead || (m_health && m_health->IsDead()))
    {
        ChangeState(EnemyState::dead);
        return;
    }

    m_jugglePhase = JugglePhase::Ascent;
    m_juggleHoldTimer = 0.0f;
    m_recoveryTimer = 0.0f;

    if (m_currentState != EnemyState::airborne_juggle)
    {
        ChangeState(EnemyState::airborne_juggle);
    }

    if (m_animation)
    {
        m_animBlackboard.actionIndex = 0; // Hit_To_Air.FBX
        m_animation->PlaySlot(Engine::Animation::AnimSlot::AirTraversal, true, 0);
    }

    if (m_movement)
    {
        // Ascend under standard 1.0x gravity to match the player's lift arc identically
        m_movement->SetGravityScale(1.0f);
        m_movement->SetVerticalVelocity(launchVelocityY);
    }
}

void AIControllerComponent::NotifyAirJuggle(const float hitStopDuration, const bool isFinisher, const float finisherFollowThrough) noexcept
{
    if (m_currentState == EnemyState::dead || (m_health && m_health->IsDead()))
    {
        ChangeState(EnemyState::dead);
        return;
    }

    if (!m_blackboard.target)
    {
        AcquirePlayerTarget();
    }
    const DirectX::XMFLOAT3 attackerPos{ m_blackboard.target ? GetWorldPosition(m_blackboard.target) : DirectX::XMFLOAT3{ 0.0f, 0.0f, 0.0f } };
    const std::uint8_t hitDir{ CalculateHitDirectionIndex(m_owner, attackerPos) };

    if (m_currentState != EnemyState::airborne_juggle)
    {
        m_jugglePhase = isFinisher ? JugglePhase::FinisherDrop : JugglePhase::FloatHold;
        ChangeState(EnemyState::airborne_juggle);
    }

    if (isFinisher)
    {
        m_jugglePhase = JugglePhase::FinisherDrop;
        m_juggleHoldTimer = finisherFollowThrough;

        // Finisher Hit: Play Hit_Air_To_Floor_Start (Node 5 of AirTraversal)
        if (m_animation)
        {
            m_animBlackboard.actionIndex = 5;
            m_animation->PlaySlot(Engine::Animation::AnimSlot::AirTraversal, true, 5);
            m_animation->ApplyHitStop(hitStopDuration);
        }

        if (m_movement)
        {
            m_movement->SetVerticalVelocity(0.0f);
            m_movement->HaltMomentum(1.0f);
            m_movement->SetGravityScale(0.0f);
        }
    }
    else
    {
        m_jugglePhase = JugglePhase::FloatHold;
        m_juggleHoldTimer = 0.85f;

        // Directional Air Hit: Node 1: Hit_Air_F, Node 2: Hit_Air_B, Node 3: Hit_Air_L, Node 4: Hit_Air_R
        const int airHitNode{ 1 + static_cast<int>(hitDir) };

        if (m_animation)
        {
            m_animBlackboard.actionIndex = airHitNode;
            m_animation->PlaySlot(Engine::Animation::AnimSlot::AirTraversal, true, airHitNode);
            m_animation->ApplyHitStop(hitStopDuration);
        }

        if (m_movement)
        {
            // Complete Zero-G Lock: eliminate all vertical drop during air combos
            m_movement->SetVerticalVelocity(0.0f);
            m_movement->HaltMomentum(1.0f);
            m_movement->SetGravityScale(0.0f);
        }
    }
}

void AIControllerComponent::NotifySpike(const float downwardVelocityY) noexcept
{
    if (m_currentState == EnemyState::dead || (m_health && m_health->IsDead()))
    {
        ChangeState(EnemyState::dead);
        return;
    }

    m_jugglePhase = JugglePhase::FinisherDrop;
    m_juggleHoldTimer = 0.0f; // Immediately drop

    if (m_currentState != EnemyState::airborne_juggle)
    {
        ChangeState(EnemyState::airborne_juggle);
    }

    if (m_animation)
    {
        m_animBlackboard.actionIndex = 5; // Hit_Air_To_Floor_Start
        m_animation->PlaySlot(Engine::Animation::AnimSlot::AirTraversal, true, 5);
    }

    if (m_movement)
    {
        m_movement->SetGravityScale(1.0f);
        m_movement->SetVerticalVelocity(downwardVelocityY);
        m_movement->HaltMomentum(1.0f);
    }
}

void AIControllerComponent::NotifyPlungeSlam() noexcept
{
    if (m_currentState == EnemyState::dead || (m_health && m_health->IsDead()))
    {
        ChangeState(EnemyState::dead);
        return;
    }

    m_jugglePhase = JugglePhase::PlungeImpact;
    m_juggleHoldTimer = 0.0f;

    if (m_currentState != EnemyState::airborne_juggle)
    {
        ChangeState(EnemyState::airborne_juggle);
    }

    StopMovement();
    if (m_movement)
    {
        m_movement->HaltMomentum(1.0f);
        m_movement->SetVerticalVelocity(0.0f);
        m_movement->SetGravityScale(1.0f);
    }

    // Play Target (Node 2 of Landing)
    if (m_animation)
    {
        m_animBlackboard.actionIndex = 2;
        m_animation->PlaySlot(Engine::Animation::AnimSlot::Landing, true, 2);
        m_recoveryTimer = m_animation->GetSlotDuration(Engine::Animation::AnimSlot::Landing);
        if (m_recoveryTimer <= 0.05f)
        {
            m_recoveryTimer = 0.8f;
        }
    }
}

void AIControllerComponent::TickAirborneJuggle(const float dt) noexcept
{
    StopMovement();
    if (!m_movement || !m_capsule) return;

    m_blackboard.stateTimer += dt;

    switch (m_jugglePhase)
    {
    case JugglePhase::Ascent:
        // Transition to Apex Float: as soon as upward launch velocity crosses zero
        if (m_movement->GetVerticalVelocity() <= 0.1f)
        {
            m_jugglePhase = JugglePhase::FloatHold;
            m_movement->SetVerticalVelocity(0.0f);
            m_movement->SetGravityScale(0.0f); // Freeze gravity at apex
            m_juggleHoldTimer = 0.85f;          // Wait up to 0.85s for player's air combo

            if (m_animation)
            {
                m_animBlackboard.actionIndex = 1; // Settle into Hit_Air_F
                m_animation->PlaySlot(Engine::Animation::AnimSlot::AirTraversal, true, 1);
            }
        }
        break;

    case JugglePhase::FloatHold:
        if (m_juggleHoldTimer > 0.0f)
        {
            m_juggleHoldTimer -= dt;
            m_movement->SetVerticalVelocity(0.0f);
            m_movement->SetGravityScale(0.0f);

            // Hold window expired (player stopped attacking): fall to floor
            if (m_juggleHoldTimer <= 0.0f)
            {
                m_jugglePhase = JugglePhase::FallLoop;
                m_movement->SetGravityScale(1.0f);

                if (m_animation)
                {
                    m_animBlackboard.actionIndex = 6; // Hit_Air_To_Floor_Loop
                    m_animation->PlaySlot(Engine::Animation::AnimSlot::AirTraversal, true, 6);
                }
            }
        }
        break;

    case JugglePhase::FinisherDrop:
        if (m_juggleHoldTimer > 0.0f)
        {
            m_juggleHoldTimer -= dt;
            m_movement->SetVerticalVelocity(0.0f);
            m_movement->SetGravityScale(0.0f);

            if (m_juggleHoldTimer <= 0.0f)
            {
                m_movement->SetGravityScale(1.0f);
                m_movement->SetVerticalVelocity(-18.0f); // Downward slam impulse

                if (m_animation)
                {
                    m_animBlackboard.actionIndex = 6; // Hit_Air_To_Floor_Loop
                    m_animation->PlaySlot(Engine::Animation::AnimSlot::AirTraversal, true, 6);
                }
            }
        }
        else
        {
            // Detect floor impact from finisher drop
            if (m_capsule->IsGrounded() && m_movement->GetVerticalVelocity() <= 0.05f)
            {
                m_jugglePhase = JugglePhase::ImpactLand;
                m_movement->HaltMomentum(1.0f);
                m_movement->SetVerticalVelocity(0.0f);

                if (m_animation)
                {
                    m_animBlackboard.actionIndex = 0; // Hit_Air_To_Floor_End.FBX
                    m_animation->PlaySlot(Engine::Animation::AnimSlot::Landing, true, 0);
                    m_recoveryTimer = m_animation->GetSlotDuration(Engine::Animation::AnimSlot::Landing);
                    if (m_recoveryTimer <= 0.05f) m_recoveryTimer = 0.6f;
                }
            }
        }
        break;

    case JugglePhase::FallLoop:
        m_movement->SetGravityScale(1.0f);
        if (m_capsule->IsGrounded() && m_movement->GetVerticalVelocity() <= 0.05f)
        {
            m_jugglePhase = JugglePhase::ImpactLand;
            m_movement->HaltMomentum(1.0f);
            m_movement->SetVerticalVelocity(0.0f);

            if (m_animation)
            {
                m_animBlackboard.actionIndex = 0; // Hit_Air_To_Floor_End
                m_animation->PlaySlot(Engine::Animation::AnimSlot::Landing, true, 0);
                m_recoveryTimer = m_animation->GetSlotDuration(Engine::Animation::AnimSlot::Landing);
                if (m_recoveryTimer <= 0.05f) m_recoveryTimer = 0.6f;
            }
        }
        break;

    case JugglePhase::ImpactLand:
    case JugglePhase::PlungeImpact:
        m_movement->HaltMomentum(1.0f);
        m_movement->SetVerticalVelocity(0.0f);
        m_recoveryTimer -= dt;

        // Floor laying animation concluded: transition to standing recovery pose
        if (m_recoveryTimer <= 0.0f || (m_animation && m_animation->GetCurrentPhase() >= 0.99f))
        {
            m_jugglePhase = JugglePhase::GetUp;
            if (m_animation)
            {
                m_animBlackboard.actionIndex = 1; // Get_up.FBX
                m_animation->PlaySlot(Engine::Animation::AnimSlot::Landing, true, 1);
                m_recoveryTimer = m_animation->GetSlotDuration(Engine::Animation::AnimSlot::Landing);
                if (m_recoveryTimer <= 0.05f) m_recoveryTimer = 1.0f;
            }
        }
        break;

    case JugglePhase::GetUp:
        m_movement->HaltMomentum(1.0f);
        m_movement->SetVerticalVelocity(0.0f);
        m_recoveryTimer -= dt;

        // Get_up concluded: recovery complete, re-engage combat pursuit
        if (m_recoveryTimer <= 0.0f || (m_animation && m_animation->GetCurrentPhase() >= 0.99f))
        {
            m_jugglePhase = JugglePhase::None;
            if (m_health)
            {
                m_health->ResetPoise();
            }
            m_blackboard.perceptionTimer = m_config.perceptionInterval;
            ChangeState(EnemyState::chase);
        }
        break;

    default:
        break;
    }
}

void AIControllerComponent::TickGrunt(const float dt) noexcept
{
    const bool hasLos{ m_blackboard.getFlag(SensoryFlag::has_line_of_sight) };
    const bool inFov{ m_blackboard.getFlag(SensoryFlag::is_target_in_fov) };
    const float attackRangeSq{ m_config.attackRange * m_config.attackRange };

    // AAA Combat Gate: Block chase transitions if leashed, de-aggroed, or player fled past boundary
    const bool isLeashed{ m_blackboard.getFlag(SensoryFlag::is_leashed) };
    const bool isDeaggroActive{ m_blackboard.deaggroCooldownTimer > 0.0f };
    const bool isTargetPastLeash{ m_blackboard.getFlag(SensoryFlag::is_target_beyond_leash) };
    const bool canAggro{ !isLeashed && !isDeaggroActive && !isTargetPastLeash };

    switch (m_currentState)
    {
    case EnemyState::idle:
        StopMovement();
        if (canAggro && hasLos && inFov)
        {
            ChangeState(EnemyState::chase);
            break;
        }

        m_blackboard.stateTimer += dt;
        if (m_blackboard.stateTimer >= m_config.idleDuration)
        {
            // Pick random wander destination within wanderRadius around spawn position
            const float angle{ Random::Get<float>(0.0f, DirectX::XM_2PI) };
            const float maxDist{ (std::max)(0.5f, m_config.wanderRadius) };
            const float distance{ Random::Get<float>(0.5f, maxDist) };

            m_blackboard.wanderDestination = {
                m_blackboard.spawnPosition.x + (std::sin(angle) * distance),
                m_blackboard.spawnPosition.y,
                m_blackboard.spawnPosition.z + (std::cos(angle) * distance)
            };

            ChangeState(EnemyState::patrol);
        }
        break;

    case EnemyState::patrol:
        if (canAggro && hasLos && inFov)
        {
            ChangeState(EnemyState::chase);
            break;
        }

        m_blackboard.stateTimer += dt;
        SteerTowards(m_blackboard.wanderDestination, dt);

        // Check if reached destination or timed out
        {
            const DirectX::XMFLOAT3 currentPos{ m_owner->transform.position };
            const float dx{ m_blackboard.wanderDestination.x - currentPos.x };
            const float dz{ m_blackboard.wanderDestination.z - currentPos.z };
            const float distSq{ (dx * dx) + (dz * dz) };

            constexpr float arrivalThresholdSq{ 0.4f * 0.4f }; // Within 40 cm
            if (distSq <= arrivalThresholdSq || m_blackboard.stateTimer >= m_config.patrolTimeout)
            {
                ChangeState(EnemyState::idle);
            }
        }
        break;

    case EnemyState::chase:
        if (!hasLos)
        {
            // Lost sight; stand-down in idle
            ChangeState(EnemyState::idle);
            break;
        }

        // Swarm Arbiter: Request token from CombatDirector before triggering attack
        if (m_blackboard.targetDistanceSq <= attackRangeSq)
        {
            if (m_blackboard.attackCooldownTimer <= 0.0f && CombatDirector::Instance().RequestAttackToken(this))
            {
                ChangeState(EnemyState::attack_windup);
            }
            else
            {
                // In melee range but awaiting attack token: strafe smoothly around slot while facing player
                const DirectX::XMFLOAT3 slotPos{ CombatDirector::Instance().GetAssignedSlotPosition(this) };
                if (slotPos.x != 0.0f || slotPos.z != 0.0f)
                {
                    SteerTowards(slotPos, dt, &m_blackboard.targetPosition);
                }
                else
                {
                    StopMovement();
                }
            }
        }
        else
        {
            // Swarm Slot Navigation: Steer toward designated ring slot while continuously facing player
            const DirectX::XMFLOAT3 slotPos{ CombatDirector::Instance().GetAssignedSlotPosition(this) };
            if (slotPos.x != 0.0f || slotPos.z != 0.0f)
            {
                SteerTowards(slotPos, dt, &m_blackboard.targetPosition);
            }
            else
            {
                SteerTowards(m_blackboard.targetPosition, dt, &m_blackboard.targetPosition);
            }
        }
        break;

    case EnemyState::attack_windup:
        m_blackboard.stateTimer += dt;
        StopMovement();
        if (m_blackboard.target)
        {
            // Smoothly track target during windup so attack aligns cleanly to player
            const DirectX::XMFLOAT3 currentPos{ GetWorldPosition(m_owner) };
            const DirectX::XMFLOAT3 targetPos{ GetWorldPosition(m_blackboard.target) };
            const float dx{ targetPos.x - currentPos.x };
            const float dz{ targetPos.z - currentPos.z };
            if ((dx * dx) + (dz * dz) > 0.01f)
            {
                const float targetYawDeg{ DirectX::XMConvertToDegrees(std::atan2(dx, dz)) };
                float deltaYaw{ targetYawDeg - m_owner->transform.rotation.y };
                while (deltaYaw > 180.0f) deltaYaw -= 360.0f;
                while (deltaYaw < -180.0f) deltaYaw += 360.0f;
                const float maxStep{ m_config.turnSpeedDeg * dt };
                m_owner->transform.rotation.y += std::clamp(deltaYaw, -maxStep, maxStep);
            }
        }
        if (m_blackboard.stateTimer >= m_config.attackWindupDuration)
        {
            ChangeState(EnemyState::attack_active);
        }
        break;

    case EnemyState::attack_active:
        m_blackboard.stateTimer += dt;
        StopMovement();
        if (m_blackboard.stateTimer >= m_config.attackActiveDuration)
        {
            m_blackboard.attackCooldownTimer = m_config.attackCooldownDuration;
            ChangeState(EnemyState::chase);
        }
        break;

    case EnemyState::hit_react:
        m_blackboard.stateTimer += dt;
        StopMovement();
        if (m_blackboard.stateTimer >= m_config.hitReactDuration)
        {
            ChangeState(EnemyState::chase);
        }
        break;

    default:
        break;
    }
}

void AIControllerComponent::TickGroundAmbush(const float dt) noexcept
{
    const float ambushRangeSq{ m_config.ambushTriggerRadius * m_config.ambushTriggerRadius };
    const float territoryRangeSq{ m_config.leashRange * m_config.leashRange };

    switch (m_currentState)
    {
    case EnemyState::ambush_dormant:
    {
        if (!m_blackboard.target)
        {
            StopMovement();
            break;
        }

        const DirectX::XMFLOAT3 currentPos{ GetWorldPosition(m_owner) };
        const DirectX::XMFLOAT3 playerPos{ GetWorldPosition(m_blackboard.target) };
        const bool hasLos{ m_blackboard.getFlag(SensoryFlag::has_line_of_sight) };
        const bool insideTerritory{ m_blackboard.targetDistanceSq <= territoryRangeSq };

        // Evaluate Player's facing orientation relative to the enemy
        const float playerYawRad{ DirectX::XMConvertToRadians(m_blackboard.target->transform.rotation.y) };
        const DirectX::XMVECTOR vPlayerFwd{ DirectX::XMVectorSet(std::sin(playerYawRad), 0.0f, std::cos(playerYawRad), 0.0f) };

        DirectX::XMVECTOR vToEnemy{ DirectX::XMVectorSet(currentPos.x - playerPos.x, 0.0f, currentPos.z - playerPos.z, 0.0f) };
        vToEnemy = DirectX::XMVector3Normalize(vToEnemy);

        float playerLookDot{ 0.0f };
        DirectX::XMStoreFloat(&playerLookDot, DirectX::XMVector3Dot(vPlayerFwd, vToEnemy));

        // TRIGGER 1: Proximity Trap (Player steps into the pillar gap/corridor)
        const bool playerInProximity{ m_blackboard.targetDistanceSq <= ambushRangeSq };

        // TRIGGER 2: Cover Blown / Spotted (Player looks directly at the hiding enemy with clear LOS)
        const bool coverBlown{ hasLos && insideTerritory && (playerLookDot > 0.35f) };

        // TRIGGER 3: Backstab Stalk (Player bypassed the pillar line and turned their back with clear LOS)
        const bool backstabOpportunity{ hasLos && insideTerritory && (playerLookDot < -0.1f) };

        if (playerInProximity || coverBlown || backstabOpportunity)
        {
            ChangeState(EnemyState::ambush_burst);
            break;
        }

        // Blind-Spot Repositioning: Stealthily creep behind the active cover obstacle
        const float dx{ m_blackboard.wanderDestination.x - currentPos.x };
        const float dz{ m_blackboard.wanderDestination.z - currentPos.z };
        const float distToHideSq{ (dx * dx) + (dz * dz) };

        // Threshold of 35 cm: creep toward hide point; once there, stop and orient toward flank
        if (distToHideSq > (0.35f * 0.35f))
        {
            SteerTowards(m_blackboard.wanderDestination, dt);
        }
        else
        {
            StopMovement();

            // Face toward anticipated player approach angle so enemy is primed to burst
            const float dxT{ playerPos.x - currentPos.x };
            const float dzT{ playerPos.z - currentPos.z };
            if ((dxT * dxT) + (dzT * dzT) > 0.01f)
            {
                const float targetYawDeg{ DirectX::XMConvertToDegrees(std::atan2(dxT, dzT)) };
                float deltaYaw{ targetYawDeg - m_owner->transform.rotation.y };
                while (deltaYaw > 180.0f) deltaYaw -= 360.0f;
                while (deltaYaw < -180.0f) deltaYaw += 360.0f;
                m_owner->transform.rotation.y += std::clamp(deltaYaw, -m_config.turnSpeedDeg * dt, m_config.turnSpeedDeg * dt);
            }
        }
        break;
    }

    case EnemyState::ambush_burst:
        m_blackboard.stateTimer += dt;
        // Allow the launch impulse to carry the enemy forward out from cover before transitioning to chase
        if (m_blackboard.stateTimer >= 0.4f)
        {
            ChangeState(EnemyState::chase);
        }
        break;

        // Once combat begins, delegate ALL states (including Idle/Patrol/Leash) to TickGrunt
    case EnemyState::idle:
    case EnemyState::patrol:
    case EnemyState::chase:
    case EnemyState::attack_windup:
    case EnemyState::attack_active:
    case EnemyState::hit_react:
        TickGrunt(dt);
        break;

    default:
        break;
    }
}

void AIControllerComponent::TickWallLurker(const float dt) noexcept
{
    switch (m_currentState)
    {
    case EnemyState::wall_hang:
    {
        if (!m_blackboard.target)
        {
            AcquirePlayerTarget();
            if (!m_blackboard.target) break;
        }

        const DirectX::XMFLOAT3 lurkerPos{ GetWorldPosition(m_owner) };
        const DirectX::XMFLOAT3 playerPos{ GetWorldPosition(m_blackboard.target) };

        // Calculate target drop point on the ledge
        const DirectX::XMFLOAT3 dropLedgePos{
            m_blackboard.spawnPosition.x + m_config.dropLedgeOffset.x,
            m_blackboard.spawnPosition.y + m_config.dropLedgeOffset.y,
            m_blackboard.spawnPosition.z + m_config.dropLedgeOffset.z
        };

        // Ceiling/Roof Invariant: Reject drops if player is on the roof or above the lurker
        const float heightDiff{ dropLedgePos.y - playerPos.y };
        if (heightDiff < 1.5f)
        {
            break;
        }

        // Raycast down from the drop ledge to WorldStatic to determine ground impact elevation
        auto* scene{ PhysicsManager::Instance().GetScene() };
        float floorY{ playerPos.y };
        if (scene)
        {
            physx::PxRaycastBuffer floorHit{};
            physx::PxQueryFilterData filterData{};
            filterData.data.word0 = CollisionLayer::WorldStatic;
            filterData.flags = physx::PxQueryFlag::eSTATIC;
            const physx::PxVec3 rayOrigin{ dropLedgePos.x, dropLedgePos.y, dropLedgePos.z };
            const physx::PxVec3 rayDir{ 0.0f, -1.0f, 0.0f };
            if (scene->raycast(rayOrigin, rayDir, 60.0f, floorHit, physx::PxHitFlag::eDEFAULT, filterData))
            {
                floorY = floorHit.block.position.y;
            }
        }

        // Scramble traversal time from hiding spot to ledge
        const float dxRoof{ dropLedgePos.x - lurkerPos.x };
        const float dzRoof{ dropLedgePos.z - lurkerPos.z };
        const float distRoof{ std::sqrt((dxRoof * dxRoof) + (dzRoof * dzRoof)) };
        const float scrambleSpeed{ m_movement ? m_movement->GetConfig().maxRunSpeed : 8.0f };
        const float tScramble{ distRoof / (scrambleSpeed > 0.1f ? scrambleSpeed : 8.0f) };

        const float dropHeight{ (std::max)(1.5f, dropLedgePos.y - floorY) };
        constexpr float v0y{ 10.0f }; // Matches wall_drop initial velocity
        constexpr float g{ 9.81f };

        // Analytic Kinematic Time-to-Floor from ledge
        const float tDrop{ (-v0y + std::sqrt((v0y * v0y) + (2.0f * g * dropHeight))) / g };
        const float tTotal{ tScramble + tDrop };

        // Pull live player velocity directly from CharacterMovementComponent
        auto* playerMotor{ m_blackboard.target->GetComponent<CharacterMovementComponent>() };
        DirectX::XMFLOAT3 playerVel{ 0.0f, 0.0f, 0.0f };
        if (playerMotor)
        {
            playerVel = playerMotor->GetTotalVelocity();
        }

        const float dxToLedge{ dropLedgePos.x - playerPos.x };
        const float dzToLedge{ dropLedgePos.z - playerPos.z };
        const float horizDistSq{ (dxToLedge * dxToLedge) + (dzToLedge * dzToLedge) };
        const float horizDist{ std::sqrt(horizDistSq) };

        // Point-blank fallback: if player is directly underneath the drop ledge footprint, trigger immediately
        const bool isPointBlank{ horizDist <= m_config.wallDropTriggerRadius };

        if (!isPointBlank)
        {
            // Ambient Combat Suppression. Discard trigger if player is still fighting previous enemies nearby
            bool isNearbyCombatActive{ false };
            for (const auto* otherEnemy : CombatDirector::Instance().GetEnemies())
            {
                if (!otherEnemy || otherEnemy == this || !otherEnemy->GetOwner()) continue;
                const EnemyState otherSt{ otherEnemy->GetCurrentState() };
                if (otherSt == EnemyState::chase || otherSt == EnemyState::attack_windup ||
                    otherSt == EnemyState::attack_active || otherSt == EnemyState::hit_react ||
                    otherSt == EnemyState::airborne_juggle)
                {
                    const DirectX::XMFLOAT3 otherPos{ GetWorldPosition(otherEnemy->GetOwner()) };
                    const float odx{ otherPos.x - playerPos.x };
                    const float odz{ otherPos.z - playerPos.z };
                    if ((odx * odx) + (odz * odz) < (12.0f * 12.0f))
                    {
                        isNearbyCombatActive = true;
                        break;
                    }
                }
            }
            if (isNearbyCombatActive)
            {
                break;
            }

            // Player Melee Action Suppression. Prevent premature drops caused by attack lunges
            const auto* playerCtrl{ m_blackboard.target->GetComponent<PlayerControllerComponent>() };
            if (!playerCtrl) break;

            const auto& intent{ playerCtrl->GetIntent() };
            const bool isPlayerAttacking{ intent.bAttackPressed || intent.bAttackHeld ||
                                         intent.bHeavyAttackPressed || intent.bHeavyAttackHeld ||
                                         intent.bSkillTriggered };
            if (isPlayerAttacking)
            {
                break;
            }

            // Intent Alignment. Verify that player analog movement is aimed within ~45 deg of corridor drop point
            const float inputLenSq{ (intent.worldMoveDirection.x * intent.worldMoveDirection.x) +
                                    (intent.worldMoveDirection.y * intent.worldMoveDirection.y) };
            if (inputLenSq < 0.15f)
            {
                break;
            }

            const float invHorizDist{ (horizDist > 0.001f) ? (1.0f / horizDist) : 0.0f };
            const float toLedgeNormX{ dxToLedge * invHorizDist };
            const float toLedgeNormZ{ dzToLedge * invHorizDist };

            const float invInputLen{ 1.0f / std::sqrt(inputLenSq) };
            const float inputDirX{ intent.worldMoveDirection.x * invInputLen };
            const float inputDirZ{ intent.worldMoveDirection.y * invInputLen };
            const float alignmentDot{ (inputDirX * toLedgeNormX) + (inputDirZ * toLedgeNormZ) };

            if (alignmentDot < 0.70f)
            {
                break;
            }

            // Dynamic Intercept Distance. Scale lead distance directly with intended locomotion speed
            const float baseSpeed{ intent.bSprintHeld
                ? (playerMotor ? playerMotor->GetConfig().sprintSpeed : 16.0f)
                : (playerMotor ? playerMotor->GetConfig().maxRunSpeed : 8.0f) };
            const float vApproach{ baseSpeed * (std::min)(1.0f, std::sqrt(inputLenSq)) * alignmentDot };

            // Ensure enemy lands with a comfortable visual space (~4.5m) in front of the player
            const float noticeDistanceBuffer{ (std::max)(4.5f, m_config.wallDropTriggerRadius + 1.0f) };
            const float leadDistance{ vApproach * tTotal };
            const float triggerDistance{ noticeDistanceBuffer + leadDistance };

            if (horizDist > triggerDistance)
            {
                break;
            }

            // Static Mesh Occlusion. Ensure clear corridor line-of-sight from floor impact point to player
            if (scene)
            {
                const physx::PxVec3 dropFloorOrigin{ dropLedgePos.x, floorY + 1.2f, dropLedgePos.z };
                const physx::PxVec3 playerChestTarget{ playerPos.x, playerPos.y + 1.2f, playerPos.z };
                const physx::PxVec3 losDiff{ playerChestTarget - dropFloorOrigin };
                const float losDist{ losDiff.magnitude() };
                if (losDist > 0.2f)
                {
                    physx::PxRaycastBuffer losHit{};
                    physx::PxQueryFilterData filterData{};
                    filterData.data.word0 = CollisionLayer::WorldStatic;
                    filterData.flags = physx::PxQueryFlag::eSTATIC;
                    if (scene->raycast(dropFloorOrigin, losDiff / losDist, losDist - 0.20f, losHit, physx::PxHitFlag::eDEFAULT, filterData))
                    {
                        break;
                    }
                }
            }
        }

        // If the enemy is hiding further back on the roof (>60cm from ledge), scramble first; else drop immediately
        if (distRoof > 0.6f)
        {
            ChangeState(EnemyState::wall_scramble);
        }
        else
        {
            ChangeState(EnemyState::wall_drop);
        }
        break;
    }

    case EnemyState::wall_scramble:
    {
        m_blackboard.stateTimer += dt;
        const DirectX::XMFLOAT3 currentPos{ GetWorldPosition(m_owner) };
        const DirectX::XMFLOAT3 dropLedgePos{
            m_blackboard.spawnPosition.x + m_config.dropLedgeOffset.x,
            m_blackboard.spawnPosition.y + m_config.dropLedgeOffset.y,
            m_blackboard.spawnPosition.z + m_config.dropLedgeOffset.z
        };

        const float dx{ dropLedgePos.x - currentPos.x };
        const float dz{ dropLedgePos.z - currentPos.z };
        const float distToLedgeSq{ (dx * dx) + (dz * dz) };

        // Reached the ledge (within 60cm) or timed out (safety abort after 3s): leap off
        if (distToLedgeSq <= (0.6f * 0.6f) || m_blackboard.stateTimer >= 3.0f)
        {
            ChangeState(EnemyState::wall_drop);
            break;
        }

        // Steer rapidly toward the drop point
        SteerTowards(dropLedgePos, dt);
        break;
    }

    case EnemyState::wall_drop:
        // Enforce zero horizontal steering while in free-fall to prevent airborne drift
        StopMovement();
        // Free-fall until the capsule controller strikes ground geometry
        if (m_capsule && m_capsule->IsGrounded())
        {
            ChangeState(EnemyState::wall_land);
        }
        break;

    case EnemyState::wall_land:
        StopMovement();
        m_blackboard.stateTimer += dt;

        // Smoothly orient toward player during landing impact recovery
        if (m_blackboard.target)
        {
            const DirectX::XMFLOAT3 currentPos{ GetWorldPosition(m_owner) };
            const DirectX::XMFLOAT3 targetPos{ GetWorldPosition(m_blackboard.target) };
            const float dx{ targetPos.x - currentPos.x };
            const float dz{ targetPos.z - currentPos.z };
            if ((dx * dx) + (dz * dz) > 0.01f)
            {
                const float targetYawDeg{ DirectX::XMConvertToDegrees(std::atan2(dx, dz)) };
                float deltaYaw{ targetYawDeg - m_owner->transform.rotation.y };
                while (deltaYaw > 180.0f) deltaYaw -= 360.0f;
                while (deltaYaw < -180.0f) deltaYaw += 360.0f;
                const float maxStep{ m_config.turnSpeedDeg * dt };
                m_owner->transform.rotation.y += std::clamp(deltaYaw, -maxStep, maxStep);
            }
        }

        // Theatrical recovery window concludes: transition directly to combat pursuit
        if (m_blackboard.stateTimer >= m_config.wallLandDuration)
        {
            ChangeState(EnemyState::chase);
        }
        break;

    case EnemyState::idle:
    case EnemyState::patrol:
    case EnemyState::chase:
    case EnemyState::attack_windup:
    case EnemyState::attack_active:
    case EnemyState::hit_react:
        TickGrunt(dt);
        break;

    default:
        break;
    }
}

void AIControllerComponent::Update(const float dt)
{
    if (!m_owner)
    {
        return;
    }

    if (!m_animation)
    {
        m_animation = m_owner->GetComponent<AnimationComponent>();
        if (m_animation)
        {
            m_animation->SetBlackboard(&m_animBlackboard);
        }
    }

    const EditorMode mode{ EditorManager::Instance().GetEditorMode() };
    const bool isPlayMode{ mode == EditorMode::Play };

    // EDIT MODE: Synchronize spawn anchor in real-time as the designer moves the entity
    if (mode == EditorMode::Edit)
    {
        m_blackboard.spawnPosition = GetWorldPosition(m_owner);
        m_wasPlayMode = false;
        return;
    }

    if (!isPlayMode)
    {
        m_wasPlayMode = false;
        return;
    }

    // PLAY MODE ENTRY: Cleanly reset state and initialize starting anchor
    if (!m_wasPlayMode)
    {
        m_wasPlayMode = true;
        m_blackboard.spawnPosition = GetWorldPosition(m_owner);
        m_blackboard.wanderDestination = m_blackboard.spawnPosition;
        m_blackboard.target = nullptr;
        m_blackboard.resetTimers();
        m_blackboard.flags.reset();

        if (m_movement)
        {
            m_initialGravity = m_movement->GetConfig().useGravity;
        }

        switch (m_config.archetype)
        {
        case EnemyArchetype::grunt:
            ChangeState(EnemyState::idle);
            // Prime stateTimer to idleDuration so the grunt directly picks a wander target and moves on frame 1
            m_blackboard.stateTimer = m_config.idleDuration;
            break;
        case EnemyArchetype::ground_ambush:
            FindCoverProps();
            ChangeState(EnemyState::ambush_dormant);
            break;
        case EnemyArchetype::wall_lurker:
            ChangeState(EnemyState::wall_hang);
            break;
        default:
            ChangeState(EnemyState::idle);
            break;
        }
    }

    // Halt logic if dead or hit-stopped
    if (m_health && (m_health->IsDead() || m_health->IsHitStopped()))
    {
        if (m_health->IsDead() && m_currentState != EnemyState::dead)
        {
            ChangeState(EnemyState::dead);
        }
        return;
    }

    // AAA Dual-Threshold Tether & Boundary Stand-Down Evaluation
    const DirectX::XMFLOAT3 currentPos{ GetWorldPosition(m_owner) };
    const DirectX::XMVECTOR vCurrent{ DirectX::XMLoadFloat3(&currentPos) };
    const DirectX::XMVECTOR vSpawn{ DirectX::XMLoadFloat3(&m_blackboard.spawnPosition) };
    const float leashRangeSq{ m_config.leashRange * m_config.leashRange };

    float distFromSpawnSq{ 0.0f };
    DirectX::XMStoreFloat(&distFromSpawnSq, DirectX::XMVector3LengthSq(DirectX::XMVectorSubtract(vCurrent, vSpawn)));

    // Inner hysteresis boundary: re-arm aggro only once returned inside (leashRange - leashHysteresis)
    const float innerLeash{ (std::max)(m_config.wanderRadius, m_config.leashRange - m_config.leashHysteresis) };
    const float innerLeashSq{ innerLeash * innerLeash };

    // Boundary Breach: Enemy reaches the outer leash perimeter while chasing
    if (distFromSpawnSq >= leashRangeSq &&
        m_currentState != EnemyState::wall_hang &&
        m_currentState != EnemyState::wall_scramble &&
        m_currentState != EnemyState::wall_drop &&
        m_currentState != EnemyState::wall_land)
    {
        m_blackboard.setFlag(SensoryFlag::is_leashed, true);

        // Stop immediately, maintain current facing direction, and enter Idle
        if (m_currentState == EnemyState::chase ||
            m_currentState == EnemyState::attack_windup ||
            m_currentState == EnemyState::attack_active)
        {
            ChangeState(EnemyState::idle);
            StopMovement();
            m_blackboard.deaggroCooldownTimer = m_config.deaggroDuration;
        }
    }
    // Hysteresis Recovery: Clear leashed status once safely inside inner boundary
    else if (distFromSpawnSq <= innerLeashSq)
    {
        m_blackboard.setFlag(SensoryFlag::is_leashed, false);
    }

    // Synchronize kinematic motor velocity with animation blend tree contract
    if (m_movement)
    {
        const DirectX::XMFLOAT3 vel{ m_movement->GetTotalVelocity() };
        m_animBlackboard.groundSpeed = std::sqrt((vel.x * vel.x) + (vel.z * vel.z));
        m_animBlackboard.verticalVelocity = vel.y;
        m_animBlackboard.setFlag(Engine::Animation::AnimFlag::is_grounded, m_movement->isGrounded());
    }

    // Amortized perception update
    UpdatePerception(dt);

    if (m_blackboard.attackCooldownTimer > 0.0f)
    {
        m_blackboard.attackCooldownTimer -= dt;
    }

    if (m_blackboard.deaggroCooldownTimer > 0.0f)
    {
        m_blackboard.deaggroCooldownTimer -= dt;
    }

    // Dispatch to archetype behaviors
    if (m_currentState == EnemyState::airborne_juggle)
    {
        TickAirborneJuggle(dt);
        return;
    }

    switch (m_config.archetype)
    {
    case EnemyArchetype::grunt:
        TickGrunt(dt);
        break;
    case EnemyArchetype::ground_ambush:
        TickGroundAmbush(dt);
        break;
    case EnemyArchetype::wall_lurker:
        TickWallLurker(dt);
        break;
    default:
        break;
    }
}

namespace
{
    // Recursively gathers scene GameObjects representing Hide_Building pillar props
    void CollectCoverPropsRecursive(GameObject* current, std::vector<GameObject*>& outProps) noexcept
    {
        if (!current) return;

        const std::string_view name{ current->GetName() };
        if (name.starts_with("Hide_Building"))
        {
            // If it has no children, it is a standalone pillar; if it has children, add them
            if (current->GetChildren().empty())
            {
                outProps.push_back(current);
            }
            else
            {
                for (const auto& child : current->GetChildren())
                {
                    if (child)
                    {
                        outProps.push_back(child.get());
                    }
                }
                return;
            }
        }

        for (const auto& child : current->GetChildren())
        {
            CollectCoverPropsRecursive(child.get(), outProps);
        }
    }
}

void AIControllerComponent::FindCoverProps() noexcept
{
    m_cachedCoverProps.clear();
    m_currentCoverObstacle = nullptr;
    if (!m_owner) return;

    // Traverse to Scene Root to scan all available props in Act_4
    GameObject* root{ m_owner };
    while (root->GetParent() != nullptr)
    {
        root = root->GetParent();
    }

    CollectCoverPropsRecursive(root, m_cachedCoverProps);
}

void AIControllerComponent::UpdateAmbushCover(const float dt) noexcept
{
    if (m_currentState != EnemyState::ambush_dormant || !m_blackboard.target)
    {
        return;
    }

    if (m_cachedCoverProps.empty())
    {
        FindCoverProps();
        if (m_cachedCoverProps.empty())
        {
            return;
        }
    }

    const DirectX::XMFLOAT3 playerPos{ GetWorldPosition(m_blackboard.target) };
    const DirectX::XMFLOAT3 currentPos{ GetWorldPosition(m_owner) };
    const DirectX::XMFLOAT3 spawnPos{ m_blackboard.spawnPosition };

    GameObject* bestObstacle{ m_currentCoverObstacle };
    float bestScore{ 999999.0f };

    // Select candidate pillar within territory using authoritative world positions
    const float maxSearchDistSq{ m_config.leashRange * m_config.leashRange };

    for (auto* prop : m_cachedCoverProps)
    {
        if (!prop || prop->IsDestroyed()) continue;

        const DirectX::XMFLOAT3 propPos{ GetWorldPosition(prop) };
        const float dxSpawn{ propPos.x - spawnPos.x };
        const float dzSpawn{ propPos.z - spawnPos.z };
        const float distToSpawnSq{ (dxSpawn * dxSpawn) + (dzSpawn * dzSpawn) };

        if (distToSpawnSq > maxSearchDistSq) continue;

        const float dxPlayer{ propPos.x - playerPos.x };
        const float dzPlayer{ propPos.z - playerPos.z };
        const float distToPlayerSq{ (dxPlayer * dxPlayer) + (dzPlayer * dzPlayer) };

        if (distToPlayerSq < 0.01f) continue;

        const float invDist{ 1.0f / std::sqrt(distToPlayerSq) };
        const float dirX{ dxPlayer * invDist };
        const float dirZ{ dzPlayer * invDist };

        // Geometric shadow position: directly behind the pillar opposite the player in world space
        const DirectX::XMFLOAT3 hidePos{
            propPos.x + (dirX * m_config.ambushStandoffDistance),
            spawnPos.y,
            propPos.z + (dirZ * m_config.ambushStandoffDistance)
        };

        const float dxEnemy{ hidePos.x - currentPos.x };
        const float dzEnemy{ hidePos.z - currentPos.z };
        float score{ (dxEnemy * dxEnemy) + (dzEnemy * dzEnemy) };

        // Hysteresis bias: favor staying with current pillar to prevent ping-ponging
        if (prop == m_currentCoverObstacle)
        {
            score -= 4.0f;
        }

        if (score < bestScore)
        {
            bestScore = score;
            bestObstacle = prop;
        }
    }

    if (bestObstacle)
    {
        m_currentCoverObstacle = bestObstacle;
        const DirectX::XMFLOAT3 propPos{ GetWorldPosition(bestObstacle) };

        const float dxPlayer{ propPos.x - playerPos.x };
        const float dzPlayer{ propPos.z - playerPos.z };
        const float distToPlayer{ std::sqrt((dxPlayer * dxPlayer) + (dzPlayer * dzPlayer)) };

        if (distToPlayer > 0.01f)
        {
            const float invDist{ 1.0f / distToPlayer };
            const float dirX{ dxPlayer * invDist };
            const float dirZ{ dzPlayer * invDist };

            m_blackboard.wanderDestination = {
                propPos.x + (dirX * m_config.ambushStandoffDistance),
                spawnPos.y,
                propPos.z + (dirZ * m_config.ambushStandoffDistance)
            };
        }
    }
}

namespace
{
    // Precomputed unit-circle lookup table on the XZ plane (Zero runtime trigonometry per frame)
    [[nodiscard]] const std::array<DirectX::XMFLOAT2, 24>& GetPrecomputedUnitCircle() noexcept
    {
        static const auto s_table{ []() noexcept {
            std::array<DirectX::XMFLOAT2, 24> table{};
            constexpr float step{ DirectX::XM_2PI / 24.0f };
            for (std::size_t i{ 0 }; i < 24; ++i)
            {
                const float theta{ step * static_cast<float>(i) };
                table[i] = { std::sin(theta), std::cos(theta) };
            }
            return table;
        }() };
        return s_table;
    }

    // Helper: Draws a horizontal wire circle centered at world coordinate using precomputed LUT
    void DrawWireCircle(
        ShapeRenderer* shapes,
        const DirectX::XMFLOAT3& center,
        const float radius,
        const DirectX::XMFLOAT4& color) noexcept
    {
        if (!shapes || radius <= 0.001f) return;

        const auto& lut{ GetPrecomputedUnitCircle() };
        for (std::size_t i{ 0 }; i < lut.size(); ++i)
        {
            const std::size_t nextIdx{ (i + 1) % lut.size() };
            const DirectX::XMFLOAT3 p0{ center.x + (lut[i].x * radius), center.y, center.z + (lut[i].y * radius) };
            const DirectX::XMFLOAT3 p1{ center.x + (lut[nextIdx].x * radius), center.y, center.z + (lut[nextIdx].y * radius) };
            shapes->DrawLine(p0, p1, color);
        }
    }

    // Helper: Draws a curved wire arc for vision sector visualization
    void DrawWireArc(
        ShapeRenderer* shapes,
        const DirectX::XMFLOAT3& center,
        const float radius,
        const float startAngleRad,
        const float endAngleRad,
        const int subdivisions,
        const DirectX::XMFLOAT4& color) noexcept
    {
        if (!shapes || subdivisions < 2 || radius <= 0.001f) return;

        const float step{ (endAngleRad - startAngleRad) / static_cast<float>(subdivisions) };
        for (int i{ 0 }; i < subdivisions; ++i)
        {
            const float a0{ startAngleRad + (step * static_cast<float>(i)) };
            const float a1{ startAngleRad + (step * static_cast<float>(i + 1)) };

            const DirectX::XMFLOAT3 p0{ center.x + (std::sin(a0) * radius), center.y, center.z + (std::cos(a0) * radius) };
            const DirectX::XMFLOAT3 p1{ center.x + (std::sin(a1) * radius), center.y, center.z + (std::cos(a1) * radius) };
            shapes->DrawLine(p0, p1, color);
        }
    }

    // Maps operational state directly to a distinct debug visual color
    [[nodiscard]] constexpr DirectX::XMFLOAT4 GetStateDebugColor(const EnemyState state) noexcept
    {
        switch (state)
        {
        case EnemyState::idle:
        case EnemyState::patrol:
            return { 0.2f, 0.9f, 0.3f, 0.9f }; // Calm Green
        case EnemyState::chase:
            return { 1.0f, 0.8f, 0.1f, 0.95f }; // Alert Amber
        case EnemyState::attack_windup:
        case EnemyState::attack_active:
            return { 1.0f, 0.15f, 0.15f, 1.0f }; // Danger Red
        case EnemyState::hit_react:
            return { 0.9f, 0.2f, 0.9f, 0.95f }; // Stunned Magenta
        case EnemyState::airborne_juggle:
            return { 1.0f, 0.6f, 0.1f, 1.0f }; // Sky Gold (Airborne)
        case EnemyState::ambush_dormant:
            return { 0.9f, 0.45f, 0.1f, 0.85f }; // Camo Orange
        case EnemyState::ambush_burst:
            return { 0.9f, 0.45f, 0.1f, 0.85f }; // Camo Orange
        case EnemyState::wall_hang:
        case EnemyState::wall_scramble:
        case EnemyState::wall_drop:
            return { 0.2f, 0.8f, 1.0f, 0.9f }; // High-Perch Cyan
        case EnemyState::wall_land:
            return { 0.2f, 0.5f, 1.0f, 0.95f }; // Heavy Landing Blue
        case EnemyState::dead:
        default:
            return { 0.4f, 0.4f, 0.4f, 0.5f }; // Inactive Grey
        }
    }
}

void AIControllerComponent::DrawGizmo(const GizmoContext& ctx) noexcept
{
    // Fast-fail if Navigation Gizmos are toggled off in the editor toolbar
    if (!(ctx.categoryMask & static_cast<std::uint32_t>(GizmoCategory::Navigation)))
    {
        return;
    }

    if (!m_owner || !ctx.shapes)
    {
        return;
    }

    if (EditorManager::Instance().GetEditorMode() == EditorMode::Edit)
    {
        m_blackboard.spawnPosition = GetWorldPosition(m_owner);
    }

    const DirectX::XMFLOAT3 pos{ GetWorldPosition(m_owner) };
    const float halfHeight{ m_capsule ? m_capsule->GetTotalHalfHeight() : 1.0f };
    const float eyeHeight{ halfHeight * 1.5f };
    const DirectX::XMFLOAT3 eyePos{ pos.x, pos.y + eyeHeight, pos.z };

    // State Indicator Overhead Marker (DOOM style)
    // Renders a small floating indicator above the capsule representing real-time cognitive state
    const DirectX::XMFLOAT4 stateColor{ GetStateDebugColor(m_currentState) };
    const DirectX::XMFLOAT3 markerPos{ pos.x, pos.y + (halfHeight * 2.0f) + 0.35f, pos.z };
    ctx.shapes->DrawBox(markerPos, { 0.0f, 0.0f, 0.0f }, { 0.15f, 0.15f, 0.15f }, stateColor);

    // Vision Cone & Peripheral Arc
    constexpr DirectX::XMFLOAT4 fovColor{ 1.0f, 0.9f, 0.2f, 0.85f };
    const float yawRad{ DirectX::XMConvertToRadians(m_owner->transform.rotation.y) };
    const float halfFovRad{ DirectX::XMConvertToRadians(m_config.sightFovDeg * 0.5f) };
    const float leftAngle{ yawRad - halfFovRad };
    const float rightAngle{ yawRad + halfFovRad };

    const DirectX::XMFLOAT3 leftRayEnd{
        eyePos.x + (std::sin(leftAngle) * m_config.sightRange),
        eyePos.y,
        eyePos.z + (std::cos(leftAngle) * m_config.sightRange)
    };
    const DirectX::XMFLOAT3 rightRayEnd{
        eyePos.x + (std::sin(rightAngle) * m_config.sightRange),
        eyePos.y,
        eyePos.z + (std::cos(rightAngle) * m_config.sightRange)
    };

    ctx.shapes->DrawLine(eyePos, leftRayEnd, fovColor);
    ctx.shapes->DrawLine(eyePos, rightRayEnd, fovColor);
    DrawWireArc(ctx.shapes, eyePos, m_config.sightRange, leftAngle, rightAngle, 12, fovColor);

    // Combat Attack Spacing Ring (Physical Melee Trigger Boundary)
    constexpr DirectX::XMFLOAT4 attackRingColor{ 1.0f, 0.2f, 0.2f, 0.9f };
    DrawWireCircle(ctx.shapes, { pos.x, pos.y + 0.05f, pos.z }, m_config.attackRange, attackRingColor);

    // Wander Area & Spawn Anchor
    constexpr DirectX::XMFLOAT4 wanderColor{ 0.2f, 0.6f, 1.0f, 0.5f };
    DrawWireCircle(ctx.shapes, { m_blackboard.spawnPosition.x, m_blackboard.spawnPosition.y + 0.04f, m_blackboard.spawnPosition.z }, m_config.wanderRadius, wanderColor);

    // Spawn Anchor Crosshair
    constexpr float crossSize{ 0.35f };
    ctx.shapes->DrawLine(
        { m_blackboard.spawnPosition.x - crossSize, m_blackboard.spawnPosition.y + 0.04f, m_blackboard.spawnPosition.z },
        { m_blackboard.spawnPosition.x + crossSize, m_blackboard.spawnPosition.y + 0.04f, m_blackboard.spawnPosition.z },
        wanderColor);
    ctx.shapes->DrawLine(
        { m_blackboard.spawnPosition.x, m_blackboard.spawnPosition.y + 0.04f, m_blackboard.spawnPosition.z - crossSize },
        { m_blackboard.spawnPosition.x, m_blackboard.spawnPosition.y + 0.04f, m_blackboard.spawnPosition.z + crossSize },
        wanderColor);

    // Active Patrol Destination Vector
    if (m_currentState == EnemyState::patrol)
    {
        constexpr DirectX::XMFLOAT4 patrolLineColor{ 0.2f, 1.0f, 0.4f, 0.75f };
        ctx.shapes->DrawLine(pos, m_blackboard.wanderDestination, patrolLineColor);
        ctx.shapes->DrawBox(m_blackboard.wanderDestination, { 0.0f, 0.0f, 0.0f }, { 0.2f, 0.2f, 0.2f }, patrolLineColor);
    }

    // Leash Disengage Perimeter (Outer Boundary)
    constexpr DirectX::XMFLOAT4 leashColor{ 0.5f, 0.5f, 0.6f, 0.35f };
    DrawWireCircle(ctx.shapes, { m_blackboard.spawnPosition.x, m_blackboard.spawnPosition.y + 0.02f, m_blackboard.spawnPosition.z }, m_config.leashRange, leashColor);

    // Inner Re-Arm Hysteresis Perimeter
    const float innerLeashRadius{ (std::max)(m_config.wanderRadius, m_config.leashRange - m_config.leashHysteresis) };
    constexpr DirectX::XMFLOAT4 innerLeashColor{ 0.2f, 0.7f, 0.5f, 0.25f };
    DrawWireCircle(ctx.shapes, { m_blackboard.spawnPosition.x, m_blackboard.spawnPosition.y + 0.02f, m_blackboard.spawnPosition.z }, innerLeashRadius, innerLeashColor);

    // Archetype-Specific Trigger Footprints
    if (m_config.archetype == EnemyArchetype::ground_ambush)
    {
        constexpr DirectX::XMFLOAT4 ambushColor{ 1.0f, 0.45f, 0.1f, 0.75f };
        DrawWireCircle(ctx.shapes, { pos.x, pos.y + 0.06f, pos.z }, m_config.ambushTriggerRadius, ambushColor);

        // Visual telemetry for active cover prop and dynamic blind-spot shadow point
        if (m_currentCoverObstacle)
        {
            constexpr DirectX::XMFLOAT4 coverLineColor{ 0.1f, 0.85f, 0.95f, 0.85f };
            const DirectX::XMFLOAT3 propPos{ GetWorldPosition(m_currentCoverObstacle) };

            // Line from pillar to shadow hide point
            ctx.shapes->DrawLine(propPos, m_blackboard.wanderDestination, coverLineColor);
            ctx.shapes->DrawBox(m_blackboard.wanderDestination, { 0.0f, 0.0f, 0.0f }, { 0.25f, 0.25f, 0.25f }, coverLineColor);

            // Pillar base boundary
            DrawWireCircle(ctx.shapes, { propPos.x, propPos.y + 0.05f, propPos.z }, 1.0f, coverLineColor);
        }
    }
    else if (m_config.archetype == EnemyArchetype::wall_lurker &&
        (m_currentState == EnemyState::wall_hang || m_currentState == EnemyState::wall_scramble || m_currentState == EnemyState::wall_drop))
    {
        constexpr DirectX::XMFLOAT4 dropColor{ 0.85f, 0.25f, 0.95f, 0.85f };
        constexpr DirectX::XMFLOAT4 scrambleColor{ 0.2f, 0.9f, 1.0f, 0.85f };
        constexpr DirectX::XMFLOAT4 sprintTriggerColor{ 1.0f, 0.35f, 0.1f, 0.65f };

        const DirectX::XMFLOAT3 dropLedgePos{
            m_blackboard.spawnPosition.x + m_config.dropLedgeOffset.x,
            m_blackboard.spawnPosition.y + m_config.dropLedgeOffset.y,
            m_blackboard.spawnPosition.z + m_config.dropLedgeOffset.z
        };

        // Draw scramble vector on the roof if a distinct ledge offset is configured
        const float offsetLenSq{
            (m_config.dropLedgeOffset.x * m_config.dropLedgeOffset.x) +
            (m_config.dropLedgeOffset.y * m_config.dropLedgeOffset.y) +
            (m_config.dropLedgeOffset.z * m_config.dropLedgeOffset.z)
        };
        if (offsetLenSq > 0.04f)
        {
            ctx.shapes->DrawLine(pos, dropLedgePos, scrambleColor);
            ctx.shapes->DrawBox(dropLedgePos, { 0.0f, 0.0f, 0.0f }, { 0.25f, 0.25f, 0.25f }, scrambleColor);
        }

        float floorY{ dropLedgePos.y - 12.0f };
        auto* scene{ PhysicsManager::Instance().GetScene() };
        if (scene)
        {
            physx::PxRaycastBuffer hit{};
            physx::PxQueryFilterData filter{};
            filter.data.word0 = CollisionLayer::WorldStatic;
            filter.flags = physx::PxQueryFlag::eSTATIC;
            if (scene->raycast(physx::PxVec3{ dropLedgePos.x, dropLedgePos.y, dropLedgePos.z }, physx::PxVec3{ 0.0f, -1.0f, 0.0f }, 100.0f, hit, physx::PxHitFlag::eDEFAULT, filter))
            {
                floorY = hit.block.position.y;
            }
        }

        const DirectX::XMFLOAT3 floorPos{ dropLedgePos.x, floorY, dropLedgePos.z };

        // Plumb line from ledge point down to corridor floor
        ctx.shapes->DrawLine(dropLedgePos, floorPos, dropColor);

        // Base landing impact footprint
        DrawWireCircle(ctx.shapes, { floorPos.x, floorPos.y + 0.05f, floorPos.z }, m_config.wallDropTriggerRadius, dropColor);

        // Calculate total lead time (roof scramble + free-fall drop) for gizmo visualization
        const float dxRoof{ dropLedgePos.x - pos.x };
        const float dzRoof{ dropLedgePos.z - pos.z };
        const float distRoof{ std::sqrt((dxRoof * dxRoof) + (dzRoof * dzRoof)) };
        const float scrambleSpeed{ m_movement ? m_movement->GetConfig().maxRunSpeed : 8.0f };
        const float tScramble{ distRoof / (scrambleSpeed > 0.1f ? scrambleSpeed : 8.0f) };

        const float dropHeight{ (std::max)(1.5f, dropLedgePos.y - floorY) };
        constexpr float v0y{ 10.0f };
        constexpr float g{ 9.81f };
        const float tDrop{ (-v0y + std::sqrt((v0y * v0y) + (2.0f * g * dropHeight))) / g };
        const float tTotal{ tScramble + tDrop };

        // Draw dynamic sprint lead horizon matching runtime predictive intercept
        const float noticeBuffer{ (std::max)(4.5f, m_config.wallDropTriggerRadius + 1.0f) };
        const float maxSprintTriggerRadius{ noticeBuffer + (16.0f * tTotal) };
        DrawWireCircle(ctx.shapes, { floorPos.x, floorPos.y + 0.03f, floorPos.z }, noticeBuffer, { 0.2f, 1.0f, 0.4f, 0.85f });
        DrawWireCircle(ctx.shapes, { floorPos.x, floorPos.y + 0.03f, floorPos.z }, maxSprintTriggerRadius, sprintTriggerColor);
    }

    // Sensory Line-of-Sight Ray to Acquired Target
    if (m_blackboard.target)
    {
        const bool hasLos{ m_blackboard.getFlag(SensoryFlag::has_line_of_sight) };
        const DirectX::XMFLOAT4 losColor{ hasLos ? DirectX::XMFLOAT4{ 0.1f, 1.0f, 0.2f, 0.9f } : DirectX::XMFLOAT4{ 1.0f, 0.1f, 0.1f, 0.4f } };
        const DirectX::XMFLOAT3 targetPos{ m_blackboard.target->transform.position };
        ctx.shapes->DrawLine(eyePos, { targetPos.x, targetPos.y + 1.2f, targetPos.z }, losColor);
    }
}

void AIControllerComponent::DrawInspector()
{
    ImGui::TextDisabled("AI Perception & State Orchestrator");
    ImGui::Separator();

    static constexpr const char* s_archetypeNames[] = { "Grunt (Melee Chaser)", "Ground Ambush (Prop)", "Wall Lurker (Drop & Chase)" };
    int currentArchetype = static_cast<int>(m_config.archetype);
    if (ImGui::Combo("Archetype", &currentArchetype, s_archetypeNames, 3))
    {
        m_config.archetype = static_cast<EnemyArchetype>(currentArchetype);
        switch (m_config.archetype)
        {
        case EnemyArchetype::grunt: ChangeState(EnemyState::idle); break;
        case EnemyArchetype::ground_ambush: ChangeState(EnemyState::ambush_dormant); break;
        case EnemyArchetype::wall_lurker: ChangeState(EnemyState::wall_hang); break;
        default: break;
        }
    }

    ImGui::Spacing();
    ImGui::TextDisabled("PERCEPTION TUNING");
    ImGui::DragFloat("Sight Range", &m_config.sightRange, 0.5f, 1.0f, 50.0f, "%.1f m");
    ImGui::SliderFloat("Sight FOV", &m_config.sightFovDeg, 10.0f, 360.0f, "%.0f deg");
    ImGui::DragFloat("Attack Range", &m_config.attackRange, 0.1f, 0.5f, 10.0f, "%.2f m");
    ImGui::DragFloat("Leash Range", &m_config.leashRange, 0.5f, 2.0f, 100.0f, "%.1f m");
    ImGui::DragFloat("Leash Hysteresis", &m_config.leashHysteresis, 0.2f, 0.5f, 20.0f, "%.1f m");
    ImGui::DragFloat("Wander Radius", &m_config.wanderRadius, 0.2f, 0.0f, 30.0f, "%.1f m");
    ImGui::DragFloat("Turn Speed", &m_config.turnSpeedDeg, 10.0f, 45.0f, 1440.0f, "%.0f deg/s");
    ImGui::DragFloat("Idle Duration", &m_config.idleDuration, 0.1f, 0.1f, 10.0f, "%.1f s");
    ImGui::DragFloat("De-aggro Duration", &m_config.deaggroDuration, 0.1f, 0.5f, 10.0f, "%.1f s");
    ImGui::DragFloat("Patrol Timeout", &m_config.patrolTimeout, 0.2f, 1.0f, 30.0f, "%.1f s");

    if (m_config.archetype == EnemyArchetype::ground_ambush)
    {
        ImGui::DragFloat("Ambush Radius", &m_config.ambushTriggerRadius, 0.2f, 0.5f, 20.0f, "%.1f m");
        ImGui::DragFloat("Standoff Dist", &m_config.ambushStandoffDistance, 0.1f, 0.5f, 10.0f, "%.1f m");
        if (m_currentCoverObstacle)
        {
            ImGui::TextColored(ImVec4{ 0.2f, 0.9f, 0.9f, 1.0f }, "Cover: %s", m_currentCoverObstacle->GetName().c_str());
        }
    }
    else if (m_config.archetype == EnemyArchetype::wall_lurker)
    {
        ImGui::DragFloat3("Drop Ledge Offset", &m_config.dropLedgeOffset.x, 0.1f, -50.0f, 50.0f, "%.2f m");
        ImGui::DragFloat("Drop Standoff Radius", &m_config.wallDropTriggerRadius, 0.2f, 0.5f, 20.0f, "%.1f m");
        ImGui::DragFloat("Landing Duration", &m_config.wallLandDuration, 0.05f, 0.1f, 3.0f, "%.2f s");
        ImGui::DragFloat("Wall Eject Speed", &m_config.wallDropEjectSpeed, 0.1f, 0.5f, 10.0f, "%.1f m/s");
    }

    ImGui::Spacing();
    ImGui::TextDisabled("COMBAT TIMING");
    ImGui::DragFloat("Windup Duration", &m_config.attackWindupDuration, 0.05f, 0.05f, 3.0f, "%.2f s");
    ImGui::DragFloat("Active Duration", &m_config.attackActiveDuration, 0.05f, 0.05f, 3.0f, "%.2f s");
    ImGui::DragFloat("Attack Cooldown", &m_config.attackCooldownDuration, 0.1f, 0.1f, 5.0f, "%.2f s");
    ImGui::DragFloat("Hit React Time", &m_config.hitReactDuration, 0.05f, 0.05f, 2.0f, "%.2f s");

    ImGui::Spacing();
    ImGui::TextDisabled("RUNTIME TELEMETRY");
    ImGui::BeginDisabled();

    static constexpr const char* s_stateNames[] = {
        "Idle", "Patrol", "Ambush Dormant", "Ambush Burst",
        "Wall Hang", "Wall Scramble", "Wall Drop", "Wall Land", "Chase", "Attack Windup",
        "Attack Active", "Hit React", "Airborne Juggle", "Dead"
    };
    ImGui::Text("Current State: %s", s_stateNames[static_cast<std::size_t>(m_currentState)]);
    ImGui::Text("Dist to Target: %.2f m", std::sqrt(m_blackboard.targetDistanceSq));

    bool losCheck = m_blackboard.getFlag(SensoryFlag::has_line_of_sight);
    bool fovCheck = m_blackboard.getFlag(SensoryFlag::is_target_in_fov);
    ImGui::Checkbox("Has Line of Sight", &losCheck);
    ImGui::Checkbox("Is in FOV", &fovCheck);

    ImGui::EndDisabled();
}

void AIControllerComponent::Serialize(nlohmann::json& outJson) const
{
    outJson["Archetype"] = static_cast<int>(m_config.archetype);
    outJson["SightRange"] = m_config.sightRange;
    outJson["SightFovDeg"] = m_config.sightFovDeg;
    outJson["AttackRange"] = m_config.attackRange;
    outJson["LeashRange"] = m_config.leashRange;
    outJson["LeashHysteresis"] = m_config.leashHysteresis;
    outJson["WanderRadius"] = m_config.wanderRadius;
    outJson["TurnSpeedDeg"] = m_config.turnSpeedDeg;
    outJson["IdleDuration"] = m_config.idleDuration;
    outJson["DeaggroDuration"] = m_config.deaggroDuration;
    outJson["PatrolTimeout"] = m_config.patrolTimeout;
    outJson["AmbushTriggerRadius"] = m_config.ambushTriggerRadius;
    outJson["AmbushStandoffDistance"] = m_config.ambushStandoffDistance;
    outJson["DropLedgeOffsetX"] = m_config.dropLedgeOffset.x;
    outJson["DropLedgeOffsetY"] = m_config.dropLedgeOffset.y;
    outJson["DropLedgeOffsetZ"] = m_config.dropLedgeOffset.z;
    outJson["WallDropTriggerRadius"] = m_config.wallDropTriggerRadius;
    outJson["WallLandDuration"] = m_config.wallLandDuration;
    outJson["WallDropEjectSpeed"] = m_config.wallDropEjectSpeed;
    outJson["AttackWindupDuration"] = m_config.attackWindupDuration;
    outJson["AttackActiveDuration"] = m_config.attackActiveDuration;
    outJson["AttackCooldownDuration"] = m_config.attackCooldownDuration;
    outJson["HitReactDuration"] = m_config.hitReactDuration;
}

void AIControllerComponent::Deserialize(const nlohmann::json& inJson)
{
    m_config.archetype = static_cast<EnemyArchetype>(inJson.value("Archetype", 0));
    m_config.sightRange = inJson.value("SightRange", 14.0f);
    m_config.sightFovDeg = inJson.value("SightFovDeg", 90.0f);
    m_config.attackRange = inJson.value("AttackRange", 1.8f);
    m_config.leashRange = inJson.value("LeashRange", 22.0f);
    m_config.leashHysteresis = inJson.value("LeashHysteresis", 4.0f);
    m_config.wanderRadius = inJson.value("WanderRadius", 4.0f);
    m_config.turnSpeedDeg = inJson.value("TurnSpeedDeg", 360.0f);
    m_config.idleDuration = inJson.value("IdleDuration", 1.5f);
    m_config.deaggroDuration = inJson.value("DeaggroDuration", 2.5f);
    m_config.patrolTimeout = inJson.value("PatrolTimeout", 6.0f);
    m_config.ambushTriggerRadius = inJson.value("AmbushTriggerRadius", 4.5f);
    m_config.ambushStandoffDistance = inJson.value("AmbushStandoffDistance", 2.2f);
    m_config.dropLedgeOffset = {
        inJson.value("DropLedgeOffsetX", 0.0f),
        inJson.value("DropLedgeOffsetY", 0.0f),
        inJson.value("DropLedgeOffsetZ", 0.0f)
    };
    m_config.wallDropTriggerRadius = inJson.value("WallDropTriggerRadius", 3.5f);
    m_config.wallLandDuration = inJson.value("WallLandDuration", 0.45f);
    m_config.wallDropEjectSpeed = inJson.value("WallDropEjectSpeed", 2.5f);
    m_config.attackWindupDuration = inJson.value("AttackWindupDuration", 0.5f);
    m_config.attackActiveDuration = inJson.value("AttackActiveDuration", 0.35f);
    m_config.attackCooldownDuration = inJson.value("AttackCooldownDuration", 1.2f);
    m_config.hitReactDuration = inJson.value("HitReactDuration", 0.45f);

    switch (m_config.archetype)
    {
    case EnemyArchetype::grunt: ChangeState(EnemyState::idle); break;
    case EnemyArchetype::ground_ambush: ChangeState(EnemyState::ambush_dormant); break;
    case EnemyArchetype::wall_lurker: ChangeState(EnemyState::wall_hang); break;
    default: break;
    }
}

REGISTER_COMPONENT(AIControllerComponent)